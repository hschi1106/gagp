from pathlib import Path
from argparse import Namespace, ArgumentParser
from gagp_tools.experiments.fixed_asgp.low_budget import Study, read, save, PAPER_TASKS, snapshot_mismatches
r=Path.cwd();b=r/'logs/all-task-repair-20261015'
parser=ArgumentParser();parser.add_argument('--out',type=Path);cli=parser.parse_args()
a=Namespace(out=cli.out or b/'final-five-mode-smoke',binary=b/'smallhash-screen/bench',settings=b/'settings-final.json',stage='final-five-mode-smoke',generations=2,rss_gib=4,budget=600,timeout=45,native_depth=24,source_balanced=True)
s=Study(a)
s.manifest['binary_source_head']='8249c0d';s.manifest['binary_note']='Archived smallhash candidate; final current cpp tree identical to 8249c0d after rejected transpose and Slice-hash reverts.';s.persist()
s.manifest['population']=8
s.manifest['note']='Reuses existing source/grammar/P8 prepared files without prepare. All30 full1024-case CPU-GPU snapshots and five native mode P8G2 smokes. Not P1024 speed evidence.';s.persist()
for task in PAPER_TASKS:
 f=b/'support-balanced'/task;p=f/'p8.prepared.json';o=s.out/f'{task}.snapshot.json'
 c=s.run(task,'snapshot',[a.binary,'measure',f/'source.json',p,f/'grammar.json',8,'snapshot',o],{'GAGP_SNAPSHOT_CPU':'1'})
 mismatches=snapshot_mismatches(read(o)['programs']) if c['status']=='ok' else None
 statuses=[c['status']]
 for mode in ['asgp_1t','gagp_cpu','gpu_eval','gpu_repro','gpu_overlap']:
  statuses.append(s.search(task,f,p,8,mode,0))
 s.manifest['tasks'][task]={'status':'passed' if all(x=='ok' for x in statuses) and mismatches==0 else 'failed','snapshot_mismatches':mismatches,'statuses':statuses};s.persist()
 print(task,s.manifest['tasks'][task],flush=True)
