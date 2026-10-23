"""Fixed old-training cell-layout comparison after actual15-test23 admission."""
import fcntl
import os
from pathlib import Path
import time
import traceback

import cell_soa_pilot as helper
import serial
import study_statistics


name = 'cell-soa0'
record = serial.RAW / (name + '-controller.json')
assert not record.exists()
paths = [Path(__file__), serial.BASE / 'cell_soa_pilot.py', serial.BASE / 'stage_pilot.py',
         serial.BASE / 'vm_pilot.py', serial.BASE / 'serial.py', serial.BASE / 'study_statistics.py',
         serial.BASE / 'protocol.json']
state = dict(pid=os.getpid(), status='waiting_for_actual23_admission', started_epoch=time.time(),
             deadline_epoch=serial.DEADLINE, harness_sha256={str(p): serial.sha(p) for p in paths},
             rule='fixed8 development tasks/bothP/G8/seed0; shape/poolON/block1; 22OFF/23OFF/23ON; '
                  'one global choice >=2% vs BOTH controls, otherwise OFF; no heldout or full30 speed claim')
serial.save(record, state)
try:
    while True:
        prerequisites = [serial.RAW / 'build-attempt23/manifest.json',
                         serial.RAW / 'cell-soa-admission0/manifest.json']
        data = [serial.read(p) if p.exists() else {} for p in prerequisites]
        failed = [d for d in data if d.get('status') in ('failed', 'stopped')]
        candidate_path = serial.RAW / 'cell-soa-candidate0-controller.json'
        candidate = serial.read(candidate_path) if candidate_path.exists() else {}
        if failed or candidate.get('status') == 'stopped':
            raise RuntimeError('23 native/source/resource admission failed; preserve attempt')
        if all(d.get('status') == 'ok' for d in data):
            break
        if time.time() > serial.DEADLINE - 10800:
            raise RuntimeError('cell SoA admission missed original budget')
        time.sleep(10)
    build, admission = data
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        for path, digest in state['harness_sha256'].items():
            assert serial.sha(path) == digest
        assert serial.sha(build['binary']) == build['binary_sha256'] == admission['binary_sha256']
        protocol = serial.read(serial.BASE / 'protocol.json')
        state.update(status='coarse', binary_sha256=build['binary_sha256'],
                     control_binary_sha256=admission['control_binary_sha256'])
        serial.save(record, state)
    helper.run(build['binary'], name + '-coarse', [1024, 16384], protocol['development']['tasks'])
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        comparison_path = serial.RAW / (name + '-coarse-comparisons.json')
        rows = serial.read(comparison_path)
        expected = {(task, p) for task in protocol['development']['tasks'] for p in (1024, 16384)}
        means = {}
        for variant in helper.VARIANTS[1:]:
            group = [r for r in rows if r['variant'] == variant]
            assert len(group) == 16 and {(r['task'], r['population']) for r in group} == expected
            assert all(all(r[k] for k in ('final_descriptors_equal', 'full_final_ast_equal',
                                         'champion_ast_fitness_equal')) for r in group)
            means[variant] = study_statistics.geometric_mean(r['speedup'] for r in group)
        active = serial.read(serial.RAW / (name + '-coarse-soa/manifest.json'))['jobs']
        assert len(active) == 16 and any(j.get('cell_soa_active') for j in active.values()), 'no actual SoA kernel exercised'
        selected = 'soa' if means['soa'] >= max(1.0, means['ordinary']) * 1.02 else 'baseline'
        selection = dict(selected=selected, enabled=selected == 'soa', geometric_means=means,
                         epoch=time.time(), scope=state['rule'], extra=helper.execution_settings(selected),
                         binary_sha256=build['binary_sha256'], comparisons_sha256=serial.sha(comparison_path),
                         active_cells=sum(j.get('cell_soa_active', False) for j in active.values()))
        serial.save(serial.RAW / (name + '-development-selection.json'), selection)
    state.update(status='requested_phase_complete', selection=selection, finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped', error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
    raise
finally:
    serial.save(record, state)
