"""Independent exact-distance development prototype, no heldout/production edit."""
import fcntl
import os
from pathlib import Path
import subprocess
import time
import traceback

import serial


out = serial.RAW / 'myers64-prototype0'
out.mkdir()
record = out / 'manifest.json'
paths = [Path(__file__), serial.BASE / 'prototypes/myers64.hpp', serial.BASE / 'prototypes/myers64_probe.cu']
state = dict(pid=os.getpid(), status='waiting_for_shared_lock', started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
             source_sha256={str(p): serial.sha(p) for p in paths},
             scope='exact whole-sequence one-word Levenshtein on synthetic development inputs; no new task/test data or production modification')
serial.save(record, state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        assert time.time() < serial.DEADLINE - 10800
        assert all(serial.sha(p) == h for p, h in state['source_sha256'].items())
        state['status'] = 'building_and_testing'
        commands = [['/usr/local/cuda-12.6/bin/nvcc', '-std=c++17', '-O2', '-arch=sm_86',
                     str(paths[-1]), '-o', str(out / 'probe')], [str(out / 'probe')]]
        for i, command in enumerate(commands):
            with (out / f'check{i}.log').open('x') as log:
                child = subprocess.Popen(command, cwd=serial.ROOT, stdout=log, stderr=subprocess.STDOUT)
                state.update(child_pid=child.pid, current_command=command)
                serial.save(record, state)
                result = child.wait()
            state.setdefault('results', []).append(result)
            if result:
                raise RuntimeError('prototype failed; preserve attempt, not production admission')
        output = (out / 'check1.log').read_text()
        assert 'PASS cases=22242;' in output
        assert 'host/GPU bit-vector and independent host/GPU row recurrence' in output
        assert all(serial.sha(p) == h for p, h in state['source_sha256'].items()), 'prototype sources changed during checks'
        state.update(status='ok', finished_epoch=time.time(), binary_sha256=serial.sha(out / 'probe'),
                     observed_output=output, output_sha256=serial.sha(out / 'check1.log'),
                     algorithm_reference='https://doi.org/10.1145/316542.316550',
                     caveat='synthetic kernel timing excludes preprocessing/uploads/VM/evolution; no formal speed or native production fidelity claim')
except BaseException as error:
    state.update(status='failed', error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
    raise
finally:
    serial.save(record, state)
