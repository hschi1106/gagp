"""Same-binary, fixed-16GiB scaling; preserve the earlier matrix and failures."""
from argparse import ArgumentParser, Namespace
from pathlib import Path
import ctypes
import fcntl
import json
import os
import threading
import time

from gagp_tools.experiments.fixed_asgp.low_budget import Study, PAPER_TASKS, read, sha, validate_search

ROOT = Path.cwd()
BASE = ROOT / 'logs/all30-scaling-20261016'
p = ArgumentParser(description=__doc__)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--binary', type=Path, default=Path('cpp/build/release/gagp_fixed_asgp_bench'))
p.add_argument('--resume', action='store_true')
p.add_argument('--tasks', nargs='+', choices=PAPER_TASKS, default=list(PAPER_TASKS))
p.add_argument('--populations', nargs='+', type=int, choices=[1024,2048,4096,8192,16384], default=[16384,8192,4096,2048,1024])
p.add_argument('--gpu-only', action='store_true')
a = p.parse_args()
a.out.parent.mkdir(parents=True, exist_ok=True)
lock = (a.out.parent / (a.out.name + '.controller.lock')).open('a')
fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
config = Namespace(out=a.out, binary=a.binary,
    settings=ROOT / 'benchmarks/fixed_asgp/optimization/results/list-5x-20261015/settings-workspace-pool16.json',
    stage='all30-scaling-same16', generations=4, rss_gib=32, budget=43200,
    timeout=1800, native_depth=24, source_balanced=True)
if a.resume:
    s = Study.__new__(Study)
    s.args=config;s.out=a.out.resolve();s.manifest=read(s.out/'manifest.json')
    assert s.manifest['stage']==config.stage
    commands={tuple(c['command']) for c in s.manifest['cells'] if c['status']=='running'}
    for proc in Path('/proc').glob('[0-9]*'):
        try:
            cmd=tuple(x.decode() for x in (proc/'cmdline').read_bytes().split(b'\0') if x)
        except (OSError,UnicodeDecodeError):
            continue
        if cmd in commands:raise RuntimeError(f'previous native process still active: {proc.name}')
    for cell in s.manifest['cells']:
        if cell['status']=='running':cell['status']='interrupted'
    s.args.binary=s.out/'bench'
    assert sha(s.args.binary)==s.manifest['binary_sha256']
    s.env={k:v for k,v in os.environ.items() if not k.startswith('GAGP_') and k not in ('LD_PRELOAD','MALLOC_CONF')}
    s.env.update(s.manifest['settings'])
    s.started=time.monotonic()-s.manifest['elapsed_s']
else:
    s=Study(config)
    s.manifest.update(results={},populations=a.populations,population=None,
        note='Fixed16GiB for every population. Same candidate binary for all GPU cells; no smaller-capacity retries. '
             'G4+final evaluation/1024 cases/CPU1T. Full final-population CPU fitness audit at P16384, outside evolve wall time. '
             'Earlier ASGP controls reused only with exact source/prepared/grammar inputs and identical settings; '
             'the implementation changes GPU buffer lifetime/capacity only. Missing controls are measured after GPU support checks.',
        controls_manifest=str(BASE/'matrix/manifest.json'),controls_sha256=sha(BASE/'matrix/manifest.json'))
assert s.manifest['settings']['GAGP_GPU_PAYLOAD_WORKSPACE_MIB']=='16384'
s.manifest.setdefault('runner_versions',[]).append({'path':str(Path(__file__).resolve()),'sha256':sha(Path(__file__))})
s.persist()
controls=read(BASE/'matrix/manifest.json');artifacts=read(BASE/'artifacts/manifest.json')
alternate=read(BASE/'capacity-retries/manifest.json')
assert controls['settings']==s.manifest['settings']

# Read-only NVML sampling does not alter device limits or application workers.
class Memory(ctypes.Structure):
    _fields_=[('total',ctypes.c_ulonglong),('free',ctypes.c_ulonglong),('used',ctypes.c_ulonglong)]
nvml=ctypes.CDLL('libnvidia-ml.so.1')
assert nvml.nvmlInit_v2()==0
handle=ctypes.c_void_p();assert nvml.nvmlDeviceGetHandleByIndex_v2(0,ctypes.byref(handle))==0
peak=0;stop=threading.Event()
def sample():
    global peak
    with (s.out/'vram.csv').open('a') as out:
        while not stop.is_set():
            mem=Memory()
            if nvml.nvmlDeviceGetMemoryInfo(handle,ctypes.byref(mem))==0:
                peak=max(peak,mem.used)
                out.write(f'{time.time_ns()},{mem.used},{mem.total}\n');out.flush()
            stop.wait(.2)
sampler=threading.Thread(target=sample,daemon=True);sampler.start()

def entry(task,pop):
    if pop==1024:
        old=ROOT/'logs/all-task-repair-20261015'
        return {'source':str(old/'support-balanced'/task/'source.json'),
            'grammar':str(old/'support-balanced'/task/'grammar.json'),
            'prepared':str(old/'benchmark-all-g4'/f'{task}.p1024.prepared.json')}
    return artifacts['tasks'][task]['prefixes'][str(pop)]

