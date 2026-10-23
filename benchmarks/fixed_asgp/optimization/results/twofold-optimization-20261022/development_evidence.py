"""Checked development matrices, separate from formal speed/quality evidence.

This reader can supply a development appendix or an honest partial report if
the runtime rule cannot admit a formal matrix. It never creates a freeze or
infers quality, and never qualifies the thread objective from development data.
"""
import hashlib
import json
import math
from pathlib import Path

import report_data
import serial
import study_statistics
import vm_pilot


FIDELITY = ('final_descriptors_equal', 'full_final_ast_equal',
            'champion_ast_fitness_equal')
RUN_SETTINGS = {'GAGP_SEARCH_GENERATIONS', 'GAGP_BM_SEED', 'GAGP_BM_CASE_COUNT',
                'GAGP_SEARCH_EXPORT', 'GAGP_SEARCH_HISTORY_EXPORT'}


def summarize(rows, tasks, populations=(1024, 16384)):
    expected = {(task, p) for task in tasks for p in populations}
    if not tasks or len(set(tasks)) != len(tasks):
        raise ValueError('nonempty distinct task list required')
    groups = {}
    for row in rows:
        key = (row['task'], row['population'])
        family = (row['family'], row['variant'])
        group = groups.setdefault(family, {})
        if key not in expected or key in group:
            raise ValueError('unexpected or duplicate development cell')
        group[key] = row
    recipes = []
    for (family, variant), lookup in sorted(groups.items()):
        qualified = [r for r in lookup.values() if
                     r.get('status') == 'ok' and all(r.get(k) is True for k in FIDELITY)
                     and type(r.get('speedup')) in (float, int)
                     and math.isfinite(r['speedup']) and r['speedup'] > 0]
        # A fast partial subset is never labeled as a full-task mean.
        complete = len(qualified) == len(expected)
        means = {str(p): study_statistics.geometric_mean(
                    r['speedup'] for r in qualified if r['population'] == p)
                 if sum(r['population'] == p for r in qualified) == len(tasks) else None
                 for p in populations}
        means['pooled'] = study_statistics.geometric_mean(
            r['speedup'] for r in qualified) if complete else None
        recipes.append(dict(family=family, variant=variant, qualified_cells=len(qualified),
                            expected_cells=len(expected), complete=complete,
                            geometric_means=means))
    eligible = [r for r in recipes if r['complete']]
    return dict(scope='old TRAINING development only; G8/R1/seed0/CPU16',
                task_count=len(tasks), populations=list(populations),
                recipes=recipes, best_complete=max(eligible,
                    key=lambda r: r['geometric_means']['pooled']) if eligible else None,
                formal_target_proven=False, independent_quality_proven=False,
                full_population_cpu_audit_proven=False)


def fingerprint(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'),
                                     ensure_ascii=False).encode()).hexdigest()


