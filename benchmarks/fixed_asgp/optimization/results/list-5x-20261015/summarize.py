"""Summarize a single coarse continuation screen; never substitute denominators."""
import csv
import json
from argparse import ArgumentParser
from pathlib import Path
from statistics import median

p = ArgumentParser()
p.add_argument('--runs', required=True, type=Path)
p.add_argument('--out', required=True, type=Path)
a = p.parse_args()
a.out.mkdir(parents=True, exist_ok=False)
root = Path.cwd()
prior = root / 'logs/all-task-repair-20261015/final-all30-g4'
baseline = json.loads((root / 'benchmarks/fixed_asgp/optimization/results/repair-20261015/final-all30/summary.json').read_text())
before = {x['task']: x for x in baseline['rows']}
manifest = json.loads((a.runs / 'manifest.json').read_text())
rows, generations, checks = [], [], []
semantic_keys = ['best', 'best_nodes', 'mean', 'cases', 'errors', 'timeouts', 'fallbacks',
    'unscored', 'changed', 'unchanged', 'rejected', 'operator_fallbacks', 'budget_rejections',
    'contract_rejections', 'generation_rejections']
for cell in manifest['cells']:
    if cell['status'] != 'ok' or 'output' not in cell:
        continue
    f = a.runs / cell['output']
    d = json.loads(f.read_text())
    if d['mode'] != 'gpu_overlap':
        continue
    task = cell['task']
    # Denominators belong only to the frozen four-generation, seed-zero study.
    qualified = len(d['generations']) == 4 and d['seed'] == 0 and d['population'] == d['cases'] == 1024
    old = before[task]
    wall = d['evolve_wall_ms'] / 1000
    row = dict(task=task, seed=d['seed'], generations=len(d['generations']),
        asgp_s=old['asgp_s'] if qualified else None, before_gpu_ero_s=old['gpu_ero_s'] if qualified else None,
        gpu_ero_s=wall, speedup=old['asgp_s'] / wall if qualified else None,
        runtime_improvement=old['gpu_ero_s'] / wall if qualified else None,
        initial_admission_ms=d['initial_admission_ms'], final_eval_ms=d['final_eval_ms'],
        early_generation_ms=median(g['generation_ms'] for g in d['generations'][:2]),
        late_generation_ms=median(g['generation_ms'] for g in d['generations'][-2:]),
        final_best=d['final_best'], unique_final_genomes=d['unique_final_genomes'],
        final_mean_nodes=sum(x['nodes'] for x in d['final_population']) / d['population'],
        cpu_audit_mismatches=d['cpu_audit_mismatches'], reproduction_profile=d['reproduction_profile'],
        reproduction_fallback_reason=d['reproduction_fallback_reason'])
    for key in ['cases', 'errors', 'timeouts', 'fallbacks', 'unscored']:
        row['generation_' + key] = sum(g[key] for g in d['generations'])
    rows.append(row)
    for g in d['generations']:
        generations.append(dict(task=task, seed=d['seed'], **g))
    reference = prior / f.name
    if qualified and reference.exists():
        z = json.loads(reference.read_text())
        export = f.with_name(f.name.replace('.json', '.final.json'))
        checks.append(dict(task=task, final_population_equal=d['final_population'] == z['final_population'],
            generation_semantics_equal=all({k:g[k] for k in semantic_keys} == {k:q[k] for k in semantic_keys}
                for g, q in zip(d['generations'], z['generations'])),
            export_ast_equal=json.loads(export.read_text()) == json.loads((prior / export.name).read_text())))

def write_csv(name, data):
    if not data:
        return
    fields = list(dict.fromkeys(k for row in data for k in row))
    with (a.out / name).open('w') as out:
        writer = csv.DictWriter(out, fieldnames=fields)
        writer.writeheader()
        writer.writerows(data)

write_csv('speedups.csv', rows)
write_csv('generations.csv', generations)
(a.out / 'summary.json').write_text(json.dumps(dict(
    note='One cold process per task/seed: coarse screening, no precision or convergence-equivalence claim.',
    source_head=manifest.get('binary_source_head', manifest['source_head']), binary_sha256=manifest['binary_sha256'],
    raw=str(a.runs), settings=manifest['settings'], controls_reused=manifest['controls_reused'],
    rows=rows, exact_comparisons=checks), indent=2) + '\n')
if rows and all(x['speedup'] is not None for x in rows):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    import numpy as np
    fig, ax = plt.subplots(figsize=(10, 6.8), layout='constrained')
    y = np.arange(len(rows))
    ax.barh(y - .18, [x['asgp_s'] / x['before_gpu_ero_s'] for x in rows], .34,
        color='#adb5bd', label='Previous GPU-er-o')
    bars = ax.barh(y + .18, [x['speedup'] for x in rows], .34,
        color='#247ba0', label='Current GPU-er-o')
    ax.bar_label(bars, fmt='%.2fx', padding=4, fontsize=9)
    ax.axvline(5, color='#6c757d', linestyle=':', linewidth=1.3, label='5x milestone')
    ax.axvline(10, color='#d1495b', linestyle='--', linewidth=1.5, label='10x target')
    ax.set_yticks(y, [x['task'].replace('_', ' ').title() for x in rows])
    ax.invert_yaxis()
    ax.set_xlabel('ASGP 1T / GPU-er-o complete-search speedup')
    ax.set_title('Sequence-related tasks: complete four-generation search\nP=1024, 1024 full cases, CPU 1T + GPU; one cold run per task')
    ax.grid(axis='x', alpha=.2)
    ax.set_axisbelow(True)
    ax.set_xlim(0, max(10.8, max(x['speedup'] for x in rows) * 1.15))
    ax.legend(loc='lower right', frameon=False)
    fig.savefig(a.out / 'speedup-comparison.png', dpi=180)
    fig.savefig(a.out / 'speedup-comparison.pdf')
