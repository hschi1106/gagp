"""Dependent training judges and profiler summaries; no fresh test inputs."""
import fcntl
import os
from pathlib import Path
import subprocess
import sys
import time

import study

record=study.RAW/'followup-diagnostics.json'
if record.exists():raise RuntimeError('followup already launched; inspect its record, do not duplicate')
state=dict(pid=os.getpid(),status='waiting',started_epoch=time.time(),steps=[])
study.save(record,state)
for phase,task in [('profile-baseline','all_equal'),('profile-tuple','is_sorted'),('profile-tuple','all_equal')]:
    folder=study.RAW/phase
    output=folder/(task+'.stats')
    cmd=['nsys','stats','--report','cuda_gpu_kern_sum,cuda_api_sum','--format','csv','--output',str(output),str(folder/(task+'.nsys-rep'))]
    with open('/tmp/gagp-support-20261015-gpu.lock','a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        log=folder/(task+'.stats.log')
        if log.exists():raise RuntimeError('profiler summary attempt already exists')
        with log.open('w') as stream:
            result=subprocess.run(cmd,cwd=study.ROOT,stdout=stream,stderr=subprocess.STDOUT,timeout=600)
    state['steps'].append(dict(command=cmd,returncode=result.returncode,log=str(log)))
    study.save(record,state)
    if result.returncode:raise RuntimeError('nsys stats failed; preserved log')
while study.read(study.RAW/'development/manifest.json')['status']!='complete':
    if time.time()>study.DEADLINE-14400:raise RuntimeError('development missed diagnostics deadline')
    time.sleep(10)
script=study.BASE/'development_judges.py'
log=study.RAW/'development-training-judges-controller.log'
if log.exists():raise RuntimeError('training judges attempt already exists')
state['status']='training_judges';study.save(record,state)
with log.open('w') as stream:
    result=subprocess.run([sys.executable,str(script)],cwd=study.ROOT,stdout=stream,stderr=subprocess.STDOUT)
state.update(status='complete' if result.returncode==0 else 'failed',finished_epoch=time.time(),returncode=result.returncode)
study.save(record,state)
