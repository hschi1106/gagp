"""Explicit development extension: test the larger 1024 geometry after smaller groups lost.

Original development attempts and selection stay immutable. No formal work starts here.
"""
from contextlib import contextmanager
import fcntl
import math
import os
from pathlib import Path
import subprocess
import time
import serial
from development import TASKS, POPULATIONS, alive, gpu_lock, run_search, source_record


def profile_summaries(binary):
    out = serial.RAW / 'nsys-summary-attempt0'
    if out.exists():
        raise RuntimeError('profile summary attempt already exists')
    out.mkdir()
    record = dict(status='running', started_epoch=time.time(), reports={})
    serial.save(out/'manifest.json', record)
    for report in sorted((serial.RAW/'nsys-development').glob('*.nsys-rep')):
        command = ['nsys', 'stats', '--report', 'cuda_gpu_kern_sum,cuda_api_sum',
                   '--format', 'csv', '--output', str(out/report.stem), str(report)]
        before = serial.sha(report)
        with (out/(report.stem+'.log')).open('x') as log:
            result = subprocess.run(command, cwd=serial.ROOT, stdout=log, stderr=subprocess.STDOUT,
                                    timeout=180)
        assert serial.sha(report) == before, 'original profile changed'
        record['reports'][report.name] = dict(command=command, returncode=result.returncode,
                                             report_sha256=before)
        serial.save(out/'manifest.json', record)
    with (out/'binary-resources.txt').open('x') as log:
        result = subprocess.run(['cuobjdump', '--dump-resource-usage', str(binary)],
                                cwd=serial.ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=120)
    record.update(status='ok' if len(record['reports'])==2 and
                  all(r['returncode']==0 for r in record['reports'].values()) and result.returncode==0
                  else 'analysis_failed', resource_returncode=result.returncode, finished_epoch=time.time())
    record['outputs_sha256'] = {p.name:serial.sha(p) for p in out.iterdir() if p.name!='manifest.json'}
    serial.save(out/'manifest.json', record)


def main():
    guard = (serial.RAW/'extension1024.lock').open('a')
    fcntl.flock(guard, fcntl.LOCK_EX|fcntl.LOCK_NB)
    controller = serial.RAW/'extension1024-controller.json'
    if controller.exists():
        raise RuntimeError('controller record exists; inspect old attempt, never silently restart')
    record = dict(pid=os.getpid(), status='waiting_development', started_epoch=serial.START,
                  deadline_epoch=serial.DEADLINE)
    serial.save(controller, record)
    try:
        while True:
            previous = serial.read(serial.RAW/'development-controller.json')
            stages = [serial.read(p) for p in (serial.RAW/'launch-geometry-development').glob('manifest.json')]
            children = [j['pid'] for m in stages for j in m['jobs'].values()
                        if j.get('pid') and alive(j['pid'])]
            if not alive(previous['pid']) and not children:
                break
            if time.time() >= serial.DEADLINE-8*3600:
                raise RuntimeError('insufficient extension budget while waiting')
            time.sleep(10)
        assert previous['status']=='requested_phase_complete', 'previous development stopped'
        selected = serial.read(serial.RAW/'launch-geometry-selection.json')
        assert any(r['blocksize']==512 and r['valid'] for r in selected['ranking']), '512 reference fidelity failed'
        build = serial.read(serial.RAW/'geometry-build/manifest.json')
        binary = serial.RAW/'geometry-build/bench'
        assert serial.sha(binary)==build['binary_sha256']==selected['binary_sha256']
        with gpu_lock():
            assert source_record()==build['source_sha256'], 'C++ source changed since development build'
        protocol = serial.read(serial.BASE/'protocol.json')
        plan = serial.read(serial.BASE/'extension1024-plan.json')
        stage = serial.Stage('launch-geometry-extension1024', binary, settings=protocol['settings'])
        stage.m['extension_plan_sha256'] = serial.sha(serial.BASE/'extension1024-plan.json')
        stage.persist()
        record.update(status='tuning', binary_sha256=build['binary_sha256']);serial.save(controller,record)
        for ti, task in enumerate(TASKS):
            for pi, population in enumerate(POPULATIONS):
                blocks = (512,1024) if (ti+pi)%2==0 else (1024,512)
                for block in blocks:
                    run_search(stage,task,population,block,8)
        with gpu_lock():
            comparisons = []
            for task in TASKS:
                for population in POPULATIONS:
                    a,b = [stage.m['jobs'][f'{task}.p{population}.b{block}.s0.g8'] for block in (512,1024)]
                    row = dict(task=task,population=population,valid=False)
                    if a['status']==b['status']=='ok':
                        x,y = [serial.read(j['output']) for j in (a,b)]
                        hx,hy = [serial.read(j['history']) for j in (a,b)]
                        row.update(final_descriptors_equal=x['final_population']==y['final_population'],
                            champion_fitness_equal=[r['best'] for r in x['generations']]==[r['best'] for r in y['generations']],
                            champion_ast_equal=[r['ast'] for r in hx['history']]==[r['ast'] for r in hy['history']],
                            final_full_ast_equal=a['export_sha256']==b['export_sha256'],
                            speedup=a['search_s']/b['search_s'])
                        row['valid'] = (a['fidelity_ok'] and b['fidelity_ok'] and row['final_descriptors_equal']
                            and row['champion_fitness_equal'] and row['champion_ast_equal'] and row['final_full_ast_equal'])
                    comparisons.append(row)
            valid = len(comparisons)==10 and all(r['valid'] for r in comparisons)
            ratios = [r['speedup'] for r in comparisons if r.get('speedup')]
            gain = math.exp(sum(map(math.log,ratios))/len(ratios)) if ratios else None
            admissible = valid and min(ratios)>=1/1.1
            candidate = 1024 if admissible and gain>1 else 512
            serial.save(serial.RAW/'extension1024-selection.json',dict(
                selection_epoch=time.time(), formal_freeze=False, selected_blocksize=candidate,
                basis=plan['selection'], binary_sha256=build['binary_sha256'],
                valid=valid, admissible=admissible, geometric_mean=gain,
                minimum=min(ratios) if ratios else None, comparisons=comparisons,
                previous_selection_sha256=serial.sha(serial.RAW/'launch-geometry-selection.json'),
                plan_sha256=stage.m['extension_plan_sha256']))
            stage.m.update(status='requested_phase_complete',finished_epoch=time.time());stage.persist()
            record['status']='summarizing_profiles';serial.save(controller,record)
            profile_summaries(binary)
        record.update(status='requested_phase_complete',finished_epoch=time.time())
    except Exception as error:
        record.update(status='stopped',error=repr(error),finished_epoch=time.time())
        raise
    finally:
        serial.save(controller,record)


if __name__=='__main__':
    main()

