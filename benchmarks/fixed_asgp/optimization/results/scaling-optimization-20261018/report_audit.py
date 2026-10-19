"""Independent post-timing report checks. No source artifacts are modified."""
import hashlib
import json
import math
from pathlib import Path
import serial
from pilot import validate
from report_stats import VARIANTS


class FileHashes:
    def __init__(self):
        self.records = {}

    def verify(self, path, expected):
        path = Path(path)
        stat = path.stat()
        identity = (stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns)
        prior = self.records.get(str(path))
        if prior is None:
            prior = self.records[str(path)] = dict(identity=identity, sha256=serial.sha(path))
        assert prior['identity'] == identity, 'file changed during report: '+str(path)
        assert prior['sha256'] == expected, 'frozen hash differs: '+str(path)
        return prior['sha256']

    def verify_stable(self):
        for name, record in self.records.items():
            stat = Path(name).stat()
            assert record['identity'] == (stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns), 'file changed during report: '+name


def fingerprint(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'),
                                      ensure_ascii=False, allow_nan=False).encode()).hexdigest()



def attempt_metadata(job):
    """Keep original attempts traceable even if their report audit is invalid.

    Metadata is copied, never used to grant timing/fidelity eligibility. Missing
    outputs remain missing; this function does not read or fabricate an artifact.
    """
    out = {key:job[key] for key in ('started_epoch','pid','returncode','log','output','export','history') if key in job}
    if 'validation_error' in job:
        out['original_validation_error'] = job['validation_error']
    if 'reason' in job:
        out['original_failure_reason'] = job['reason']
    return out


def expected_job(frozen, matrix, tag, task, population, repetition, variant):
    matrix = Path(matrix).resolve()
    item = frozen['inputs'][task]
    mode = 'asgp_1t' if variant == 'asgp' else 'gpu_overlap'
    output = str(matrix/(tag+'.json'))
    export = str(matrix/(tag+'.export.json'))
    history = str(matrix/(tag+'.history.json'))
    extra = dict(GAGP_SEARCH_GENERATIONS=str(frozen['generations']), GAGP_BM_SEED='0',
                 GAGP_BM_CASE_COUNT='1024', GAGP_SEARCH_EXPORT=export, GAGP_SEARCH_HISTORY_EXPORT=history)
    if repetition != 0:
        extra['GAGP_SEARCH_EXPORT_BEST_ONLY'] = '1'
    if variant != 'asgp':
        extra.update(GAGP_GPU_PRIVATE_TUPLE_SCORE='1' if variant == 'gpu_improved' else '0',
                     GAGP_BM_GPU_BLOCKSIZE='512')
        if repetition == 0:
            extra['GAGP_SEARCH_CPU_ALL'] = '1'
    command = [str(matrix/'bench'), 'measure', item['source'],
               item['prefixes'][str(population)]['prepared'], item['grammar'], str(population), 'search_'+mode, output]
    return dict(command=command, extra=extra, mode=mode, output=output, export=export, history=history)


