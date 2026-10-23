"""Record focused scientific-harness checks under the common execution lock."""
import fcntl
import os
import subprocess
import time

import serial

out=serial.RAW/'harness-checks-attempt1';out.mkdir()
state=dict(status='waiting',pid=os.getpid(),started_epoch=time.time(),deadline_epoch=serial.DEADLINE)
serial.save(out/'manifest.json',state)
with serial.GPU_LOCK.open('a') as lock:
    fcntl.flock(lock,fcntl.LOCK_EX)
    state['status']='running';serial.save(out/'manifest.json',state)
    command=['python3','-m','unittest','discover','-s',str(serial.BASE),'-p','test_*.py','-v']
    with (out/'checks.log').open('w') as log:
        child=subprocess.Popen(command,cwd=serial.ROOT,stdout=log,stderr=subprocess.STDOUT)
        state.update(child_pid=child.pid,command=command);serial.save(out/'manifest.json',state)
        result=child.wait()
    state.update(status='ok' if result==0 else 'failed',returncode=result,finished_epoch=time.time())
    serial.save(out/'manifest.json',state)
