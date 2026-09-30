"""Serial controls and Nsight traces for the fixed 1024 x 1024 workload."""
from pathlib import Path
import hashlib,json,os,subprocess,time
ROOT=Path.cwd(); OUT=ROOT/'logs/profiler-baseline-20261001'; ART=ROOT/'logs/fixed-asgp/artifacts'
ORIGINAL=ROOT/'cpp/build/release/gagp_fixed_asgp_bench'; PROBE=OUT/'build/gagp_fixed_asgp_bench'
TASKS=('sum_of_elements','median','house_robber'); MODES=('gpu_eval','gpu_repro','gpu_overlap')
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def save(path,data):path.write_text(json.dumps(data,indent=2,allow_nan=False)+'\n')
def capture(command):return subprocess.run(command,cwd=ROOT,capture_output=True,text=True,check=True).stdout.strip()
frozen=json.loads((ART/'manifest.json').read_text())
for records in frozen['tasks'].values():
    for name,value in records.items():assert digest(ART/name)==value,name
manifest={'population':1024,'cases':1024,'warmup':1,'measured':3,
          'git_head':capture(['git','rev-parse','HEAD']),
          'original_binary':str(ORIGINAL),'original_binary_sha256':digest(ORIGINAL),
          'probe_binary':str(PROBE),'probe_binary_sha256':digest(PROBE),
          'instrumentation_sha256':digest(OUT/'instrumentation.patch'),
          'frozen':frozen,'frozen_manifest_sha256':digest(ART/'manifest.json'),
          'nsys':capture(['nsys','--version']),
          'gpu':capture(['nvidia-smi','--query-gpu=index,name,driver_version,memory.used,utilization.gpu','--format=csv']),
          'cpu':capture(['lscpu']),'cuda_device':0,'cells':[],'complete':False}
env=dict(os.environ,GAGP_CUDA_DEVICE='0')
start=time.monotonic()
def execute(command,stem,kind,task=None,mode=None):
    logfile=OUT/'runs'/f'{stem}.log'; logfile.parent.mkdir(exist_ok=True)
    begin=time.monotonic()
    with logfile.open('w') as log:
        result=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=300)
    entry={'kind':kind,'task':task,'mode':mode,'command':list(map(str,command)),
           'log':str(logfile),'returncode':result.returncode,'wall_seconds':time.monotonic()-begin}
    manifest['cells'].append(entry)
    save(OUT/'manifest.json',manifest)
    if result.returncode:raise RuntimeError(f'command failed: {logfile}')
    return entry
def measure(task,mode,binary,kind):
    stem=f'{task}.{mode}.{kind}'; output=OUT/'runs'/f'{stem}.jsonl'
    args=[binary,'measure',ART/f'{task}.source.json',ART/f'{task}.p1024.prepared.json',
          ART/f'{task}.grammar.json','1024',mode,output]
    if kind=='trace':
        args=['nsys','profile','--trace=cuda,nvtx,osrt','--sample=none','--cpuctxsw=none',
              '--cuda-memory-usage=true','--osrt-threshold=1000000','--stats=false',
              '--output='+str(OUT/'runs'/stem),*args]
    entry=execute(args,stem,kind,task,mode)
    rows=[json.loads(line) for line in output.read_text().splitlines()]
    assert [r['rep'] for r in rows]==[-1,0,1,2]
    assert all((r['task'],r['mode'],r['population'],r['cases'])==(task,mode,1024,1024) for r in rows)
    entry.update(rows=rows,raw_sha256=digest(output))
    save(OUT/'manifest.json',manifest)
    print(f'{stem}: OK ({time.monotonic()-start:.1f}s)',flush=True)
    if kind=='trace':
        execute(['nsys','export','--type','sqlite','--output',OUT/'runs'/f'{stem}.sqlite',
                 OUT/'runs'/f'{stem}.nsys-rep'],stem+'.export','export',task,mode)
    return rows
for task in TASKS:
    measure(task,'asgp_1t',ORIGINAL,'original')
    for mode in MODES:
        original=measure(task,mode,ORIGINAL,'original')
        probe=measure(task,mode,PROBE,'probe')
        trace=measure(task,mode,PROBE,'trace')
        # Instrumentation must preserve every non-timing result/counter, per rep.
        def semantic(row):return {k:v for k,v in row.items() if not k.endswith('_ms')}
        assert [semantic(r) for r in original]==[semantic(r) for r in probe]==[semantic(r) for r in trace],(task,mode)
manifest['complete']=True;manifest['wall_seconds']=time.monotonic()-start
save(OUT/'manifest.json',manifest)
print(f'All controls, traces, and semantic checks complete in {manifest["wall_seconds"]:.1f}s',flush=True)
