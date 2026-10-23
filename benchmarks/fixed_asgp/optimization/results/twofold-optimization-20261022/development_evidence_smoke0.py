"""Exercise the development reader on actual completed all30 data, under lock."""
import fcntl
import os
from pathlib import Path
import time
import traceback

import development_evidence
import serial


out = serial.RAW / 'development-evidence-smoke0'
out.mkdir()
state = dict(pid=os.getpid(), status='waiting', started_epoch=time.time(),
             deadline_epoch=serial.DEADLINE, native_searches_started=0,
             scope='existing stage1 development artifacts only; no new candidate/data/freeze')
serial.save(out / 'manifest.json', state)
try:
    checks = serial.read(serial.RAW / 'harness-checks-attempt23/manifest.json')
    assert checks['status'] == 'ok' and checks['returncode'] == 0
    paths = [Path(__file__), serial.BASE / 'development_evidence.py',
             serial.BASE / 'report_data.py', serial.BASE / 'vm_pilot.py',
             serial.BASE / 'protocol.json']
    hashes = {str(path): serial.sha(path) for path in paths}
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        assert time.time() < serial.DEADLINE - 10800
        for path, digest in checks['source_sha256'].items():
            assert serial.sha(path) == digest, 'checked study source changed'
        state.update(status='checking_completed_all30', source_sha256=hashes)
        serial.save(out / 'manifest.json', state)
        result = development_evidence.read_families(
            ['stage1'], serial.read(serial.BASE / 'protocol.json'))
        assert len(result['cells']) == 240 and len(result['recipes']) == 4
        assert all(row['complete'] and row['qualified_cells'] == 60
                   for row in result['recipes'])
        assert result['best_complete']['variant'] == 'pool_shape'
        assert not result['formal_target_proven'] and not result['independent_quality_proven']
        path = out / 'result.json'
        serial.save(path, result)
        assert all(serial.sha(path) == digest for path, digest in hashes.items())
        state.update(status='ok', result=str(path), result_sha256=serial.sha(path),
                     best_complete=result['best_complete'], finished_epoch=time.time())
except BaseException as error:
    state.update(status='failed', error=repr(error), traceback=traceback.format_exc(),
                 finished_epoch=time.time())
    raise
finally:
    serial.save(out / 'manifest.json', state)
