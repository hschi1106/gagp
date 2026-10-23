"""Independent copied-header concat probe; production CPP and samples unchanged."""
import fcntl
import importlib.util
import os
from pathlib import Path
import shutil
import signal
import subprocess
import time
import traceback

import serial


out = serial.RAW / 'private-append-prototype0'
out.mkdir()
record = out / 'manifest.json'
source = serial.BASE / 'prototypes/private_append_probe.cu'
patcher = serial.BASE / 'prototypes/private_append_overlay.py'
state = dict(pid=os.getpid(), status='waiting', started_epoch=time.time(),
    deadline_epoch=serial.DEADLINE, source_sha256={str(p): serial.sha(p) for p in
    (Path(__file__), source, patcher)}, production_cpp_mutated=False,
    scope='independent CPU vectors vs original24 GPU and copied-header OFF/ON; synthetic kernel diagnostics only, no GAGP search claim')
serial.save(record, state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        assert time.time() < serial.DEADLINE - 10800
        build = serial.read(serial.RAW / 'build-attempt24/manifest.json')
        assert build['status'] == 'ok'
        for path, digest in build['source_sha256'].items():
            assert serial.sha(serial.ROOT / path) == digest, 'current CPP differs from admitted24'
        overlay = out / 'overlay/runtime/gpu'
        shutil.copytree(serial.ROOT / 'cpp/src/runtime/gpu', overlay)
        spec = importlib.util.spec_from_file_location('private_append_overlay', patcher)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.apply(overlay)
        state.update(status='running', reference_build_sha256=serial.sha(serial.RAW / 'build-attempt24/manifest.json'),
                     reference_binary_sha256=build['binary_sha256'],
                     copied_header_sha256=serial.sha(overlay / 'device/builtins_device.cuh'))
        serial.save(record, state)
        nvcc = '/usr/local/cuda-12.6/bin/nvcc'
        commands = [
            [nvcc, '-std=c++17', '-O3', '-lineinfo', '-arch=sm_86',
             '-Icpp/include', '-Icpp/src', str(source), '-o', str(out / 'reference')],
            [str(out / 'reference')],
            [nvcc, '-std=c++17', '-O3', '-lineinfo', '-arch=sm_86', '-DPROBE_OPT',
             '-Icpp/include', '-I' + str(out / 'overlay'), '-Icpp/src',
             str(source), '-o', str(out / 'candidate')],
            [str(out / 'candidate')]]
        for i, command in enumerate(commands):
            log_path = out / f'check{i}.log'
            remaining = serial.DEADLINE - 10800 - time.time()
            assert remaining > 0
            with log_path.open('x') as log:
                child = subprocess.Popen(command, cwd=serial.ROOT, stdout=log,
                                         stderr=subprocess.STDOUT, start_new_session=True)
                state.update(child_pid=child.pid, current_command=command, command_index=i)
                serial.save(record, state)
                try:
                    code = child.wait(timeout=min(240, remaining))
                except subprocess.TimeoutExpired:
                    os.killpg(child.pid, signal.SIGTERM)
                    try:
                        child.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        os.killpg(child.pid, signal.SIGKILL)
                        child.wait()
                    raise
            state.setdefault('results', []).append(code)
            serial.save(record, state)
            if code:
                raise RuntimeError(f'copied-header probe command{i} failed; preserve all attempts')
        for path, digest in build['source_sha256'].items():
            assert serial.sha(serial.ROOT / path) == digest, 'production CPP changed during independent probe'
        assert all(serial.sha(path) == digest for path, digest in state['source_sha256'].items())
        output = (out / 'check3.log').read_text()
        assert 'PASS traces=' in output and 'KERNEL_DIAGNOSTIC' in output
        state.update(status='ok', observed_output=output, finished_epoch=time.time(),
                     binary_sha256={name: serial.sha(out / name) for name in ('reference', 'candidate')})
except BaseException as error:
    state.update(status='failed', error=repr(error), traceback=traceback.format_exc(),
                 finished_epoch=time.time())
    raise
finally:
    serial.save(record, state)
