"""Prove that controller recovery retained every prior audit attempt."""
import copy

import serial


INTERRUPTED_FIELDS = {'status','task','population','variant','seed','repetition','cases',
    'interrupted_observed_epoch','process_s_is_lower_bound','native_execution_coverage_unknown',
    'recovery','native_fidelity_pass'}


def retained_jobs(before, after, recovery_epoch):
    """Validate preserved results and prohibit hidden reruns of an existing tag."""
    interrupted = []
    for tag, old in before.items():
        if tag not in after:
            raise ValueError('recovery dropped a prior attempt')
        current = after[tag]
        if old['status'] != 'running':
            if current != old:
                raise ValueError('recovery replaced a prior terminal attempt')
            continue
        preserved = {key: value for key, value in current.items() if key not in INTERRUPTED_FIELDS}
        original = {key: value for key, value in old.items() if key not in INTERRUPTED_FIELDS}
        if preserved != original or current.get('status') != 'interrupted':
            raise ValueError('interrupted attempt was rerun or changed')
        if (current.get('native_fidelity_pass') is not False or
                current.get('native_execution_coverage_unknown') is not True or
                current.get('process_s_is_lower_bound') is not True):
            raise ValueError('unknown interrupted audit was promoted')
        interrupted.append(tag)
    for tag, job in after.items():
        if tag not in before and 'started_epoch' in job and job['started_epoch'] < recovery_epoch:
            raise ValueError('new recovery job predates recovery')
    return dict(prior_attempts=len(before), prior_terminal_attempts=len(before)-len(interrupted),
                retained_interrupted_tags=interrupted, newly_recorded_attempts=len(after)-len(before))


def verify(evidence):
    path = serial.RAW / 'native-resume2-controller.json'
    if not path.exists():
        return None
    current = evidence.checked(path, serial.sha(path))
    if current['deadline_epoch'] != serial.DEADLINE:
        raise ValueError('recovery changed the deadline')
    for source, digest in current['harness_sha256'].items():
        if serial.sha(source) != digest:
            raise ValueError('preserved recovery harness changed')
        if str(source) in evidence.hashes and evidence.hashes[str(source)] != digest:
            raise ValueError('conflicting recovery harness hash')
        evidence.hashes[str(source)] = digest
    for preserved, digest in current['preserved_sha256'].items():
        evidence.checked(preserved, digest)
    first_path = serial.RAW / 'native-resume1-controller.json'
    first = evidence.checked(first_path, serial.sha(first_path))
    if first['status'] != 'stopped' or 'previous process still present' not in first['error']:
        raise ValueError('first recovery startup failure missing')
    for source, digest in first['harness_sha256'].items():
        if serial.sha(source) != digest:
            raise ValueError('preserved recovery harness changed')
        if str(source) in evidence.hashes and evidence.hashes[str(source)] != digest:
            raise ValueError('conflicting recovery harness hash')
        evidence.hashes[str(source)] = digest
    for preserved, digest in first['preserved_sha256'].items():
        evidence.checked(preserved, digest)
    freeze = evidence.checked(current['freeze'], current['freeze_sha256'])
    if current['freeze_sha256'] != first['freeze_sha256']:
        raise ValueError('recovery changed the speed freeze')
    observation_path = serial.RAW / 'timing-complete-observation0.json'
    observation = evidence.checked(observation_path, serial.sha(observation_path))
    for stage, digest in observation['timing_stage_sha256'].items():
        evidence.checked(serial.ROOT / stage, digest)
    rows = []
    for variant in ('baseline', 'candidate'):
        name = 'best-effort1-speed-full-native-' + variant
        filename = name + '--manifest.json'
        original = evidence.checked(serial.RAW / 'native-resume1-preserved' / filename,
                                    serial.sha(serial.RAW / 'native-resume1-preserved' / filename))
        second = evidence.checked(serial.RAW / 'native-resume2-preserved' / filename,
                                  serial.sha(serial.RAW / 'native-resume2-preserved' / filename))
        if second != original:
            raise ValueError('first recovery ran or changed an audit before failing')
        latest = evidence.checked(serial.RAW / name / 'manifest.json', serial.sha(serial.RAW / name / 'manifest.json'))
        if latest['settings'] != original['settings'] or latest['binary_sha256'] != original['binary_sha256']:
            raise ValueError('native recovery changed binary/settings')
        if latest['binary_sha256'] != freeze['variants'][variant]['binary_sha256']:
            raise ValueError('native recovery binary differs from freeze')
        rows.append(dict(variant=variant, **retained_jobs(original['jobs'], latest['jobs'], current['started_epoch'])))
    return dict(verified=True, cause_proven=False, original_search_manifests_unchanged=True,
                freeze_unchanged=True, no_existing_audit_rerun=True, first_recovery_started_no_audits=True,
                rows=rows, recovery=copy.deepcopy(current),
                scope='interrupted rows remain unknown; successful sibling audits do not replace missing evidence')
