"""Wait pinned14, integrate exact O(P) one-elite reduction and pin15."""
import fcntl
import importlib.util
import os
from pathlib import Path
import time
import traceback

import build_elite
import serial

record=serial.RAW/'elite-candidate1-controller.json';assert not record.exists()
paths=[Path(__file__),serial.BASE/'build_elite.py',serial.BASE/'prototypes/apply_linear_elite1.py',
    serial.BASE/'prototypes/test_linear_evolution.patch.py',serial.BASE/'serial.py']
state=dict(pid=os.getpid(),status='waiting_for_build15',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
    harness_sha256={str(p):serial.sha(p) for p in paths})
serial.save(record,state)
try:
    while True:
        path=serial.RAW/'build-attempt15/manifest.json';prior=serial.read(path) if path.exists() else {}
        controller=serial.read(serial.RAW/'pool-corrected0-controller.json')
        if prior.get('status')=='failed' or controller['status']=='stopped':raise RuntimeError('pool candidate prerequisite failed; no elite source applied')
        if prior.get('status')=='ok':
            proc=Path(f'/proc/{prior["pid"]}/stat')
            if not proc.exists() or proc.read_text().split()[2]=='Z':break
        if time.time()>serial.DEADLINE-10800:raise RuntimeError('elite candidate prerequisite missed budget')
        time.sleep(10)
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest
        preflight=serial.read(serial.RAW/'linear-elite-preflight0/manifest.json');assert preflight['status']=='ok'
        for path,digest in preflight['source_sha256'].items():assert serial.sha(path)==digest
        spec=importlib.util.spec_from_file_location('elite_apply',serial.BASE/'prototypes/apply_linear_elite1.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        state.update(status='building',changed_paths=module.apply());serial.save(record,state)
    build_elite.run('build-attempt16')
    build=serial.read(serial.RAW/'build-attempt16/manifest.json');assert build['status']=='ok'
    state.update(status='ok',binary=build['binary'],binary_sha256=build['binary_sha256'],finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
    raise
finally:serial.save(record,state)
