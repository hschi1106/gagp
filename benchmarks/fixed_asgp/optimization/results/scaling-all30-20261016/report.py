"""Summarize recorded scaling cells; failures and pending cells have no ratio."""
from argparse import ArgumentParser
from pathlib import Path
import csv
import json

from gagp_tools.experiments.fixed_asgp.low_budget import PAPER_TASKS, read, validate_search

POPS = (1024, 2048, 4096, 8192, 16384)
p = ArgumentParser(description=__doc__)
p.add_argument('--matrix', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--capacity-results', type=Path, help='Separate explicitly configured smaller-workspace runs')
a = p.parse_args()
m = read(a.matrix / 'manifest.json')
supplement = read(a.capacity_results) if a.capacity_results else None
if supplement:
    assert supplement['binary_sha256'] == m['binary_sha256']
    assert supplement['settings'] == m['settings']
a.out.mkdir(parents=True, exist_ok=False)
rows, generations = [], []


def status(cell):
    value = cell.get('status', 'pending')
    if value in ('ok', 'pending'):
        return value
    log = a.matrix / cell.get('log', '/missing')
    text = log.read_text().lower() if log.is_file() else ''
    if 'preparation item limit' in text:
        return 'constant_preparation_limit'
    if 'out of memory' in text:
        return 'oom'
    if 'allocation failure' in text or 'cuda alloc' in text:
        return 'allocation_failure'
    return value


for task in PAPER_TASKS:
    for population in POPS:
        record = m.get('results', {}).get(f'{task}.p{population}', {})
        selected = supplement.get('results', {}).get(f'{task}.p{population}', {}) if supplement else {}
        if selected:
            assert selected['inputs'] == record['inputs']
        row = {'task': task, 'population': population, 'cases': 1024, 'generations': 4,
               'asgp_status': 'pending', 'gpu_status': 'pending', 'speedup': None}
        for mode, label in [('asgp_1t', 'asgp'), ('gpu_overlap', 'gpu')]:
            cell = record.get(mode, {})
            if label == 'gpu' and selected.get('complete'):
                row['gpu_original_status'] = status(cell)
                row['gpu_original_log'] = cell.get('log')
                if selected.get('gpu_overlap'):
                    cell = selected['gpu_overlap']
                    row['gpu_separate_capacity_config'] = True
            row[label + '_status'] = status(cell)
            if label == 'gpu':
                row['workspace_mib'] = cell.get('workspace_mib')
            row[label + '_output'] = cell.get('output')
            row[label + '_process_s'] = cell.get('process_s')
            collection = supplement if label == 'gpu' and row.get('gpu_separate_capacity_config') else m
            process_cell = next((c for c in collection.get('cells', [])
                if cell.get('log') and Path(c.get('log', '/missing')).name == Path(cell['log']).name), {})
            row[label + '_peak_rss_bytes'] = process_cell.get('peak_rss_bytes')
            if cell.get('status') != 'ok':
                continue
            data = read(Path(cell['output']))
            validate_search(data, population, 4, mode)
            row.update({label + '_wall_s': data['evolve_wall_ms'] / 1000,
                        label + '_final_best': data['final_best'],
                        label + '_unique': data['unique_final_genomes'],
                        label + '_initial_admission_ms': data['initial_admission_ms'],
                        label + '_final_eval_ms': data['final_eval_ms']})
            if label == 'gpu':
                row.update(workspace_mib=cell['workspace_mib'],
                    cpu_audit_mismatches=data['cpu_audit_mismatches'],
                    reproduction_profile=data['reproduction_profile'],
                    reproduction_fallback_reason=data['reproduction_fallback_reason'],
                    genotype_device_bytes=data['genotype_device_bytes'],
                    errors=sum(g['errors'] for g in data['generations']),
                    timeouts=sum(g['timeouts'] for g in data['generations']),
                    fallback_cases=sum(g['fallbacks'] for g in data['generations']),
                    unscored=sum(g['unscored'] for g in data['generations']),
                    evaluated_pairs=sum(g['cases'] for g in data['generations']))
            generations.extend(dict(task=task, population=population, mode=mode, **g)
                               for g in data['generations'])
        if row['asgp_status'] == row['gpu_status'] == 'ok':
            row['speedup'] = row['asgp_wall_s'] / row['gpu_wall_s']
        rows.append(row)


def csv_file(path, values):
    fields = list(dict.fromkeys(k for row in values for k in row))
    with path.open('w') as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(values)


csv_file(a.out / 'cells.csv', rows)
if generations:
    csv_file(a.out / 'generations.csv', generations)
complete = sum(r['asgp_status'] != 'pending' and r['gpu_status'] != 'pending' for r in rows)
passed = sum(r['speedup'] is not None for r in rows)
summary = dict(note='One cold process per configured run. Complete1024cases/G4+finaleval, '
    'CPU1T, fresh own-population ASGP controls. Qualified P1024 GPU runs reused by hash. '
    'No extrapolated ratios or precision/quality-equivalence claim. Alternate workspace '
    'runs exclude earlier failed probes; all attempts remain in matrix manifest.',
    matrix_manifest=str(a.matrix / 'manifest.json'),
    capacity_manifest=str(a.capacity_results) if a.capacity_results else None, source_head=m['source_head'],
    binary_source_head=m.get('binary_source_head', m['source_head']),
    binary_sha256=m['binary_sha256'], completed_cells=complete, qualified_cells=passed,
    total_cells=150, rows=rows)
(a.out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
table = '# ASGP 1T / GPU-er-o: complete G4 scaling\n\n'
table += f'Completed cells: {complete}/150; qualified ratios: {passed}.\n\n'
table += '| Task | P1024 | P2048 | P4096 | P8192 | P16384 |\n| --- | ---: | ---: | ---: | ---: | ---: |\n'
for task in PAPER_TASKS:
    values = []
    for r in [x for x in rows if x['task'] == task]:
        if r['speedup'] is not None:
            value = f'{r["speedup"]:.2f}x'
            if r['workspace_mib'] != 16384:
                value += f' ({r["workspace_mib"] // 1024}GiB)'
        else:
            value = r['gpu_status'] if r['gpu_status'] != 'ok' else 'ASGP ' + r['asgp_status']
        values.append(value)
    table += '| ' + task + ' | ' + ' | '.join(values) + ' |\n'
table += '\nDefault:16GiB arena budget;8/4GiB allocation-only retries explicitly marked. Failed/pending cells have no speedup.\n'
(a.out / 'report.md').write_text(table)

if passed:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    import matplotlib.colors as colors
    import numpy as np
    matrix = np.full((30, 5), np.nan)
    for i, task in enumerate(PAPER_TASKS):
        for j, population in enumerate(POPS):
            r = next(x for x in rows if x['task'] == task and x['population'] == population)
            if r['speedup'] is not None:
                matrix[i, j] = r['speedup']
    cmap = plt.get_cmap('cividis').copy()
    cmap.set_bad('#eeeeee')
    fig, ax = plt.subplots(figsize=(10, 13.5), layout='constrained')
    im = ax.imshow(matrix, cmap=cmap, norm=colors.LogNorm(vmin=min(.5, np.nanmin(matrix)),
        vmax=max(40, np.nanmax(matrix))), aspect='auto')
    for i in range(30):
        for j in range(5):
            r = rows[i * 5 + j]
            if r['speedup'] is None:
                state = r['gpu_status'] if r['gpu_status'] != 'ok' else 'ASGP ' + r['asgp_status']
                label = {'constant_preparation_limit': 'limit', 'allocation_failure': 'alloc',
                         'validation_failed': 'audit', 'timeout': 'timeout'}.get(state, state)
            else:
                label = f'{r["speedup"]:.2f}x'
                if r['workspace_mib'] != 16384:
                    label += f'\n{r["workspace_mib"] // 1024} GiB'
            ax.text(j, i, label, ha='center', va='center', fontsize=8,
                    color='white' if np.isfinite(matrix[i, j]) and matrix[i, j] < 4 else 'black')
    ax.set_xticks(range(5), [str(p) for p in POPS])
    ax.set_yticks(range(30), [t.replace('_', ' ').title() for t in PAPER_TASKS])
    ax.set_xlabel('Population')
    ax.set_title('ASGP 1T / GPU-er-o complete-search speedup\n1024 full cases, G4 + final evaluation, CPU 1T; one cold process/cell')
    fig.colorbar(im, ax=ax, label='Speedup (log scale)', shrink=.65)
    fig.savefig(a.out / 'scaling.png', dpi=180)
    fig.savefig(a.out / 'scaling.pdf')
