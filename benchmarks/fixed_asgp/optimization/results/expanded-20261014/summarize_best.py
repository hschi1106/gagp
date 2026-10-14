"""Post-search archived-best audits supplement, never replace, final quality."""
from __future__ import annotations

import argparse
import csv
import json
import statistics
from pathlib import Path

TASKS = ('count_odds', 'last_index_of_zero')
MODES = ('asgp_1t', 'gagp_cpu', 'gpu_eval', 'gpu_repro', 'gpu_overlap')
LABELS = ('ASGP 1T', 'GAGP CPU 1T', 'GPU evaluation', 'GPU eval + reproduction', 'GPU overlap')
SEEDS = (0, 1, 2)
EXPECTED = {(t, m, s) for t in TASKS for m in MODES for s in SEEDS}
OUTPUTS = ('best-ever.csv', 'best-ever-vs-final-quality.png', 'best-ever-vs-final-quality.svg',
           'cumulative-best-fitness.png', 'cumulative-best-fitness.svg', 'best-ever-status.json')


def read(path):
    return json.loads(Path(path).read_text())


def cells(path):
    result = {}
    for cell in read(path)['cells']:
        key = cell.get('task'), cell.get('mode'), cell.get('seed')
        if key not in EXPECTED or key in result:
            raise ValueError(f'Unexpected or duplicate cell: {path}/{key}')
        if cell['status'] != 'ok' or not Path(cell.get('output', '')).is_file():
            raise ValueError(f'Incomplete cell: {path}/{key}')
        result[key] = cell
    if set(result) != EXPECTED:
        raise ValueError(f'Expected 30 completed cells: {path}; missing {sorted(EXPECTED-set(result))}')
    return result


def quality(cell, task):
    value = read(cell['output'])
    score = value['final']
    if value['task'] != task or len(value['cases']) != 1024 or score['cases'] != 1024 or len(score['case_results']) != 1024:
        raise ValueError(f'Expected full 1024-case quality audit: {cell["output"]}')
    return score, value['cases_sha256']


def collect(study):
    formal = cells(study/'formal/manifest.json')
    final = cells(study/'quality/manifest.json')
    archived = cells(study/'best-quality/manifest.json')
    rows, curves, hashes, lengths = [], {}, {}, {}
    for task, mode, seed in sorted(EXPECTED):
        key = task, mode, seed
        result = read(formal[key]['output'])
        history = result['generations']
        if result['population'] != 1024 or result['cases'] != 1024 or result['mode'] != mode or result['seed'] != seed:
            raise ValueError(f'Formal identity mismatch: {key}')
        if not history or [r['generation'] for r in history] != list(range(len(history))):
            raise ValueError(f'Invalid generation sequence: {key}')
        lengths.setdefault(task, len(history))
        if lengths[task] != len(history):
            raise ValueError(f'Unequal generation budgets: {task}')
        scores = [r['best'] for r in history] + [result['final_best']]
        best = max(scores)
        selected = archived[key]
        generation = selected['selected_generation']
        if selected['selected_training_fitness'] != best:
            raise ValueError(f'Archived selection is not training best: {key}')
        if generation == 'final':
            if result['final_best'] != best:
                raise ValueError(f'Final selection is not training best: {key}')
        elif result['final_best'] == best or not isinstance(generation, int) or not 0 <= generation < len(history) or history[generation]['best'] != best:
            raise ValueError(f'Invalid selection or final tie preference: {key}')
        final_score, final_hash = quality(final[key], task)
        best_score, best_hash = quality(selected, task)
        hashes.setdefault(task, final_hash)
        if final_hash != best_hash or final_hash != hashes[task]:
            raise ValueError(f'Heldout cases differ across paired audits: {key}')
        rows.append(dict(task=task, mode=mode, seed=seed, generations=len(history),
            final_training_fitness=result['final_best'], best_ever_training_fitness=best,
            lost_best=result['final_best'] < best, selected_generation=generation,
            final_heldout_accuracy=final_score['exact_accuracy'],
            best_ever_heldout_accuracy=best_score['exact_accuracy'],
            heldout_accuracy_delta=best_score['exact_accuracy']-final_score['exact_accuracy'],
            final_heldout_capped_l1=final_score['capped_l1_sum'],
            best_ever_heldout_capped_l1=best_score['capped_l1_sum'],
            final_heldout_errors=final_score['runtime_errors'],
            best_ever_heldout_errors=best_score['runtime_errors'],
            cases=1024, cases_sha256=final_hash,
            scope='Post-search archive selection; not native elitism; outside timing; no population injection',
            formal_source=formal[key]['output'], final_quality_source=final[key]['output'],
            best_quality_source=selected['output']))
        running = []
        for score in scores:
            running.append(max(score, running[-1]) if running else score)
        curves[key] = running
    return rows, curves


