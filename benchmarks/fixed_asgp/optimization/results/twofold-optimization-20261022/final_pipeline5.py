"""Keep active TRAINING16; after corrected gate7, reuse it only for the identical recipe."""
import os
from pathlib import Path
import subprocess
import time
import traceback

import best_selection_private_adjacent1_recipe
import freeze_and_run_shared
import serial


def exited(data):
    for key in ('pid', 'child_pid'):
        proc = Path(f'/proc/{data.get(key)}/stat')
        if proc.exists() and proc.read_text().split()[2] != 'Z':
            return False
    return True


def same_recipe(a, b):
    return a['binary_sha256'] == b['binary_sha256'] and a['extra'] == b['extra']


name = 'final-pipeline5'
record = serial.RAW / (name + '-controller.json')
assert not record.exists()
paths = [Path(__file__)] + [serial.BASE / p for p in (
    'best_selection_private_adjacent1_recipe.py', 'training_best_private_adjacent1_recipe.py',
    'training_timing12.py', 'preparation_timing10.py', 'freeze_and_run_shared.py')]
state = dict(pid=os.getpid(), status='waiting_for_corrected_full30', started_epoch=time.time(),
             deadline_epoch=serial.DEADLINE, harness_sha256={str(p): serial.sha(p) for p in paths},
             scope='old TRAINING16 and its costs continue untouched; use them only if chosen immutable binary and global options are identical; otherwise fresh TRAINING17; no heldout before real freeze')
serial.save(record, state)


def wait_controller(component, allow_stopped=False):
    path = serial.RAW / (component + '-controller.json')
    while True:
        data = serial.read(path) if path.exists() else {}
        if data.get('status') in ('failed', 'stopped') and not allow_stopped:
            raise RuntimeError('required controller failed: ' + component)
        if data.get('status') in ('ok', 'requested_phase_complete', 'stopped') and exited(data):
            return data
        if time.time() > serial.DEADLINE - 10800:
            raise RuntimeError('final prerequisites exceeded original reserve: ' + component)
        time.sleep(10)


try:
    wait_controller('combined7')
    wait_controller('training16')
    wait_controller('training-timing11')
    wait_controller('preparation-timing9')
    selected = best_selection_private_adjacent1_recipe.select()
    old = serial.read(serial.RAW / 'training16-development-selection.json')
    decision_path = serial.RAW / 'late-training-selection0.json'
    assert not decision_path.exists()
    reuse = same_recipe(selected['selected'], old['selected'])
    serial.save(decision_path, dict(epoch=time.time(), selected=selected, previous_training=old,
        exact_recipe_unchanged=reuse, scope=state['scope'], heldout_opened=False))
    if reuse:
        training, timing, preparation = 'training16', 'training-timing11', 'preparation-timing9'
        state.update(status='reusing_identical_old_training', decision=str(decision_path), training=training)
        serial.save(record, state)
    else:
        state.update(status='fresh_training17_for_new_winner', decision=str(decision_path))
        serial.save(record, state)
        commands = [
            ('training17', ['python3', str(serial.BASE / 'training_best_private_adjacent1_recipe.py'),
                            '--name', 'training17', '--prior-name', 'combined7', '--build-name', 'build-attempt22']),
            ('training-timing12', ['python3', str(serial.BASE / 'training_timing12.py')]),
            ('preparation-timing10', ['python3', str(serial.BASE / 'preparation_timing10.py')])]
        children = {}
        for component, command in commands:
            assert not (serial.RAW / (component + '-controller.json')).exists()
            with (serial.RAW / (component + '.log')).open('x') as log:
                child = subprocess.Popen(command, cwd=serial.ROOT, env=dict(os.environ, PYTHONPATH='tools'),
                                         stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            children[component] = dict(pid=child.pid, command=command)
        state['new_training_children'] = children
        serial.save(record, state)
        for component in children:
            wait_controller(component)
        training, timing, preparation = 'training17', 'training-timing12', 'preparation-timing10'
    for component in ('coarse-gate0', 'combined1', 'combined2', 'combined3', 'combined4', 'combined5', 'combined6', 'combined7',
                      'fused-cold-admission1', 'shared-cpu-smoke0'):
        wait_controller(component)
    for component in ('private-launch0', 'projection-simple0', 'view-geometry1', 'profile-shape19-0',
                      'profile-parent21-0', 'profile-cold19-0', 'cell-soa0', 'short-edit0', 'private-adjacent0', 'private-adjacent1'):
        wait_controller(component, allow_stopped=True)
    for component in ('harness-checks-attempt26', 'reference-reader-smoke0', 'provenance-build22', 'resources-build22'):
        path = serial.RAW / component / 'manifest.json'
        while True:
            data = serial.read(path) if path.exists() else {}
            if data.get('status') == 'failed':
                raise RuntimeError('required proof failed: ' + component)
            if data.get('status') == 'ok' and exited(data):
                break
            if time.time() > serial.DEADLINE - 10800:
                raise RuntimeError('proof exceeded original reserve')
            time.sleep(10)
    for path, digest in state['harness_sha256'].items():
        assert serial.sha(path) == digest
    state.update(status='freezing_from_actual_training_costs', training=training, timing=timing, preparation=preparation)
    serial.save(record, state)
    freeze_and_run_shared.main('formal0', training, timing, preparation,
        ['combined1', 'combined2', 'combined3', 'combined4', 'combined5', 'combined6', 'combined7', 'stage1', 'shape1', 'threads0',
         'repro-block0', 'parent-cache0', 'fused-cold0', 'coarse-gate0', 'profile-shape19-0', 'profile-parent21-0', 'profile-cold19-0'])
    result = serial.read(serial.RAW / 'formal0-controller.json')
    state.update(status=result['status'], finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped', error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
    raise
finally:
    serial.save(record, state)
