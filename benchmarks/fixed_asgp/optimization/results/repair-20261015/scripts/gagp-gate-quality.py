from pathlib import Path
from argparse import Namespace
from gagp_tools.experiments.fixed_asgp.low_budget import Study, read,validate_search
root=Path.cwd();base=root/'logs/all-task-repair-20261015'
a=Namespace(out=base/'source-gate-quality',binary=root/'cpp/build/release/gagp_fixed_asgp_bench',settings=base/'settings-final.json',stage='source-gate-quality',generations=32,rss_gib=4,budget=300,timeout=30,native_depth=None,source_balanced=False)
s=Study(a);s.manifest['note']='Explicit sourcecap7+uniformphase search profile. A/B only crossover gate1.0 vs0.7. Different trajectories; no identical-workload speedup. 3seeds only, not convergence equivalence.';s.persist()
for task,gens in [('sum_of_elements',32),('house_robber',32),('median',64)]:
 folder=base/'projected-quality'/task;statuses=[]
 for gate in ['0','1']:
  for seed in range(3):
   tag=f'gate{gate}.g{gens}.s{seed}';out=s.out/f'{task}.{tag}.json';export=s.out/f'{task}.{tag}.final.json'
   cell=s.run(task,tag,[a.binary,'measure',folder/'source.json',folder/'p1024.prepared.json',folder/'grammar.json',1024,'search_gpu_overlap',out],{'GAGP_SEARCH_GENERATIONS':str(gens),'GAGP_BM_SEED':str(seed),'GAGP_SEARCH_EXPORT':str(export),'GAGP_BM_SOURCE_DEPTH':'7','GAGP_GPU_UNIFORM_PHASE_SITES':'1','GAGP_GPU_SOURCE_CROSSOVER_GATE':gate})
   statuses.append(cell['status'])
   if cell['status']=='ok':
    d=read(out);validate_search(d,1024,gens,'gpu_overlap');cell.update(evolve_wall_ms=d['evolve_wall_ms'],final_best=d['final_best'],output=out.name);s.persist()
    statuses.append(s.run(task,tag+'.heldout',[a.binary,'quality',task,export,folder/'grammar.json',s.out/f'{task}.{tag}.heldout.json'])['status'])
 s.manifest['tasks'][task]={'status':'passed' if all(x=='ok' for x in statuses) else 'failed'};s.persist()