def run(task,pop,mode,record):
    global peak
    prefix=f'p{pop}.{mode}.'
    attempt=sum(c['task']==task and c['phase'].startswith(prefix) for c in s.manifest['cells'])
    tag=prefix+f'w16384.a{attempt}'
    output=s.out/f'{task}.{tag}.json';export=s.out/f'{task}.{tag}.final.json'
    source=entry(task,pop)
    extra={'GAGP_SEARCH_GENERATIONS':'4','GAGP_BM_SEED':'0',
        'GAGP_GPU_PAYLOAD_WORKSPACE_MIB':'16384','GAGP_SEARCH_EXPORT':str(export)}
    if mode=='gpu_overlap' and pop==16384:extra['GAGP_SEARCH_CPU_ALL']='1'
    peak=0;start=time.time_ns()
    cell=s.run(task,tag,[s.args.binary,'measure',source['source'],source['prepared'],source['grammar'],
        pop,'search_'+mode,output],extra)
    result=dict(status=cell['status'],workspace_mib=16384 if mode=='gpu_overlap' else None,
        output=str(output),log=cell.get('log'),process_s=cell.get('process_s'),
        sampled_device_peak_bytes=peak,start_unix_ns=start,end_unix_ns=time.time_ns())
    if cell['status']=='ok':
        try:
            data=read(output);validate_search(data,pop,4,mode)
            if mode=='gpu_overlap' and pop==16384 and data['cpu_audit_program_count']!=pop:
                raise ValueError('incomplete full-population CPU audit')
            if mode=='gpu_overlap':
                key=f'{task}.p{pop}'
                before=controls.get('results',{}).get(key,{}).get('gpu_overlap',{})
                if before.get('status')!='ok':
                    before=alternate.get('results',{}).get(key,{}).get('gpu_overlap',{})
                if before.get('status')=='ok':
                    old_data=read(Path(before['output']))
                    fields=('best','best_nodes','mean','cases','errors','timeouts','fallbacks','unscored',
                        'changed','unchanged','rejected','budget_rejections','contract_rejections',
                        'generation_rejections','operator_fallbacks')
                    if data['final_population']!=old_data['final_population'] or any(
                        left.get(k)!=right.get(k) for left,right in zip(data['generations'],old_data['generations']) for k in fields):
                        raise ValueError('buffer lifetime changed full fitness or semantic/operator trace')
                    old_export=Path(before['output']).with_suffix('.final.json')
                    if read(export)!=read(old_export):
                        raise ValueError('buffer lifetime changed complete exported AST population')
                    result['exact_reference_output']=before['output']
                    result['exact_reference_workspace_mib']=before['workspace_mib']
            result.update(evolve_wall_ms=data['evolve_wall_ms'],final_best=data['final_best'],
                cpu_audit_mismatches=data.get('cpu_audit_mismatches'),sha256=sha(output))
        except (ValueError,KeyError) as error:
            result.update(status='validation_failed',reason=str(error))
    record['attempts'].append(dict(mode=mode,**result));record[mode]=result
    record['complete']=all(m in record for m in ('asgp_1t','gpu_overlap'))
    record['status']='pass' if record['complete'] and all(record[m]['status']=='ok' for m in ('asgp_1t','gpu_overlap')) else 'failed' if record['complete'] else 'pending'
    record['speedup']=record['asgp_1t']['evolve_wall_ms']/record['gpu_overlap']['evolve_wall_ms'] if record['status']=='pass' else None
    s.persist()

try:
    for pop in a.populations:
        for task in a.tasks:
            key=f'{task}.p{pop}'
            record=s.manifest['results'].setdefault(key,dict(complete=False,attempts=[]))
            inputs={path:sha(Path(path)) for path in entry(task,pop).values() if path and Path(path).is_file()}
            if 'inputs' in record:assert record['inputs']==inputs
            record['inputs']=inputs
            old=controls.get('results',{}).get(key,{})
            if 'asgp_1t' not in record and old.get('asgp_1t',{}).get('status')=='ok':
                assert old['inputs']==inputs
                data=read(Path(old['asgp_1t']['output']));validate_search(data,pop,4,'asgp_1t')
                record['asgp_1t']=dict(old['asgp_1t'],reused_control=True,
                    log=str(BASE/'matrix'/old['asgp_1t']['log']),sha256=sha(Path(old['asgp_1t']['output'])))
            s.persist()
            if 'gpu_overlap' not in record:run(task,pop,'gpu_overlap',record)
    if not a.gpu_only:
        for pop in a.populations:
            for task in a.tasks:
                record=s.manifest['results'][f'{task}.p{pop}']
                if 'asgp_1t' not in record:run(task,pop,'asgp_1t',record)
    s.manifest['requested_phase_complete']=True;s.persist()
finally:
    stop.set();sampler.join(timeout=2);nvml.nvmlShutdown()