def validate_run(job, raw, history, tag, task, population, repetition, variant, generations):
    """Recheck actual output, preserving work status separately from CPU fidelity."""
    assert job['status'] == 'ok'
    assert (job['task'], job['population'], job['repetition'], job['variant']) == (task, population, repetition, variant)
    mode = 'asgp_1t' if variant == 'asgp' else 'gpu_overlap'
    assert job['mode'] == raw['mode'] == mode
    full = repetition == 0 and variant != 'asgp'
    checked = validate(raw, mode, population, generations, full)
    assert math.isclose(job['search_s'], checked['search_s'], rel_tol=1e-12, abs_tol=1e-9)
    assert job['process_s'] >= checked['search_s']-0.001
    assert job['full_final_cpu_audit'] == full
    rows = raw['generations']
    assert [row['generation'] for row in rows] == list(range(generations))
    assert [row['generation'] for row in history['history']] == list(range(generations))
    result = dict(tag=tag, task=task, population=population, repetition=repetition, variant=variant,
        status=job['status'], original_status=job['status'], audit_ok=True,
        search_s=checked['search_s'], process_s=job['process_s'], seed=0, generations=generations,
        full_final_cpu_audit=full, cpu_audit_s=raw.get('cpu_audit_ms', 0)/1000,
        cpu_audit_program_count=checked['cpu_audit_program_count'],
        cpu_audit_wrong_program_count=raw.get('cpu_audit_wrong_program_count'),
        cpu_audit_fitness_mismatches=checked['cpu_audit_fitness_mismatches'],
        cpu_audit_wrong_mismatches=checked['cpu_audit_wrong_mismatches'],
        sampled_card_peak_bytes=job.get('sampled_card_peak_bytes'),
        peak_rss_bytes=job.get('peak_rss_bytes'),
        output=job['output'], export=job['export'], history=job['history'])
    if variant != 'asgp':
        assert raw['gpu_blocksize'] == raw['cpu_audit_reduction_lanes'] == 512
        assert raw['verification_top_k'] == 0 and raw['verification_near_zero'] == -1
        guard_fields = ('programs','cases','fitness_corrections','wrong_corrections','wall_ms')
        # Offline final-population CPU auditing is separate from a CPU guard
        # inside the timed GPU search. This frozen arm has no training guard.
        for verification in [raw['verification'], *(row['verification'] for row in rows)]:
            assert all(verification[field] == 0 for field in guard_fields), 'unexpected timed CPU training guard'
        result.update(cpu_training_guard_programs=raw['verification']['programs'],
            cpu_training_guard_cases=raw['verification']['cases'],
            cpu_training_guard_s=raw['verification']['wall_ms']/1000,
            cpu_training_fitness_corrections=raw['verification']['fitness_corrections'],
            cpu_training_wrong_corrections=raw['verification']['wrong_corrections'])
        assert job['cpu_audit_program_count'] == checked['cpu_audit_program_count']
        assert job['cpu_audit_fitness_mismatches'] == checked['cpu_audit_fitness_mismatches']
        assert job['cpu_audit_wrong_mismatches'] == checked['cpu_audit_wrong_mismatches']
        result['fidelity_ok'] = checked['cpu_audit_fitness_mismatches'] == checked['cpu_audit_wrong_mismatches'] == 0
        assert job['fidelity_ok'] == result['fidelity_ok']
        assert max(row.get('payload_workspace_retained_bytes', 0) for row in rows) <= 16*1024**3
        result.update(reproduction_profile=raw['reproduction_profile'],
            genotype_device_bytes=raw.get('genotype_device_bytes'),
            observed_evaluated_program_cases=sum(row['cases'] for row in rows),
            unscored=sum(row['unscored'] for row in rows),
            cpu_audit_wrong_program_count=raw['cpu_audit_wrong_program_count'])
    native = variant != 'asgp'
    signature = dict(final_descriptors=fingerprint(raw['final_population']),
        champion_fitness=[row['best'] for row in rows],
        champion_asts=fingerprint([row['ast' if native else 'program'] for row in history['history']]),
        final_export=job['export_sha256'])
    return result, signature


def compare_execution(a, b, task, population, repetition):
    return dict(task=task, population=population, repetition=repetition,
        final_descriptors_equal=a['final_descriptors'] == b['final_descriptors'],
        champion_fitness_equal=a['champion_fitness'] == b['champion_fitness'],
        champion_ast_equal=a['champion_asts'] == b['champion_asts'],
        final_export_equal=a['final_export'] == b['final_export'],
        full_final_ast_comparison=repetition == 0)


def verify_global_provenance(frozen, parent, matrix, freeze_path, hashes):
    assert frozen['started_epoch'] == serial.START and frozen['deadline_epoch'] == serial.DEADLINE
    assert frozen['populations'] == [1024,16384] and frozen['cases'] == 1024 and frozen['seed'] == 0
    assert frozen['generations'] == 8 and frozen['repetitions'] == 2 and frozen['planned_runs'] == 360
    assert frozen['gpu_blocksize'] == 512 and frozen['compact_root_vm'] is False
    assert set(frozen['tasks']) == set(serial.PAPER_TASKS) and len(frozen['tasks']) == 30
    assert frozen['settings'] == parent['settings']
    assert frozen['settings']['GAGP_GPU_PAYLOAD_WORKSPACE_MIB'] == '16384'
    assert frozen['settings']['GAGP_HOST_THREADS'] == '1'
    assert parent['freeze_sha256'] == serial.sha(freeze_path)
    assert parent['binary_sha256'] == frozen['binary_sha256']
    hashes.verify(frozen['binary'], frozen['binary_sha256'])
    hashes.verify(Path(matrix)/'bench', frozen['binary_sha256'])
    for field in ('harness_sha256','external_source_sha256','linked_input_sha256','allocation_inputs_sha256'):
        for path, digest in frozen[field].items():
            hashes.verify(path, digest)
    hashes.verify(serial.RAW/'compact-root-selection.json', frozen['selection_sha256'])
    for path, digest in frozen['source_sha256'].items():
        hashes.verify(serial.ROOT/path, digest)
    for task in frozen['tasks']:
        item = frozen['inputs'][task]
        for field in ('source','grammar'):
            hashes.verify(item[field], item[field+'_sha256'])
        for population in frozen['populations']:
            prefix = item['prefixes'][str(population)]
            hashes.verify(prefix['prepared'], prefix['sha256'])


