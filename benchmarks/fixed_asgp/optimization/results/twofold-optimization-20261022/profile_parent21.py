"""Nsight diagnostic of measured cold-start/reproduction bottlenecks, no ratios."""
import fcntl
import os
from pathlib import Path
import subprocess
import time
import traceback

import serial
import stage_pilot

name='profile-parent21-0';record=serial.RAW/(name+'-controller.json');assert not record.exists()
state=dict(pid=os.getpid(),status='waiting_for_lock',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
           scope='nsys-only diagnosis on old TRAINING; profiled times excluded from all headline ratios')
serial.save(record,state)
try:
    protocol=serial.read(serial.BASE/'protocol.json')
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        build=serial.read(serial.RAW/'build-attempt21/manifest.json')
        assert build['status']=='ok' and serial.sha(build['binary'])==build['binary_sha256']
        extra=dict(stage_pilot.execution_settings('pool_shape'),GAGP_GPU_CUDA_POOL='0',GAGP_GPU_PARENT_FITNESS_CACHE='1')
        stage=serial.Stage(name,build['binary'],settings=protocol['settings'])
        stage.m.update(harness_sha256={str(p):serial.sha(p) for p in [Path(__file__),serial.BASE/'stage_pilot.py',serial.BASE/'serial.py']},diagnostic_only=True);stage.persist()
    state['status']='profiling';serial.save(record,state)
    for task,population in [('median',1024),('filter_odd',16384)]:
        inputs=protocol['inputs'][task];tag=f'{task}.p{population}.s0.g8.profile'
        output=stage.out/(tag+'.json');trace=stage.out/tag
        prefix=['nsys','profile','--trace=cuda,nvtx','--sample=none','--cpuctxsw=none','-o',str(trace)]
        job=stage.run(tag,['measure',inputs['source'],inputs['prefixes'][str(population)]['prepared'],
            inputs['grammar'],population,'search_gpu_overlap',output],
            dict(extra,GAGP_SEARCH_GENERATIONS='8',GAGP_BM_SEED='0',GAGP_BM_CASE_COUNT='1024'),
            timeout=1200,prefix=prefix,reserve=10800)
        with serial.GPU_LOCK.open('a') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX)
            job.update(task=task,population=population,scope='diagnostic_profile_only',output=str(output))
            if job['status']!='ok':stage.persist();raise RuntimeError('nsys diagnostic failed; preserve and inspect')
            data=serial.read(output)
            assert data['cpu_audit_mismatches']==data['cpu_audit_wrong_mismatches']==0
            assert data['reproduction_profile'].startswith('native-gpu-phase-') and not data['reproduction_fallback_reason']
            job['output_sha256']=serial.sha(output)
            artifact=Path(str(trace)+'.nsys-rep');assert artifact.exists()
            job.update(trace=str(artifact),trace_sha256=serial.sha(artifact));stage.persist()
            for report in ('cuda_gpu_kern_sum','cuda_api_sum'):
                target=stage.out/(tag+'.'+report+'.csv')
                command=['nsys','stats','--report',report,'--format','csv','--force-export=true',str(artifact)]
                with target.open('x') as stream:
                    child=subprocess.Popen(command,cwd=serial.ROOT,stdout=stream,stderr=subprocess.STDOUT)
                    state.update(child_pid=child.pid,current_command=command);serial.save(record,state)
                    result=child.wait(timeout=min(600,max(1,serial.DEADLINE-10800-time.time())))
                assert result==0,'nsys stats export failed'
                job.setdefault('stats',{})[report]=dict(path=str(target),sha256=serial.sha(target))
                stage.persist()
    stage.m.update(status='requested_phase_complete',finished_epoch=time.time());stage.persist()
    state.update(status='requested_phase_complete',finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
    raise
finally:serial.save(record,state)
