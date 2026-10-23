"""Standalone GPU elite ordering oracle; no runtime/source integration yet."""
import fcntl
import os
import subprocess
import time
import traceback

import serial

out=serial.RAW/'linear-elite-preflight0';out.mkdir();record=out/'manifest.json'
state=dict(pid=os.getpid(),status='waiting_for_lock',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
           scope='GPU argmax ordering probe only; no native reproduction integration or speed claim')
serial.save(record,state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        files=[serial.BASE/'prototypes/linear_elite.cuh',serial.BASE/'prototypes/linear_elite_probe.cu']
        state.update(status='running',source_sha256={str(path):serial.sha(path) for path in files})
        compiler=next(line.split('=',1)[1] for line in (serial.ROOT/'cpp/build/release/CMakeCache.txt').read_text().splitlines()
                      if line.startswith('CMAKE_CUDA_COMPILER:STRING='))
        commands=[[compiler,'-std=c++17','-arch=sm_86',str(files[1]),'-o',str(out/'elite-probe')],[str(out/'elite-probe')]]
        state['commands']=commands
        for index,command in enumerate(commands):
            with (out/f'check{index}.log').open('x') as log:
                child=subprocess.Popen(command,cwd=serial.ROOT,stdout=log,stderr=subprocess.STDOUT)
                state.update(child_pid=child.pid,current_command=index);serial.save(record,state)
                result=child.wait()
            state.setdefault('results',[]).append(result);serial.save(record,state)
            if result:raise RuntimeError('elite standalone oracle failed')
        state.update(status='ok',binary_sha256=serial.sha(out/'elite-probe'),finished_epoch=time.time());serial.save(record,state)
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());serial.save(record,state)
    raise
