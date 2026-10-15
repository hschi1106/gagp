from pathlib import Path
from argparse import Namespace
from gagp_tools.experiments.fixed_asgp.low_budget import Study,read,save
r=Path.cwd();b=r/'logs/all-task-repair-20261015'
a=Namespace(out=b/'transpose-screen',binary=r/'cpp/build/release/gagp_fixed_asgp_bench',settings=b/'settings-final.json',stage='transpose-index-ab',generations=0,rss_gib=4,budget=180,timeout=40,native_depth=None,source_balanced=False)
s=Study(a);rows=[]
key=lambda p:[{k:x[k] for k in ['fitness','cases','errors','timeouts','fallbacks','unscored']} for x in p]
for task,pop,control in [('filter_odd',64,b/'initial-overflow-screen/filter_odd.p64.json'),('double_letters',64,b/'trim-screen/double_letters.trim0.json'),('filter_odd',1024,b/'initial-overflow-screen/filter_odd.p1024.json')]:
 ref=read(control)['programs'];folder=b/'support-balanced'/task
 for rep,flag in enumerate(['0','1','1','0'] if pop==64 else ['0','1']):
  o=s.out/f'{task}.p{pop}.r{rep}.flag{flag}.json'
  c=s.run(task,f'p{pop}.r{rep}.flag{flag}',[a.binary,'measure',folder/'source.json',b/'benchmark-all-g4'/f'{task}.p1024.prepared.json',folder/'grammar.json',pop,'snapshot',o],{'GAGP_GPU_TRANSPOSE_PAYLOAD_INDEX':flag,'GAGP_SNAPSHOT_DIAGNOSTICS':'1'})
  row={'task':task,'population':pop,'repeat':rep,'flag':flag,'status':c['status']}
  if c['status']=='ok':
   d=read(o);row.update({k:v for k,v in d.items() if k!='programs'});row['baseline_equal']=key(d['programs'])==key(ref)
   if not row['baseline_equal']:row['status']='parity_failed'
  rows.append(row);save(s.out/'summary.json',rows);print(task,pop,flag,row['status'],row.get('eval_ms'),flush=True)
