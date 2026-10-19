"""Verify post-search CPU judgments without selecting or reranking any program."""
from pathlib import Path
from champion_audit import first_native_program, validate_judgment
from champion_judges import expected_tags
from report_audit import FileHashes, attempt_metadata, fingerprint
import serial


def score_fields(score):
    loss_field = 'exact_loss_sum' if 'exact_loss_sum' in score else 'capped_l1_sum'
    return dict(wrong=score['wrong'], runtime_errors=score['runtime_errors'],
                common_loss=score[loss_field], backend=score['backend'])


def verify_selection(proof, source_job, hashes):
    """A derivative must contain exactly the already chosen first native AST."""
    assert proof['parent_export'] == source_job['export']
    assert proof['parent_export_sha256'] == source_job['export_sha256']
    hashes.verify(source_job['export'], source_job['export_sha256'])
    hashes.verify(proof['derived_export'], proof['derived_export_sha256'])
    expected = first_native_program(source_job['export'])
    derived = serial.read(proof['derived_export'])
    assert derived == dict(programs=[expected]), 'derived export changes the preselected champion'
    assert fingerprint(expected) == proof['preselected_first_program_sha256']
    return proof['derived_export']


def audit_judges(frozen, parent, matrix, judges, parent_manifest_sha, hashes=None):
    """Keep one record per requested search, including missing/failed judgments."""
    hashes = hashes if hashes is not None else FileHashes()
    expected = list(expected_tags(frozen))
    judge_path = Path(judges)/'manifest.json'
    manifest = serial.read(judge_path) if judge_path.exists() else None
    if manifest is not None:
        hashes.verify(judge_path, serial.sha(judge_path))
        assert manifest['parent_manifest_sha256'] == parent_manifest_sha
        assert manifest['parent_freeze_sha256'] == parent['freeze_sha256']
        assert manifest['settings'] == frozen['settings']
        assert manifest['binary_sha256'] == frozen['binary_sha256']
        hashes.verify(Path(judges)/'bench', frozen['binary_sha256'])
        for path, digest in manifest['judge_harness_sha256'].items():
            hashes.verify(path, digest)
        assert set(manifest['jobs']).issubset({row[0] for row in expected}), 'unexpected CPU judge samples'
    records, champions, quality, issues, case_hashes = [], [], {}, [], {}
    last_search_end = max((j.get('started_epoch', 0)+j.get('process_s', 0)
                          for j in parent['jobs'].values() if j.get('pid')), default=0)
    for tag, task, population, repetition, variant in expected:
        source_job = parent['jobs'].get(tag)
        job = manifest['jobs'].get(tag) if manifest else None
        row = dict(tag=tag, task=task, population=population, repetition=repetition, variant=variant,
                   status=job['status'] if job else 'missing',
                   original_status=job['status'] if job else 'missing', audit_ok=False,
                   process_s=job.get('process_s') if job else None)
        if job:
            row.update(attempt_metadata(job))
        if not job or job['status'] != 'ok':
            if job:
                row.update(process_s=job.get('process_s'), search_status=job.get('search_status'))
            records.append(row)
            continue
        try:
            assert source_job and source_job['status'] == 'ok', 'no successful parent search'
            assert (job['task'],job['population'],job['repetition'],job['variant']) == (task,population,repetition,variant)
            for field in ('output','export','history'):
                hashes.verify(source_job[field], source_job[field+'_sha256'])
            item = frozen['inputs'][task]
            export = source_job['export']
            if variant == 'asgp':
                assert job['selected_final_proof'] is None
            else:
                export = verify_selection(job['selected_final_proof'], source_job, hashes)
            output = str((Path(judges)/(tag+'.cpu.json')).resolve())
            command = [str((Path(judges)/'bench').resolve()), 'quality-source', item['source'],
                       export, item['grammar'], output, source_job['history']]
            assert job['command'] == command, 'wrong CPU judge command/history/champion'
            assert job['extra'] == dict(GAGP_BM_CASE_COUNT='1024')
            expected_inputs = {item['source']:item['source_sha256'], item['grammar']:item['grammar_sha256'],
                               export:serial.sha(export), source_job['history']:source_job['history_sha256']}
            assert job['input_sha256'] == expected_inputs
            assert job['output'] == output
            assert job['started_epoch'] >= last_search_end, 'CPU judge overlaps formal timing'
            assert job['process_s'] >= 0
            assert job['started_epoch']+job['process_s'] <= serial.DEADLINE+5
            hashes.verify(output, job['output_sha256'])
            data = serial.read(output)
            summary = validate_judgment(data, task, frozen['generations'], frozen['cases'], variant)
            assert summary == job['summary'], 'stored CPU summary disagrees with original case results'
            if task not in case_hashes:
                # Read this source once, after timing and under the global lock.
                # C++ canonical JSON and this input use sorted object keys and
                # integral numeric fields; the frozen full source hash binds it.
                hashes.verify(item['source'], item['source_sha256'])
                cases = serial.read(item['source'])['cases']
                assert len(cases) == frozen['cases']
                case_hashes[task] = fingerprint(cases)
            assert summary['cases_sha256'] == case_hashes[task], 'CPU judge used different cases'
            row.update(status='ok', audit_ok=True, process_s=job['process_s'],
                       output=output, output_sha256=job['output_sha256'], **summary)
            if repetition == 0:
                quality.setdefault(f'{task}.p{population}', {})[variant] = summary
            for index, score in [(r['generation'], r) for r in data['history']]+[(frozen['generations'],data['final'])]:
                champions.append(dict(tag=tag,task=task,population=population,variant=variant,
                    repetition=repetition,generation=index,stage='final' if index == frozen['generations'] else 'history',
                    cases=frozen['cases'],cases_sha256=summary['cases_sha256'],**score_fields(score)))
        except Exception as error:
            row.update(status='audit_invalid', audit_error=repr(error), audit_ok=False)
            issues.append(dict(tag=tag, issue=repr(error)))
        records.append(row)
    hashes.verify_stable()
    return dict(records=records, champions=champions, quality=quality, issues=issues,
                cases_sha256=case_hashes, requested_runs=len(expected),
                judge_manifest_present=manifest is not None)


def quality_counts(records, population, variant):
    """One seed per task, repetition zero only; do not count timing repeats twice."""
    selected = [r for r in records if r['population'] == population and r['variant'] == variant and r['repetition'] == 0]
    valid = [r for r in selected if r['status'] == 'ok' and r.get('audit_ok') is True]
    return dict(population=population, variant=variant, requested_tasks=len(selected), audited_tasks=len(valid),
        initial_success=sum(r['initial_success'] for r in valid),
        final_success=sum(r['final_success'] for r in valid),
        final_success_after_gen0=sum(r['final_success_after_gen0'] for r in valid),
        initial_success_lost=sum(r['initial_success_lost'] for r in valid),
        independent_search_seeds=1, dataset='fixed training only; no heldout measured')
