"""Select a global operator policy from old TRAINING evidence, then freeze."""
from __future__ import annotations
from collections import Counter, defaultdict
import math
from pathlib import Path
import statistics
import time

import study


def training_summary():
    summary=study.read(study.RAW/'site-training-judges/summary.json')
    rows=summary['rows'];variants=list(summary['variants'])
    expected={(task,seed,variant) for task in study.TASKS for seed in range(5) for variant in variants}
    assert {(r['task'],r['seed'],r['variant']) for r in rows}==expected
    assert len(rows)==len(expected) and all(r['status']=='ok' for r in rows)
    data={}
    for row in rows:
        doc=study.read(study.RAW/'site-training-judges'/(row['job']+'.json'))['final']
        loss=doc.get('capped_l1_mean',doc.get('exact_loss_mean'))
        assert loss is not None and math.isfinite(loss)
        data[row['task'],row['seed'],row['variant']]=dict(wrong=doc['wrong'],loss=loss)
    ranks=defaultdict(list)
    for task in study.TASKS:
        for seed in range(5):
            values={v:data[task,seed,v]['loss'] for v in variants}
            for variant,value in values.items():
                lower=sum(other<value for other in values.values())
                tied=sum(other==value for other in values.values())
                ranks[variant].append(1+lower+(tied-1)/2)
    statistics_rows=[]
    for variant in variants:
        per_task={task:sum(data[task,seed,variant]['wrong']==0 for seed in range(5)) for task in study.TASKS}
        wrong_by_task={task:statistics.mean(data[task,seed,variant]['wrong'] for seed in range(5)) for task in study.TASKS}
        statistics_rows.append(dict(variant=variant,training_perfect=sum(per_task.values()),
            mean_paired_loss_rank=statistics.mean(ranks[variant]),per_task_training_perfect=per_task,
            per_task_mean_wrong=wrong_by_task))
    order={v:i for i,v in enumerate(variants)}
    ordered=sorted(statistics_rows,key=lambda r:(-r['training_perfect'],r['mean_paired_loss_rank'],order[r['variant']]))
    baseline=next(r for r in statistics_rows if r['variant']=='baseline')
    winner=ordered[0]
    improved=sum(winner['per_task_mean_wrong'][t]<baseline['per_task_mean_wrong'][t] for t in study.TASKS)
    worsened=sum(winner['per_task_mean_wrong'][t]>baseline['per_task_mean_wrong'][t] for t in study.TASKS)
    strictly_better=(winner['training_perfect']>baseline['training_perfect'] or
        (winner['training_perfect']==baseline['training_perfect'] and winner['mean_paired_loss_rank']<baseline['mean_paired_loss_rank']))
    selected=winner['variant'] if strictly_better and improved>=worsened else 'baseline'
    return dict(selected=selected,variants=summary['variants'],statistics=statistics_rows,
        consistency=dict(improved_tasks=improved,worsened_tasks=worsened),
        rule='more training-perfect champions, then lower mean within-task/seed rank of common training loss; exact ties prefer fewer controls; adopt only if improved-task count >= worsened-task count; no heldout scores',
        selected_epoch=time.time())


if __name__=='__main__':
    result=training_summary()
    destination=study.BASE/'candidate-selection.json'
    if destination.exists():raise RuntimeError('selection already frozen; do not overwrite')
    study.save(destination,result)
    print(result)
