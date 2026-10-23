"""Admit private adjacent storage only after independent exact CPU/GPU probe."""
import fcntl
import importlib.util
import os
from pathlib import Path
import time
import traceback
import build_cell_soa
import serial

record=serial.RAW/'private-adjacent-candidate0-controller.json';assert not record.exists()
paths=[Path(__file__),serial.BASE/'build_cell_soa.py',serial.BASE/'serial.py']+[
    serial.BASE/'prototypes'/p for p in ('apply_private_adjacent.py','private_append_overlay.py',
                                       'private_append_probe.cu','runtime_private_adjacent_test_tail.cuh')]
state=dict(pid=os.getpid(),status='waiting_for_gpu_lock',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
           harness_sha256={str(p):serial.sha(p) for p in paths})
serial.save(record,state)
try:
    proof_path=serial.RAW/'private-append-prototype0/manifest.json';proof=serial.read(proof_path)
    assert proof['status']=='ok' and proof['results']==[0,0,0,0]
    proc=Path(f'/proc/{proof["pid"]}/stat');assert not proc.exists() or proc.read_text().split()[2]=='Z'
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        assert time.time()<serial.DEADLINE-10800
        previous=serial.read(serial.RAW/'build-attempt24/manifest.json');assert previous['status']=='ok'
        for path,digest in previous['source_sha256'].items():assert serial.sha(serial.ROOT/path)==digest
        for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest
        spec=importlib.util.spec_from_file_location('adjacent_apply',serial.BASE/'prototypes/apply_private_adjacent.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        module.apply(dry_run=True)
        state.update(status='applying',proof=str(proof_path),proof_sha256=serial.sha(proof_path),changed_paths=module.apply())
        serial.save(record,state)
    build_cell_soa.run('build-attempt25')
    build=serial.read(serial.RAW/'build-attempt25/manifest.json')
    assert build['status']=='ok' and build['results']==[0,0,0]
    assert 'physical64/reserved2143 append+prepend' in (serial.RAW/'build-attempt25/check2.log').read_text() or '100% tests passed' in (serial.RAW/'build-attempt25/check2.log').read_text()
    state.update(status='ok',binary=build['binary'],binary_sha256=build['binary_sha256'],finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
finally:serial.save(record,state)
