"""Use existing fixed runners with frozen inputs and fresh output directories."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[5]
TASKS = ['sum_of_elements', 'house_robber', 'median']
LOCK = '/tmp/gagp-parallel-20261013-benchmark.lock'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('stage', choices=['timing', 'quality', 'other-modes', 'snapshot', 'scaling'])
p.add_argument('--out', type=Path, required=True)
p.add_argument('--binary', type=Path, default=ROOT/'cpp/build/release/gagp_fixed_asgp_bench')
a = p.parse_args()
out = a.out.resolve()
out.mkdir(parents=True, exist_ok=False)
old = json.loads((Path(__file__).parent/'contract.json').read_text())['baseline_manifest']
flags = old['flags'] | {k: '1' for k in (
    'GAGP_MEASURE_PHASE_CACHE', 'GAGP_PER_PHASE_STACK_BOUND', 'GAGP_HYBRID_PHASE_EXECUTOR',
    'GAGP_GPU_WINDOW_LEAF_DIRECT', 'GAGP_WINDOW_SOA', 'GAGP_DYNAMIC_PROGRAM_QUEUE')}
(out/'flags.json').write_text(json.dumps(flags, indent=2)+'\n')
def overrides(settings):
    return [x for k, v in settings.items() if k.startswith('GAGP_') for x in ('--flag', f'{k}={v}')]
def run(command, label):
    with (out/(label+'.log')).open('w') as log:
        subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    print('DONE', label, flush=True)
def search(label, binary, settings, extra):
    run(['python3', 'benchmarks/fixed_asgp/optimization/short_search.py', '--binary', str(binary),
         '--out', str(out/label), '--tasks', *TASKS, *overrides(settings), *extra], label)
if a.stage == 'timing':
    # Nine fresh cold searches, with three interleaved archived controls.
    for repeat in range(9):
        if repeat % 3 == 0:
            search(f'before-{repeat}', old['binary'], old['flags'], ['--modes', 'gpu_overlap'])
        search(f'after-{repeat}', a.binary, flags, ['--modes', 'gpu_overlap'])
    search('asgp-drift', old['binary'], old['flags'], ['--modes', 'asgp_1t'])
elif a.stage == 'quality':
    for arm in ['before', 'after']:
        search(arm, old['binary'] if arm == 'before' else a.binary,
               old['flags'] if arm == 'before' else flags,
               ['--modes', 'gpu_overlap', '--generations', '128', '--seeds', '0', '1', '2'])
elif a.stage == 'other-modes':
    search('gpu-three', a.binary, flags, ['--modes', 'gpu_eval', 'gpu_repro', '--repeats', '3'])
    search('five-mode-smoke', a.binary, flags, ['--generations', '1'])
elif a.stage == 'snapshot':
    for task in TASKS:
        for arm, binary, settings in [('before', old['binary'], old['flags']), ('after', a.binary, flags)]:
            env = {k: v for k, v in os.environ.items() if not k.startswith('GAGP_') and k not in ('LD_PRELOAD', 'MALLOC_CONF')}
            env.update(settings, GAGP_SNAPSHOT_CPU='1', GAGP_SNAPSHOT_PENALTY='1' if task == 'median' else '1000')
            artifacts = ROOT/'logs/fixed-asgp/artifacts'
            cmd = [str(binary), 'measure', *[str(artifacts/f'{task}.{s}.json') for s in ('source', 'p1024.prepared', 'grammar')], '1024', 'snapshot', str(out/f'{task}.{arm}.json')]
            with open(LOCK, 'a') as lock:
                fcntl.flock(lock, fcntl.LOCK_EX)
                with (out/f'{task}.{arm}.log').open('w') as log:
                    subprocess.run(cmd, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
            print('DONE', task, arm, flush=True)
        before = json.loads((out/f'{task}.before.json').read_text())['programs']
        after = json.loads((out/f'{task}.after.json').read_text())['programs']
        assert before == after, task
elif a.stage == 'scaling':
    if a.binary.resolve() != (ROOT/'cpp/build/release/gagp_fixed_asgp_bench').resolve():
        p.error('existing scaling runner archives the current Release build; use its binary')
    run(['flock', LOCK, 'python3', 'benchmarks/fixed_asgp/optimization/native_candidate.py',
         'scaling', '--out', str(out/'runs'), '--tasks', *TASKS, *overrides(flags)], 'scaling')
