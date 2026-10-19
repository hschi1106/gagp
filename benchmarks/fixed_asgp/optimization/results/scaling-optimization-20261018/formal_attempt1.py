"""Explicit corrected formal attempt: distinct controller and stage locks; same frozen matrix."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import time
import serial
from development import alive, gpu_lock
from pilot import validate

VARIANTS = ('asgp', 'gpu_original', 'gpu_improved')


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--resume', action='store_true')
    args = parser.parse_args()
    controller_lock = (serial.RAW / 'formal-attempt1-controller.lock').open('a')
    fcntl.flock(controller_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    for p in serial.RAW.glob('*controller.json'):
        controller = serial.read(p)
        assert controller['pid'] == os.getpid() or not alive(controller['pid']), f'controller is still active: {p}'
    freeze_path = serial.RAW / 'formal-freeze-attempt1.json'
    frozen = serial.read(freeze_path)
    assert frozen['started_epoch'] == serial.START and frozen['deadline_epoch'] == serial.DEADLINE
    assert frozen['generations'] >= 4 and frozen['repetitions'] >= 1
    assert set(frozen['tasks']) == set(serial.PAPER_TASKS) and len(frozen['tasks']) == 30
    assert frozen['populations'] == [1024, 16384]
    assert frozen['settings'] == serial.read(serial.BASE / 'protocol.json')['settings']
    with gpu_lock():
        assert serial.sha(frozen['binary']) == frozen['binary_sha256']
        for path, digest in frozen['harness_sha256'].items():
            assert serial.sha(path) == digest, 'frozen harness changed: ' + path
        for field in ('external_source_sha256', 'linked_input_sha256'):
            for path, digest in frozen.get(field, {}).items():
                assert serial.sha(path) == digest, 'frozen dependency changed: ' + path
        for path, digest in frozen['source_sha256'].items():
            assert serial.sha(serial.ROOT / path) == digest, 'frozen source changed: ' + path
    with gpu_lock():
        stage = serial.Stage('formal-attempt1', frozen['binary'], settings=frozen['settings'], resume=args.resume)
    digest = serial.sha(freeze_path)
    if 'freeze_sha256' in stage.m:
        assert stage.m['freeze_sha256'] == digest, 'formal freeze changed'
    else:
        stage.m.update(freeze_sha256=digest, freeze=frozen, planned_runs=30*2*3*frozen['repetitions'])
        stage.persist()
    controller_path = serial.RAW / 'formal-attempt1-controller.json'
    record = dict(pid=os.getpid(), status='running', started_epoch=serial.START,
                  deadline_epoch=serial.DEADLINE, stage=str(stage.out), freeze_sha256=digest)
    serial.save(controller_path, record)
    try:
        for ti, task in enumerate(frozen['tasks']):
            for pi, population in enumerate(frozen['populations']):
                inp = frozen['inputs'][task]
                prepared = inp['prefixes'][str(population)]
                with gpu_lock():
                    for path, expected in ((inp['source'], inp['source_sha256']),
                                           (inp['grammar'], inp['grammar_sha256']),
                                           (prepared['prepared'], prepared['sha256'])):
                        assert serial.sha(path) == expected, 'frozen input changed: ' + path
                for rep in range(frozen['repetitions']):
                    offset = (ti + pi + rep) % len(VARIANTS)
                    for variant in VARIANTS[offset:] + VARIANTS[:offset]:
                        mode = 'asgp_1t' if variant == 'asgp' else 'gpu_overlap'
                        tag = f'{task}.p{population}.{variant}.s0.g{frozen["generations"]}.r{rep}'
                        output = stage.out / (tag + '.json')
                        export = stage.out / (tag + '.export.json')
                        history = stage.out / (tag + '.history.json')
                        full = rep == 0
                        extra = dict(GAGP_SEARCH_GENERATIONS=str(frozen['generations']),
                                     GAGP_BM_SEED='0', GAGP_BM_CASE_COUNT='1024',
                                     GAGP_SEARCH_EXPORT=str(export), GAGP_SEARCH_HISTORY_EXPORT=str(history))
                        if not full:
                            extra['GAGP_SEARCH_EXPORT_BEST_ONLY'] = '1'
                        if mode == 'gpu_overlap':
                            extra.update(GAGP_GPU_PRIVATE_TUPLE_SCORE='1' if variant == 'gpu_improved' else '0',
                                         GAGP_BM_GPU_BLOCKSIZE=str(frozen['gpu_blocksize'] if variant == 'gpu_improved' else 512))
                            if variant == 'gpu_improved' and frozen['compact_root_vm']:
                                extra['GAGP_GPU_COMPACT_ROOT_VM'] = '1'
                            if full:
                                extra['GAGP_SEARCH_CPU_ALL'] = '1'
                        job = stage.run(tag, ['measure', inp['source'], prepared['prepared'],
                                             inp['grammar'], population, 'search_' + mode, output],
                                        extra, timeout=frozen['timeout_s'], reserve=frozen['reserve_s'])
                        with gpu_lock():
                            job.update(task=task, population=population, variant=variant, mode=mode,
                                       generations=frozen['generations'], repetition=rep, seed=0,
                                       output=str(output), export=str(export), history=str(history),
                                       full_final_cpu_audit=full and mode=='gpu_overlap')
                            if job['status'] == 'ok':
                                try:
                                    data = serial.read(output)
                                    job.update(**validate(data, mode, population, frozen['generations'], full))
                                    if mode == 'gpu_overlap':
                                        block = frozen['gpu_blocksize'] if variant=='gpu_improved' else 512
                                        assert data['gpu_blocksize'] == data['cpu_audit_reduction_lanes'] == block
                                        job['fidelity_ok'] = (job['cpu_audit_fitness_mismatches'] ==
                                                              job['cpu_audit_wrong_mismatches'] == 0)
                                        job['validation'] = 'work_complete' if job['fidelity_ok'] else 'fidelity_mismatch'
                                    for field, path in (('output', output), ('export', export), ('history', history)):
                                        current = serial.sha(path)
                                        if field+'_sha256' in job:
                                            assert current == job[field+'_sha256'], 'completed artifact changed'
                                        job[field+'_sha256'] = current
                                except Exception as error:
                                    job.update(status='invalid_output', validation_error=repr(error))
                            stage.persist()
                    with gpu_lock():
                        jobs = [stage.m['jobs'][f'{task}.p{population}.{variant}.s0.g{frozen["generations"]}.r{rep}']
                                for variant in ('gpu_original', 'gpu_improved')]
                        if all(j['status']=='ok' for j in jobs):
                            a, b = [serial.read(j['output']) for j in jobs]
                            ah, bh = [serial.read(j['history']) for j in jobs]
                            observation = dict(task=task, population=population, repetition=rep,
                                final_descriptors_equal=a['final_population']==b['final_population'],
                                champion_fitness_equal=[r['best'] for r in a['generations']]==
                                                      [r['best'] for r in b['generations']],
                                champion_ast_equal=[r['ast'] for r in ah['history']]==[r['ast'] for r in bh['history']],
                                final_export_equal=jobs[0]['export_sha256']==jobs[1]['export_sha256'],
                                full_final_ast_comparison=full)
                            stage.m.setdefault('execution_comparisons', {})[f'{task}.p{population}.r{rep}'] = observation
                            stage.persist()
        stage.m.update(status='requested_phase_complete', finished_epoch=time.time())
        stage.persist()
        record.update(status='requested_phase_complete', finished_epoch=time.time())
    except Exception as error:
        record.update(status='stopped', error=repr(error), finished_epoch=time.time())
        raise
    finally:
        serial.save(controller_path, record)


if __name__ == '__main__':
    main()

