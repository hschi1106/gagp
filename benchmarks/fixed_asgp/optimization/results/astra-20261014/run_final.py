"""Reproduce final Astra checks with existing runners; never prepare inputs."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[5]
LOCK = '/tmp/gagp-parallel-20261013-benchmark.lock'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', type=Path, required=True)
args = p.parse_args()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=False)
baseline = ROOT/'logs/optimization/parallel-20261013/followup-main/manifest.json'
flags = json.loads(baseline.read_text())['flags']
flags.update(GAGP_GPU_FUSED_DONOR_SITES='1', GAGP_CPU_FUSED_ADMISSION='1',
             GAGP_CPU_COMPACT_ADMISSION='1', GAGP_GPU_CONSTRUCTIVE_EXPORT='1',
             GAGP_REUSE_IMPORT_CERTIFICATE='1', GAGP_PREPARED_CASE_VIEWS='1')
(out/'flags.json').write_text(json.dumps(flags, indent=2)+'\n')
overrides = [arg for k, v in flags.items() if k.startswith('GAGP_')
             for arg in ('--flag', f'{k}={v}')]

def run(command, log=None):
    if log:
        with (out/log).open('w') as f:
            subprocess.run(command, cwd=ROOT, stdout=f, stderr=subprocess.STDOUT, check=True)
    else:
        subprocess.run(command, cwd=ROOT, check=True)

run(['flock', LOCK, 'cmake', '--build', 'cpp/build/release', '-j3'], 'build.log')
run(['flock', LOCK, 'ctest', '--test-dir', 'cpp/build/release', '--output-on-failure'], 'tests.log')
short = ['python3', 'benchmarks/fixed_asgp/optimization/short_search.py',
         '--binary', 'cpp/build/release/gagp_fixed_asgp_bench', *overrides]
run([*short, '--out', str(out/'gpu-three'), '--modes', 'gpu_eval', 'gpu_repro', '--repeats', '3'])
# Cold complete searches use nine fresh processes, retaining initialization.
run([*short, '--out', str(out/'gpu-nine'), '--modes', 'gpu_overlap', '--repeats', '9'])
run([*short, '--out', str(out/'long'), '--modes', 'gpu_overlap',
     '--generations', '128', '--seeds', '0', '1', '2'])
native = ['flock', LOCK, 'python3', 'benchmarks/fixed_asgp/optimization/native_candidate.py']
# Fixed-parent formal timing separately uses three processes, warmup + three samples each.
run([*native, 'formal', '--out', str(out/'frozen-nine'), '--tasks', 'house_robber', 'median', *overrides])
run([*native, 'scaling', '--out', str(out/'scaling'), '--tasks', 'house_robber', 'median', *overrides])
run([*native, 'daily', '--out', str(out/'sum-regression'), '--tasks', 'sum_of_elements', *overrides])
print('Final validation complete:', out, flush=True)
