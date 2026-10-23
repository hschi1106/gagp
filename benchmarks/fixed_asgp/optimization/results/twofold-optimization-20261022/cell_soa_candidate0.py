"""Apply private-cell interleaving under the common lock; admit immutable23."""
import fcntl
import importlib.util
import os
from pathlib import Path
import time
import traceback

import build_cell_soa
import serial


record = serial.RAW / 'cell-soa-candidate0-controller.json'
assert not record.exists()
paths = [Path(__file__), serial.BASE / 'build_cell_soa.py', serial.BASE / 'serial.py',
         serial.BASE / 'prototypes/apply_private_cell_soa.py']
state = dict(pid=os.getpid(), status='waiting_for_lock', started_epoch=time.time(),
             deadline_epoch=serial.DEADLINE,
             harness_sha256={str(p): serial.sha(p) for p in paths})
serial.save(record, state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        assert time.time() < serial.DEADLINE - 10800
        previous = serial.read(serial.RAW / 'build-attempt22/manifest.json')
        assert previous['status'] == 'ok'
        for path, digest in previous['source_sha256'].items():
            assert serial.sha(serial.ROOT / path) == digest, 'current source differs from22'
        for path, digest in state['harness_sha256'].items():
            assert serial.sha(path) == digest
        spec = importlib.util.spec_from_file_location('cell_soa_apply', paths[-1])
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.apply(dry_run=True)
        state.update(status='applying', changed_paths=module.apply())
        serial.save(record, state)
    build_cell_soa.run('build-attempt23')
    build = serial.read(serial.RAW / 'build-attempt23/manifest.json')
    assert build['status'] == 'ok' and build['results'] == [0, 0, 0]
    assert serial.sha(build['binary']) == build['binary_sha256']
    state.update(status='ok', binary=build['binary'], binary_sha256=build['binary_sha256'],
                 finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped', error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
    raise
finally:
    serial.save(record, state)
