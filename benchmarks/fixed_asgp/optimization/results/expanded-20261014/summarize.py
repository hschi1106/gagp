"""Read-only expanded-study summaries; three seeds are independent searches."""
from __future__ import annotations

import argparse
import csv
import json
import statistics
from pathlib import Path

TASKS = ('count_odds', 'last_index_of_zero')
# Representation support alone does not qualify a workload for timed comparison.
UNMEASURED = {
    'negative_to_zero': {
        'status': 'unmeasured_memory_safety_and_GPU_evaluation_failure',
        'reason': 'CPU P8 two-generation random-list preflight exceeded 70 GiB RSS and was stopped; GPU known-solution audit failed. No trustworthy P1024 timing or qualified speedup.',
        'reproduction': 'Generic GPU reproduction is available; this does not establish GPU evaluation correctness or safe CPU execution.',
        'evidence': ['negative-cpu-memory-stop.json', 'artifacts/negative_to_zero.audit.json'],
    },
    'stock_with_cooldown': {
        'status': 'unsupported_exact_clamped_memo_transitions',
        'reason': 'Existing memoized coordinate transitions cannot express ASGP clamped dependencies exactly.',
        'evidence': ['benchmarks/fixed_asgp/optimization/stock_capability.cpp'],
    },
}
MODES = ('asgp_1t', 'gagp_cpu', 'gpu_eval', 'gpu_repro', 'gpu_overlap')
LABELS = ('ASGP 1T', 'GAGP CPU 1T', 'GPU evaluation', 'GPU eval + reproduction', 'GPU overlap')
SEEDS = (0, 1, 2)


def read(path):
    return json.loads(Path(path).read_text())


def normal_fitness(task, mode, value):
    return value + (1024 if task == 'negative_to_zero' and mode == 'asgp_1t' else 0)


def stats(values):
    values = [v for v in values if v is not None]
    return (statistics.median(values), min(values), max(values)) if values else (None, None, None)


