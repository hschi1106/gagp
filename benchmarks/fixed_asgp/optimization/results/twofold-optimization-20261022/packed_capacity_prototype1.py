"""Copied-header semantic replay/capacity proof; production CPP and searches untouched."""
import fcntl
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import time
import traceback

import serial

out = serial.RAW / 'packed-capacity-prototype1'
out.mkdir()
paths = [Path(__file__), serial.BASE / 'prototypes/private_append_probe.cu',
         serial.BASE / 'prototypes/packed_capacity_overlay1.py', serial.BASE / 'prototypes/packed_capacity_main.cuh']
state = dict(pid=os.getpid(), status='waiting_for_lock', started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
             source_sha256={str(p): serial.sha(p) for p in paths}, production_cpp_mutated=False,
             scope='development-only independent CPU/GPU semantic replay proof in COPIED26 headers; small per-lane capacities are unit boundaries, not a reduced16GiB benchmark setting')
serial.save(out / 'manifest.json', state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        assert time.time() < serial.DEADLINE - 10800
        build = serial.read(serial.RAW / 'build-attempt26/manifest.json')
        assert build['status'] == 'ok'
        for path, digest in build['source_sha256'].items():
            assert serial.sha(serial.ROOT / path) == digest
        for path, digest in state['source_sha256'].items():
            assert serial.sha(path) == digest
        overlay = out / 'overlay/runtime/gpu'
        shutil.copytree(serial.ROOT / 'cpp/src/runtime/gpu', overlay)
        spec = importlib.util.spec_from_file_location('packed_overlay', paths[2])
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.apply(overlay)
        source = paths[1].read_text()
        source = source[:source.index('\nint main() {')]
        source = source.replace('int lanes,bool enabled)', 'int lanes,int mode)')
        anchor = '  const int lane=blockIdx.x*blockDim.x+threadIdx.x;if(lane>=lanes)return;'
        assert source.count(anchor) == 1
        source = source.replace(anchor, anchor + '\n  const bool enabled=mode!=0;')
        anchor = '  state.private_adjacent_concat=enabled;'
        assert source.count(anchor) == 1
        source = source.replace(anchor, anchor + '\n  state.private_packed_capacity=mode==2;')
        probe = out / 'packed_probe.cu'
        probe.write_text(source + paths[3].read_text())
        state.update(status='compiling', generated_probe_sha256=serial.sha(probe),
                     copied_header_sha256=serial.sha(overlay / 'device/builtins_device.cuh'))
        commands = [
            ['/usr/local/cuda-12.6/bin/nvcc', '-O3', '-std=c++17', '-arch=sm_86', '-DPROBE_OPT',
             '-I'+str(out / 'overlay'), '-I'+str(serial.ROOT / 'cpp/include'),
             '-I'+str(serial.ROOT / 'cpp/src'), str(probe), '-o', str(out / 'probe')],
            [str(out / 'probe')]]
        for index, command in enumerate(commands):
            log = out / f'check{index}.log'
            with log.open('x') as stream:
                child = subprocess.Popen(command, cwd=serial.ROOT, stdout=stream, stderr=subprocess.STDOUT)
                state.update(child_pid=child.pid, command=command, command_index=index)
                serial.save(out / 'manifest.json', state)
                result = child.wait()
            state.setdefault('results', []).append(result)
            if result:
                raise RuntimeError('packed prototype command failed: '+str(index))
        for path, digest in build['source_sha256'].items():
            assert serial.sha(serial.ROOT / path) == digest
        state.update(status='ok', observed_output=(out / 'check1.log').read_text(),
                     binary_sha256=serial.sha(out / 'probe'), reference_build_sha256=serial.sha(serial.RAW / 'build-attempt26/manifest.json'),
                     finished_epoch=time.time())
except BaseException as error:
    state.update(status='failed', error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
    raise
finally:
    serial.save(out / 'manifest.json', state)
