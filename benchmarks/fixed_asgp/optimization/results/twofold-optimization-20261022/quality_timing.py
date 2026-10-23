"""Describe the fresh quality workload without replacing the primary speed grid.

Call only with report_data's hash-checked runs and paired champion audits.
Every frozen task/seed remains present. An overall mean requires every pair;
successful timing alone never substitutes for missing or failed native judges.
"""
import math

import study_statistics


def summarize(runs, pairs, tasks, seeds, population, generations):
    if not tasks or not seeds or len(set(tasks)) != len(tasks) or len(set(seeds)) != len(seeds):
        raise ValueError('distinct nonempty frozen tasks and seeds required')
    expected = {(task, seed) for task in tasks for seed in seeds}
    run_lookup = {}
    for run in runs:
        key = (run['task'], run['seed'], run['variant'])
        if key[:2] not in expected or key[2] not in ('baseline', 'candidate') or key in run_lookup:
            raise ValueError('unexpected or duplicate quality timing run')
        if run['population'] != population or run['generations'] != generations:
            raise ValueError('quality timing workload differs from freeze')
        if run['status'] == 'ok':
            value = run.get('search_s')
            if type(value) not in (int, float) or not math.isfinite(value) or value <= 0:
                raise ValueError('invalid successful quality search time')
        run_lookup[key] = run
    pair_lookup = {}
    for pair in pairs:
        key = (pair['task'], pair['seed'])
        if key not in expected or key in pair_lookup:
            raise ValueError('unexpected or duplicate quality timing pair')
        pair_lookup[key] = pair
    rows = []
    for task in tasks:
        for seed in seeds:
            pair = pair_lookup.get((task, seed), {})
            a, b = [run_lookup.get((task, seed, variant), {}) for variant in ('baseline', 'candidate')]
            known = (pair.get('complete_pair') is True and pair.get('execution_fidelity') is True
                     and all(run.get('status') == 'ok' and run.get('quality_audited') is True for run in (a, b)))
            times = [run.get('search_s') if run.get('status') == 'ok' else None for run in (a, b)]
            rows.append(dict(task=task, seed=seed, audited_pair=known,
                baseline_status=a.get('status', 'missing'), candidate_status=b.get('status', 'missing'),
                baseline_search_s=times[0], candidate_search_s=times[1],
                speedup=times[0] / times[1] if known else None))
    task_rows = []
    for task in tasks:
        ratios = [row['speedup'] for row in rows if row['task'] == task and row['audited_pair']]
        task_rows.append(dict(task=task, expected_pairs=len(seeds), audited_pairs=len(ratios),
            unknown_pairs=len(seeds)-len(ratios), full_coverage=len(ratios) == len(seeds),
            observed_geometric_mean=study_statistics.geometric_mean(ratios) if ratios else None,
            observed_seed_ratio_range=[min(ratios), max(ratios)] if ratios else None))
    known = sum(row['audited_pair'] for row in rows)
    complete = known == len(expected)
    mean = study_statistics.geometric_mean(row['observed_geometric_mean'] for row in task_rows) if complete else None
    if complete:
        assert math.isclose(mean, study_statistics.geometric_mean(row['speedup'] for row in rows), rel_tol=1e-12)
    return dict(population=population, generations=generations, expected_pairs=len(expected),
        audited_pairs=known, unknown_pairs=len(expected)-known, full_coverage=complete,
        all_task_geometric_mean=mean, tasks=task_rows, pairs=rows, primary_target_proven=False,
        scope='fresh quality workload, actual training cases per task; paired search-function times; all generation/final champion train/test audits, not full final-population CPU/AST coverage; does not replace C1024 primary speed grid',
        weighting='equal frozen tasks and equal paired seeds; overall mean withheld if any pair is unknown',
        range_scope='observed seed ratio range is descriptive, not a confidence interval')
