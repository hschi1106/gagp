"""Bound the old-training exact-AST opportunity; do not alter execution/reuse rules."""
from collections import Counter
import fcntl
import json
import os
from pathlib import Path
import time
import traceback

import serial


def origin_fields(value):
    if isinstance(value, dict):
        return sum(('origin' in key.lower()) + origin_fields(item) for key, item in value.items())
    if isinstance(value, list):
        return sum(origin_fields(item) for item in value)
    return 0


def summary(programs):
    keys = [json.dumps(p, sort_keys=True, separators=(',', ':')) for p in programs]
    counts = Counter(keys)
    return dict(population=len(programs), unique_complete_ast=len(counts),
                duplicate_members=len(programs) - len(counts),
                maximum_identical_copies=max(counts.values()),
                grammar_origin_fields=sum(origin_fields(p) for p in programs))


out = serial.RAW / 'execution-identity-diagnosis0'
out.mkdir()
state = dict(pid=os.getpid(), status='waiting_for_lock', started_epoch=time.time(),
             deadline_epoch=serial.DEADLINE,
             scope='fixed development8/P1024/old TRAINING initial and completed combined1 G8 exports; no fresh seed/data/search, no candidate selection or cache change')
serial.save(out / 'manifest.json', state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if time.time() >= serial.DEADLINE - 10800:
            raise RuntimeError('diagnosis exceeded original report reserve')
        protocol = serial.read(serial.BASE / 'protocol.json')
        prior_path = serial.RAW / 'initial-duplicate-diagnosis0/manifest.json'
        prior = serial.read(prior_path)
        assert prior['status'] == 'ok'
        previous = {r['task']: r for r in prior['rows']}
        manifest_path = serial.RAW / 'combined1-p1024-candidate/manifest.json'
        manifest = serial.read(manifest_path)
        assert manifest['status'] == 'requested_phase_complete'
        evidence = {str(prior_path): serial.sha(prior_path), str(manifest_path): serial.sha(manifest_path)}
        rows = []
        for task in protocol['development']['tasks']:
            source = protocol['inputs'][task]['prefixes']['1024']
            assert serial.sha(source['prepared']) == source['sha256']
            initial = serial.read(source['prepared'])['programs']
            start = summary(initial)
            assert start['population'] == 1024
            assert start['duplicate_members'] == previous[task]['duplicate_members']
            job = manifest['jobs'][f'{task}.p1024.s0.g8']
            assert job['status'] == 'ok' and serial.sha(job['export']) == job['export_sha256']
            final_document = serial.read(job['export'])
            # The native population export is an array of complete ASTs.
            assert isinstance(final_document, list), 'inspect actual export schema rather than dropping fields'
            final = summary(final_document)
            assert final['population'] == 1024
            evidence.update({source['prepared']: source['sha256'], job['export']: job['export_sha256']})
            rows.append(dict(task=task, population=1024, generations=8, initial=start, final=final,
                initial_only_ideal_evaluation_factor=1024 / start['unique_complete_ast']))
            print(rows[-1], flush=True)
        state.update(status='ok', rows=rows, input_sha256=evidence, helper_sha256=serial.sha(Path(__file__)),
                     finished_epoch=time.time(),
                     caveat='exact AST duplication is diagnostic only, not reusable-score proof; initial-only factor ignores hashing/compilation/other generations and is not an end-to-end speed bound; origin-free executable equality still requires full compiler/context/fuel proof')
except BaseException as error:
    state.update(status='failed', error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
    raise
finally:
    serial.save(out / 'manifest.json', state)
