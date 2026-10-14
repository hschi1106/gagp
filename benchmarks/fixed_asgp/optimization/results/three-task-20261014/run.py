"""Rerun three full-case native workloads; preserve historical inputs and outputs."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[5]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
out = a.out.resolve()
out.mkdir(parents=True, exist_ok=False)
best = json.loads((ROOT/'benchmarks/fixed_asgp/optimization/results/bloat-cost-20261014/final.json').read_text())
binary = Path(best['binary'])
assert hashlib.sha256(binary.read_bytes()).hexdigest() == best['binary_sha256']
# Native fixed runner archives the build binary; both stages must use the same runtime.
assert hashlib.sha256((ROOT/'cpp/build/release/gagp_fixed_asgp_bench').read_bytes()).hexdigest() == best['binary_sha256']
flags = [a for k, v in best['flags'].items() if k.startswith('GAGP_') for a in ('--flag', f'{k}={v}')]
with (out/'run.log').open('w') as log:
    subprocess.run(['python3', 'benchmarks/fixed_asgp/optimization/short_search.py', '--binary', str(binary),
                    '--out', str(out/'search'), '--tasks', 'sum_of_elements', 'house_robber', 'median',
                    '--repeats', '3', *flags], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(['flock', '/tmp/gagp-parallel-20261013-benchmark.lock', 'python3',
                    'benchmarks/fixed_asgp/optimization/native_candidate.py', 'daily', '--out', str(out/'fixed'),
                    '--tasks', 'sum_of_elements', 'house_robber', 'median', *flags],
                   cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
