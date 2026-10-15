from pathlib import Path
from argparse import Namespace, ArgumentParser
from gagp_tools.experiments.fixed_asgp.low_budget import Study, read, PAPER_TASKS,sha
root=Path.cwd();base=root/'logs/all-task-repair-20261015'
parser=ArgumentParser();parser.add_argument('--out',type=Path);cli=parser.parse_args()
a=Namespace(out=cli.out or base/'final-all30-g4',binary=base/'smallhash-screen/bench',settings=base/'settings-final.json',stage='final-all30-coarse',generations=4,rss_gib=4,budget=900,timeout=150,native_depth=24,source_balanced=True)
s=Study(a)
s.manifest['binary_source_head']='8249c0d';s.manifest['binary_note']='Archived smallhash candidate; final current cpp tree identical to 8249c0d after rejected transpose and Slice-hash reverts.';s.persist()
s.manifest['controls_reused']={'path':str(base/'benchmark-all-g4/manifest.json'),'sha256':sha(base/'benchmark-all-g4/manifest.json'),'reason':'ASGP source, binary ASGP logic, frozen source cases and search settings unchanged by native GPU runtime changes. Same P1024/C1024/G4/seed0, own ASGP controls, no historical denominator substitution.'}
s.manifest['note']='All30 same four-generation native search contract, no projected depth cap or uniform-phase variation. One cold process per task; coarse screening, not precision benchmarks.';s.persist()
for task in PAPER_TASKS:
 folder=base/'support-balanced'/task;prepared=base/'benchmark-all-g4'/f'{task}.p1024.prepared.json'
 s.manifest['tasks'][task]={'status':'running','inputs':{str(p):sha(p) for p in [folder/'source.json',folder/'grammar.json',prepared]}};s.persist()
 status=s.search(task,folder,prepared,1024,'gpu_overlap',0)
 s.manifest['tasks'][task]['status']='passed' if status=='ok' else status;s.persist()
# Same-round controls check environmental drift; preserve original denominator.
for task in ['house_robber','median']:
 s.search(task,base/'support-balanced'/task,base/'benchmark-all-g4'/f'{task}.p1024.prepared.json',1024,'asgp_1t',0)
