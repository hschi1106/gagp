"""Measure full paired native audit costs on complete C7 OLD TRAINING, not searches.

The previous formal0 forecast correctly refused the grid. This diagnostic
retains that decision and measures current exact-AST dedup costs rather than
assuming a discount from old full-population CPU timings. All formal references
must still be freshly executed. Stop this calibration after fifty minutes.
"""
import fcntl
import os
from pathlib import Path
import time
import traceback

import full_population_audit_shared_policy as native
import serial


name = 'development-native-cost0'
record = serial.RAW / (name + '-controller.json')
assert not record.exists()
paths = [Path(__file__)] + [serial.BASE / (module + '.py') for module in (
    'full_population_audit_shared_policy', 'full_population_audit_shared_timed',
    'full_population_audit_shared', 'full_population_audit_v2',
    'full_population_audit', 'champion_audit_dedup', 'shared_cpu_reference', 'serial')]
paths.append(serial.BASE / 'protocol.json')
state = dict(pid=os.getpid(), status='checking_prerequisites', started_epoch=time.time(),
             deadline_epoch=serial.DEADLINE, calibration_limit_s=3000,
             scope='all30/bothP C7 G8 oldTRAINING full native costs only; no search rerun or fresh heldout',
             harness_sha256={str(p): serial.sha(p) for p in paths})
serial.save(record, state)
try:
    for component in ('combined7', 'final-pipeline5', 'formal0', 'training17',
                      'training-timing12', 'preparation-timing10'):
        prior = serial.read(serial.RAW / (component + '-controller.json'))
        assert prior['status'] in ('requested_phase_complete', 'budget_insufficient')
        proc = Path(f'/proc/{prior["pid"]}/stat')
        assert not proc.exists() or proc.read_text().split()[2] == 'Z'
    checks = serial.read(serial.RAW / 'harness-checks-attempt31' / 'manifest.json')
    assert checks['status'] == 'ok'
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        protocol = serial.read(serial.BASE / 'protocol.json')
        selection = serial.read(serial.RAW / 'training17-development-selection.json')
        assert selection['selected']['family'] == 'combined7'
        assert serial.sha(protocol['baseline_binary']) == protocol['baseline_sha256']
        assert serial.sha(selection['binary']) == selection['binary_sha256']
        variants = {v: dict(extra=selection['variants'][v]['extra'],
            binary=protocol['baseline_binary'] if v == 'baseline' else selection['binary'])
            for v in ('baseline', 'candidate')}
        freeze = dict(variants=variants, reserve_s=protocol['reserve_s'],
                      native_audit_neutral_extra={'GAGP_FUSED_COLD_EXECUTABLE': '0'})
        stages = {v: serial.Stage(name + '-' + v, spec['binary'], settings=protocol['settings'])
                  for v, spec in variants.items()}
        state.update(status='full_native_old_training', rows=[],
                     selection_sha256=serial.sha(serial.RAW / 'training17-development-selection.json'),
                     auxiliary_snapshot_policy=freeze['native_audit_neutral_extra'])
        serial.save(record, state)
    limit = min(state['started_epoch'] + state['calibration_limit_s'],
                serial.DEADLINE - protocol['reserve_s'])
    for task in protocol['tasks']:
        for population in (1024, 16384):
            reference = None
            for variant in ('baseline', 'candidate'):
                if time.time() >= limit - 5:
                    raise RuntimeError('calibration limit reached; retain completed audits and all attempts')
                manifest = serial.read(serial.RAW / f'combined7-p{population}-{variant}' / 'manifest.json')
                assert manifest['status'] == 'requested_phase_complete'
                search = dict(manifest['jobs'][f'{task}.p{population}.s0.g8'], repetition=0)
                assert search['status'] == 'ok'
                current = dict(freeze, full_audit_timeout_s=max(1, limit-time.time()))
                row = native.run(stages[variant], search, protocol['inputs'][task], current, reference)
                state['rows'].append(dict(task=task, population=population, variant=variant,
                    status=row['status'], process_s=row.get('process_s'),
                    native_controller_cpu_s=row.get('native_controller_cpu_s'),
                    elapsed_s=row.get('native_call_elapsed_s_including_lock_wait'),
                    actual_cpu_programs=row.get('native_cpu_executed_programs'),
                    cpu_reference_verified=row.get('cpu_reference_verified', False),
                    job_tag=f'{task}.p{population}.{variant}.final.full-native'))
                serial.save(record, state)
                assert row['status'] == 'ok' and row['native_fidelity_pass'], 'native calibration failed; retain evidence'
                if variant == 'candidate':
                    assert row.get('cpu_reference_verified') and row['native_cpu_executed_programs'] == 0
                else:
                    reference = row
    for path, digest in state['harness_sha256'].items():
        assert serial.sha(path) == digest
    for stage in stages.values():
        stage.m.update(status='requested_phase_complete', finished_epoch=time.time())
        stage.persist()
    state.update(status='requested_phase_complete', finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped', error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
    raise
finally:
    serial.save(record, state)
