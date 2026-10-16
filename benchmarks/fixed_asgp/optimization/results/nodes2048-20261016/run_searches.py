"""All30 largest-population support with uniform2048 nodes and16GiB workspace."""
from argparse import ArgumentParser, Namespace
import ctypes
import fcntl
import os
import subprocess
from pathlib import Path
import threading
import time

from gagp_tools.experiments.fixed_asgp.low_budget import Study, PAPER_TASKS, MODES, read, save, sha, validate_search

ROOT=Path.cwd()
p=ArgumentParser(description=__doc__)
p.add_argument('--out',type=Path,required=True)
p.add_argument('--prepared',type=Path,default=ROOT/'logs/nodes2048-20261016')
p.add_argument('--binary',type=Path,default=ROOT/'cpp/build/release/gagp_fixed_asgp_bench')
p.add_argument('--tasks',nargs='+',choices=PAPER_TASKS,default=list(PAPER_TASKS))
a=p.parse_args()
a.out.parent.mkdir(parents=True,exist_ok=True)
lock=(a.out.parent/(a.out.name+'.controller.lock')).open('a')
fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
s=Study(Namespace(out=a.out,binary=a.binary,
    settings=ROOT/'benchmarks/fixed_asgp/optimization/results/list-5x-20261015/settings-workspace-pool16.json',
    stage='uniform-nodes2048-all30-searches',generations=4,rss_gib=32,budget=21600,
    timeout=1800,native_depth=24,native_nodes=2048,source_balanced=True))
s.manifest.update(population=16384,runner_sha256=sha(Path(__file__)),tasks_requested=a.tasks,
    preparation_manifest=str(a.prepared/'manifest.json'),results={},small_modes={},
    uniform_native_limits=dict(max_nodes=2048,max_depth=24),phase_capacity=1024,
    note='All requested tasks use same native2048/depth24 grammar profile and16GiB workspace. '
         'Unchanged source16384 parents/full1024cases, seed0,G4+finaleval,CPU1T. '
         'Full final-population CPU fitness audit outside evolve wall. '
         'ASGP own-P controls reused by source hash; ASGP implementation/depth7 is unchanged. '
         'Separate from old1024-node matrix; not equal search-space or precision evidence.')
s.persist()
cpu_name=next(line.split(':',1)[1].strip() for line in Path('/proc/cpuinfo').read_text().splitlines() if line.startswith('model name'))
save(s.out/'environment.json',dict(cpu=cpu_name,
    gpu_inventory=subprocess.check_output(['nvidia-smi','--query-gpu=index,name,memory.total,driver_version,uuid','--format=csv'],text=True),
    cuda_device=s.env['GAGP_CUDA_DEVICE'],cuda_visible_devices=os.environ.get('CUDA_VISIBLE_DEVICES'),
    binary_sha256=s.manifest['binary_sha256'],source_head=s.manifest['source_head'],
    cpu_threads=1,workspace_mib=16384,native_limits=dict(max_nodes=2048,max_depth=24)))
# Wait for all offline work to finish so it cannot compete with formal runs.
while True:
    preparation=read(a.prepared/'manifest.json')
    if preparation.get('requested_phase_complete'):
        break
    if time.monotonic()-s.started>s.args.budget:
        raise TimeoutError('offline preparation did not finish within total budget')
    time.sleep(1)
s.manifest['preparation_sha256']=sha(a.prepared/'manifest.json')
controls_path=ROOT/'logs/all30-scaling-20261016/same16/manifest.json'
controls=read(controls_path)
s.manifest.update(controls_manifest=str(controls_path),controls_sha256=sha(controls_path))
assert s.env['GAGP_GPU_PAYLOAD_WORKSPACE_MIB']=='16384'
for task in a.tasks:
    entry=preparation['catalog'][task]
    assert entry['native_limits']==dict(max_nodes=2048,max_depth=24)
    for field in ('source','grammar','prepared'):
        if field in entry:
            assert sha(Path(entry[field]))==entry[field+'_sha256']
    if entry['admission_status']=='ok':
        old=controls['results'][task+'.p16384']
        assert entry['source_sha256']==old['inputs'][entry['source']]
    s.manifest['results'][task]=dict(status='pending',inputs=entry)
s.persist()

