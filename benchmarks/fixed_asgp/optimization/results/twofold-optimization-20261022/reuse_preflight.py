"""Check staged source transformations and host reuse scope without CPP edits."""
import fcntl
import importlib.util
import os
from pathlib import Path
import subprocess
import time
import traceback

import serial

out=serial.RAW/'unchanged-preflight-attempt0';out.mkdir()
record=out/'manifest.json'
state=dict(status='waiting',pid=os.getpid(),started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
           scope='staged transformations and standalone host scope oracle; CPP untouched; no GPU/performance claim')
serial.save(record,state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        state['status']='running';serial.save(record,state)
        script=serial.BASE/'prototypes/apply_unchanged_reuse.py'
        spec=importlib.util.spec_from_file_location('unchanged_source_preflight',script)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        proposed=module.apply(dry_run=True)
        for path,text in proposed.items():
            target=out/'proposed'/path.relative_to(serial.ROOT);target.parent.mkdir(parents=True,exist_ok=True);target.write_text(text)
        state['proposed_paths']=[str(p.relative_to(serial.ROOT)) for p in proposed]
        state['prototype_sha256']={str(p):serial.sha(p) for p in [
            script,*[serial.BASE/'prototypes'/name for name in ['completed_fitness_reuse.hpp',
                'test_unchanged_host.patch.py','test_unchanged_gpu.patch.py','unchanged_authority.patch.py','completed_fitness_host_probe.cpp']]]}
        commands=[['c++','-std=c++17','-O2','-I'+str(serial.ROOT/'cpp/include'),'-I'+str(serial.BASE/'prototypes'),
                   str(serial.BASE/'prototypes/completed_fitness_host_probe.cpp'),'-o',str(out/'host-probe')],
                  [str(out/'host-probe')]]
        state['commands']=commands
        for index,command in enumerate(commands):
            with (out/f'check{index}.log').open('x') as log:
                child=subprocess.Popen(command,cwd=serial.ROOT,stdout=log,stderr=subprocess.STDOUT)
                state.update(child_pid=child.pid,current_command=index);serial.save(record,state)
                result=child.wait()
            state.setdefault('results',[]).append(result);serial.save(record,state)
            if result:raise RuntimeError(f'preflight command{index} failed')
        state.update(status='ok',host_probe_sha256=serial.sha(out/'host-probe'),finished_epoch=time.time());serial.save(record,state)
except BaseException as error:
    state.update(status='failed',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());serial.save(record,state)
    raise
