"""After complete current native cost measurements, retry only the budget forecast.

No formal0 search/freeze existed. The new formal1 uses the same selected
immutable GPU binary and the unchanged global scale/safety/reserve rule.
"""
import os
from pathlib import Path
import time
import traceback

import freeze_and_run_measured_native
import serial


name = 'final-pipeline6'
record = serial.RAW / (name + '-controller.json')
assert not record.exists()
paths = [Path(__file__)] + [serial.BASE / (module + '.py') for module in (
    'freeze_and_run_measured_native', 'forecast_measured_native', 'formal_shared_policy',
    'full_population_audit_shared_policy', 'freeze_rule', 'serial')]
state = dict(pid=os.getpid(), status='waiting_for_complete_current_native_costs',
             started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
             harness_sha256={str(p): serial.sha(p) for p in paths},
             scope='formal0 budget refusal preserved; complete new oldTRAINING cost evidence only; same scale rule and immutable GPU recipe; all formal references fresh')
serial.save(record, state)
try:
    for previous in ('final-pipeline5', 'formal0'):
        prior = serial.read(serial.RAW / (previous + '-controller.json'))
        assert prior['status'] == 'budget_insufficient'
        proc = Path(f'/proc/{prior["pid"]}/stat')
        assert not proc.exists() or proc.read_text().split()[2] == 'Z'
    assert not list(serial.RAW.glob('formal0-*-freeze.json'))
    assert not (serial.RAW / 'formal1-controller.json').exists()
    while True:
        prior = serial.read(serial.RAW / 'development-native-cost0-controller.json')
        proc = Path(f'/proc/{prior["pid"]}/stat')
        exited = not proc.exists() or proc.read_text().split()[2] == 'Z'
        if prior['status'] == 'stopped':
            raise RuntimeError('current native cost attempt stopped; preserve all attempts')
        if prior['status'] == 'requested_phase_complete' and exited:
            break
        if exited:
            raise RuntimeError('native cost controller disappeared without terminal evidence')
        if time.time() >= serial.DEADLINE - 10800:
            raise RuntimeError('original report reserve reached before cost completion')
        time.sleep(10)
    while True:
        checks = serial.read(serial.RAW / 'harness-checks-attempt32' / 'manifest.json')
        if checks['status'] == 'failed':
            raise RuntimeError('new harness guards failed; preserve the attempt')
        proc = Path(f'/proc/{checks["pid"]}/stat')
        if checks['status'] == 'ok' and (not proc.exists() or proc.read_text().split()[2] == 'Z'):
            break
        if time.time() >= serial.DEADLINE - 10800:
            raise RuntimeError('original report reserve reached before guards completed')
        time.sleep(10)
    for path, digest in state['harness_sha256'].items():
        assert serial.sha(path) == digest
    state['status'] = 'forecasting_from_complete_current_native_costs'
    serial.save(record, state)
    freeze_and_run_measured_native.main('formal1', 'training17', 'training-timing12',
                                      'preparation-timing10', ['combined7', 'development-native-cost0'])
    result = serial.read(serial.RAW / 'formal1-controller.json')
    state.update(status=result['status'], finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped', error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
    raise
finally:
    serial.save(record, state)
