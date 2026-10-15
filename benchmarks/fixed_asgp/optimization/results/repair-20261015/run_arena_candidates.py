"""Historical A/B screen using archived prototype binary; window/root prototypes were removed."""
from pathlib import Path
import argparse
import json
import sys

ROOT = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(ROOT / 'tools'))
from gagp_tools.experiments.fixed_asgp.low_budget import Study, read, save, snapshot_mismatches

p = argparse.ArgumentParser()
p.add_argument('--out', type=Path, required=True)
p.add_argument('--binary', type=Path, default=ROOT/'logs/all-task-repair-20261015/arena-candidates/bench')
p.add_argument('--baseline', type=Path, default=ROOT/'logs/all-task-repair-20261015/benchmark-all-g4/bench')
p.add_argument('--population', type=int, default=64)
p.add_argument('--tasks', nargs='+', default=['filter_odd', 'is_sorted'])
p.add_argument('--budget', type=float, default=240)
p.add_argument('--timeout', type=float, default=45)
a = p.parse_args()
a.settings = ROOT/'logs/all-task-repair-20261015/settings-tiled.json'
a.stage='snapshot-ab'; a.generations=0; a.rss_gib=4; a.native_depth=None; a.source_balanced=False
study=Study(a)
study.manifest["population"]=a.population;study.persist()
flags=['GAGP_GPU_FUSED_LIST_CONCAT','GAGP_ARENA_WINDOW_EXECUTOR','GAGP_ARENA_COMPACT_ROOT']
variants=[('baseline',a.baseline,[]),('bound32',a.binary,[]),
          ('fused',a.binary,[flags[0]]),('window',a.binary,[flags[1]]),
          ('root',a.binary,[flags[2]]),('combined',a.binary,flags)]
rows=[]
for task in a.tasks:
    support=ROOT/'logs/all-task-repair-20261015/support-balanced'/task
    prepared=ROOT/'logs/all-task-repair-20261015/benchmark-all-g4'/f'{task}.p1024.prepared.json'
    reference=None
    for label,binary,enabled in variants:
        out=study.out/f'{task}.{label}.json'
        env={f:'1' if f in enabled else '0' for f in flags}
        env.update(GAGP_SNAPSHOT_CPU='1',GAGP_SNAPSHOT_DIAGNOSTICS='1')
        cell=study.run(task,label,[binary,'measure',support/'source.json',prepared,
            support/'grammar.json',a.population,'snapshot',out],env)
        row={'task':task,'variant':label,'population':a.population,'cases':1024,'status':cell['status']}
        if cell['status']=='ok':
            data=read(out); programs=data['programs']
            key=[{k:r[k] for k in ('fitness','cases','errors','timeouts','fallbacks','unscored')} for r in programs]
            if label=='baseline':reference=key
            row.update({k:v for k,v in data.items() if k!='programs'})
            row['cpu_mismatches']=snapshot_mismatches(programs)
            row['baseline_equal']=key==reference
            if row['cpu_mismatches'] or not row['baseline_equal']:row['status']='parity_failed'
        rows.append(row);save(study.out/'summary.json',rows)
        print(task,label,row['status'],round(row.get('eval_ms',0),2),flush=True)
    study.manifest['tasks'][task]={'status':'passed' if all(r['status']=='ok' for r in rows if r['task']==task) else 'failed'}
    study.persist()
