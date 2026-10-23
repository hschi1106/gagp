"""Resume only never-started offline audits after verified controller loss.

No search, failed audit, or interrupted audit is rerun. Original manifests and
controllers are archived before explicit serial.Stage(resume=True).
"""
from argparse import ArgumentParser
import copy
import fcntl
import os
from pathlib import Path
import shutil
import time
import traceback

import serial
import full_population_audit_shared_policy as audit


def old_processes_absent(documents):
    pids = set()
    for doc in documents:
        for key in ('pid', 'controller_pid', 'child_pid'):
            if doc.get(key): pids.add(doc[key])
        for job in doc.get('jobs', {}).values():
            if job.get('status') in ('running', 'waiting') and job.get('pid'):
                pids.add(job['pid'])
    for pid in pids:
        assert not Path(f'/proc/{pid}').exists(), ('previous process still present', pid)
    return sorted(pids)


def main(resume):
    assert resume, 'explicit --resume is required'
    name = 'best-effort1-speed'
    failed = serial.read(serial.RAW / 'native-resume1-controller.json')
    assert failed['status'] == 'stopped'
    old_processes_absent([failed])
    record = serial.RAW / 'native-resume2-controller.json'
    assert not record.exists(), 'preserve prior recovery attempt'
    leases = []
    for suffix in ('best-effort1', name):
        lease = (serial.RAW / (suffix + '-controller.lock')).open('a')
        fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
        leases.append(lease)
    paths = [serial.RAW / (s + '-controller.json') for s in
             ('best-effort1', 'best-effort1-quality', name)]
    paths += [serial.RAW / (name + '-full-native-' + v) / 'manifest.json'
              for v in ('baseline', 'candidate')]
    old = [serial.read(p) for p in paths]
    absent = old_processes_absent(old)
    archive = serial.RAW / 'native-resume2-preserved'
    assert not archive.exists()
    archive.mkdir()
    proofs = {}
    for p in paths:
        target = archive / (p.parent.name + '--' + p.name)
        shutil.copy2(p, target)
        proofs[str(target)] = serial.sha(target)
    freeze_path = serial.RAW / (name + '-freeze.json')
    freeze = serial.read(freeze_path)
    assert serial.sha(freeze_path) == old[2]['freeze_sha256']
    assert freeze['deadline_epoch'] == serial.DEADLINE
    for path, digest in freeze['harness_sha256'].items():
        assert serial.sha(path) == digest, ('frozen harness changed', path)
    state = dict(pid=os.getpid(), status='resuming_never_started_native_audits',
                 started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
                 freeze=str(freeze_path), freeze_sha256=serial.sha(freeze_path),
                 prior_processes_verified_absent=absent, preserved_sha256=proofs,
                 failed_startup_recovery=str(serial.RAW / 'native-resume1-controller.json'),
                 correction='copy old speed state before assigning new PID; attempt1 launched no audit',
                 cause='controller and child absent; no terminal log; cause unproven',
                 policy='never rerun any existing audit or search; preserve interrupted failure',
                 harness_sha256={str(Path(__file__).resolve()): serial.sha(__file__)})
    serial.save(record, state)
    speed_state = copy.deepcopy(old[2])
    speed_state.update(pid=os.getpid(), recovery=str(record),
                       prior_pid=old[2]['pid'], status='resuming_native_audits')
    serial.save(paths[2], speed_state)
    old[0].update(status='stopped', finished_epoch=time.time(),
                  error='controller disappeared without terminal record; see native-resume2-controller.json')
    serial.save(paths[0], old[0])
    stages = {}
    try:
        with serial.GPU_LOCK.open('a') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            old_processes_absent(old)
            for variant, spec in freeze['variants'].items():
                stage = serial.Stage(name + '-full-native-' + variant,
                                     spec['binary'], settings=freeze['settings'], resume=True)
                for tag, job in stage.m['jobs'].items():
                    if job['status'] == 'interrupted':
                        # Existing process_s is only a last-observed lower bound.
                        task, p, _, _ = tag.split('.', 3)
                        job.update(task=task, population=int(p[1:]), variant=variant,
                                   seed=0, repetition=0, cases=1024,
                                   interrupted_observed_epoch=time.time(),
                                   process_s_is_lower_bound=True,
                                   native_execution_coverage_unknown=True,
                                   recovery=str(record), native_fidelity_pass=False)
                stage.persist()
                stages[variant] = stage
        searches = {v: serial.read(serial.RAW / (name + '-' + v) / 'manifest.json')
                    for v in ('baseline', 'candidate')}
        for task in freeze['tasks']:
            for population in freeze['populations']:
                reference = None
                search_tag = f'{task}.p{population}.s0.g{freeze["generations"]}.r0'
                for variant in ('baseline', 'candidate'):
                    tag = f'{task}.p{population}.{variant}.final.full-native'
                    stage = stages[variant]
                    if tag in stage.m['jobs']:
                        result = stage.m['jobs'][tag]
                    else:
                        result = audit.run(stage, searches[variant]['jobs'][search_tag],
                                           freeze['inputs'][task], freeze, reference)
                    if variant == 'baseline': reference = result
        jobs = [j for s in stages.values() for j in s.m['jobs'].values()]
        for stage in stages.values():
            stage.m.update(status='requested_phase_complete', freeze=str(freeze_path),
                           freeze_sha256=serial.sha(freeze_path), finished_epoch=time.time())
            stage.persist()
        state.update(status='requested_phase_complete', finished_epoch=time.time(),
                     full_native_audits_expected=120,
                     full_native_audits_ok=sum(j['status']=='ok' and j.get('native_fidelity_pass',False) for j in jobs),
                     full_native_audits_pass=len(jobs)==120 and all(j['status']=='ok' and j.get('native_fidelity_pass',False) for j in jobs))
        speed_state.update(**{k: state[k] for k in ('status','finished_epoch','full_native_audits_expected','full_native_audits_ok','full_native_audits_pass')})
    except BaseException as error:
        state.update(status='stopped', error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        speed_state.update(status='stopped', error=repr(error), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)
        serial.save(paths[2], speed_state)


if __name__ == '__main__':
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--resume', action='store_true')
    main(parser.parse_args().resume)
