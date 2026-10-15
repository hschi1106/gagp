from pathlib import Path
from argparse import Namespace
from gagp_tools.experiments.fixed_asgp.low_budget import Study, read, save
root=Path.cwd();base=root/'logs/all-task-repair-20261015'
a=Namespace(out=base/'trim-screen',binary=root/'cpp/build/release/gagp_fixed_asgp_bench',settings=base/'settings-final.json',stage='exact-trim-ab',generations=0,rss_gib=4,budget=240,timeout=60,native_depth=None,source_balanced=False)
s=Study(a);rows=[]
key=lambda p:[{k:x[k] for k in ['fitness','cases','errors','timeouts','fallbacks','unscored']} for x in p]
for task in ['filter_odd','double_letters','replace_space_with_newline']:
 baseline=None
 for flag in ['0','1']:
  out=s.out/f'{task}.trim{flag}.json'
  cell=s.run(task,'trim'+flag,[a.binary,'measure',base/'support-balanced'/task/'source.json',base/'benchmark-all-g4'/f'{task}.p1024.prepared.json',base/'support-balanced'/task/'grammar.json',64,'snapshot',out],{'GAGP_FITNESS_TRIM_SEQUENCE_ENDS':flag,'GAGP_SNAPSHOT_CPU':'1','GAGP_SNAPSHOT_DIAGNOSTICS':'1'})
  row={'task':task,'population':64,'trim':flag,'status':cell['status']}
  if cell['status']=='ok':
   d=read(out);row.update({k:v for k,v in d.items() if k!='programs'})
   if baseline is None:baseline=d['programs']
   row['baseline_equal']=key(d['programs'])==key(baseline)
   row['cpu_equal']=all(x['fitness']==x['cpu_fitness'] and x['errors']==x['cpu_errors'] and x['timeouts']==x['cpu_timeouts'] for x in d['programs'])
   if not row['baseline_equal'] or not row['cpu_equal']:row['status']='parity_failed'
  rows.append(row);save(s.out/'summary.json',rows);print(task,flag,row['status'],row.get('eval_ms'),row.get('baseline_equal'),flush=True)