def plots(out, rows, curves):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    colors = plt.get_cmap('tab10').colors
    fig, axes = plt.subplots(1, 2, figsize=(12, 5))
    for ax, task in zip(axes, TASKS):
        for i, mode in enumerate(MODES):
            group = [r for r in rows if r['task']==task and r['mode']==mode]
            for offset, field, label, shade in ((-.18, 'final_heldout_accuracy', 'Final champion', .45),
                                               (.18, 'best_ever_heldout_accuracy', 'Archived training-best', 1)):
                values = [r[field] for r in group]
                mid = statistics.median(values)
                ax.bar(i+offset, mid, .34, color=colors[i], alpha=shade,
                    hatch='//' if offset<0 else '', label=label if i==0 else None)
                ax.errorbar(i+offset, mid, yerr=[[mid-min(values)], [max(values)-mid]], color='black', capsize=3)
        ax.set_title(task.replace('_',' ').title())
        ax.set_xticks(range(5), LABELS, rotation=30, ha='right', fontsize=8)
        ax.set_ylabel('Independent CPU heldout exact accuracy')
        ax.set_ylim(0,1.05)
        ax.legend(fontsize=8)
    fig.suptitle('Final vs archived training-best: median and range across 3 search seeds')
    fig.text(.5,.015,'1024 common heldout cases. Archive selection occurs after search; it does not add elitism or replace final quality.',ha='center',fontsize=9)
    fig.tight_layout(rect=(0,.07,1,.94))
    for ext in ('png','svg'): fig.savefig(out/f'best-ever-vs-final-quality.{ext}',dpi=180)
    plt.close(fig)
    fig, axes = plt.subplots(1,2,figsize=(12,4.5))
    for ax, task in zip(axes,TASKS):
        for i, mode in enumerate(MODES):
            values = [curves[task,mode,seed] for seed in SEEDS]
            columns = list(zip(*values)); x=list(range(len(columns)))
            ax.plot(x,[statistics.median(c) for c in columns],color=colors[i],label=LABELS[i])
            ax.fill_between(x,[min(c) for c in columns],[max(c) for c in columns],color=colors[i],alpha=.12)
        ax.set_title(task.replace('_',' ').title())
        ax.set_xlabel('Generation; final evaluation at rightmost point')
        ax.set_ylabel('Cumulative best reported training fitness')
        ax.grid(alpha=.2)
    axes[0].legend(fontsize=8)
    fig.suptitle('Post-search cumulative best: 3-seed median and min–max range')
    fig.text(.5,.015,'Descriptive archive, not native elitism. Reported fitness can differ across evaluators; final quality remains primary.',ha='center',fontsize=9)
    fig.tight_layout(rect=(0,.07,1,.94))
    for ext in ('png','svg'): fig.savefig(out/f'cumulative-best-fitness.{ext}',dpi=180)
    plt.close(fig)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--study',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    rows,curves=collect(args.study.resolve())
    out=args.out.resolve()
    if any((out/name).exists() for name in OUTPUTS):
        raise ValueError('Refusing to overwrite existing archived-best outputs')
    out.mkdir(parents=True,exist_ok=True)
    with (out/'best-ever.csv').open('x',newline='') as stream:
        writer=csv.DictWriter(stream,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
    plots(out,rows,curves)
    status=dict(cells=30, post_search=True, timed=False, population_injection=False,
        interpretation='Archive selection is not native elitism. ASGP native elite1 and GAGP search retention differ. Final quality remains the primary endpoint.',
        selection='Maximum reported training fitness across history and final; final wins ties. Heldout results never select the archived program.',
        lost_best_by_task_mode={task:{mode:sum(r['lost_best'] for r in rows if r['task']==task and r['mode']==mode) for mode in MODES} for task in TASKS})
    (out/'best-ever-status.json').write_text(json.dumps(status,indent=2)+'\n')


if __name__=='__main__':main()
