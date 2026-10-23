"""Fuse already-validated cold identity/executable lookup; pin after13checks."""
import fcntl
import importlib.util
import os
from pathlib import Path
import time
import traceback

import build_cold
import serial

name='fused-cold-candidate0'
record=serial.RAW/(name+'-controller.json');assert not record.exists()
paths=[Path(__file__),serial.BASE/'build_cold.py',serial.BASE/'serial.py',
       serial.BASE/'prototypes/apply_fused_cold_executable.py',
       serial.BASE/'prototypes/fused_cold_scalar_probe.txt']
state=dict(pid=os.getpid(),status='waiting_for_lock',started_epoch=time.time(),
           deadline_epoch=serial.DEADLINE,harness_sha256={str(p):serial.sha(p) for p in paths})
serial.save(record,state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        assert time.time()<serial.DEADLINE-10800
        previous=serial.read(serial.RAW/'build-attempt21/manifest.json')
        assert previous['status']=='ok'
        for path,digest in previous['source_sha256'].items():assert serial.sha(serial.ROOT/path)==digest
        for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest
        spec=importlib.util.spec_from_file_location('fused_cold_apply',serial.BASE/'prototypes/apply_fused_cold_executable.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        module.apply(dry_run=True)
        state.update(status='building',changed_paths=module.apply());serial.save(record,state)
    build_cold.run('build-attempt22')
    build=serial.read(serial.RAW/'build-attempt21/manifest.json');assert build['status']=='ok'
    state.update(status='ok',binary=build['binary'],binary_sha256=build['binary_sha256'],finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
finally:serial.save(record,state)