class Memory(ctypes.Structure):
    _fields_=[('total',ctypes.c_ulonglong),('free',ctypes.c_ulonglong),('used',ctypes.c_ulonglong)]
nvml=ctypes.CDLL('libnvidia-ml.so.1');assert nvml.nvmlInit_v2()==0
handle=ctypes.c_void_p();assert nvml.nvmlDeviceGetHandleByIndex_v2(0,ctypes.byref(handle))==0
stop=threading.Event();peak=0

def sample():
    global peak
    with (s.out/'vram.csv').open('a') as out:
        while not stop.is_set():
            mem=Memory()
            if nvml.nvmlDeviceGetMemoryInfo(handle,ctypes.byref(mem))==0:
                peak=max(peak,mem.used)
                out.write(f'{time.time_ns()},{mem.used},{mem.total}\n');out.flush()
            stop.wait(.2)

def measure(task,population,mode,generations):
    global peak
    entry=preparation['catalog'][task];tag=f'p{population}.{mode}'
    output=s.out/f'{task}.{tag}.json'
    extra=dict(GAGP_BM_SEED='0',GAGP_SEARCH_GENERATIONS=str(generations),
        GAGP_SEARCH_EXPORT=str(s.out/f'{task}.{tag}.final.json'))
    if population==16384:extra['GAGP_SEARCH_CPU_ALL']='1'
    peak=0
    cell=s.run(task,tag,[s.args.binary,'measure',entry['source'],entry['prepared'],entry['grammar'],
        population,'search_'+mode,output],extra)
    result=dict(status=cell['status'],output=str(output),log=cell.get('log'),process_s=cell.get('process_s'),
        sampled_device_peak_bytes=peak,workspace_mib=16384 if mode!='asgp_1t' else None)
    if cell['status']=='ok':
        try:
            data=read(output);validate_search(data,population,generations,mode)
            if mode in ('gpu_repro','gpu_overlap'):
                assert data['reproduction_profile'].startswith('native-gpu-phase-')
                assert not data['reproduction_fallback_reason']
            result.update(evolve_wall_s=data['evolve_wall_ms']/1000,
                reproduction_profile=data['reproduction_profile'],cpu_audit_mismatches=data.get('cpu_audit_mismatches'),
                cpu_audit_program_count=data.get('cpu_audit_program_count'),
                final_max_nodes=max(row['nodes'] for row in data['final_population']))
            if population==16384:
                assert result['cpu_audit_program_count']==population and result['cpu_audit_mismatches']==0
                result.update(errors=sum(g['errors'] for g in data['generations']),
                    timeouts=sum(g['timeouts'] for g in data['generations']),
                    unscored=sum(g['unscored'] for g in data['generations']))
                assert result['unscored']==0 and result['final_max_nodes']<=2048
        except (AssertionError,KeyError,ValueError) as error:
            result.update(status='validation_failed',reason=str(error))
    return result

sampler=threading.Thread(target=sample,daemon=True);sampler.start()
try:
    if 'house_robber' in a.tasks:
        for mode in MODES:
            s.manifest['small_modes'][mode]=measure('house_robber',8,mode,2)
            s.persist()
    order=sorted(a.tasks,key=lambda task:task!='house_robber')
    for task in order:
        record=s.manifest['results'][task]
        if record['inputs']['admission_status']!='ok':
            record.update(status='initial_admission_limit',reason=record['inputs']['admission_status'])
        else:
            record.update(measure(task,16384,'gpu_overlap',4))
            control=controls['results'][task+'.p16384']['asgp_1t']
            assert control['status']=='ok'
            asgp=read(Path(control['output']));validate_search(asgp,16384,4,'asgp_1t')
            record['asgp_control']=dict(output=control['output'],sha256=sha(Path(control['output'])),
                evolve_wall_s=asgp['evolve_wall_ms']/1000,reused=True)
            if record['status']=='ok':record['speedup']=asgp['evolve_wall_ms']/1000/record['evolve_wall_s']
        s.persist();print('completed',task,record['status'],flush=True)
finally:
    stop.set();sampler.join()
    s.manifest['requested_phase_complete']=all(r['status']!='pending' for r in s.manifest['results'].values())
    s.persist()
print('passed',sum(r['status']=='ok' for r in s.manifest['results'].values()),'of',len(a.tasks),flush=True)
