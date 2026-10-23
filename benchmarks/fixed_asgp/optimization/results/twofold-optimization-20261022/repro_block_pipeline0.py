"""One global reproduction block choice on fixed development8/bothP only."""
import fcntl
import os
from pathlib import Path
import time
import traceback

import repro_block_pilot
import serial
import study_statistics

name = 'repro-block0'
record = serial.RAW / (name + '-controller.json')
assert not record.exists()
paths = [Path(__file__), serial.BASE / 'repro_block_pilot.py',
         serial.BASE / 'stage_pilot.py', serial.BASE / 'vm_pilot.py',
         serial.BASE / 'serial.py', serial.BASE / 'study_statistics.py',
         serial.BASE / 'protocol.json']
state = dict(pid=os.getpid(), status='waiting_for_lock', started_epoch=time.time(),
             deadline_epoch=serial.DEADLINE,
             harness_sha256={str(p): serial.sha(p) for p in paths},
             rule='same admitted20 binary; fixed development8/bothP/G8/seed0; '
                  'all legal blocks1/2/4/8/16/32/64/128; global16-cell mean; '
                  'retain1 unless >=2% faster; no heldout; not headline ratios')
serial.save(record, state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        for path, digest in state['harness_sha256'].items():
            assert serial.sha(path) == digest
        protocol = serial.read(serial.BASE / 'protocol.json')
        build = serial.read(serial.RAW / 'build-attempt20/manifest.json')
        assert build['status'] == 'ok'
        assert serial.sha(build['binary']) == build['binary_sha256']
        state.update(status='coarse', binary=build['binary'],
                     binary_sha256=build['binary_sha256'])
        serial.save(record, state)
    repro_block_pilot.run(build['binary'], name + '-coarse', [1024, 16384],
                         protocol['development']['tasks'])
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        rows = serial.read(serial.RAW / (name + '-coarse-comparisons.json'))
        expected = {(t, p) for t in protocol['development']['tasks']
                    for p in (1024, 16384)}
        means = {}
        per_population = {}
        for variant in repro_block_pilot.VARIANTS[1:]:
            group = [r for r in rows if r['variant'] == variant]
            assert len(group) == 16
            assert {(r['task'], r['population']) for r in group} == expected
            assert all(all(r[k] for k in ('final_descriptors_equal',
                       'full_final_ast_equal', 'champion_ast_fitness_equal')) for r in group)
            means[variant] = study_statistics.geometric_mean(r['speedup'] for r in group)
            per_population[variant] = {
                str(p): study_statistics.geometric_mean(r['speedup'] for r in group
                                                       if r['population'] == p)
                for p in (1024, 16384)}
        selected = max(means, key=means.get)
        if means[selected] < 1.02:
            selected = 'baseline'
        selection = dict(status='ok', selected=selected,
                         threads=1 if selected == 'baseline' else int(selected[7:]),
                         geometric_means=means, per_population=per_population,
                         epoch=time.time(), binary_sha256=build['binary_sha256'],
                         scope=state['rule'], coarse_comparisons_sha256=serial.sha(
                             serial.RAW / (name + '-coarse-comparisons.json')))
        serial.save(serial.RAW / (name + '-development-selection.json'), selection)
    state.update(status='requested_phase_complete', selection=selection,
                 finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped', error=repr(error), traceback=traceback.format_exc(),
                 finished_epoch=time.time())
    raise
finally:
    serial.save(record, state)