def summarize(study: Path, allow_incomplete=False):
    manifest = read(study / 'formal/manifest.json')
    if manifest['flags'].get('GAGP_BM_COMMON_FITNESS') != '1' or manifest['flags'].get('GAGP_HOST_THREADS') != '1':
        raise ValueError('Formal study must use common fitness and one host thread')
    quality_path = study / 'quality/manifest.json'
    quality_manifest = read(quality_path) if quality_path.exists() else {'cells': []}
    indexed, qualities, missing = {}, {}, []
    for cell in manifest['cells']:
        if all(k in cell for k in ('task', 'mode', 'seed')):
            key = cell['task'], cell['mode'], cell['seed']
            if key[0] not in TASKS or key[1] not in MODES or key[2] not in SEEDS:
                raise ValueError(f'out-of-scope formal cell: {key}')
            if key in indexed:
                raise ValueError(f'duplicate formal cell: {key}')
            indexed[key] = cell
    for cell in quality_manifest['cells']:
        if cell.get('status') == 'ok' and all(k in cell for k in ('task', 'mode', 'seed')):
            key = cell['task'], cell['mode'], cell['seed']
            if key in qualities:
                raise ValueError(f'duplicate quality cell: {key}')
            qualities[key] = read(cell['output'])
    artifacts = Path(manifest.get('artifacts', study / 'artifacts'))
    audits = {task: read(artifacts / f'{task}.audit.json') for task in TASKS}
    records, curves, quality_rows = [], {}, []
    for task in TASKS:
        for mode in MODES:
            for seed in SEEDS:
                key = task, mode, seed
                cell = indexed.get(key)
                if not cell or cell.get('status') != 'ok' or not Path(cell.get('output', '')).is_file():
                    missing.append(f'formal:{task}/{mode}/{seed}')
                    continue
                data = read(cell['output'])
                if data['population'] != 1024 or data['cases'] != 1024 or data['seed'] != seed or data['mode'] != mode:
                    raise ValueError(f'workload identity mismatch: {key}')
                rows = data['generations']
                if len(rows) != manifest['generations'][task] or len(data['final_population']) != 1024:
                    raise ValueError(f'incomplete search: {key}')
                if [r['generation'] for r in rows] != list(range(len(rows))):
                    raise ValueError(f'invalid generation sequence: {key}')
                if any(r['expected_program_cases'] != 1024**2 for r in rows):
                    raise ValueError(f'wrong case workload: {key}')
                gpu = mode.startswith('gpu')
                if gpu and any(not r['case_counters_available'] or r['cases'] != 1024**2 or r['unscored'] != 0 for r in rows):
                    raise ValueError(f'incomplete GPU case accounting: {key}')
                if mode in ('gpu_repro', 'gpu_overlap') and (data['reproduction_profile'] == 'cpu' or sum(r['repro_kernel_ms'] for r in rows) <= 0):
                    raise ValueError(f'GPU reproduction not exercised: {key}')
                reasons = []
                audit = audits[task]
                if audit['known_solution_cpu_mismatches'] or audit['known_solution_asgp_wrong']:
                    reasons.append('known_solution_reference_failure')
                target = 1024 if task == 'negative_to_zero' else 0
                if gpu and audit['known_solution_gpu_fitness'] != target:
                    reasons.append('known_solution_GPU_evaluation_failure')
                if gpu and data.get('cpu_audit_mismatches', 0):
                    reasons.append('final_population_CPU_audit_mismatch')
                window = max(1, len(rows) // 4)
                record = dict(task=task, mode=mode, seed=seed, generations=len(rows),
                    search_ms=data['evolve_wall_ms'], process_ms=cell['process_wall_s'] * 1000,
                    early_generation_ms=statistics.median(r['generation_ms'] for r in rows[:window]),
                    late_generation_ms=statistics.median(r['generation_ms'] for r in rows[-window:]),
                    final_fitness=normal_fitness(task, mode, data['final_best']),
                    cpu_audit_mismatches=data.get('cpu_audit_mismatches'),
                    cpu_audit_program_count=data.get('cpu_audit_program_count'),
                    unique_genotypes=data['unique_final_genomes'],
                    errors=sum(r.get('errors', 0) for r in rows) if gpu else None,
                    timeouts=sum(r.get('timeouts', 0) for r in rows) if gpu else None,
                    qualified=not reasons, qualification_reason=';'.join(reasons),
                    reproduction_profile=data['reproduction_profile'],
                    reproduction_fallback_reason=data.get('reproduction_fallback_reason', ''))
                curves[key] = [normal_fitness(task, mode, r['best']) for r in rows]
                quality = qualities.get(key)
                if quality is None:
                    missing.append(f'quality:{task}/{mode}/{seed}')
                else:
                    if quality['task'] != task or len(quality['cases']) != 1024:
                        raise ValueError(f'quality case identity mismatch: {key}')
                    expected = {0, len(rows)//2, len(rows)-1}
                    if {r['generation'] for r in quality['history']} != expected:
                        raise ValueError(f'incomplete initial/middle/last audit: {key}')
                    for point, score in [('final', quality['final'])] + [(str(r['generation']), r) for r in quality['history']]:
                        if score['cases'] != 1024 or len(score['case_results']) != 1024:
                            raise ValueError(f'quality audit is not full-case: {key}/{point}')
                        quality_rows.append(dict(task=task, mode=mode, seed=seed, checkpoint=point,
                            cases=1024, cases_sha256=quality['cases_sha256'],
                            exact_accuracy=score['exact_accuracy'], runtime_errors=score['runtime_errors'],
                            common_loss=score.get('exact_loss_sum', score.get('capped_l1_sum'))))
                    record['heldout_exact_accuracy'] = quality['final']['exact_accuracy']
                records.append(record)
    if missing and not allow_incomplete:
        raise ValueError('Missing cells (use --allow-incomplete only for explicit partial reports): ' + ', '.join(missing))
    for task in TASKS:
        hashes = {r['cases_sha256'] for r in quality_rows if r['task'] == task}
        if len(hashes) > 1:
            raise ValueError(f'heldout case hashes differ: {task}')
    baselines = {(r['task'], r['seed']): r for r in records if r['mode'] == 'asgp_1t'}
    for r in records:
        baseline = baselines.get((r['task'], r['seed']))
        for metric in ('search_ms', 'process_ms'):
            ratio = baseline[metric] / r[metric] if baseline else None
            r['measured_' + metric + '_ratio'] = ratio
            r['qualified_' + metric + '_speedup'] = ratio if r['qualified'] and baseline and baseline['qualified'] else None
    numeric = ('search_ms', 'process_ms', 'early_generation_ms', 'late_generation_ms', 'final_fitness',
        'cpu_audit_mismatches', 'cpu_audit_program_count', 'unique_genotypes', 'errors', 'timeouts',
        'heldout_exact_accuracy', 'measured_search_ms_ratio', 'measured_process_ms_ratio',
        'qualified_search_ms_speedup', 'qualified_process_ms_speedup')
    summary = []
    for task in TASKS:
        for mode in MODES:
            group = [r for r in records if (r['task'], r['mode']) == (task, mode)]
            item = dict(task=task, mode=mode, seeds=len(group),
                generations=manifest['generations'][task],
                qualified=bool(group) and all(r['qualified'] for r in group),
                qualification_reason=';'.join(sorted({r['qualification_reason'] for r in group if r['qualification_reason']})),
                reproduction_profiles=';'.join(sorted({r['reproduction_profile'] for r in group})))
            for field in numeric:
                values = [r.get(field) for r in group]
                # Do not present a qualified aggregate when even one seed fails.
                if field.startswith('qualified_') and not item['qualified']:
                    values = []
                for suffix, value in zip(('median', 'min', 'max'), stats(values)):
                    item[field + '_' + suffix] = value
            summary.append(item)
    return summary, records, curves, quality_rows, missing


def write_csv(path, rows):
    if not rows:
        path.write_text('')
        return
    fields = list(dict.fromkeys(k for row in rows for k in row))
    with path.open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def native_controls(study):
    path = study/'native-controls/manifest.json'
    if not path.exists():
        return []
    output = []
    for cell in read(path)['cells']:
        if cell.get('status') != 'ok' or not cell.get('output'):
            continue
        data = read(cell['output'])
        output.append(dict(task=cell['task'], mode=cell['mode'], seed=cell['seed'],
            metric='native ASGP policy; not directly comparable to common fitness',
            search_ms=data['evolve_wall_ms'], process_ms=cell['process_wall_s']*1000,
            native_final_fitness=data['final_best'], generations=len(data['generations']),
            unique_genotypes=data['unique_final_genomes']))
    return output


def plots(out, summary, curves):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    colors = plt.get_cmap('tab10').colors
    fig, axes = plt.subplots(1, len(TASKS), figsize=(11, 5))
    for ax, task in zip(axes, TASKS):
        group = [r for r in summary if r['task'] == task]
        for i, row in enumerate(group):
            y, low, high = (row['search_ms_' + s] for s in ('median', 'min', 'max'))
            if y is None:
                continue
            bar = ax.bar(i, y/1000, color=colors[i], hatch='//' if not row['qualified'] else '')
            ax.errorbar(i, y/1000, yerr=[[(y-low)/1000], [(high-y)/1000]], color='black', capsize=3)
            ax.bar_label(bar, labels=[f'{y/1000:.2f}'], padding=3, fontsize=8)
        ax.set_title(task.replace('_', ' ').title())
        ax.set_xticks(range(5), LABELS, rotation=35, ha='right', fontsize=8)
        ax.set_yscale('log')
        ax.set_ylabel('Complete search time (seconds, log scale)')
    partial = any(r['seeds'] != 3 for r in summary)
    prefix = 'PARTIAL — ' if partial else ''
    fig.suptitle(prefix + 'P1024 / C1024: median and range across independent search seeds')
    fig.text(.5, .01, 'Hatched: correctness failed. Negative to Zero and Stock excluded with explicit reasons in status.json.', ha='center', fontsize=9)
    fig.tight_layout(rect=(0, .06, 1, .94))
    fig.savefig(out / 'complete-search-time.png', dpi=180)
    fig.savefig(out / 'complete-search-time.svg')
    plt.close(fig)
    fig, axes = plt.subplots(1, len(TASKS), figsize=(11, 4.5))
    for ax, task in zip(axes, TASKS):
        for i, mode in enumerate(MODES):
            values = [curves[(task, mode, s)] for s in SEEDS if (task, mode, s) in curves]
            if not values:
                continue
            cols = list(zip(*values))
            x = list(range(len(cols)))
            ax.plot(x, [statistics.median(c) for c in cols], color=colors[i], label=LABELS[i])
            ax.fill_between(x, [min(c) for c in cols], [max(c) for c in cols], color=colors[i], alpha=.12)
        ax.set_title(task.replace('_', ' ').title())
        ax.set_xlabel('Generation (pre-reproduction population)')
        ax.set_ylabel('Correct count' if task == 'negative_to_zero' else 'Negative capped absolute error')
        ax.grid(alpha=.2)
    axes[0].legend(fontsize=8)
    fig.suptitle(prefix + 'Search-reported fitness: seed median and min–max band (not independent CPU quality)')
    fig.text(.5, .01, 'Full 1024-case independent CPU audits: quality.csv. Two unmeasured task limitations: status.json.', ha='center', fontsize=9)
    fig.tight_layout(rect=(0, .06, 1, .94))
    fig.savefig(out / 'fitness-by-generation.png', dpi=180)
    fig.savefig(out / 'fitness-by-generation.svg')
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--study', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--allow-incomplete', action='store_true')
    args = parser.parse_args()
    summary, records, curves, quality, missing = summarize(args.study.resolve(), args.allow_incomplete)
    out = args.out.resolve()
    if out == args.study.resolve() or out in [args.study.resolve()/p for p in ('artifacts', 'formal', 'quality', 'pilot', 'native-controls')]:
        raise ValueError('Output must be a separate summary directory')
    out.mkdir(parents=True, exist_ok=False)
    write_csv(out/'summary.csv', summary)
    write_csv(out/'per-seed.csv', records)
    write_csv(out/'quality.csv', quality)
    write_csv(out/'native-controls.csv', native_controls(args.study.resolve()))
    (out/'status.json').write_text(json.dumps(dict(missing=missing,
        complete=not missing, measured_tasks=list(TASKS), expected_formal_cells=30, expected_quality_cells=30,
        unmeasured_tasks=UNMEASURED,
        pilot_policy='pilot/ preserves the interrupted original attempt; pilot-supported/ is the two-task pilot.',
        seed_policy='Three independent searches; ranges describe seed variability, not repeated identical workloads.',
        ratios='Median of seed-matched ASGP ratios; measured ratios are descriptive only when correctness fails.',
        quality='Same independent local ASGP test split, 1024 cases per checkpoint; not official PSB. Natural duplicates and possible training overlap retained.',
        list_fitness='Common exact metric: ASGP negative error + 1024; GAGP correct count. Native-policy controls must not use this conversion.',
        stock_probe='benchmarks/fixed_asgp/optimization/stock_capability.cpp'), indent=2)+'\n')
    plots(out, summary, curves)


if __name__ == '__main__':
    main()
