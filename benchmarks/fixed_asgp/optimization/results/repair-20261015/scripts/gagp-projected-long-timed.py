from pathlib import Path
from argparse import Namespace
from gagp_tools.experiments.fixed_asgp.low_budget import Study, read
root=Path.cwd();base=root/'logs/all-task-repair-20261015'
a=Namespace(out=base/'projected-long-timed',binary=root/'cpp/build/release/gagp_fixed_asgp_bench',settings=base/'settings-best.json',stage='projected-long-timed',generations=32,rss_gib=4,budget=500,timeout=25,native_depth=None,source_balanced=False)
s=Study(a);s.manifest['binary_source_head']='6ac07f2';s.manifest['note']='Before/after source-depth search profile, different variation trajectories; no identical-workload speedup claim. ASGP seed0 long controls reused from long-original32/long-median64; unchangedsourceandASGPlogic.';s.persist()
for task,gens in [('sum_of_elements',32),('house_robber',32),('median',64)]:
 a.generations=gens;statuses=[]
 for profile in ['before','projected']:
  s.env.pop('GAGP_BM_SOURCE_DEPTH',None)
  if profile=='projected':s.env['GAGP_BM_SOURCE_DEPTH']='7'
  folder=(base/'projected-quality'/task) if profile=='projected' else base/'support-balanced'/task
  prepared=(folder/'p1024.prepared.json') if profile=='projected' else base/'benchmark-all-g4'/f'{task}.p1024.prepared.json'
  for seed in range(3):
   # Separate Study out subfolder while preserving one immutable manifest.
   run_tag=f'{task}.{profile}.s{seed}';output=s.out/(run_tag+'.json');export=s.out/(run_tag+'.final.json')
   cell=s.run(task,profile+f'.s{seed}',[a.binary,'measure',folder/'source.json',prepared,folder/'grammar.json',1024,'search_gpu_overlap',output],{'GAGP_SEARCH_GENERATIONS':str(gens),'GAGP_BM_SEED':str(seed),'GAGP_SEARCH_EXPORT':str(export),'GAGP_BM_SOURCE_DEPTH':'7'} if profile=='projected' else {'GAGP_SEARCH_GENERATIONS':str(gens),'GAGP_BM_SEED':str(seed),'GAGP_SEARCH_EXPORT':str(export)})
   if cell['status']=='ok':
    from gagp_tools.experiments.fixed_asgp.low_budget import validate_search
    d=read(output);validate_search(d,1024,gens,'gpu_overlap');cell.update(output=output.name,evolve_wall_ms=d['evolve_wall_ms'],final_best=d['final_best'],profile=profile);s.persist()
    statuses.append(s.run(task,profile+f'.s{seed}.heldout',[a.binary,'quality',task,export,folder/'grammar.json',s.out/(run_tag+'.heldout.json')])['status'])
   statuses.append(cell['status'])
 s.manifest['tasks'][task]={'status':'passed' if all(x=='ok' for x in statuses) else 'failed'};s.persist()
