"""Build/run the isolated CUDA pool lifetime oracle without CPP mutation."""
import fcntl
import os
import shutil
import subprocess
import time
import traceback

import serial

out=serial.RAW/'pool-preflight0';out.mkdir();record=out/'manifest.json'
state=dict(pid=os.getpid(),status='waiting_for_lock',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
           scope='standalone CUDA pool ordering/lifetime only; CPP untouched; no speed or evolution claim')
serial.save(record,state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        proposed=out/'proposed';proposed.mkdir()
        inputs=['cuda_allocation_scope.hpp','allocation_gpu.hpp','allocation_pool.cu','test_cuda_allocation_pool.cu']
        state.update(status='running',prototype_sha256={str(serial.BASE/'prototypes'/n):serial.sha(serial.BASE/'prototypes'/n) for n in inputs})
        for name in inputs:
            target=proposed/name if name!='allocation_gpu.hpp' else proposed/'include/gagp/runtime/gpu/allocation_gpu.hpp'
            target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(serial.BASE/'prototypes'/name,target)
        compiler=next(line.split('=',1)[1] for line in (serial.ROOT/'cpp/build/release/CMakeCache.txt').read_text().splitlines()
                      if line.startswith('CMAKE_CUDA_COMPILER:STRING='))
        commands=[[compiler,'-std=c++17','-arch=sm_86','-I'+str(proposed/'include'),
                   str(proposed/'allocation_pool.cu'),str(proposed/'test_cuda_allocation_pool.cu'),'-o',str(out/'pool-probe')],
                  [str(out/'pool-probe')]]
        state['commands']=commands
        for index,command in enumerate(commands):
            with (out/f'check{index}.log').open('x') as log:
                child=subprocess.Popen(command,cwd=serial.ROOT,stdout=log,stderr=subprocess.STDOUT)
                state.update(child_pid=child.pid,current_command=index);serial.save(record,state);result=child.wait()
            state.setdefault('results',[]).append(result);serial.save(record,state)
            if result:raise RuntimeError(f'pool preflight{index} failed({result})')
        state.update(status='ok',binary_sha256=serial.sha(out/'pool-probe'),finished_epoch=time.time());serial.save(record,state)
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());serial.save(record,state)
    raise
