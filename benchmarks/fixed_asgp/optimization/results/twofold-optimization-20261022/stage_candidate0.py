"""Integrate shared sequential-stage storage only after recorded standalone checks."""
import fcntl
import importlib.util
import os
from pathlib import Path
import time
import traceback

import build_stage
import serial

record=serial.RAW/'stage-candidate0-controller.json';assert not record.exists()
paths=[Path(__file__),serial.BASE/'build_stage.py',serial.BASE/'prototypes/apply_stage_storage.py',
    serial.BASE/'prototypes/cuda_stage_storage.hpp',serial.BASE/'prototypes/stage_storage_gpu.hpp',
    serial.BASE/'prototypes/stage_storage1.cu',serial.BASE/'prototypes/test_stage_storage1.cu',serial.BASE/'serial.py']
state=dict(pid=os.getpid(),status='checking_prerequisites',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
    harness_sha256={str(p):serial.sha(p) for p in paths})
serial.save(record,state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        preflight=serial.read(serial.RAW/'stage-storage-preflight1/manifest.json');assert preflight['status']=='ok'
        for prior in (preflight,serial.read(serial.RAW/'build-attempt18/manifest.json')):
            for key in ('pid','child_pid'):
                pid=prior.get(key);proc=Path(f'/proc/{pid}/stat')
                assert not proc.exists() or proc.read_text().split()[2]=='Z',('previous process still active',pid)
        for path,digest in preflight['source_sha256'].items():assert serial.sha(path)==digest
        for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest
        spec=importlib.util.spec_from_file_location('stage_apply',serial.BASE/'prototypes/apply_stage_storage.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        module.apply(dry_run=True)
        state.update(status='building',changed_paths=module.apply());serial.save(record,state)
    build_stage.run('build-attempt19')
    build=serial.read(serial.RAW/'build-attempt19/manifest.json');assert build['status']=='ok'
    state.update(status='ok',binary=build['binary'],binary_sha256=build['binary_sha256'],finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
    raise
finally:serial.save(record,state)
