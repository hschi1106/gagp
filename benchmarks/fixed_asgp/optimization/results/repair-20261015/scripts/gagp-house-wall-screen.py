from pathlib import Path
from argparse import Namespace
from gagp_tools.experiments.fixed_asgp.low_budget import Study
root=Path.cwd();base=root/'logs/all-task-repair-20261015'
a=Namespace(out=base/'projected-house48',binary=base/'projected-long-timed/bench',settings=base/'settings-best.json',stage='projected-quality-longer',generations=48,rss_gib=4,budget=300,timeout=25,native_depth=None,source_balanced=False)
s=Study(a);s.env['GAGP_BM_SOURCE_DEPTH']='7';s.manifest['settings']['GAGP_BM_SOURCE_DEPTH']='7';s.manifest['binary_source_head']='6ac07f2';s.manifest['note']='48-generation projected candidate versus32-generation before, three seeds. Reportactualwalltimes; thisisnot an equalgeneration or exactequalwall experiment.';s.persist()
folder=base/'projected-quality/house_robber';statuses=[]
for seed in range(3):
 status=s.search('house_robber',folder,folder/'p1024.prepared.json',1024,'gpu_overlap',seed);statuses.append(status)
 if status=='ok':
  tag=f'house_robber.gpu_overlap.s{seed}'
  statuses.append(s.run('house_robber',f's{seed}.heldout',[a.binary,'quality','house_robber',s.out/(tag+'.final.json'),folder/'grammar.json',s.out/(tag+'.heldout.json')])['status'])
s.manifest['tasks']['house_robber']={'status':'passed' if all(x=='ok' for x in statuses) else 'failed'};s.persist()
