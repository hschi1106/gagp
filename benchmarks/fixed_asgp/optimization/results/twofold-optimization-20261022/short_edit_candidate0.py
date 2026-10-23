"""Admit exact short-list scoring only after actual math/ownership23 proofs."""
import fcntl
import importlib.util
import os
from pathlib import Path
import time
import traceback

import build_cell_soa
import serial


record = serial.RAW / 'short-edit-candidate0-controller.json'
assert not record.exists()
paths = [Path(__file__), serial.BASE / 'build_cell_soa.py', serial.BASE / 'serial.py',
         serial.BASE / 'prototypes/apply_short_edit.py', serial.BASE / 'prototypes/runtime_short_edit.hpp',
         serial.BASE / 'prototypes/runtime_short_edit_patterns.hpp', serial.BASE / 'prototypes/runtime_short_edit_device.cuh',
         serial.BASE / 'prototypes/runtime_short_edit_checks.cuh']
state = dict(pid=os.getpid(), status='waiting_for_proofs', started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
             harness_sha256={str(p): serial.sha(p) for p in paths})
serial.save(record, state)
try:
    prerequisites = [serial.RAW / p / 'manifest.json' for p in
                     ('build-attempt23', 'cell-soa-admission0', 'private-cell-lanes0', 'myers64-prototype0')]
    for path in prerequisites:
        data = serial.read(path)
        assert data['status'] == 'ok', ('unadmitted prerequisite', str(path))
        proc = Path(f'/proc/{data["pid"]}/stat')
        assert not proc.exists() or proc.read_text().split()[2] == 'Z', 'proof controller still active'
    prototype = serial.read(prerequisites[-1])
    assert 'PASS cases=22242;' in prototype['observed_output']
    for path, digest in prototype['source_sha256'].items():
        assert serial.sha(path) == digest
    # The production-shaped primitive is the exact function tested by the
    # independent CPU/GPU probe, with only names/device annotations changed.
    original = (serial.BASE / 'prototypes/myers64.hpp').read_text()
    original = original[original.index('template<class Reader>'):original.index('\nstruct Reader {')]
    original = original.replace('GAGP_MYERS_HD', 'GAGP_SHORT_EDIT_CALLABLE').replace('const Entry*', 'const DShortEditEntry*')
    original = original.replace('distance64', 'short_edit_distance64').replace('matching_positions', 'short_edit_matching_positions')
    actual = (serial.BASE / 'prototypes/runtime_short_edit.hpp').read_text()
    actual = actual[actual.index('template<class Reader>'):actual.index('\nstruct DShortEditPattern {')]
    assert original == actual, 'production math differs from actually tested primitive'
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        assert time.time() < serial.DEADLINE - 10800
        previous = serial.read(prerequisites[0])
        for path, digest in previous['source_sha256'].items():
            assert serial.sha(serial.ROOT / path) == digest, 'current CPP differs from admitted23'
        for path, digest in state['harness_sha256'].items():
            assert serial.sha(path) == digest
        spec = importlib.util.spec_from_file_location('short_edit_apply', paths[3])
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.apply(dry_run=True)
        state.update(status='applying', proof_sha256={str(p): serial.sha(p) for p in prerequisites},
                     changed_paths=module.apply())
        serial.save(record, state)
    build_cell_soa.run('build-attempt24')
    build = serial.read(serial.RAW / 'build-attempt24/manifest.json')
    assert build['status'] == 'ok' and build['results'] == [0, 0, 0]
    assert serial.sha(build['binary']) == build['binary_sha256']
    state.update(status='ok', binary=build['binary'], binary_sha256=build['binary_sha256'], finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped', error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
    raise
finally:
    serial.save(record, state)
