"""Expanded native benchmark; new inputs only, serialized GPU jobs, common fitness."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(ROOT/'tools'))
from gagp_tools.experiments.fixed_asgp.grammar import definition
TASKS = ('count_odds', 'last_index_of_zero')
MODES = ('asgp_1t', 'gagp_cpu', 'gpu_eval', 'gpu_repro', 'gpu_overlap')
LOCK = '/tmp/gagp-parallel-20261013-benchmark.lock'

def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def read(p): return json.loads(Path(p).read_text())
def save(p, x): Path(p).write_text(json.dumps(x, indent=2)+'\n')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('stage',choices=['prepare','pilot','formal','quality','best-quality','native-controls'])
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--artifacts',type=Path)
    p.add_argument('--pilot',type=Path)
    p.add_argument('--runs',type=Path)
    p.add_argument('--binary',type=Path,default=ROOT/'cpp/build/release/gagp_fixed_asgp_bench')
    a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
    flags=read(Path(__file__).with_name('settings.json'))
    flags['GAGP_BM_COMMON_FITNESS']='1'
    assert flags['GAGP_HOST_THREADS']=='1'
    binary=out/'bench';shutil.copy2(a.binary.resolve(),binary)
    m={'stage':a.stage,'binary':str(binary),'binary_sha256':sha(binary),'flags':flags,
       'source_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
       'source_patch':subprocess.check_output(['git','diff','--','cpp','tools/gagp_tools/experiments/fixed_asgp'],cwd=ROOT,text=True),
       'asgp_source_sha256':{str(f.relative_to(Path('/home/hschi1106/r13921069'))):sha(f) for f in sorted(Path('/home/hschi1106/r13921069/src').rglob('*')) if f.is_file()},
       'contract':{'population':1024,'cases':1024,'host_threads':1,'numeric_fitness':'negative capped absolute error, cap1000 incl errors',
                   'list_fitness':'correct count incl error0; ASGP native exact-loss plus1024 affine-equivalent',
                   'native_policy_changes':{'last_index_of_zero':'ASGP discounted L1 -> capped L1','negative_to_zero':'ASGP Levenshtein -> exact; GAGP error penalty0'},
                   'list_backend':'existing generic GPU reproduction; phase population disabled; no CPU variation fallback',
                   'timing':'evolve_wall_ms cold search through final eval; process adds input/audit/export; all complete pairs',
                   'quality':'3seeds, common CPU heldout audit; not convergence equivalence',
                   'unsupported':{'stock_with_cooldown':'exact clamped memo coordinate transitions unsupported', 'negative_to_zero':'GPU knownsolution fails list arena; CPU random P8 evolution exceeded70GiB; no fullP1024 timing qualified'}},'cells':[]}
    def env(task,native=False):
        e={k:v for k,v in os.environ.items() if not k.startswith('GAGP_') and k not in ('LD_PRELOAD','MALLOC_CONF')};e.update(flags)
        if task=='negative_to_zero':e.pop('GAGP_GPU_PHASE_POPULATION',None)
        if native:e.pop('GAGP_BM_COMMON_FITNESS',None)
        return e
    def command(cmd,label,e,timeout=240):
        cell={'tag':label,'command':list(map(str,cmd)),'settings':{k:v for k,v in e.items() if k.startswith('GAGP_')},'status':'waiting'}
        m['cells'].append(cell);save(out/'manifest.json',m)
        with open(LOCK,'a') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX);start=time.monotonic();cell['started_epoch']=time.time();cell['status']='running';save(out/'manifest.json',m)
            with (out/(label+'.log')).open('w') as log:
                try:
                    process=subprocess.Popen(['/usr/bin/time','-f','peak_rss_kib=%M\nprocess_wall_s=%e','-o',str(out/(label+'.resources')),*map(str,cmd)],cwd=ROOT,env=e,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
                    deadline=time.monotonic()+timeout
                    peak_rss=0
                    while process.poll() is None:
                        # Monitor only this process group; no system setting or other job is touched.
                        rss=0
                        for stat in Path('/proc').glob('[0-9]*/stat'):
                            try:
                                fields=stat.read_text().rsplit(')',1)[1].split()
                                if int(fields[2])==process.pid:rss+=int(fields[21])*os.sysconf('SC_PAGE_SIZE')
                            except (OSError,ValueError,IndexError):pass
                        peak_rss=max(peak_rss,rss)
                        if rss>16*1024**3:
                            cell['resource_guard']='RSS exceeded16GiB; incomplete search excluded'
                            raise subprocess.TimeoutExpired(cmd,timeout)
                        if time.monotonic()>deadline:raise subprocess.TimeoutExpired(cmd,timeout)
                        time.sleep(.5)
                    cell['observed_peak_process_group_rss_bytes']=peak_rss
                    cell['returncode']=process.returncode;cell['status']='ok' if process.returncode==0 else 'failed'
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid,signal.SIGTERM)
                    try:process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid,signal.SIGKILL);process.wait()
                    cell['status']='resource_limit' if cell.get('resource_guard') else 'timeout'
            cell['process_wall_s']=time.monotonic()-start
        save(out/'manifest.json',m);print(cell['status'],label,round(cell['process_wall_s'],2),flush=True)
        return cell
    if a.stage=='prepare':
        for task in (*TASKS,'negative_to_zero','stock_with_cooldown'):
            source=out/f'{task}.source.json'
            cell=command([binary,'freeze',task,source],task+'.freeze',env(task));assert cell['status']=='ok'
            if task=='stock_with_cooldown':continue
            grammar=out/f'{task}.grammar.json';save(grammar,definition(ROOT,task))
            prepared=out/f'{task}.p1024.prepared.json'
            cell=command([binary,'prepare',source,grammar,1024,prepared],task+'.prepare',env(task),600);assert cell['status']=='ok'
            cell=command([binary,'audit',source,grammar,out/f'{task}.audit.json'],task+'.audit',env(task));assert cell['status']=='ok'
        m['frozen_sha256']={f.name:sha(f) for f in out.glob('*.json') if f.name!='manifest.json'}
        save(out/'manifest.json',m);return
    if a.stage in ('quality','best-quality'):
        study=read(a.runs/'manifest.json');artifacts=Path(study['artifacts']);m['runs_manifest']=str(a.runs/'manifest.json')
        for c in study['cells']:
            if c['status']!='ok' or not c.get('output'):continue
            task=c['task'];tag=c['tag'];target=out/(tag+'.quality.json')
            exported=a.runs/(tag+'.final.json');history=a.runs/(tag+'.history.json')
            selected={}
            if a.stage=='best-quality':
                result=read(c['output']);rows=read(history)['history'];best=max(rows,key=lambda r:r['fitness'])
                generation='final';fitness=result['final_best']
                if best['fitness']>fitness:
                    generation=best['generation'];fitness=best['fitness']
                    artifact={'task':task,'best_program':best['program']} if c['mode']=='asgp_1t' else {'programs':[best['ast']]}
                else:artifact=read(exported)
                exported=out/(tag+'.selected.json');save(exported,artifact)
                selected={'selected_generation':generation,'selected_training_fitness':fitness,
                          'selection':'max training fitness over saved generations and final; final wins ties; no test-case ranking'}
            cmd=[binary,'quality',task,exported,artifacts/(task+'.grammar.json'),target]
            if a.stage=='quality':cmd.append(history)
            cell=command(cmd,tag,env(task),180);cell.update(output=str(target),task=task,mode=c['mode'],seed=c['seed'],**selected);save(out/'manifest.json',m)
        return
    artifacts=a.artifacts.resolve();am=read(artifacts/'manifest.json');m['artifacts']=str(artifacts);m['frozen_sha256']=am['frozen_sha256']
    for name,digest in am['frozen_sha256'].items():assert sha(artifacts/name)==digest,name
    generations={t:4 for t in TASKS};seeds=[0]
    if a.stage in ('formal','native-controls'):
        pilot=read(a.pilot/'manifest.json');assert all(c['status']=='ok' for c in pilot['cells']), 'pilot failure requires explicit handling, no silent task omission'
        for task in TASKS:
            costs=[c['process_wall_s']/c['generations'] for c in pilot['cells'] if c['task']==task]
            # Freeze same generation count for every mode/seed of a task BEFORE formal runs.
            generations[task]=next((g for g in (32,16,8,4) if sum(costs)*g*3<=900),4)
        seeds=[0,1,2]
    m['generations']=generations;m['seed_policy']='one independent process per seed; seed variability, not identical-workload repeats';save(out/'manifest.json',m)
    for task in TASKS:
        if a.stage=='native-controls' and task=='count_odds':continue
        for seed in seeds:
            for mode in (('asgp_1t',) if a.stage=='native-controls' else MODES):
                tag=f'{task}.{mode}.s{seed}';target=out/(tag+'.json');e=env(task,a.stage=='native-controls')
                e.update(GAGP_SEARCH_PENALTY='0' if task=='negative_to_zero' else '1000',GAGP_BM_SEED=str(seed),GAGP_SEARCH_GENERATIONS=str(generations[task]),GAGP_SEARCH_EXPORT=str(out/(tag+'.final.json')),GAGP_SEARCH_HISTORY_EXPORT=str(out/(tag+'.history.json')))
                cell=command([binary,'measure',artifacts/(task+'.source.json'),artifacts/(task+'.p1024.prepared.json'),artifacts/(task+'.grammar.json'),1024,'search_'+mode,target],tag,e,900 if a.stage!='pilot' else 240)
                cell.update(task=task,mode=mode,seed=seed,generations=generations[task],output=str(target))
                if cell['status']=='ok':
                    r=read(target);assert r['population']==r['cases']==1024 and len(r['generations'])==generations[task]
                    assert len(r['final_population'])==1024 and all(x['expected_program_cases']==1024**2 for x in r['generations'])
                    if mode.startswith('gpu'):assert all(x['case_counters_available'] and x['cases']==1024**2 and x['unscored']==0 for x in r['generations'])
                    cell.update(evolve_wall_ms=r['evolve_wall_ms'],final_best=r['final_best'],reproduction_profile=r['reproduction_profile'],reproduction_fallback_reason=r.get('reproduction_fallback_reason',''),cpu_audit_mismatches=r.get('cpu_audit_mismatches'))
                    if mode in ('gpu_repro','gpu_overlap'):
                        assert r['reproduction_profile']!='cpu', 'CPU reproduction cannot be labelled GPU'
                        assert sum(x['repro_kernel_ms'] for x in r['generations'])>0, 'GPU variation not exercised'
                save(out/'manifest.json',m)
                if cell['status']!='ok':return

if __name__=='__main__':main()
