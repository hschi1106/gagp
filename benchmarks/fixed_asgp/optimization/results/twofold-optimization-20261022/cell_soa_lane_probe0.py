"""Separate distinct-lane scratch-alias test on exact admitted23 headers."""
import fcntl
import os
from pathlib import Path
import subprocess
import time
import traceback

import serial


out = serial.RAW / 'private-cell-lanes0'
out.mkdir()
record = out / 'manifest.json'
source = serial.BASE / 'prototypes/private_cell_lane_probe.cu'
state = dict(pid=os.getpid(), status='waiting_for_admitted23', started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
             source=str(source), source_sha256=serial.sha(source))
serial.save(record, state)
try:
    while True:
        path = serial.RAW / 'cell-soa-admission0/manifest.json'
        admission = serial.read(path) if path.exists() else {}
        if admission.get('status') == 'failed':
            raise RuntimeError('23 admission failed; no source-probe claim')
        if admission.get('status') == 'ok':
            break
        if time.time() > serial.DEADLINE - 10800:
            raise RuntimeError('lane probe missed original deadline reserve')
        time.sleep(10)
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        assert serial.sha(source) == state['source_sha256']
        for path, digest in admission['source_sha256'].items():
            assert serial.sha(serial.ROOT / path) == digest, 'admitted23 headers changed'
        commands = [['/usr/local/cuda-12.6/bin/nvcc', '-std=c++17', '-O2', '-arch=sm_86',
                     '-Icpp/include', '-Icpp/src', str(source), '-o', str(out / 'probe')], [str(out / 'probe')]]
        state.update(status='running', candidate_binary_sha256=admission['binary_sha256'],
                     admission_sha256=serial.sha(serial.RAW / 'cell-soa-admission0/manifest.json'))
        serial.save(record, state)
        for i, command in enumerate(commands):
            log_path = out / f'check{i}.log'
            with log_path.open('x') as log:
                child = subprocess.Popen(command, cwd=serial.ROOT, stdout=log, stderr=subprocess.STDOUT)
                state.update(child_pid=child.pid, current_command=command)
                serial.save(record, state)
                result = child.wait()
            state.setdefault('results', []).append(result)
            if result:
                raise RuntimeError('distinct-lane probe command failed; preserve attempt')
        state.update(status='ok', finished_epoch=time.time(), binary_sha256=serial.sha(out / 'probe'),
                     scope='96 unique full-width lane inputs x contiguous/interleaved layouts; ownership/score/rewind/overflow, no speed claim')
except BaseException as error:
    state.update(status='failed', error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
    raise
finally:
    serial.save(record, state)
