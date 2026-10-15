from pathlib import Path
from argparse import Namespace
from gagp_tools.experiments.fixed_asgp.low_budget import Study, read, save
from gagp_tools.experiments.fixed_asgp.source_depth import annotate
import shutil
root=Path.cwd();base=root/'logs/all-task-repair-20261015'
a=Namespace(out=base/'projected-quality',binary=root/'cpp/build/release/gagp_fixed_asgp_bench',settings=base/'settings-best.json',stage='projected-quality',generations=4,rss_gib=4,budget=240,timeout=30,native_depth=None,source_balanced=False)
s=Study(a);s.env['GAGP_BM_SOURCE_DEPTH']='7';s.manifest['settings']['GAGP_BM_SOURCE_DEPTH']='7';s.manifest['controls_reused']='quality-balanced ASGP seeds0/1/2: same source/ASGPbinary logic/fitness/elitism/initialpopulation; source-depth native-only opt-in';s.persist()
for task in ['sum_of_elements','house_robber','median']:
 folder=s.out/task;folder.mkdir();shutil.copy2(base/'support-balanced'/task/'source.json',folder/'source.json');save(folder/'grammar.json',annotate(read(base/'support-balanced'/task/'grammar.json')))
 prepared=folder/'p1024.prepared.json';cell=s.run(task,'prepare1024',[a.binary,'prepare',folder/'source.json',folder/'grammar.json',1024,prepared]);statuses=[cell['status']]
 if cell['status']=='ok':
  a.generations=2
  for mode in ['asgp_1t','gagp_cpu','gpu_eval','gpu_repro','gpu_overlap']:
   # Smoke uses P8; store separate seeds99 so quality outputs never overwrite.
   statuses.append(s.search(task,folder,prepared,8,mode,99))
  a.generations=4
  for seed in range(3):
   status=s.search(task,folder,prepared,1024,'gpu_overlap',seed);statuses.append(status)
   if status=='ok':
    tag=f'gpu_overlap.s{seed}'
    statuses.append(s.run(task,tag+'.heldout',[a.binary,'quality',task,s.out/f'{task}.{tag}.final.json',folder/'grammar.json',s.out/f'{task}.{tag}.heldout.json'])['status'])
 s.manifest['tasks'][task]={'status':'passed' if all(x=='ok' for x in statuses) else 'failed'};s.persist()
