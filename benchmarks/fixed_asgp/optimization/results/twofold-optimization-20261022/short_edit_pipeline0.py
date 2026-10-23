"""Global five-arm old-training comparison; preserve the earlier parser failure."""
import fcntl
import os
from pathlib import Path
import time
import traceback

import serial
import short_edit_pilot as helper
import study_statistics


name = 'short-edit0'
record = serial.RAW / (name + '-controller.json')
assert not record.exists()
paths = [Path(__file__), serial.BASE / 'short_edit_pilot.py', serial.BASE / 'stage_pilot.py',
         serial.BASE / 'vm_pilot.py', serial.BASE / 'serial.py', serial.BASE / 'protocol.json']
state = dict(pid=os.getpid(), status='waiting_for_actual24_admission', started_epoch=time.time(),
             deadline_epoch=serial.DEADLINE, harness_sha256={str(p): serial.sha(p) for p in paths},
             rule='fixed8 development tasks/bothP/G8/seed0; same shape/poolON/block1; '
                  '23OFF/24OFF/24short/24SoA/24short+SoA; one global best arm >=2% vs BOTH OFF controls; '
                  'no heldout; must obtain NEW complete60-cell comparisons before final eligibility')
serial.save(record, state)
try:
    while True:
        prerequisites = [serial.RAW / p / 'manifest.json' for p in
                         ('build-attempt24', 'short-edit-admission0', 'harness-checks-attempt22')]
        data = [serial.read(p) if p.exists() else {} for p in prerequisites]
        if any(d.get('status') in ('failed', 'stopped') for d in data):
            raise RuntimeError('24 source/native/harness prerequisite failed; preserve attempt')
        candidate = serial.read(serial.RAW / 'short-edit-candidate0-controller.json')
        if candidate['status'] == 'stopped':
            raise RuntimeError('candidate24 stopped; preserve attempt')
        if all(d.get('status') == 'ok' for d in data):
            break
        if time.time() > serial.DEADLINE - 10800:
            raise RuntimeError('short-edit admission missed original deadline reserve')
        time.sleep(10)
    build, admission, checks = data
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        for path, digest in state['harness_sha256'].items():
            assert serial.sha(path) == digest
        assert serial.sha(build['binary']) == build['binary_sha256'] == admission['binary_sha256']
        protocol = serial.read(serial.BASE / 'protocol.json')
        failed = serial.RAW / 'cell-soa0-controller.json'
        state.update(status='coarse', binary_sha256=build['binary_sha256'],
                     control_binary_sha256=admission['control_binary_sha256'],
                     preserved_prior_attempt=str(failed), preserved_prior_attempt_sha256=serial.sha(failed),
                     prior_failure='SoA0 baseline22 exited0/CPU16pass but parser read root execution_profile; actual profiles are per generation. Original invalid record remains; new matrix uses NEW23/24 controls, no overwrite or native rerun.')
        serial.save(record, state)
    helper.run(build['binary'], name + '-coarse', [1024, 16384], protocol['development']['tasks'])
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        path = serial.RAW / (name + '-coarse-comparisons.json')
        rows = serial.read(path)
        expected = {(task, p) for task in protocol['development']['tasks'] for p in (1024, 16384)}
        means = {}
        for variant in helper.VARIANTS[1:]:
            group = [r for r in rows if r['variant'] == variant]
            assert len(group) == 16 and {(r['task'], r['population']) for r in group} == expected
            assert all(all(r[k] for k in ('final_descriptors_equal', 'full_final_ast_equal',
                                         'champion_ast_fitness_equal')) for r in group)
            means[variant] = study_statistics.geometric_mean(r['speedup'] for r in group)
        active = {}
        for variant in ('short', 'soa', 'short_soa'):
            jobs = serial.read(serial.RAW / f'{name}-coarse-{variant}/manifest.json')['jobs']
            assert len(jobs) == 16 and all(j['status'] == 'ok' for j in jobs.values())
            active[variant] = dict(short=sum(j['short_edit_active_generations'] for j in jobs.values()),
                                   soa=sum(j['cell_soa_active_generations'] for j in jobs.values()))
            if variant in ('short', 'short_soa'):
                assert active[variant]['short'] > 0, 'no actual applicable short-edit profile'
            if variant in ('soa', 'short_soa'):
                assert active[variant]['soa'] > 0, 'no actual SoA arena profile'
        selected = max(('short', 'soa', 'short_soa'), key=means.get)
        if means[selected] < max(1.0, means['ordinary']) * 1.02:
            selected = 'baseline'
        selection = dict(selected=selected, enabled=selected != 'baseline', geometric_means=means,
                         epoch=time.time(), scope=state['rule'], extra=helper.execution_settings(selected),
                         binary_sha256=build['binary_sha256'], comparisons_sha256=serial.sha(path),
                         active_generations=active)
        serial.save(serial.RAW / (name + '-development-selection.json'), selection)
    state.update(status='requested_phase_complete', selection=selection, finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped', error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
    raise
finally:
    serial.save(record, state)
