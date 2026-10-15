from pathlib import Path
from argparse import Namespace
from gagp_tools.experiments.fixed_asgp.low_budget import Study, read, save
root=Path.cwd();base=root/'logs/all-task-repair-20261015'
a=Namespace(out=base/'workspace-screen',binary=root/'cpp/build/release/gagp_fixed_asgp_bench',settings=base/'settings-best.json',stage='workspace-snapshot-ab',generations=0,rss_gib=4,budget=300,timeout=90,native_depth=None,source_balanced=False)
s=Study(a);rows=[]
for task,pop,caps in [('filter_odd',64,[512,2048,4096,8192]),('negative_to_zero',64,[512,2048,4096,8192]),('is_sorted',64,[512,2048,4096,8192]),('filter_odd',1024,[512,4096,8192])]:
 baseline=read(base/'initial-overflow-screen'/f'{task}.p{pop}.json')['programs']
 key=lambda p:[{k:x[k] for k in ['fitness','cases','errors','timeouts','fallbacks','unscored']} for x in p]
 for cap in caps:
  out=s.out/f'{task}.p{pop}.m{cap}.json'
  cell=s.run(task,f'p{pop}.m{cap}',[a.binary,'measure',base/'support-balanced'/task/'source.json',base/'benchmark-all-g4'/f'{task}.p1024.prepared.json',base/'support-balanced'/task/'grammar.json',pop,'snapshot',out],{'GAGP_SNAPSHOT_CPU':'0','GAGP_SNAPSHOT_DIAGNOSTICS':'1','GAGP_GPU_ADAPTIVE_REPLAY':'1','GAGP_GPU_PAYLOAD_WORKSPACE_MIB':str(cap)})
  row={'task':task,'population':pop,'workspace_mib':cap,'status':cell['status']}
  if cell['status']=='ok':
   d=read(out);row.update({k:v for k,v in d.items() if k!='programs'});row['baseline_equal']=key(d['programs'])==key(baseline)
   if not row['baseline_equal']:row['status']='parity_failed'
  rows.append(row);save(s.out/'summary.json',rows);print(task,pop,cap,row['status'],row.get('eval_ms'),row.get('payload_replay_launches'),row.get('baseline_equal'),flush=True)
