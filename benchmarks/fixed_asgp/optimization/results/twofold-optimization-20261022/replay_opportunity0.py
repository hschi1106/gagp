"""Measure existing replay event cost in COMPLETE C2 development outputs; no new searches."""
import fcntl
import math
import os
from pathlib import Path
import time
import traceback

import serial

out = serial.RAW / 'replay-opportunity0'
out.mkdir()
state = dict(pid=os.getpid(), status='waiting_for_lock', started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
             scope='all30/bothP/C1024/G8/complete C2 candidate old TRAINING outputs only; timing diagnosis, no new candidate/data/search')
serial.save(out / 'manifest.json', state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        assert time.time() < serial.DEADLINE - 10800
        tasks = serial.read(serial.BASE / 'protocol.json')['tasks']
        rows = []
        evidence = {}
        for population in (1024, 16384):
            path = serial.RAW / f'combined2-p{population}-candidate/manifest.json'
            manifest = serial.read(path)
            assert manifest['status'] == 'requested_phase_complete'
            evidence[str(path)] = serial.sha(path)
            for task in tasks:
                job = manifest['jobs'][f'{task}.p{population}.s0.g8']
                assert job['status'] == 'ok' and serial.sha(job['output']) == job['output_sha256']
                data = serial.read(job['output'])
                assert data['population'] == population and data['cases'] == 1024 and len(data['generations']) == 8
                events = data['generations']
                wall = data['evolve_wall_ms'] / 1000
                evaluation = math.fsum(g['eval_ms'] for g in events) / 1000
                replay = math.fsum(g['replay_kernel_event_ms'] for g in events) / 1000
                assert 0 <= replay <= evaluation + .05 and evaluation <= wall + .05
                rows.append(dict(task=task, population=population, search_s=wall, eval_s=evaluation,
                    replay_event_s=replay, replay_fraction_of_search=replay / wall,
                    replay_programs=sum(g['payload_replay_programs'] for g in events),
                    replay_cases=sum(g['payload_replay_cases'] for g in events),
                    zero_replay_event_scenario=wall / (wall - replay) if wall > replay else None))
                evidence[job['output']] = job['output_sha256']
        assert len(rows) == 60
        scenarios = {str(p): math.exp(math.fsum(math.log(r['zero_replay_event_scenario']) for r in rows
                     if r['population'] == p) / 30) for p in (1024, 16384)}
        scenarios['pooled'] = math.exp(math.fsum(math.log(r['zero_replay_event_scenario']) for r in rows) / 60)
        state.update(status='ok', rows=rows, zero_replay_event_scenario=scenarios, evidence_sha256=evidence,
                     helper_sha256=serial.sha(Path(__file__)), finished_epoch=time.time(),
                     caveat='GPU event components overlap enclosing evaluation/search wall; never add event durations to wall/API waits. Removing all replay events is an optimistic unmeasured scenario, not an attainable improvement or rigorous bound. Physical-allocator benefit requires a separate semantic/provenance/capacity proof and new unprofiled searches.')
except BaseException as error:
    state.update(status='failed', error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
    raise
finally:
    serial.save(out / 'manifest.json', state)
