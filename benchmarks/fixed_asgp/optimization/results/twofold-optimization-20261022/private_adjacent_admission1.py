"""Record actual26 native/source/binary lineage and static CUDA resources."""
import fcntl
import os
from pathlib import Path
import subprocess
import time
import traceback

import serial


out = serial.RAW / 'private-adjacent-admission1'
out.mkdir()
record = out / 'manifest.json'
state = dict(pid=os.getpid(), status='waiting_for_actual26', started_epoch=time.time(), deadline_epoch=serial.DEADLINE)
serial.save(record, state)
try:
    while True:
        path = serial.RAW / 'private-adjacent-candidate1-controller.json'
        candidate = serial.read(path)
        if candidate['status'] == 'stopped':
            raise RuntimeError('candidate26 native admission failed; preserve its attempt')
        proc = Path(f'/proc/{candidate["pid"]}/stat')
        if candidate['status'] == 'ok' and (not proc.exists() or proc.read_text().split()[2] == 'Z'):
            break
        if time.time() > serial.DEADLINE - 10800:
            raise RuntimeError('native25 admission exceeded original reserve')
        time.sleep(10)
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        build_path = serial.RAW / 'build-attempt26/manifest.json'
        build = serial.read(build_path)
        assert build['status'] == 'ok' and build['results'] == [0, 0, 0]
        assert serial.sha(build['binary']) == build['binary_sha256'] == candidate['binary_sha256']
        native = (build_path.parent / 'check2.log').read_text()
        assert '100% tests passed, 0 tests failed out of 15' in native and '***Skipped' not in native
        for path, digest in build['source_sha256'].items():
            assert serial.sha(serial.ROOT / path) == digest, 'actual26 source changed'
        proof_path = serial.RAW / 'private-append-prototype0/manifest.json'
        proof = serial.read(proof_path)
        assert proof['status'] == 'ok' and proof['results'] == [0, 0, 0, 0]
        for path, digest in proof['source_sha256'].items():
            assert serial.sha(path) == digest
        assert serial.sha(serial.ROOT / 'cpp/src/runtime/gpu/device/builtins_device.cuh') == proof['copied_header_sha256']
        control_path = serial.RAW / 'build-attempt24/manifest.json'
        control = serial.read(control_path)
        assert control['status'] == 'ok' and serial.sha(control['binary']) == control['binary_sha256']
        state.update(status='static_resource_inspection', binary=build['binary'], binary_sha256=build['binary_sha256'],
                     control_binary_sha256=control['binary_sha256'], source_sha256=build['source_sha256'],
                     proof_sha256=serial.sha(proof_path), build_manifest_sha256=serial.sha(build_path),
                     control_manifest_sha256=serial.sha(control_path), helper_sha256=serial.sha(Path(__file__)))
        # cuobjdump reads the pinned image. It does not profile/execute a GPU.
        command = ['/usr/local/cuda-12.6/bin/cuobjdump', '--dump-resource-usage', build['binary']]
        log_path = out / 'resource-usage.txt'
        with log_path.open('x') as log:
            child = subprocess.Popen(command, cwd=serial.ROOT, stdout=log, stderr=subprocess.STDOUT)
            state.update(child_pid=child.pid, command=command)
            serial.save(record, state)
            assert child.wait() == 0
        lines = log_path.read_text().splitlines()
        kernels = [dict(symbol=line.strip(), resources=lines[i + 1].strip()) for i, line in enumerate(lines)
                   if 'Function ' in line and 'evaluate_fitness_programs_impl' in line]
        assert kernels
        state.update(status='ok', kernels=kernels, resource_sha256=serial.sha(log_path), finished_epoch=time.time(),
                     scope='actual15 targeted native checks + immutable25/source/prototype byte identity/control24 and static resource inspection; not full native suite, profiling or search-speed evidence')
except BaseException as error:
    state.update(status='failed', error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
    raise
finally:
    serial.save(record, state)
