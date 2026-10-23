"""Verify the empty startup failure and distinguish child versus controller costs."""
import math

import serial


def same_repaired_scope(speed, quality, parent_speed, parent_quality):
    for field in ('started_epoch', 'deadline_epoch', 'settings', 'inputs', 'tasks', 'variants', 'resources', 'reserve_s',
                  'candidate_selection', 'candidate_source_sha256', 'candidate_source_snapshot'):
        if not speed[field] == quality[field] == parent_speed[field] == parent_quality[field]:
            raise ValueError('startup recovery changed the selected scope')
    for field in ('populations', 'generations', 'repetitions', 'cases'):
        if speed[field] != parent_speed[field]:
            raise ValueError('startup recovery changed the speed grid')
    for field in ('population', 'generations', 'seeds', 'data_seed', 'train_limit', 'test_limit'):
        if quality[field] != parent_quality[field]:
            raise ValueError('startup recovery changed the quality grid')


def recovery(speed, quality, evidence):
    info = speed.get('empty_parent_recovery')
    if info is None:
        return None
    if info != quality.get('empty_parent_recovery') or info['parent_attempt'] != 'best-effort0' or info['parent_cohort_empty'] is not True:
        raise ValueError('invalid empty startup recovery record')
    parent_speed = evidence.checked(serial.RAW / 'best-effort0-speed-freeze.json', info['parent_speed_freeze_sha256'])
    parent_quality = evidence.checked(serial.RAW / 'best-effort0-quality-freeze.json', info['parent_quality_freeze_sha256'])
    same_repaired_scope(speed, quality, parent_speed, parent_quality)
    if (parent_quality['first_heldout_use'] != 'after immutable best-effort candidate/full-grid freeze' or
            quality['first_heldout_use'] != 'after immutable candidate/scale freeze'):
        raise ValueError('the recorded startup marker correction differs')
    paths = [serial.RAW / (name + '-controller.json') for name in ('best-effort0', 'best-effort0-quality')]
    paths.append(serial.RAW / 'best-effort0-quality-inputs' / 'manifest.json')
    controls = [evidence.checked(path, serial.sha(path)) for path in paths]
    if any(control['status'] != 'stopped' for control in controls[:2]):
        raise ValueError('parent startup failure is not preserved')
    cohort = controls[2]
    if cohort['jobs'] or cohort.get('inputs'):
        raise ValueError('an executed cohort cannot be silently replaced')
    folder = paths[-1].parent
    if list(folder.rglob('pool.json')) or list(folder.rglob('*.source.json')):
        raise ValueError('parent attempt accessed data before the metadata repair')
    return dict(verified=True, parent_attempt=info['parent_attempt'], parent_jobs=0,
                parent_status='stopped_before_any_dataset_or_sample', scope_unchanged=True,
                corrected_field='first_heldout_use', original_freeze_hashes_preserved=True)


def native_costs(rows):
    elapsed = []
    controller = []
    for row in rows:
        for key, values in [('native_call_elapsed_s_including_lock_wait', elapsed),
                            ('native_controller_cpu_s', controller)]:
            value = row.get(key)
            if value is None:
                continue
            if not math.isfinite(value) or value < 0:
                raise ValueError('invalid recorded native controller cost')
            values.append(value)
    return dict(expected_calls=len(rows), elapsed_recorded_calls=len(elapsed),
                known_elapsed_s=math.fsum(elapsed), controller_recorded_calls=len(controller),
                known_controller_cpu_s=math.fsum(controller),
                scope='native call wall includes child, Python/JSON and lock wait; controller CPU may overlap child wall and must not be added to it; cost coverage is not audit success')
