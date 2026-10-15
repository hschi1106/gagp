from pathlib import Path
from argparse import Namespace
from gagp_tools.experiments.fixed_asgp.low_budget import Study, read,validate_search
root=Path.cwd();base=root/'logs/all-task-repair-20261015'
a=Namespace(out=base/'uniform-phase-quality',binary=root/'cpp/build/release/gagp_fixed_asgp_bench',settings=base/'settings-best.json',stage='uniform-phase-quality',generations=32,rss_gib=4,budget=240,timeout=30,native_depth=None,source_balanced=False)
s=Study(a); s.manifest['note']='Different search rule: uniform phase per operator vs whole-genome site draw. Compare earlier projected-long-timed, 3 seeds, no convergence-equivalence claim.'; s.persist()
task='house_robber';folder=base/'projected-quality'/task
for gens in [32,48]:
 for seed in range(3):
  tag=f'g{gens}.s{seed}';out=s.out/f'{task}.{tag}.json';export=s.out/f'{task}.{tag}.final.json'
  cell=s.run(task,tag,[a.binary,'measure',folder/'source.json',folder/'p1024.prepared.json',folder/'grammar.json',1024,'search_gpu_overlap',out],{'GAGP_SEARCH_GENERATIONS':str(gens),'GAGP_BM_SEED':str(seed),'GAGP_SEARCH_EXPORT':str(export),'GAGP_BM_SOURCE_DEPTH':'7','GAGP_GPU_UNIFORM_PHASE_SITES':'1'})
  if cell['status']=='ok':
   d=read(out);validate_search(d,1024,gens,'gpu_overlap');cell.update(evolve_wall_ms=d['evolve_wall_ms'],final_best=d['final_best'],output=out.name);s.persist()
   s.run(task,tag+'.heldout',[a.binary,'quality',task,export,folder/'grammar.json',s.out/f'{task}.{tag}.heldout.json'])
