"""Bounded serial multi-generation comparison; never prepares frozen inputs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import math
import statistics
import signal
import subprocess
import time

TASKS = ('median', 'house_robber', 'sum_of_elements')
MODES = ('asgp_1t', 'gagp_cpu', 'gpu_eval', 'gpu_repro', 'gpu_overlap')
ROOT = Path(__file__).resolve().parents[3]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--stage', choices=('smoke', 'main', 'extended'), default='main')
    parser.add_argument('--budget-seconds', type=int, default=2700)
    parser.add_argument('--tasks', nargs='+', choices=TASKS, default=list(TASKS))
    parser.add_argument('--case-penalty', type=float)
    parser.add_argument('--extension-observed-timing',action='store_true',help='Use completed longer runs to avoid extrapolating bloat from generation16')
    args = parser.parse_args()
    os.chdir(ROOT)
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    manifest_path = out / 'manifest.json'
    reference = json.loads((ROOT / 'benchmarks/fixed_asgp/optimization/results/native-counted-validation.json').read_text())
    artifacts = ROOT / 'logs/fixed-asgp/artifacts'
    flags = dict(reference['flags'], GAGP_GPU_DIAGNOSTICS='1', GAGP_SEARCH_TRACE='1')
    if args.case_penalty is not None:
        if not math.isfinite(args.case_penalty) or args.case_penalty<=0:raise ValueError('positive finite penalty required')
        flags['GAGP_SEARCH_PENALTY']=str(args.case_penalty)
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text())
        binary = Path(manifest['binary'])
        assert sha(binary) == manifest['binary_sha256'] and flags == manifest['flags']
    else:
        binary = out / 'bench'
        assert not binary.exists()
        shutil.copy2(ROOT / 'cpp/build/release/gagp_fixed_asgp_bench', binary)
        hashes = {name: sha(artifacts / name) for name in reference['frozen_sha256']}
        assert hashes == reference['frozen_sha256']
        manifest = dict(binary=str(binary), binary_sha256=sha(binary), frozen_sha256=hashes,
                        flags=flags, source_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
                        contract='P1024 C1024; same frozen initial population; seeds0/1/2; 16 complete generations + final evaluation; no early success stop; native operators retained; no profiler; serial CPU1T+GPU',
                        timing='evolve wall includes initialization/final evaluation; input-through-report includes import/export/audit; trace I/O included in evolve wall but outside individual generation timer',
                        tasks=list(args.tasks), modes=list(MODES), cells=[])
        (out / 'source.patch').write_text(subprocess.check_output(['git', 'diff', '--', 'cpp'], text=True))
        (out / 'device.txt').write_text(subprocess.check_output(['nvidia-smi'], text=True))
    def save():
        manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    save()
    start = time.monotonic()
    cells = []
    if args.stage == 'smoke':
        cells = [('median', mode, 0, 2, 8) for mode in MODES]
    elif args.stage == 'main':
        for seed in range(3):
            for task in args.tasks:
                order = MODES[seed:] + MODES[:seed]
                cells.extend((task, mode, seed, 16, 1024) for mode in order)
    else:
        # Choose extension lengths from primary timing only, never fitness.
        # Runs already covering12s need no extension for the10s comparison.
        for seed in range(3):
            for task in args.tasks:
                for mode in MODES:
                    source = next(c for c in manifest['cells'] if c['tag']==f'main.{task}.{mode}.s{seed}' and c['status']=='ok')
                    report = json.loads(Path(source['output']).read_text())
                    if report['generations'][-1]['elapsed_search_ms'] >= 12000:
                        continue
                    rate = statistics.median(r['generation_ms'] for r in report['generations'][-4:])
                    requested = max(32, math.ceil(14000 / max(rate, .001)))
                    if mode in ('gpu_repro', 'gpu_overlap'): requested=max(128,requested)
                    generations=min(2048,2**math.ceil(math.log2(requested)))
                    if args.extension_observed_timing:
                        observed=[]
                        for c in manifest['cells']:
                            if c['status']!='ok' or not c['tag'].startswith('extended.') or c['task']!=task or c['mode']!=mode:continue
                            rows=json.loads(Path(c['output']).read_text())['generations']
                            reached=next((r['generation']+1 for r in rows if r['elapsed_search_ms']>=14000),None)
                            if reached is not None:observed.append(reached)
                        if observed:
                            count=max(32,max(observed))
                            if mode in ('gpu_repro','gpu_overlap'):count=max(128,count)
                            generations=min(2048,2**math.ceil(math.log2(count)))
                    cells.append((task,mode,seed,generations,1024))
        manifest['extension_policy']='Timing-only allocation: extend primary runs finishing<12s to power-of-two generations predicted to cover14s, min32 (GPU-er/o min128), max2048. Common quality budget10s; report uncovered runs, never extrapolate. Separate fresh process with same seed; prefixes checked.'
        if args.extension_observed_timing:manifest['extension_amendment']='Remaining extensions use time-to14s from completed longer runs of same task/mode, rounded up to power-of-two with previous floors/cap. Timing only; counters/objective never used. Original completed lengths unchanged.'
        save()
    for task, mode, seed, gens, population in cells:
        tag = f'{args.stage}.{task}.{mode}.s{seed}'
        if any(c['tag'] == tag and c['status'] == 'ok' for c in manifest['cells']):
            continue
        remaining = args.budget_seconds - (time.monotonic() - start)
        if remaining < 30:
            manifest['budget_stop'] = dict(stage=args.stage, next_tag=tag, elapsed=time.monotonic()-start)
            save()
            break
        file_tag=tag
        attempt=0
        while list(out.glob(file_tag+'.*')):
            attempt+=1
            file_tag=f'{tag}.attempt{attempt}'
        target=out/f'{file_tag}.json'
        env = {k: v for k, v in os.environ.items() if not k.startswith('GAGP_') and k not in ('LD_PRELOAD', 'MALLOC_CONF')}
        env.update(flags, GAGP_BM_SEED=str(seed), GAGP_SEARCH_GENERATIONS=str(gens),
                   GAGP_SEARCH_EXPORT=str(out/f'{file_tag}.final.json'), GAGP_SEARCH_HISTORY_EXPORT=str(out/f'{file_tag}.history.json'))
        cmd = [str(binary), 'measure', str(artifacts/f'{task}.source.json'),
               str(artifacts/f'{task}.p1024.prepared.json'), str(artifacts/f'{task}.grammar.json'),
               str(population), 'search_'+mode, str(target)]
        print('START', tag, flush=True)
        begin = time.monotonic()
        cell = dict(tag=tag, task=task, mode=mode, seed=seed, generations=gens,
                    population=population, cases=1024, command=cmd, flags=env | {}, output=str(target))
        # Record only task settings, never unrelated inherited environment values.
        cell['flags'] = {k:v for k,v in env.items() if k.startswith('GAGP_') or k in flags}
        try:
            with (out/f'{file_tag}.log').open('w') as log:
                timed=['/usr/bin/time', '-f', 'peak_rss_kib=%M\nprocess_wall_s=%e', '-o', str(out/f'{file_tag}.resources'), *cmd]
                process=subprocess.Popen(timed,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
                try:
                    code=process.wait(timeout=min(600,remaining))
                except subprocess.TimeoutExpired:
                    # The time wrapper owns a child benchmark. Retire only this
                    # run's new process group so a timeout cannot leave GPU work.
                    os.killpg(process.pid,signal.SIGKILL)
                    process.wait()
                    raise
                if code:raise subprocess.CalledProcessError(code,timed)
            report = json.loads(target.read_text())
            assert report['population']==population and report['cases']==1024 and report['mode']==mode
            assert len(report['generations'])==gens and len(report['final_population'])==population
            assert [r['generation'] for r in report['generations']]==list(range(gens))
            assert all(r['expected_program_cases']==population*1024 for r in report['generations'])
            if mode.startswith('gpu'):
                assert all(r['case_counters_available'] and r['cases']==population*1024 and r['unscored']==0 for r in report['generations'])
            if mode in ('gpu_repro','gpu_overlap'):
                assert report['reproduction_profile']=='native-gpu-phase-v2-counted'
            cell.update(status='ok', cpu_audit_mismatches=report.get('cpu_audit_mismatches'), final_best=report['final_best'], evolve_wall_ms=report['evolve_wall_ms'])
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired, AssertionError) as error:
            cell.update(status='failed', error=str(error))
        cell['process_wall_s']=time.monotonic()-begin
        manifest['cells'].append(cell)
        save()
        print('DONE', tag, cell['status'], round(cell['process_wall_s'],2), cell.get('final_best'), flush=True)
        if cell['status']!='ok':
            raise RuntimeError(f'Inspect failed cell {tag}; no silent omissions')
    manifest['last_stage_elapsed_s']=time.monotonic()-start
    save()

if __name__=='__main__':
    main()
