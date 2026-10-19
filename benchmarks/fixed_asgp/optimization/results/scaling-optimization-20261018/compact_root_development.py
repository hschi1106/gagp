"""Evaluate the verified compact root VM after the complete native regression exits."""
from contextlib import contextmanager
import fcntl
import math
import os
from pathlib import Path
import statistics
import subprocess
import time
import serial
from development import TASKS, POPULATIONS, alive, gpu_lock, inputs, source_record
from pilot import validate


def search(stage,task,population,enabled,rep):
    tag=f'{task}.p{population}.root{enabled}.s0.g8.r{rep}'
    out=stage.out/(tag+'.json');export=stage.out/(tag+'.export.json');history=stage.out/(tag+'.history.json')
    source,prepared,grammar=inputs(task,population)
    extra=dict(GAGP_SEARCH_GENERATIONS='8',GAGP_BM_SEED='0',GAGP_BM_CASE_COUNT='1024',
               GAGP_BM_GPU_BLOCKSIZE='512',GAGP_GPU_PRIVATE_TUPLE_SCORE='1',
               GAGP_GPU_COMPACT_ROOT_VM=str(enabled),GAGP_SEARCH_EXPORT=str(export),
               GAGP_SEARCH_HISTORY_EXPORT=str(history))
    job=stage.run(tag,['measure',source,prepared,grammar,population,'search_gpu_overlap',out],
                  extra,timeout=1800,reserve=8*3600)
    with gpu_lock():
        job.update(task=task,population=population,compact_root=enabled,repetition=rep,
                   output=str(out),export=str(export),history=str(history),profiled=False)
        if job['status']=='ok':
            try:
                data=serial.read(out);job.update(**validate(data,'gpu_overlap',population,8,False))
                assert data['gpu_blocksize']==data['cpu_audit_reduction_lanes']==512
                job['fidelity_ok']=job['cpu_audit_fitness_mismatches']==job['cpu_audit_wrong_mismatches']==0
                job['compact_root_generations']=sum('-compact-root-vm' in r['execution_profile'] and
                    '-compact-root-vm-declined' not in r['execution_profile'] for r in data['generations'])
                for field,p in [('output',out),('export',export),('history',history)]:job[field+'_sha256']=serial.sha(p)
            except Exception as error:job.update(status='invalid_output',validation_error=repr(error))
        stage.persist()


def main():
    guard=(serial.RAW/'compact-root-development.lock').open('a');fcntl.flock(guard,fcntl.LOCK_EX|fcntl.LOCK_NB)
    controller=serial.RAW/'compact-root-development-controller.json'
    if controller.exists():raise RuntimeError('controller record exists; preserve prior attempt')
    record=dict(pid=os.getpid(),status='waiting_build',started_epoch=serial.START,deadline_epoch=serial.DEADLINE)
    serial.save(controller,record)
    try:
        build_path=serial.RAW/'compact-root-build-attempt0/manifest.json'
        while True:
            build=serial.read(build_path)
            if build.get('pid') and not alive(build['pid']):break
            if time.time()>=serial.DEADLINE-8*3600:raise RuntimeError('insufficient budget waiting for build')
            time.sleep(10)
        assert build['status']=='ok','build or complete native regression failed; inspect preserved attempt'
        binary=build_path.parent/'bench';assert serial.sha(binary)==build['binary_sha256']
        with gpu_lock():
            assert source_record()==build['source_sha256'],'source changed after build'
            out=serial.RAW/'compact-root-owner-check-attempt0';out.mkdir()
            command=['ctest','--test-dir','cpp/build/release','-R','^gagp_test_gpu_phase_population$','--output-on-failure']
            env={k:v for k,v in os.environ.items() if not k.startswith('GAGP_')}
            env.update(GAGP_HOST_THREADS='1',GAGP_CUDA_DEVICE='0',GAGP_GPU_COMPACT_ROOT_VM='1')
            started=time.time()
            with (out/'ctest.log').open('x') as log:
                result=subprocess.run(command,cwd=serial.ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=900)
            serial.save(out/'manifest.json',dict(command=command,extra=env.get('GAGP_GPU_COMPACT_ROOT_VM'),
                started_epoch=started,finished_epoch=time.time(),returncode=result.returncode,status='ok' if result.returncode==0 else 'failed'))
            assert result.returncode==0,'opt-in resident owner regression failed'
        stage=serial.Stage('compact-root-development',binary,settings=serial.read(serial.BASE/'protocol.json')['settings'])
        stage.m['plan_sha256']=serial.sha(serial.BASE/'compact-root-plan.json');stage.persist()
        record.update(status='tuning',binary_sha256=build['binary_sha256']);serial.save(controller,record)
        for ti,task in enumerate(TASKS):
            for pi,population in enumerate(POPULATIONS):
                for rep in (0,1):
                    for enabled in ((0,1) if (ti+pi+rep)%2==0 else (1,0)):
                        search(stage,task,population,enabled,rep)
        with gpu_lock():
            comparisons=[]
            for task in TASKS:
                for population in POPULATIONS:
                    for rep in (0,1):
                        a,b=[stage.m['jobs'][f'{task}.p{population}.root{v}.s0.g8.r{rep}'] for v in (0,1)]
                        row=dict(task=task,population=population,repetition=rep,valid=False)
                        if a['status']==b['status']=='ok':
                            x,y=[serial.read(j['output']) for j in (a,b)]
                            hx,hy=[serial.read(j['history']) for j in (a,b)]
                            row.update(final_descriptors_equal=x['final_population']==y['final_population'],
                                final_full_ast_equal=a['export_sha256']==b['export_sha256'],
                                champion_fitness_equal=[r['best'] for r in x['generations']]==[r['best'] for r in y['generations']],
                                champion_ast_equal=[r['ast'] for r in hx['history']]==[r['ast'] for r in hy['history']],
                                speedup=a['search_s']/b['search_s'],
                                admitted_generations=b['compact_root_generations'])
                            row['valid']=a['fidelity_ok'] and b['fidelity_ok'] and all(row[k] for k in
                                ['final_descriptors_equal','final_full_ast_equal','champion_fitness_equal','champion_ast_equal'])
                        comparisons.append(row)
            ratios=[]
            for task in TASKS:
                for population in POPULATIONS:
                    rr=[r for r in comparisons if r['task']==task and r['population']==population]
                    if all('speedup' in r for r in rr):ratios.append(statistics.median(r['speedup'] for r in rr))
            valid=len(comparisons)==20 and all(r['valid'] for r in comparisons)
            gain=math.exp(sum(map(math.log,ratios))/len(ratios)) if ratios else None
            admissible=valid and len(ratios)==10 and min(ratios)>=1/1.1 and gain>=1.03
            serial.save(serial.RAW/'compact-root-selection.json',dict(selection_epoch=time.time(),
                selected_compact_root=bool(admissible),valid=valid,admissible=admissible,
                geometric_mean=gain,minimum=min(ratios) if ratios else None,comparisons=comparisons,
                binary_sha256=build['binary_sha256'],plan_sha256=stage.m['plan_sha256'],formal_freeze=False))
            stage.m.update(status='requested_phase_complete',finished_epoch=time.time());stage.persist()
        record.update(status='requested_phase_complete',finished_epoch=time.time())
    except Exception as error:
        record.update(status='stopped',error=repr(error),finished_epoch=time.time());raise
    finally:serial.save(controller,record)


if __name__=='__main__':main()

