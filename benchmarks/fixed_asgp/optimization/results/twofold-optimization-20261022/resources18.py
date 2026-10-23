"""Record static CUDA resource metadata; never uses profiled times as speedups."""
import fcntl
import os
import subprocess
import time
import traceback

import serial

out=serial.RAW/'resources-build18';out.mkdir()
record=out/'manifest.json'
state=dict(status='waiting_for_build',pid=os.getpid(),started_epoch=time.time(),deadline_epoch=serial.DEADLINE)
serial.save(record,state)
try:
    while True:
        path=serial.RAW/'build-attempt18/manifest.json';build=serial.read(path) if path.exists() else {}
        if serial.read(serial.RAW/'elite-candidate5-controller.json').get('status')=='stopped':raise RuntimeError('elite candidate prerequisite stopped')
        if build.get('status')=='failed':raise RuntimeError('build18 failed; no resource claim')
        if build.get('status')=='ok':break
        if time.time()>serial.DEADLINE-10800:raise RuntimeError('resource check exceeded budget')
        time.sleep(10)
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        assert serial.sha(build['binary'])==build['binary_sha256']
        state.update(status='running',binary=build['binary'],binary_sha256=build['binary_sha256'])
        serial.save(record,state)
        command=['/usr/local/cuda-12.6/bin/cuobjdump','--dump-resource-usage',build['binary']]
        with (out/'resource-usage.txt').open('x') as log:
            child=subprocess.Popen(command,cwd=serial.ROOT,stdout=log,stderr=subprocess.STDOUT)
            state.update(child_pid=child.pid,command=command);serial.save(record,state)
            result=child.wait()
        assert result==0,'cuobjdump failed'
        text=(out/'resource-usage.txt').read_text().splitlines();rows=[]
        for i,line in enumerate(text):
            if 'Function ' in line and 'evaluate_fitness_programs_impl' in line:
                rows.append(dict(symbol=line.strip(),resources=text[i+1].strip()))
        state.update(status='ok',kernels=rows,finished_epoch=time.time())
        serial.save(record,state)
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
    serial.save(record,state)
    raise
