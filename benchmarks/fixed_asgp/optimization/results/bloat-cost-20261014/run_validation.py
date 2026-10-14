"""Existing fixed runners, fresh outputs; no preparation or timing-boundary changes."""
import argparse
import json
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parents[5]
LOCK = '/tmp/gagp-parallel-20261013-benchmark.lock'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', type=Path, required=True)
args = p.parse_args()
out = args.out.resolve(); out.mkdir(parents=True, exist_ok=False)
previous = json.loads((ROOT/'logs/optimization/astra-20261014/final/gpu-nine/manifest.json').read_text())
base = previous['flags']
flags = base | {'GAGP_GPU_DEFERRED_CROSSOVER_COPY':'1', 'GAGP_GPU_FUSED_SPLICE_DEPTH':'1', 'GAGP_GPU_COMPACT_COMPILE_FRAMES':'1'}
(out/'flags.json').write_text(json.dumps(flags,indent=2)+'\n')
def run(command, log):
    with (out/log).open('w') as f:
        subprocess.run(command,cwd=ROOT,stdout=f,stderr=subprocess.STDOUT,check=True)
def overrides(values):
    return [a for k,v in values.items() if k.startswith('GAGP_') for a in ('--flag',f'{k}={v}')]
def short(label, binary, settings, extra):
    run(['python3','benchmarks/fixed_asgp/optimization/short_search.py','--binary',str(binary),'--out',str(out/label),*overrides(settings),*extra],label+'.log')
# Every search is a fresh process and includes cold initialization/final work.
for repeat in range(9):
    for arm in (('before','after') if repeat%2==0 else ('after','before')):
        short(f'{arm}-{repeat}', previous['binary'] if arm=='before' else ROOT/'cpp/build/release/gagp_fixed_asgp_bench', base if arm=='before' else flags,['--modes','gpu_overlap'])
short('long',ROOT/'cpp/build/release/gagp_fixed_asgp_bench',flags,['--modes','gpu_overlap','--generations','128','--seeds','0','1','2'])
short('gpu-three',ROOT/'cpp/build/release/gagp_fixed_asgp_bench',flags,['--modes','gpu_eval','gpu_repro','--repeats','3'])
short('five-mode-smoke',ROOT/'cpp/build/release/gagp_fixed_asgp_bench',flags,['--generations','1'])
native=['flock',LOCK,'python3','benchmarks/fixed_asgp/optimization/native_candidate.py']
for stage,tasks in [('formal',['house_robber','median']),('scaling',['house_robber','median']),('daily',['sum_of_elements'])]:
    run([*native,stage,'--out',str(out/stage),'--tasks',*tasks,*overrides(flags)],stage+'.log')
print('Validation complete',out,flush=True)
