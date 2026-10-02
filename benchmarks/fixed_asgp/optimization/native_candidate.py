"""Serial validation of the native GPU profile; reads frozen artifacts only."""
from pathlib import Path
import argparse,hashlib,json,os,runpy,statistics,subprocess,time
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('stage',choices=('daily','formal','paired','screen','snapshot','quality','scaling','cpu-views'))
parser.add_argument('--out',type=Path,required=True,help='New result directory, or an existing manifest to resume')
args=parser.parse_args()
root=Path.cwd();out=root/args.out;out.mkdir(parents=True,exist_ok=True)
a=root/'logs/fixed-asgp/artifacts';manifest_path=out/'manifest.json' 
TASKS=('sum_of_elements','house_robber','median');MODES=('asgp_1t','gagp_cpu','gpu_eval','gpu_repro','gpu_overlap')
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
base=runpy.run_path('benchmarks/fixed_asgp/optimization/round2.py')['FLAGS']
base.update(GAGP_SINGLE_EVAL_IDENTITY='1',GAGP_OWNED_CROSSOVER_HANDOFF='1',LD_PRELOAD=str(root/'logs/optimization/native-allocator/extracted/usr/lib/x86_64-linux-gnu/libjemalloc.so.2'),MALLOC_CONF='background_thread:false,narenas:1',OMP_NUM_THREADS='1',OPENBLAS_NUM_THREADS='1',MKL_NUM_THREADS='1')
fast=dict(base,GAGP_GPU_PHASE_POPULATION='1',GAGP_OWNED_PHASE_PACK='1',GAGP_OWNED_PHASE_CAPS='1',GAGP_COORDINATE_EXECUTOR='1',GAGP_COUNTED_SITE_DRAW='1')
# The existing optional CPU window profile is measured separately before any adoption.
if not Path(base['LD_PRELOAD']).is_file():raise FileNotFoundError('Required archived jemalloc dependency: '+base['LD_PRELOAD'])
if manifest_path.exists():
 m=json.loads(manifest_path.read_text());b=Path(m['binary']);assert digest(b)==m['sha256']
 assert base==m['base_flags'] and fast==m['fast_flags'], 'Refuse to resume with changed settings'
