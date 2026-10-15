from pathlib import Path
from argparse import Namespace
from gagp_tools.experiments.fixed_asgp.low_budget import Study,read,save,validate_search
r=Path.cwd();b=r/'logs/all-task-repair-20261015'
a=Namespace(out=b/'readonly-screen',binary=r/'cpp/build/release/gagp_fixed_asgp_bench',settings=b/'settings-final.json',stage='readonly-payload-ab',generations=4,rss_gib=4,budget=180,timeout=30,native_depth=24,source_balanced=True)
s=Study(a);rows=[]
key=lambda p:[{k:x[k] for k in ['fitness','cases','errors','timeouts','fallbacks','unscored']} for x in p]
for t in ['small_or_large','grade','median']:
 folder=b/'support-balanced'/t;prepared=b/'benchmark-all-g4'/f'{t}.p1024.prepared.json';ref=None;ref_final=None
 for flag in ['0','1']:
  o=s.out/f'{t}.snapshot{flag}.json'
  c=s.run(t,'snapshot'+flag,[a.binary,'measure',folder/'source.json',prepared,folder/'grammar.json',1024,'snapshot',o],{'GAGP_GPU_READ_ONLY_PAYLOAD':flag,'GAGP_SNAPSHOT_CPU':'1'})
  if c['status']=='ok':
   d=read(o)
   if ref is None:ref=d['programs']
   rows.append({'task':t,'flag':flag,'eval_ms':d['eval_ms'],'execution_profile':d['execution_profile'],'baseline_equal':key(d['programs'])==key(ref),'cpu_equal':all(x['fitness']==x['cpu_fitness'] and x['errors']==x['cpu_errors'] and x['timeouts']==x['cpu_timeouts'] for x in d['programs'])});save(s.out/'summary.json',rows)
  o=s.out/f'{t}.search{flag}.json';export=s.out/f'{t}.search{flag}.final.json'
  c=s.run(t,'search'+flag,[a.binary,'measure',folder/'source.json',prepared,folder/'grammar.json',1024,'search_gpu_overlap',o],{'GAGP_GPU_READ_ONLY_PAYLOAD':flag,'GAGP_SEARCH_GENERATIONS':'4','GAGP_BM_SEED':'0','GAGP_SEARCH_EXPORT':str(export)})
  if c['status']=='ok':
   d=read(o);validate_search(d,1024,4,'gpu_overlap')
   if ref_final is None:ref_final=d['final_population']
   c.update(output=o.name,evolve_wall_ms=d['evolve_wall_ms'],final_best=d['final_best'],final_equal=ref_final==d['final_population']);s.persist()
 s.manifest['tasks'][t]={'status':'complete'};s.persist()