def read_families(families, protocol, raw=serial.RAW):
    """Recompute ratios and fidelity from immutable outputs, retaining failures."""
    evidence = report_data.Evidence()
    output_cache = {}
    stage_cache = {}
    baseline_sha = protocol['baseline_sha256']
    tasks = protocol['tasks']
    rows = []
    recipe_identity = {}

    def stage(name):
        if name not in stage_cache:
            path = raw / name / 'manifest.json'
            if not path.exists():
                stage_cache[name] = dict(status='missing', jobs={})
            else:
                data = serial.read(path)
                assert data['settings'] == protocol['settings'], 'global settings differ'
                assert serial.sha(path.parent / 'bench') == data['binary_sha256']
                evidence.hashes[str(path)] = serial.sha(path)
                evidence.hashes[str(path.parent / 'bench')] = data['binary_sha256']
                stage_cache[name] = data
        return stage_cache[name]

    def output(job):
        key = (job['output'], job['output_sha256'])
        if key not in output_cache:
            data = evidence.output(job)
            vm_pilot.validate(data, job['population'], 8)
            source = protocol['inputs'][job['task']]
            expected = [(source['source'], source['source_sha256']),
                        (source['prefixes'][str(job['population'])]['prepared'],
                         source['prefixes'][str(job['population'])]['sha256']),
                        (source['grammar'], source['grammar_sha256'])]
            for field, index in [('source', 2), ('prepared', 3), ('grammar', 4)]:
                path = job['command'][index]
                expected_path, expected_sha = expected[index - 2]
                assert path == expected_path and job['input_sha256'][path] == expected_sha
                if path not in evidence.hashes:
                    assert serial.sha(path) == job['input_sha256'][path]
                    evidence.hashes[path] = job['input_sha256'][path]
                assert evidence.hashes[path] == expected_sha
            assert job['command'][1] == 'measure' and job['command'][6] == 'search_gpu_overlap'
            assert job['extra']['GAGP_SEARCH_GENERATIONS'] == '8'
            assert job['extra']['GAGP_BM_SEED'] == '0'
            assert job['extra']['GAGP_BM_CASE_COUNT'] == '1024'
            assert int(job['command'][5]) == job['population']
            assert serial.START <= job['started_epoch'] < serial.DEADLINE
            assert job['started_epoch'] + job['process_s'] <= serial.DEADLINE
            export = Path(job['export'])
            if str(export) not in evidence.hashes:
                assert serial.sha(export) == job['export_sha256']
                evidence.hashes[str(export)] = job['export_sha256']
            history = evidence.output(job, 'history')
            output_cache[key] = dict(search_s=data['evolve_wall_ms'] / 1000,
                descriptors=fingerprint(data['final_population']), export=job['export_sha256'],
                champions=fingerprint([(g.get('ast', g.get('program')), g['fitness'])
                                      for g in history['history']]))
        return output_cache[key]

    for family in families:
        comparisons = []
        for p in (1024, 16384):
            path = raw / f'{family}-p{p}-comparisons.json'
            if path.exists():
                document = serial.read(path)
                comparisons.extend(document)
                evidence.hashes[str(path)] = serial.sha(path)
        lookup = {}
        for row in comparisons:
            key = (row['task'], row['population'], row['variant'])
            if key in lookup or row['task'] not in tasks or row['population'] not in (1024, 16384):
                raise ValueError('unexpected or duplicate recorded comparison')
            lookup[key] = row
        variants = {row['variant'] for row in comparisons}
        # An arm that failed before producing any comparison is still an arm.
        for p in (1024, 16384):
            prefix = f'{family}-p{p}-'
            variants.update(path.parent.name[len(prefix):]
                            for path in raw.glob(prefix + '*/manifest.json'))
        variants.discard('baseline')
        variants = sorted(variants)
        for variant in variants:
            for p in (1024, 16384):
                base = stage(f'{family}-p{p}-baseline')
                candidate = stage(f'{family}-p{p}-{variant}')
                if base['status'] != 'missing':
                    assert base['binary_sha256'] == baseline_sha, 'historical/other denominator'
                for task in tasks:
                    row = dict(family=family, variant=variant, task=task, population=p,
                               generations=8, repetitions=1, seed=0, status='missing')
                    tag = f'{task}.p{p}.s0.g8'
                    jobs = [m['jobs'].get(tag, dict(status='missing')) for m in (base, candidate)]
                    row['statuses'] = [j['status'] for j in jobs]
                    recorded = lookup.get((task, p, variant))
                    if all(j['status'] == 'ok' for j in jobs) and recorded is not None:
                        assert all(j['task'] == task and j['population'] == p for j in jobs)
                        a, b = [output(j) for j in jobs]
                        assert jobs[0]['input_sha256'] == jobs[1]['input_sha256']
                        identity = (candidate['binary_sha256'],
                            fingerprint({k: v for k, v in jobs[1]['extra'].items() if k not in RUN_SETTINGS}))
                        family_key = (family, variant)
                        if family_key in recipe_identity:
                            assert recipe_identity[family_key] == identity, 'per-task/P recipe change'
                        recipe_identity[family_key] = identity
                        base_identity = (base['binary_sha256'],
                            fingerprint({k: v for k, v in jobs[0]['extra'].items() if k not in RUN_SETTINGS}))
                        base_key = (family, 'baseline')
                        if base_key in recipe_identity:
                            assert recipe_identity[base_key] == base_identity, 'baseline per-task/P recipe change'
                        recipe_identity[base_key] = base_identity
                        ratio = a['search_s'] / b['search_s']
                        assert math.isclose(ratio, recorded['speedup'], rel_tol=1e-10)
                        fidelity = dict(final_descriptors_equal=a['descriptors'] == b['descriptors'],
                            full_final_ast_equal=a['export'] == b['export'],
                            champion_ast_fitness_equal=a['champions'] == b['champions'])
                        assert all(fidelity[k] == recorded[k] for k in FIDELITY)
                        row.update(status='ok' if all(fidelity.values()) else 'fidelity_failed',
                            baseline_search_s=a['search_s'], candidate_search_s=b['search_s'],
                            speedup=ratio, candidate_binary_sha256=identity[0], **fidelity)
                    elif any(j['status'] not in ('ok', 'missing') for j in jobs):
                        row['status'] = 'failed_or_unfinished'
                    rows.append(row)
    result = summarize(rows, tasks)
    result.update(cells=rows, checked_artifact_sha256=evidence.hashes,
                  provenance='fresh within-round denominator; checked outputs/exports/champions/inputs/binaries/settings')
    return result
