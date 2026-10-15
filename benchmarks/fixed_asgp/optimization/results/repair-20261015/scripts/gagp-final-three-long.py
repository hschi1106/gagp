from pathlib import Path
from argparse import Namespace, ArgumentParser
from gagp_tools.experiments.fixed_asgp.low_budget import Study, read,sha
r=Path.cwd();b=r/'logs/all-task-repair-20261015'
parser=ArgumentParser();parser.add_argument('--out',type=Path);cli=parser.parse_args()
a=Namespace(out=cli.out or b/'final-three-long',binary=b/'smallhash-screen/bench',settings=b/'settings-final.json',stage='final-three-long-coarse',generations=32,rss_gib=4,budget=90,timeout=30,native_depth=24,source_balanced=True)
s=Study(a);s.manifest['binary_source_head']='8249c0d';s.manifest['binary_note']='Archived smallhash candidate; final current cpp tree identical to 8249c0d after rejected transpose and Slice-hash reverts.';s.persist();s.manifest['note']='One cold GPU-er-o process/task; reuse own same-contract long ASGP controls. Sum32/House32/Median64 seed0. Not precision or quality equivalence.';s.persist()
for task,g,prior in [('sum_of_elements',32,'long-original32'),('house_robber',32,'long-original32'),('median',64,'long-median64')]:
 a.generations=g;folder=b/'support-balanced'/task;prepared=b/prior/f'{task}.p1024.prepared.json'
 s.manifest['tasks'][task]={'generations':g,'control_manifest':str(b/prior/'manifest.json'),'control_sha256':sha(b/prior/'manifest.json'),'input_sha256':{str(p):sha(p) for p in [folder/'source.json',folder/'grammar.json',prepared]}};s.persist()
 status=s.search(task,folder,prepared,1024,'gpu_overlap',0);s.manifest['tasks'][task]['status']=status;s.persist()
