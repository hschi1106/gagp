"""After all timed searches exit, audit every exported final/generation champion.

No new search, ranking, candidate choice, test-set use, or failed-run replacement.
"""
from argparse import ArgumentParser
import fcntl
import json
import os
from pathlib import Path
import re
import time
import serial
from development import alive, gpu_lock
from champion_audit import first_native_program, validate_judgment


VARIANTS = ('asgp', 'gpu_original', 'gpu_improved')


def expected_tags(frozen):
    for task in frozen['tasks']:
        for population in frozen['populations']:
            for repetition in range(frozen['repetitions']):
                for variant in VARIANTS:
                    tag = f'{task}.p{population}.{variant}.s0.g{frozen["generations"]}.r{repetition}'
                    yield tag, task, population, repetition, variant


def derived_best_export(original, expected_sha256, target):
    assert serial.sha(original) == expected_sha256, 'original formal export changed'
    program = first_native_program(original)
    serialized = json.dumps(dict(programs=[program]), separators=(',', ':'), ensure_ascii=False)+'\n'
    if target.exists():
        assert target.read_text() == serialized, 'existing derived champion differs; preserve it'
    else:
        with target.open('x') as stream:
            stream.write(serialized)
    import hashlib
    identity = hashlib.sha256(json.dumps(program, sort_keys=True, separators=(',', ':'),
                                         ensure_ascii=False).encode()).hexdigest()
    return dict(parent_export=str(original), parent_export_sha256=expected_sha256,
                derived_export=str(target), derived_export_sha256=serial.sha(target),
                preselected_first_program_sha256=identity,
                selection='unchanged already-selected formal programs[0]; no rescoring or candidate selection')


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--matrix', type=Path, default=serial.RAW/'formal-attempt1')
    parser.add_argument('--freeze', type=Path, default=serial.RAW/'formal-freeze-attempt1.json')
    parser.add_argument('--stage', default='champion-judges-attempt0')
    parser.add_argument('--resume', action='store_true')
    parser.add_argument('--allow-partial', action='store_true')
    args = parser.parse_args()
    assert re.fullmatch('[a-z0-9-]+', args.stage), 'use one explicit stage name'
    guard = (serial.RAW/(args.stage+'-controller.lock')).open('a')
    fcntl.flock(guard, fcntl.LOCK_EX | fcntl.LOCK_NB)
    for path in serial.RAW.glob('*controller.json'):
        other = serial.read(path)
        assert other['pid'] == os.getpid() or not alive(other['pid']), 'other controller alive: '+str(path)
    controller = serial.RAW/(args.stage+'-controller.json')
    with gpu_lock():
        parent = serial.read(args.matrix/'manifest.json')
        frozen = serial.read(args.freeze)
        parent_sha = serial.sha(args.matrix/'manifest.json')
        assert parent['freeze_sha256'] == serial.sha(args.freeze)
        assert parent['binary_sha256'] == frozen['binary_sha256'] == serial.sha(frozen['binary'])
        assert frozen['started_epoch'] == serial.START and frozen['deadline_epoch'] == serial.DEADLINE
        assert not alive(parent['controller_pid']), 'timed controller is still alive'
        for job in parent['jobs'].values():
            assert not (job.get('pid') and alive(job['pid'])), 'timed child is still alive'
        expected = list(expected_tags(frozen))
        assert args.allow_partial or (parent['status'] == 'requested_phase_complete' and
                                      set(parent['jobs']) == {entry[0] for entry in expected})
        for field in ('harness_sha256', 'external_source_sha256', 'linked_input_sha256'):
            for path, digest in frozen[field].items():
                assert serial.sha(path) == digest, 'frozen dependency changed: '+path
        for path, digest in frozen['source_sha256'].items():
            assert serial.sha(serial.ROOT/path) == digest, 'frozen C++ source changed: '+path
        stage = serial.Stage(args.stage, frozen['binary'], settings=frozen['settings'], resume=args.resume)
        current_sources = {str(serial.BASE/name):serial.sha(serial.BASE/name) for name in
                           ('champion_judges.py', 'champion_audit.py', 'serial.py')}
        if 'parent_manifest_sha256' in stage.m:
            assert stage.m['parent_manifest_sha256'] == parent_sha, 'parent matrix changed after judges began'
            assert stage.m['judge_harness_sha256'] == current_sources, 'judge harness changed; preserve prior attempt'
        else:
            stage.m.update(parent_manifest_sha256=parent_sha, parent_manifest=str(args.matrix/'manifest.json'),
                parent_freeze_sha256=serial.sha(args.freeze), planned_runs=len(expected),
                judge_harness_sha256=current_sources, dataset='frozen formal TRAINING cases only; independent test not measured',
                evaluation='ASGP source CPU evaluator; GPU candidates native CPU interpreter; common projected exact/L1 metrics distinct from raw search fitness')
            stage.persist()
    record = dict(pid=os.getpid(), status='running', stage=str(stage.out), deadline_epoch=serial.DEADLINE)
    serial.save(controller, record)
    try:
        for tag, task, population, repetition, variant in expected:
            source_job = parent['jobs'].get(tag)
            with gpu_lock():
                if not source_job or source_job['status'] != 'ok':
                    if tag not in stage.m['jobs']:
                        stage.m['jobs'][tag] = dict(status='search_not_success',
                            search_status=source_job['status'] if source_job else 'missing',
                            task=task, population=population, repetition=repetition, variant=variant)
                    stage.persist()
                    continue
                if time.time() >= serial.DEADLINE-7200:
                    stage.m['jobs'].setdefault(tag, dict(status='budget_not_run', reason='two-hour reporting reserve',
                        task=task, population=population, repetition=repetition, variant=variant))
                    stage.persist()
                    continue
                inputs = frozen['inputs'][task]
                for field in ('source', 'grammar'):
                    assert serial.sha(inputs[field]) == inputs[field+'_sha256'], 'frozen input changed'
                for field in ('export', 'history', 'output'):
                    assert serial.sha(source_job[field]) == source_job[field+'_sha256'], 'formal artifact changed'
                export = Path(source_job['export'])
                proof = None
                if variant != 'asgp':
                    export = stage.out/(tag+'.selected-final.json')
                    proof = derived_best_export(Path(source_job['export']), source_job['export_sha256'], export)
                output = stage.out/(tag+'.cpu.json')
                command = ['quality-source', inputs['source'], export, inputs['grammar'], output, source_job['history']]
            job = stage.run(tag, command, dict(GAGP_BM_CASE_COUNT='1024'), timeout=180, reserve=7200)
            with gpu_lock():
                job.update(task=task, population=population, repetition=repetition, variant=variant,
                    output=str(output), parent_search_status=source_job['status'], parent_search_tag=tag,
                    selected_final_proof=proof, dataset='frozen training cases')
                if job['status'] == 'ok':
                    try:
                        data = serial.read(output)
                        summary = validate_judgment(data, task, frozen['generations'], frozen['cases'], variant)
                        prior_case_hash = stage.m.setdefault('case_hashes', {}).setdefault(task, summary['cases_sha256'])
                        assert summary['cases_sha256'] == prior_case_hash, 'case split differs across population/variant/repetition'
                        current = serial.sha(output)
                        if 'output_sha256' in job:
                            assert current == job['output_sha256'], 'completed CPU audit changed'
                        job.update(output_sha256=current, summary=summary, validation='all final and generation champions CPU audited')
                    except Exception as error:
                        job.update(status='invalid_output', validation_error=repr(error))
                stage.persist()
        with gpu_lock():
            assert serial.sha(args.matrix/'manifest.json') == parent_sha, 'timed matrix changed while judging'
            stage.m.update(status='requested_phase_complete', finished_epoch=time.time())
            stage.persist()
        record.update(status='requested_phase_complete', finished_epoch=time.time())
    except Exception as error:
        record.update(status='stopped', error=repr(error), finished_epoch=time.time())
        raise
    finally:
        serial.save(controller, record)


if __name__ == '__main__':
    main()
