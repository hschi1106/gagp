"""Check the staged transformation without changing CPP or claiming compilation."""
import fcntl
import importlib.util
import os
import shutil
import subprocess
import time
import traceback

import serial

out=serial.RAW/'projected-preflight-attempt2';out.mkdir()
state=dict(status='waiting',pid=os.getpid(),started_epoch=time.time(),deadline_epoch=serial.DEADLINE)
serial.save(out/'manifest.json',state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        state['status']='running';serial.save(out/'manifest.json',state)
        path=serial.BASE/'prototypes/apply_projected_shape.py'
        spec=importlib.util.spec_from_file_location('projected_changes',path)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        changes=module.apply(dry_run=True)
        for p,text in changes.items():
            relative=p.relative_to(serial.ROOT);copy=out/'proposed'/relative
            copy.parent.mkdir(parents=True,exist_ok=True);copy.write_text(text)
        state.update(status='storage_probe',scope='staged transformation plus standalone register storage oracle; CPP unchanged; no VM/performance claim',
            proposed_paths=[str(p.relative_to(serial.ROOT)) for p in changes])
        serial.save(out/'manifest.json',state)
        compiler=shutil.which('nvcc');assert compiler,'nvcc missing'
        probe=out/'register-probe'
        commands=[[compiler,'-std=c++20','-arch=sm_86','-I'+str(serial.ROOT/'cpp/include'),
                   str(out/'proposed/cpp/tests/gpu/test_phase_register_gpu.cu'),'-o',str(probe)],
                  [str(probe)]]
        state['commands']=commands;state['results']=[];serial.save(out/'manifest.json',state)
        for index,command in enumerate(commands):
            with (out/f'probe{index}.log').open('x') as log:
                child=subprocess.Popen(command,cwd=serial.ROOT,stdout=log,stderr=subprocess.STDOUT)
                state.update(child_pid=child.pid,current_command=index);serial.save(out/'manifest.json',state)
                code=child.wait()
            state['results'].append(code);serial.save(out/'manifest.json',state)
            assert code==0,'staged register storage probe failed; inspect preserved log'
        state.update(status='ok',probe_binary_sha256=serial.sha(probe),finished_epoch=time.time())
        serial.save(out/'manifest.json',state)
except BaseException as error:
    state.update(status='failed',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
    serial.save(out/'manifest.json',state)
    raise
