"""Uniform 2048-node admission; House16K full GPU search, unchanged source parents."""
from argparse import ArgumentParser, Namespace
import copy
import ctypes
import fcntl
import json
from pathlib import Path
import threading
import time

from gagp_tools.experiments.fixed_asgp.low_budget import Study, PAPER_TASKS, MODES, read, save, sha, validate_search

ROOT = Path.cwd()
p = ArgumentParser(description=__doc__)
p.add_argument('--out', required=True, type=Path)
p.add_argument('--binary', type=Path, default=ROOT / 'cpp/build/release/gagp_fixed_asgp_bench')
a = p.parse_args()
a.out.parent.mkdir(parents=True, exist_ok=True)
lock = (a.out.parent / (a.out.name + '.controller.lock')).open('a')
fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
settings = ROOT / 'benchmarks/fixed_asgp/optimization/results/list-5x-20261015/settings-workspace-pool16.json'
s = Study(Namespace(out=a.out, binary=a.binary, settings=settings, stage='uniform-nodes2048-support',
    generations=4, rss_gib=32, budget=14400, timeout=1800, native_depth=24,
    native_nodes=2048, source_balanced=True))
s.manifest.update(population=16384, runner_sha256=sha(Path(__file__)), uniform_native_limits=dict(max_nodes=2048,max_depth=24),
    phase_capacity=1024, source_phase_depth=7, catalog={}, small_modes={},
    note='Fresh uniform native whole-AST node budget. Source parents/cases are unchanged; '
         'ASGP logical depth7 and other search-rule differences remain. All30 admission is checked; '
         'only House Robber has new search timings. Do not merge with the 1024-node scaling matrix.')
artifacts = read(ROOT / 'logs/all30-scaling-20261016/artifacts/manifest.json')
for task in PAPER_TASKS:
    folder = s.out / task
    folder.mkdir()
    source = Path(artifacts['tasks'][task]['prefixes']['16384']['source'])
    old_grammar = ROOT / 'logs/all-task-repair-20261015/support-balanced' / task / 'grammar.json'
    before = read(old_grammar)
    document = copy.deepcopy(before)
    assert document['search_limits'] == dict(max_nodes=1024,max_depth=24)
    document['search_limits']['max_nodes'] = 2048
    grammar = folder / 'grammar.json'
    save(grammar,document)
    s.manifest['catalog'][task] = dict(source=str(source),source_sha256=sha(source),
        original_grammar=str(old_grammar),original_grammar_sha256=sha(old_grammar),
        grammar=str(grammar),grammar_sha256=sha(grammar),native_limits=document['search_limits'])
s.persist()


def prepare(task):
    entry = s.manifest['catalog'][task]
    path = s.out / task / 'p16384.prepared.json'
    cell = s.run(task,'prepare16384',[s.args.binary,'prepare',entry['source'],entry['grammar'],16384,path])
    entry['admission_status'] = cell['status']
    if cell['status'] == 'ok':
        data = read(path)
        assert len(data['programs']) == 16384
        sizes = [len(g['nodes']) for g in data['programs']]
        assert max(sizes) <= 2048
        prior_pop='8192' if task=='house_robber' else '16384'
        prior=artifacts['tasks'][task]['prefixes'][prior_pop]['prepared']
        previous=read(Path(prior))['programs']
        assert data['programs'][:int(prior_pop)]==previous
        entry['exact_prior_prefix_check']=int(prior_pop)
        entry.update(prepared=str(path),prepared_sha256=sha(path),max_initial_ast_nodes=max(sizes),
            initial_asts_above_1024=sum(n>1024 for n in sizes))
    s.persist()
    print('admission',task,entry['admission_status'],flush=True)
    return cell['status'] == 'ok'


# Sampling is read-only and outside the application worker count.
class Memory(ctypes.Structure):
    _fields_=[('total',ctypes.c_ulonglong),('free',ctypes.c_ulonglong),('used',ctypes.c_ulonglong)]
nvml=ctypes.CDLL('libnvidia-ml.so.1')
assert nvml.nvmlInit_v2()==0
handle=ctypes.c_void_p()
assert nvml.nvmlDeviceGetHandleByIndex_v2(0,ctypes.byref(handle))==0
stop=threading.Event()
peak=0

