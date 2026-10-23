import fcntl,subprocess,time,os
from pathlib import Path
import serial
out=serial.RAW/'shape-diagnostic-attempt0'
out.mkdir()
state=dict(status='waiting',pid=os.getpid(),started_epoch=time.time(),command=['env','GAGP_DIRECT_ROOT=1',str(serial.RAW/'rejected-shape-attempt0/gagp_test_gpu_phase_population')])
serial.save(out/'manifest.json',state)
with serial.GPU_LOCK.open('a') as lock:
 fcntl.flock(lock,fcntl.LOCK_EX)
 with (out/'diagnostic.log').open('x') as log:
  child=subprocess.Popen(state['command'],cwd=serial.ROOT,stdout=log,stderr=subprocess.STDOUT)
  state.update(status='running',child_pid=child.pid);serial.save(out/'manifest.json',state)
  code=child.wait()
 state.update(status='ok' if code==0 else 'failed',returncode=code,finished_epoch=time.time());serial.save(out/'manifest.json',state)
