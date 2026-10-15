from pathlib import Path
from argparse import Namespace,ArgumentParser
from gagp_tools.experiments.fixed_asgp.low_budget import Study,read,save,snapshot_mismatches
r=Path.cwd();b=r/'logs/all-task-repair-20261015'
p=ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--settings',type=Path,required=True);a=p.parse_args()
s=Study(Namespace(out=a.out,binary=r/'cpp/build/release/gagp_fixed_asgp_bench',settings=a.settings,stage='list-five-mode-smoke',generations=2,rss_gib=4,budget=240,timeout=35,native_depth=24,source_balanced=True))
s.manifest['population']=8;s.manifest['note']='P8G2 smoke only, complete1024cases. Existing frozen prepared files, no prepare. Native five modes+CPU GPU snapshots; not headline speedup evidence.';s.persist()
for task in ['filter_odd','cumulative_sum','for_loop_index','double_letters','replace_space_with_newline','even_squares','negative_to_zero','fibonacci_array','small_or_large','grade','sum_of_elements','house_robber','median']:
 f=b/'support-balanced'/task;prepared=f/'p8.prepared.json';o=s.out/f'{task}.snapshot.json'
 cell=s.run(task,'snapshot',[s.args.binary,'measure',f/'source.json',prepared,f/'grammar.json',8,'snapshot',o],{'GAGP_SNAPSHOT_CPU':'1'})
 mismatches=snapshot_mismatches(read(o)['programs']) if cell['status']=='ok' else None
 statuses=[cell['status']]
 for mode in ['asgp_1t','gagp_cpu','gpu_eval','gpu_repro','gpu_overlap']:statuses.append(s.search(task,f,prepared,8,mode,0))
 s.manifest['tasks'][task]={'status':'passed' if all(x=='ok' for x in statuses) and mismatches==0 else 'failed','snapshot_mismatches':mismatches,'statuses':statuses};s.persist();print(task,s.manifest['tasks'][task],flush=True)
