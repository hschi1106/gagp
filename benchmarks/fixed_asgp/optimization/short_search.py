"""Serialized full-case changing-population benchmark for candidate comparisons."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]
MODES = ('asgp_1t', 'gagp_cpu', 'gpu_eval', 'gpu_repro', 'gpu_overlap')
LOCK = '/tmp/gagp-parallel-20261013-benchmark.lock'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--reference-root', type=Path, default=ROOT)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--modes', nargs='+', choices=MODES, default=list(MODES))
    p.add_argument('--tasks', nargs='+', choices=('house_robber', 'median'), default=['house_robber', 'median'])
    p.add_argument('--generations', type=int, help='Override House32/Median64 for focused experiments')
    p.add_argument('--seeds', nargs='+', type=int, help='Default House0/Median2')
    p.add_argument('--repeats', type=int, default=1)
    p.add_argument('--flag', action='append', default=[], help='Explicit recorded NAME=VALUE override')
    args = p.parse_args()
    if args.repeats < 1 or (args.generations is not None and args.generations < 1):
        p.error('positive repeats/generations required')
    refroot = args.reference_root.resolve()
    ref = json.loads((refroot/'benchmarks/fixed_asgp/optimization/results/long-search-20261013/manifest.json').read_text())
    artifacts = refroot/'logs/fixed-asgp/artifacts'
    flags = dict(ref['flags'])
    for override in args.flag:
        key, value = override.split('=', 1)
        if not key.startswith('GAGP_'):
            p.error('only explicit GAGP settings can be overridden')
        flags[key] = value
    if flags['GAGP_HOST_THREADS'] != '1':
        p.error('comparison requires CPU1T')
    frozen = {f'{task}.{suffix}': ref['frozen_sha256'][f'{task}.{suffix}']
              for task in args.tasks for suffix in ('source.json', 'grammar.json', 'p1024.prepared.json')}
    for name, digest in frozen.items():
        if sha(artifacts/name) != digest:
            raise RuntimeError('Frozen input changed: '+name)
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    binary = out/'bench'
    shutil.copy2(args.binary.resolve(), binary)
    manifest = dict(binary=str(binary), binary_sha256=sha(binary), frozen_sha256=frozen, flags=flags,
                    source_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                    source_patch=subprocess.check_output(['git', 'diff', '--', 'cpp'], cwd=ROOT, text=True),
                    contract='P1024/C1024 all pairs; CPU1T; unchanged native modes; changing populations; fresh process per repeat',
                    timing='Primary evolve_wall_ms: cold evolution initialization + complete generations + final evaluation; excludes input import and post-search audit/export. process_wall_s includes those costs. Lock waiting excluded.',
                    lock=LOCK, cells=[])
    def save():
        (out/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    save()
    for repeat in range(args.repeats):
        for task in args.tasks:
            gens = args.generations or (32 if task == 'house_robber' else 64)
            for seed in args.seeds if args.seeds is not None else [0 if task == 'house_robber' else 2]:
                for mode in args.modes:
                    tag = f'{task}.{mode}.s{seed}.r{repeat}'
                    target = out/f'{tag}.json'
                    env = {k: v for k, v in os.environ.items() if not k.startswith('GAGP_') and k not in ('LD_PRELOAD', 'MALLOC_CONF')}
                    env.update(flags, GAGP_SEARCH_PENALTY=str(1 if task == 'median' else 1000),
                               GAGP_BM_SEED=str(seed), GAGP_SEARCH_GENERATIONS=str(gens),
                               GAGP_SEARCH_EXPORT=str(out/f'{tag}.final.json'),
                               GAGP_SEARCH_HISTORY_EXPORT=str(out/f'{tag}.history.json'))
                    cmd = [str(binary), 'measure', str(artifacts/f'{task}.source.json'),
                           str(artifacts/f'{task}.p1024.prepared.json'), str(artifacts/f'{task}.grammar.json'),
                           '1024', 'search_'+mode, str(target)]
                    cell = dict(tag=tag, task=task, mode=mode, seed=seed, generations=gens, command=cmd,
                                output=str(target), flags={k:v for k,v in env.items() if k.startswith('GAGP_')}, status='waiting')
                    manifest['cells'].append(cell); save()
                    print('WAIT', tag, flush=True)
                    with open(LOCK, 'a') as lock:
                        fcntl.flock(lock, fcntl.LOCK_EX)
                        cell.update(status='running', started_epoch=time.time()); save()
                        start = time.monotonic()
                        with (out/f'{tag}.log').open('w') as log:
                            result = subprocess.run(['/usr/bin/time', '-f', 'peak_rss_kib=%M\nprocess_wall_s=%e', '-o', str(out/f'{tag}.resources'), *cmd], cwd=refroot, env=env, stdout=log, stderr=subprocess.STDOUT)
                        cell.update(process_wall_s=time.monotonic()-start, returncode=result.returncode)
                    if result.returncode:
                        cell['status'] = 'failed'; save(); raise RuntimeError(tag)
                    r = json.loads(target.read_text()); rows = r['generations']
                    assert r['population'] == r['cases'] == 1024
                    assert len(rows) == gens and len(r['final_population']) == 1024
                    assert all(x['expected_program_cases'] == 1024*1024 for x in rows)
                    if mode.startswith('gpu'):
                        assert all(x['case_counters_available'] and x['cases'] == 1024*1024 and x['unscored'] == 0 for x in rows)
                    assert r.get('cpu_audit_mismatches', 0) == 0
                    cell.update(status='ok', evolve_wall_ms=r['evolve_wall_ms'], final_best=r['final_best'],
                                reproduction_profile=r.get('reproduction_profile'))
                    save(); print('DONE', tag, round(cell['evolve_wall_ms'], 3), 'ms', flush=True)


if __name__ == '__main__':
    main()
