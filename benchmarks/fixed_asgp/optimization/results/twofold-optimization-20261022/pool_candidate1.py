"""Apply preflighted allocation scope, build and pin a new immutable candidate."""
import fcntl
import importlib.util
import os
from pathlib import Path
import time
import traceback

import build_pool
import serial

record=serial.RAW/'pool-candidate1-controller.json';assert not record.exists()
state=dict(pid=os.getpid(),status='waiting_for_lock',started_epoch=time.time(),deadline_epoch=serial.DEADLINE)
paths=[Path(__file__),serial.BASE/'build_pool.py',serial.BASE/'prototypes/apply_pool1.py',
       serial.BASE/'prototypes/test_pool_evolution1.patch.py',serial.BASE/'serial.py']
state['harness_sha256']={str(p):serial.sha(p) for p in paths}
serial.save(record,state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        assert time.time()<serial.DEADLINE-serial.read(serial.BASE/'protocol.json')['reserve_s']
        for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest
        prior=serial.read(serial.RAW/'build-attempt13/manifest.json');assert prior['status']=='ok'
        proc=Path(f'/proc/{prior["pid"]}/stat');assert not proc.exists() or proc.read_text().split()[2]=='Z'
        preflight=serial.read(serial.RAW/'pool-preflight0/manifest.json');assert preflight['status']=='ok'
        for path,digest in preflight['prototype_sha256'].items():assert serial.sha(path)==digest
        def applier(name):
            spec=importlib.util.spec_from_file_location(name,serial.BASE/'prototypes/apply_pool1.py')
            module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
        changes=applier('pool_dry_run').apply(dry_run=True)
        serial.save(serial.RAW/'pool-transform-preflight1.json',dict(status='ok',epoch=time.time(),
            paths=[str(p.relative_to(serial.ROOT)) for p in changes],
            proposed_sha256={str(p.relative_to(serial.ROOT)):__import__('hashlib').sha256(s.encode()).hexdigest() for p,s in changes.items()}))
        changed=applier('pool_apply').apply()
        state.update(status='building',changed_paths=changed);serial.save(record,state)
    build_pool.run('build-attempt14')
    build=serial.read(serial.RAW/'build-attempt14/manifest.json');assert build['status']=='ok'
    state.update(status='ok',binary=build['binary'],binary_sha256=build['binary_sha256'],finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
    raise
finally:serial.save(record,state)
