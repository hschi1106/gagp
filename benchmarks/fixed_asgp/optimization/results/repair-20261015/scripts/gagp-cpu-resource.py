from pathlib import Path
from argparse import Namespace
from gagp_tools.experiments.fixed_asgp.low_budget import Study
r=Path.cwd();b=r/'logs/all-task-repair-20261015'
a=Namespace(out=b/'negative-cpu-p1024',binary=b/'slow-p1024-g1-workspace8g/bench',settings=b/'settings-best.json',stage='cpu-memory-regression',generations=1,rss_gib=4,budget=150,timeout=140,native_depth=24,source_balanced=True)
s=Study(a);s.manifest['binary_source_head']='6ac07f2';s.manifest['note']='CPU1T P1024 full1024cases G1 plusfinaleval, guarded4GiB RSS; correctness/lifetime regression, not precise speed.';s.persist()
t='negative_to_zero';st=s.search(t,b/'support-balanced'/t,b/'benchmark-all-g4'/f'{t}.p1024.prepared.json',1024,'gagp_cpu',0);s.manifest['tasks'][t]={'status':st};s.persist()
