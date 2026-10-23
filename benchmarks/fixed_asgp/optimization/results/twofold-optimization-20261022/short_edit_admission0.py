"""Verify actual24 source/binary/native admission and static CUDA resources."""
import fcntl
import os
from pathlib import Path
import subprocess
import time
import traceback

import serial


out = serial.RAW / 'short-edit-admission0'
out.mkdir()
record = out / 'manifest.json'
state = dict(pid=os.getpid(), status='waiting_for_actual24', started_epoch=time.time(), deadline_epoch=serial.DEADLINE)
serial.save(record, state)
try:
    while True:
        path = serial.RAW / 'short-edit-candidate0-controller.json'
        previous = serial.read(path) if path.exists() else {}
        if previous.get('status') == 'stopped':
            raise RuntimeError('candidate24 stopped; preserve failed admission')
        proc = Path(f'/proc/{previous.get("pid")}/stat')
        if previous.get('status') == 'ok' and (not proc.exists() or proc.read_text().split()[2] == 'Z'):
            break
        if time.time() > serial.DEADLINE - 10800:
            raise RuntimeError('24 admission missed original budget')
        time.sleep(10)
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        build_path = serial.RAW / 'build-attempt24/manifest.json'
        build = serial.read(build_path)
        assert build['status'] == 'ok' and build['results'] == [0, 0, 0]
        assert serial.sha(build['binary']) == build['binary_sha256'] == previous['binary_sha256']
        assert '100% tests passed, 0 tests failed out of 15' in (build_path.parent / 'check2.log').read_text()
        for path, digest in build['source_sha256'].items():
            assert serial.sha(serial.ROOT / path) == digest, 'current source differs from admitted24'
        baseline_proof = serial.RAW / 'cell-soa-admission0/manifest.json'
        assert serial.read(baseline_proof)['status'] == 'ok'
        control = serial.read(serial.RAW / 'build-attempt23/manifest.json')
        assert serial.sha(control['binary']) == control['binary_sha256']
        state.update(status='checking_resources', binary=build['binary'], binary_sha256=build['binary_sha256'],
                     source_sha256=build['source_sha256'], control_binary_sha256=control['binary_sha256'],
                     control_provenance=str(baseline_proof), control_provenance_sha256=serial.sha(baseline_proof),
                     build_manifest_sha256=serial.sha(build_path))
        command = ['/usr/local/cuda-12.6/bin/cuobjdump', '--dump-resource-usage', build['binary']]
        log_path = out / 'resource-usage.txt'
        with log_path.open('x') as log:
            child = subprocess.Popen(command, cwd=serial.ROOT, stdout=log, stderr=subprocess.STDOUT)
            state.update(child_pid=child.pid, command=command)
            serial.save(record, state)
            assert child.wait() == 0
        lines = log_path.read_text().splitlines()
        kernels = [dict(symbol=line.strip(), resources=lines[i+1].strip()) for i, line in enumerate(lines)
                   if 'Function ' in line and 'evaluate_fitness_programs_impl' in line]
        assert kernels
        state.update(status='ok', kernels=kernels, resource_sha256=serial.sha(log_path),
                     helper_sha256=serial.sha(Path(__file__)), finished_epoch=time.time(),
                     scope='15 actual targeted native tests + immutable24/source and static resources; not full native suite or speed claim')
except BaseException as error:
    state.update(status='failed', error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
    raise
finally:
    serial.save(record, state)