else:
 b=out/'bench';assert not b.exists();b.write_bytes((root/'cpp/build/release/gagp_fixed_asgp_bench').read_bytes());b.chmod(0o755)
 frozen=json.loads((a/'manifest.json').read_text());identities={}
 for t in TASKS:
  for suffix in ('source.json','grammar.json','p1024.prepared.json','p8192.prepared.json'):
   name=f'{t}.{suffix}';identities[name]=digest(a/name);assert identities[name]==frozen['tasks'][t][name]
 m={'binary':str(b),'sha256':digest(b),'commit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'base_flags':base,'fast_flags':fast,'frozen_sha256':identities,'cells':[]}
 manifest_path.write_text(json.dumps(m,indent=2)+'\n')
def run(t,p,mode,tag,kind='fast',extra=None):
 if any(c['tag']==tag for c in m['cells']):return next(c for c in m['cells'] if c['tag']==tag)
 target=out/f'{tag}.json'
 # Preserve an interrupted scaling attempt; resume into a fresh output file.
 if tag.startswith('scaling.'):
  attempt=1
  while target.exists():
   target=out/f'{tag}.attempt{attempt}.json';attempt+=1
 else:assert not target.exists(),target
 active=dict(fast if kind=='fast' else base)
 if kind=='uncounted':
  active=dict(fast);active.pop('GAGP_COUNTED_SITE_DRAW')
 if extra:active.update(extra)
 env={k:v for k,v in os.environ.items() if not k.startswith('GAGP_') and k not in ('LD_PRELOAD','MALLOC_CONF')};env.update(active)
 prepared=a/f'{t}.p{1024 if p==1024 else 8192}.prepared.json'
 cmd=[str(b),'measure',str(a/f'{t}.source.json'),str(prepared),str(a/f'{t}.grammar.json'),str(p),mode,str(target)]
 begin=time.monotonic()
 with target.with_suffix('.log').open('w') as f:subprocess.run(['/usr/bin/time','-f','rss_kb=%M\nwall_seconds=%e','-o',str(target.with_suffix('.resources')),*cmd],env=env,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=3600)
 cell={'task':t,'population':p,'mode':mode,'tag':tag,'kind':kind,'flags':active,'command':cmd,'output':str(target),'wall_seconds':time.monotonic()-begin,'resources':target.with_suffix('.resources').read_text()}
 if mode in MODES:
  rows=[json.loads(s) for s in target.read_text().splitlines()];assert [r['rep'] for r in rows]==[-1,0,1,2]
  assert all(r['population']==p and r['cases']==1024 for r in rows)
  if kind=='fast' and mode in ('gpu_repro','gpu_overlap'):assert all(r['reproduction_profile']=='native-gpu-phase-v2-counted' for r in rows)
  cell['rows']=rows;cell['median_ms']=statistics.median(r['generation_ms'] for r in rows if r['rep']>=0)
 elif mode=='snapshot':
  r=json.loads(target.read_text());rows=r['programs'];assert len(rows)==p
  assert all(x['cases']==1024 and x['unscored']==0 for x in rows)
  reference=root/'logs/optimization/native-allocator-validation'/f'snapshot.{t}.after.json'
  assert rows==json.loads(reference.read_text())['programs'], 'CPU/GPU snapshot changed from historical reference'
  cell['reference']=str(reference);cell['cpu_fitness_mismatches']=[x['program'] for x in rows if x['fitness']!=x['cpu_fitness']]
 elif mode=='search':
  r=json.loads(target.read_text());assert r['cpu_audit_mismatches']==0
  assert all(g['cases']==p*1024 and g['unscored']==0 for g in r['generations'])
 m['cells'].append(cell);manifest_path.write_text(json.dumps(m,indent=2)+'\n')
 print(tag,round(cell.get('median_ms',cell['wall_seconds']),3),flush=True)
 return cell
if args.stage=='daily':
 for t in TASKS:
  for mode in MODES:run(t,1024,mode,f'daily.{t}.{mode}',extra={'GAGP_CPU_REGION_VIEWS':'1'})
elif args.stage=='formal':
 for process in range(3):
  for t in TASKS:run(t,1024,'gpu_overlap',f'formal{process}.{t}')
elif args.stage=='paired':
 for process in range(3):
  for t in TASKS:
   for kind in (('uncounted','fast') if process%2==0 else ('fast','uncounted')):
    run(t,1024,'gpu_overlap',f'paired{process}.{t}.{kind}',kind)
elif args.stage=='screen':
 for seed in range(3):
  for t in TASKS:
   tag=f'screen{seed}.{t}'
   run(t,1024,'search',tag,extra={'GAGP_BM_SEED':str(seed),'GAGP_SEARCH_GENERATIONS':'4','GAGP_GPU_DIAGNOSTICS':'1'})
elif args.stage=='snapshot':
 for t in TASKS:run(t,1024,'snapshot',f'snapshot.{t}',extra={'GAGP_SNAPSHOT_CPU':'1'})
elif args.stage=='quality':
 for seed in range(3):
  for t in TASKS:
   for kind in ('base','fast'):
    tag=f'quality{seed}.{t}.{kind}';gens=32 if kind=='base' else {'sum_of_elements':64,'house_robber':96,'median':512}[t]
    run(t,1024,'search',tag,kind,{'GAGP_BM_SEED':str(seed),'GAGP_SEARCH_GENERATIONS':str(gens),'GAGP_GPU_DIAGNOSTICS':'1','GAGP_SEARCH_EXPORT':str(out/f'{tag}.asts.json'),'GAGP_SEARCH_HISTORY_EXPORT':str(out/f'{tag}.history.json')})
elif args.stage=='scaling':
 m['scaling_flags']=dict(fast,GAGP_CPU_REGION_VIEWS='1')
 for p in (1024,2048,4096,8192):
  for t in TASKS:
   for mode in MODES:
    tag=f'scaling.p{p}.{t}.{mode}'
    if p==1024 and mode=='gagp_cpu' and not any(c['tag']==tag for c in m['cells']) and any(c['tag']==f'cpuviews2.{t}.on' for c in m['cells']):
     source=next(c for c in m['cells'] if c['tag']==f'cpuviews2.{t}.on')
     cell=dict(source,tag=tag,reused_from_tag=source['tag'],reuse_reason='Same source, archived binary, settings and frozen inputs; same-round third independent CPU process warmup+3.')
     m['cells'].append(cell);manifest_path.write_text(json.dumps(m,indent=2)+'\n')
     print(tag,'reused',source['tag'],round(cell['median_ms'],3),flush=True)
    else:run(t,p,mode,tag,extra={'GAGP_CPU_REGION_VIEWS':'1'})
elif args.stage=='cpu-views':
 for process in range(3):
  for t in TASKS:
   for on in ((False,True) if process%2==0 else (True,False)):
    run(t,1024,'gagp_cpu',f'cpuviews{process}.{t}.{"on" if on else "off"}',extra={'GAGP_CPU_REGION_VIEWS':'1'} if on else {})
