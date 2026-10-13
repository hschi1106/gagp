"""Serial, bounded long-search experiment using an unchanged archived binary."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]
MODES = ('asgp_1t', 'gagp_cpu', 'gpu_eval', 'gpu_repro', 'gpu_overlap')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--budget-seconds', type=int, default=3300)
    args = p.parse_args()
    os.chdir(ROOT)
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    ref = json.loads((ROOT/'benchmarks/fixed_asgp/optimization/results/multigen-20261013/manifest.json').read_text())
    binary = out/'bench'
    assert sha(Path(ref['binary'])) == ref['binary_sha256']
    shutil.copy2(ref['binary'], binary)
    artifacts = ROOT/'logs/fixed-asgp/artifacts'
    assert all(sha(artifacts/k) == v for k, v in ref['frozen_sha256'].items())
    flags = dict(ref['flags']); flags.pop('GAGP_SEARCH_PENALTY', None)
    assert flags['GAGP_HOST_THREADS'] == '1'
    started = time.monotonic()
    manifest = dict(binary=str(binary), binary_sha256=sha(binary), frozen_sha256=ref['frozen_sha256'],
        flags=flags, source_head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
        archived_binary_source_manifest='benchmarks/fixed_asgp/optimization/results/multigen-20261013/manifest.json',
        started_epoch=time.time(), budget_seconds=args.budget_seconds, cells=[],
        contract='P1024/C1024, full pairs, CPU1T, no profiler; House64/Median128, seeds0/1/2, all5 modes; then Median256 round-robin until budget; new process for extensions, no checkpoint resume; no fitness-based allocation',
        timing=ref['timing'], audit_penalties=dict(house_robber=1000,median=1))
    (out/'device.txt').write_text(subprocess.check_output(['nvidia-smi'],text=True))
    def save():
        manifest['elapsed_seconds'] = time.monotonic()-started
        temp = out/'manifest.tmp'
        temp.write_text(json.dumps(manifest,indent=2)+'\n')
        temp.replace(out/'manifest.json')
    jobs=[]
    for seed in range(3):
        order=MODES[seed:]+MODES[:seed]
        for task,gens in [('house_robber',64),('median',128)]:
            jobs.extend(('main',task,mode,seed,gens) for mode in order)
    # Extension ordering is fixed before seeing fitness: all seeds of each mode
    # round-robin. Start only if observed timing predicts room with 50% reserve.
    for seed in range(3):
        order=MODES[seed:]+MODES[:seed]
        jobs.extend(('extended','median',mode,seed,256) for mode in order)
    manifest['scheduled_jobs']=jobs
    save()
    for stage,task,mode,seed,gens in jobs:
        remaining=args.budget_seconds-(time.monotonic()-started)
        if remaining < 20:
            manifest['budget_stop'] = dict(next_job=[stage,task,mode,seed,gens]); break
        if stage == 'extended':
            previous=next(c for c in manifest['cells'] if c['tag']==f'main.{task}.{mode}.s{seed}')
            assert previous['status']=='ok'
            predicted=previous['process_wall_s']*2*1.5+10
            if remaining < predicted:
                manifest.setdefault('extension_skips',[]).append(dict(mode=mode,seed=seed,predicted_s=predicted,remaining_s=remaining))
                save(); continue
        tag=f'{stage}.{task}.{mode}.s{seed}'; target=out/f'{tag}.json'
        env={k:v for k,v in os.environ.items() if not k.startswith('GAGP_') and k not in ('LD_PRELOAD','MALLOC_CONF')}
        env.update(flags,GAGP_SEARCH_PENALTY=str(1 if task=='median' else 1000),GAGP_BM_SEED=str(seed),
            GAGP_SEARCH_GENERATIONS=str(gens),GAGP_SEARCH_EXPORT=str(out/f'{tag}.final.json'),
            GAGP_SEARCH_HISTORY_EXPORT=str(out/f'{tag}.history.json'))
        cmd=[str(binary),'measure',str(artifacts/f'{task}.source.json'),str(artifacts/f'{task}.p1024.prepared.json'),
             str(artifacts/f'{task}.grammar.json'),'1024','search_'+mode,str(target)]
        cell=dict(tag=tag,task=task,mode=mode,seed=seed,generations=gens,population=1024,cases=1024,
            command=cmd,flags={k:v for k,v in env.items() if k.startswith('GAGP_') or k in flags},
            output=str(target),status='running',started_epoch=time.time())
        manifest['cells'].append(cell); save()
        print('START',tag,gens,flush=True)
        begin=time.monotonic()
        with (out/f'{tag}.log').open('w') as log:
            timed=['/usr/bin/time','-f','peak_rss_kib=%M\nprocess_wall_s=%e','-o',str(out/f'{tag}.resources'),*cmd]
            process=subprocess.Popen(timed,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
            try:
                code=process.wait(timeout=remaining)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid,signal.SIGKILL); process.wait()
                cell.update(status='budget_timeout',process_wall_s=time.monotonic()-begin)
                save(); break
        cell['process_wall_s']=time.monotonic()-begin
        if code:
            cell.update(status='failed',returncode=code); save(); raise RuntimeError(tag)
        r=json.loads(target.read_text()); rows=r['generations']
        assert r['population']==r['cases']==1024 and r['mode']==mode
        assert [x['generation'] for x in rows] == list(range(gens))
        assert len(r['final_population'])==1024
        assert all(x['expected_program_cases']==1024*1024 for x in rows)
        if mode.startswith('gpu'):
            assert all(x['case_counters_available'] and x['cases']==1024*1024 and x['unscored']==0 for x in rows)
        if mode in ('gpu_repro','gpu_overlap'):
            assert r['reproduction_profile']=='native-gpu-phase-v2-counted'
        assert r.get('cpu_audit_mismatches',0)==0
        cell.update(status='ok',evolve_wall_ms=r['evolve_wall_ms'],final_best=r['final_best'],
            cpu_audit_mismatches=r.get('cpu_audit_mismatches'))
        if stage=='extended':
            old=json.loads(Path(previous['output']).read_text())['generations']
            cell['prefix_differences']=[i for i,(a,b) in enumerate(zip(old,rows)) if any(a[k]!=b[k] for k in ('best','mean','best_nodes'))]
            assert not cell['prefix_differences']
        save(); print('DONE',tag,round(cell['process_wall_s'],2),flush=True)
    manifest['finished_epoch']=time.time(); save()
    print('FINISHED',round(manifest['elapsed_seconds'],2),flush=True)

if __name__=='__main__': main()