def sample():
    global peak
    with (s.out/'vram.csv').open('a') as out:
        while not stop.is_set():
            mem=Memory()
            if nvml.nvmlDeviceGetMemoryInfo(handle,ctypes.byref(mem))==0:
                peak=max(peak,mem.used)
                out.write(f'{time.time_ns()},{mem.used},{mem.total}\n')
                out.flush()
            stop.wait(.2)


def measure(population,mode,generations):
    global peak
    entry=s.manifest['catalog']['house_robber']
    tag=f'p{population}.{mode}'
    output=s.out/f'house_robber.{tag}.json'
    extra=dict(GAGP_SEARCH_GENERATIONS=str(generations),GAGP_BM_SEED='0',
        GAGP_SEARCH_EXPORT=str(s.out/f'house_robber.{tag}.final.json'))
    if mode=='gpu_overlap' and population==16384:
        extra['GAGP_SEARCH_CPU_ALL']='1'
    peak=0
    cell=s.run('house_robber',tag,[s.args.binary,'measure',entry['source'],entry['prepared'],
        entry['grammar'],population,'search_'+mode,output],extra)
    result=dict(status=cell['status'],output=str(output),log=cell.get('log'),process_s=cell.get('process_s'))
    if mode!='asgp_1t':
        result['sampled_device_peak_bytes']=peak
    if cell['status']=='ok':
        data=read(output)
        validate_search(data,population,generations,mode)
        if mode in ('gpu_repro','gpu_overlap'):
            assert data['reproduction_profile'].startswith('native-gpu-phase-')
            assert not data['reproduction_fallback_reason']
        result.update(evolve_wall_s=data['evolve_wall_ms']/1000,
            reproduction_profile=data['reproduction_profile'],cpu_audit_mismatches=data.get('cpu_audit_mismatches'))
        if population==16384:
            assert data['cpu_audit_program_count']==16384 and data['cpu_audit_mismatches']==0
            result.update(cpu_audit_program_count=data['cpu_audit_program_count'],
                errors=sum(g['errors'] for g in data['generations']),
                timeouts=sum(g['timeouts'] for g in data['generations']),
                unscored=sum(g['unscored'] for g in data['generations']))
            assert result['unscored']==0
    print('measure',tag,result['status'],flush=True)
    return result


sampler=threading.Thread(target=sample,daemon=True)
sampler.start()
try:
    if prepare('house_robber'):
        entry=s.manifest['catalog']['house_robber']
        witness=s.run('house_robber','known-witness',[s.args.binary,'audit',entry['source'],entry['grammar'],s.out/'house-witness.json'])
        s.manifest['house_witness']=witness['status']
        for mode in MODES:
            s.manifest['small_modes'][mode]=measure(8,mode,2)
            s.persist()
        s.manifest['house16384']=measure(16384,'gpu_overlap',4)
        controls=read(ROOT/'logs/all30-scaling-20261016/same16/manifest.json')
        old=controls['results']['house_robber.p16384']
        assert entry['source_sha256']==old['inputs'][entry['source']]
        control=old['asgp_1t']
        assert control['status']=='ok'
        asgp=read(Path(control['output']))
        validate_search(asgp,16384,4,'asgp_1t')
        s.manifest['asgp_control']=dict(output=control['output'],sha256=sha(Path(control['output'])),
            evolve_wall_s=asgp['evolve_wall_ms']/1000,reused=True,
            note='Exact same source/cases/P/G4/seed; ASGP path ignores the native grammar budget and is unchanged.')
        if s.manifest['house16384']['status']=='ok':
            s.manifest['house16384']['speedup']=asgp['evolve_wall_ms']/1000/s.manifest['house16384']['evolve_wall_s']
        s.persist()
    for task in PAPER_TASKS:
        if task!='house_robber':
            prepare(task)
finally:
    stop.set()
    sampler.join()
    s.manifest['requested_phase_complete']=all('admission_status' in e for e in s.manifest['catalog'].values())
    s.persist()
print(json.dumps(dict(admissions=sum(e.get('admission_status')=='ok' for e in s.manifest['catalog'].values()),
    house16384=s.manifest.get('house16384')),indent=2),flush=True)
