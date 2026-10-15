from pathlib import Path
from argparse import Namespace
from gagp_tools.experiments.fixed_asgp.low_budget import Study, read, save, snapshot_mismatches
root=Path.cwd();base=root/'logs/all-task-repair-20261015'
a=Namespace(out=base/'initial-overflow-screen',binary=root/'cpp/build/release/gagp_fixed_asgp_bench',settings=base/'settings-best.json',stage='overflow-diagnostic',generations=0,rss_gib=4,budget=550,timeout=100,native_depth=None,source_balanced=False)
s=Study(a);rows=[]
for task,pop in [('filter_odd',64),('negative_to_zero',64),('is_sorted',64),('filter_odd',1024)]:
 out=s.out/f'{task}.p{pop}.json'
 cell=s.run(task,f'p{pop}',[a.binary,'measure',base/'support-balanced'/task/'source.json',base/'benchmark-all-g4'/f'{task}.p1024.prepared.json',base/'support-balanced'/task/'grammar.json',pop,'snapshot',out],{'GAGP_SNAPSHOT_CPU':'1','GAGP_SNAPSHOT_DIAGNOSTICS':'1','GAGP_GPU_OVERFLOW_DIAGNOSTICS':'1'})
 row={'task':task,'population':pop,'status':cell['status']}
 if cell['status']=='ok':
  d=read(out);p=d['programs'];row.update({k:v for k,v in d.items() if k!='programs'});row['cpu_mismatches']=snapshot_mismatches(p)
  row['initial_overflow_cases']=sum(x['initial_payload_overflow_cases'] for x in p)
  for k in ['lanes','tiles','lane_replay_cases','tile_replay_cases']:row[k]=sum(x.get('initial_payload_overflow_'+k,0) for x in p)
  row['overflow_programs']=[{'program':i,**{k:v for k,v in x.items() if k.startswith('initial_payload_overflow_')}} for i,x in enumerate(p) if x['initial_payload_overflow_cases']]
 rows.append(row);save(s.out/'summary.json',rows);print(task,pop,row.get('eval_ms'),row.get('initial_overflow_cases'),row.get('lane_replay_cases'),row.get('tile_replay_cases'),flush=True)
