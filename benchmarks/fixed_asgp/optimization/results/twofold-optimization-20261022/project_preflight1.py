"""Check the staged transformation without changing CPP or claiming compilation."""
import fcntl
import importlib.util
import os
import time
import traceback

import serial

out=serial.RAW/'projected-preflight-attempt1';out.mkdir()
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
        state.update(status='ok',scope='text transformation only; CPP unchanged, no compile/test/performance claim',
            proposed_paths=[str(p.relative_to(serial.ROOT)) for p in changes],finished_epoch=time.time())
        serial.save(out/'manifest.json',state)
except BaseException as error:
    state.update(status='failed',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
    serial.save(out/'manifest.json',state)
    raise
