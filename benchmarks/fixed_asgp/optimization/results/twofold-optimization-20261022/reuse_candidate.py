"""Apply only the checked staged reuse sources, then build/check/pin10."""
import fcntl
import importlib.util
import os
from pathlib import Path
import time
import traceback

import build_projected
import serial

record=serial.RAW/'unchanged-candidate-controller.json'
lease=(serial.RAW/'unchanged-candidate-controller.lock').open('a');fcntl.flock(lease,fcntl.LOCK_EX | fcntl.LOCK_NB)
assert not record.exists(),'preserve existing attempts'
files=[serial.BASE/'prototypes'/name for name in ['apply_unchanged_reuse.py','completed_fitness_reuse.hpp',
       'test_unchanged_host.patch.py','test_unchanged_gpu.patch.py','unchanged_authority.patch.py','completed_fitness_host_probe.cpp']]
files += [Path(__file__),serial.BASE/'serial.py',serial.BASE/'build_projected.py']
state=dict(pid=os.getpid(),status='waiting_for_preflight',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
           harness_sha256={str(p):serial.sha(p) for p in files})
serial.save(record,state)
try:
    while True:
        preflight=serial.read(serial.RAW/'unchanged-preflight-attempt0/manifest.json')
        if preflight['status']=='failed':raise RuntimeError('staged preflight failed; preserve and diagnose')
        if preflight['status']=='ok':break
        if time.time()>serial.DEADLINE-10800:raise RuntimeError('reuse preflight wait exceeded budget')
        time.sleep(10)
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        if time.time()>serial.DEADLINE-10800:raise RuntimeError('reuse build missed budget')
        for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest,'staged source changed'
        for path,digest in preflight['prototype_sha256'].items():assert serial.sha(path)==digest,'preflight sources changed'
        prior=serial.read(serial.RAW/'build-attempt9/manifest.json');assert prior['status']=='ok'
        proc=Path(f'/proc/{prior["pid"]}/stat');assert not proc.exists() or proc.read_text().split()[2]=='Z','prior build remains active'
        path=serial.BASE/'prototypes/apply_unchanged_reuse.py'
        spec=importlib.util.spec_from_file_location('apply_completed_fitness_candidate',path)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        state.update(status='applying_source',paths=module.apply());serial.save(record,state)
    state['status']='building';serial.save(record,state)
    build_projected.run('build-attempt10')
    build=serial.read(serial.RAW/'build-attempt10/manifest.json');assert build['status']=='ok'
    state.update(status='ok',binary=build['binary'],binary_sha256=build['binary_sha256'],finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
    raise
finally:serial.save(record,state)