def audit_matrix(frozen, parent, matrix, freeze_path, hashes=None):
    """Called only after controller/children exit, under the global GPU lock."""
    from champion_judges import expected_tags
    hashes = hashes if hashes is not None else FileHashes()
    verify_global_provenance(frozen, parent, matrix, freeze_path, hashes)
    assert set(parent['jobs']).issubset({row[0] for row in expected_tags(frozen)}), 'unexpected matrix samples'
    records, signatures, issues = [], {}, []
    for tag, task, population, repetition, variant in expected_tags(frozen):
        job = parent['jobs'].get(tag)
        base = dict(tag=tag, task=task, population=population, repetition=repetition,
                    variant=variant, status='missing', original_status='missing', audit_ok=False)
        if job is None:
            records.append(base)
            continue
        base.update(original_status=job['status'], status=job['status'], process_s=job.get('process_s'),
                    peak_rss_bytes=job.get('peak_rss_bytes'), sampled_card_peak_bytes=job.get('sampled_card_peak_bytes'))
        try:
            expected = expected_job(frozen, matrix, tag, task, population, repetition, variant)
            assert job['command'] == expected['command'], 'unexpected command or profiled search'
            assert job['extra'] == expected['extra'], 'unexpected settings'
            assert all(job[field] == expected[field] for field in ('mode','output','export','history'))
            if job.get('inputs_not_read'):
                assert job['status'] == 'budget_not_run' and 'pid' not in job
            else:
                item = frozen['inputs'][task]
                required = {item['source']:item['source_sha256'], item['grammar']:item['grammar_sha256'],
                            item['prefixes'][str(population)]['prepared']:item['prefixes'][str(population)]['sha256']}
                assert job['input_sha256'] == required, 'per-run inputs differ from freeze'
            if 'pid' in job:
                earliest = max(frozen['frozen_epoch'], frozen.get('corrected_epoch', 0))
                assert job['started_epoch'] >= earliest, 'sample predates this formal freeze'
                assert job.get('process_s', 0) >= 0
                if job['status'] != 'running':
                    assert job['started_epoch']+job['process_s'] <= serial.DEADLINE+5, 'sample exceeded original deadline'
            base['audit_ok'] = True
            if job['status'] == 'ok':
                for field in ('output','export','history'):
                    hashes.verify(job[field], job[field+'_sha256'])
                raw, history = serial.read(job['output']), serial.read(job['history'])
                base, signatures[tag] = validate_run(job, raw, history, tag, task, population, repetition,
                                                     variant, frozen['generations'])
        except Exception as error:
            base.update(status='audit_invalid', audit_ok=False, audit_error=repr(error))
            issues.append(dict(tag=tag, issue=repr(error)))
        base.update(attempt_metadata(job))
        records.append(base)
    comparisons = {}
    for task in frozen['tasks']:
        for population in frozen['populations']:
            for repetition in range(frozen['repetitions']):
                key = f'{task}.p{population}.r{repetition}'
                tags = [f'{task}.p{population}.{variant}.s0.g{frozen["generations"]}.r{repetition}'
                        for variant in ('gpu_original','gpu_improved')]
                if all(tag in signatures for tag in tags):
                    observed = compare_execution(*(signatures[tag] for tag in tags), task, population, repetition)
                    comparisons[key] = observed
                    recorded = parent.get('execution_comparisons', {}).get(key)
                    if recorded != observed:
                        # Do not use a contradictory comparison as ratio eligibility.
                        observed['final_descriptors_equal'] = False
                        issues.append(dict(pair=key, issue='recomputed execution comparison differs from original manifest'))
    hashes.verify_stable()
    return dict(records=records, execution_comparisons=comparisons, issues=issues,
                file_sha256={name:record['sha256'] for name,record in hashes.records.items()},
                initial_source_relation='Same frozen source path/seed and unchanged per-population prepared artifacts; no new universal source/native translation equivalence proof',
                dataset='fixed1024 training cases; no independent test split measured in this speed matrix')
