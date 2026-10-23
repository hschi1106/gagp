"""Independent frozen pool pilot after build16; no duplicate prior samples."""
import fcntl
import os
from pathlib import Path
import time
import traceback

import pool_pilot
import serial
import study_statistics

name='pool4';record=serial.RAW/(name+'-controller.json')
lease=(serial.RAW/(name+'-controller.lock')).open('a');fcntl.flock(lease,fcntl.LOCK_EX|fcntl.LOCK_NB)
assert not record.exists()
paths=[Path(__file__),serial.BASE/'pool_pilot.py',serial.BASE/'vm_pilot.py',
       serial.BASE/'serial.py',serial.BASE/'study_statistics.py',serial.BASE/'protocol.json']
state=dict(pid=os.getpid(),status='waiting_for_build16',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
    harness_sha256={str(p):serial.sha(p) for p in paths},
    rule='one fixed case-only upstream setting from completed old-training all30 evidence; all four arms on all30 bothP; no fresh heldout')
serial.save(record,state)
try:
    while True:
        path=serial.RAW/'build-attempt16/manifest.json';build=serial.read(path) if path.exists() else {}
        candidate_path=serial.RAW/'pool-corrected2-controller.json';candidate=serial.read(candidate_path) if candidate_path.exists() else {}
        if build.get('status')=='failed' or candidate.get('status')=='stopped':raise RuntimeError('pool build/check prerequisite failed; preserve attempt')
        if build.get('status')=='ok':break
        if time.time()>serial.DEADLINE-10800:raise RuntimeError('pool build prerequisite missed budget')
        time.sleep(10)
    for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest,'frozen pool pilot source changed'
    assert serial.sha(build['binary'])==build['binary_sha256']
    protocol=serial.read(serial.BASE/'protocol.json')
    for population in (1024,16384):
        state.update(status=f'pilot{population}',binary=build['binary'],binary_sha256=build['binary_sha256']);serial.save(record,state)
        pool_pilot.run(build['binary'],f'{name}-p{population}',[population],protocol['tasks'])
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        rows=sum([serial.read(serial.RAW/f'{name}-p{p}-comparisons.json') for p in (1024,16384)],[])
        means={}
        for variant in ('before','pool','pool_reuse'):
            group=[r for r in rows if r['variant']==variant]
            assert len(group)==60 and all(all(r[k] for k in ('final_descriptors_equal','full_final_ast_equal','champion_ast_fitness_equal')) for r in group)
            means[variant]=study_statistics.geometric_mean(r['speedup'] for r in group)
        serial.save(serial.RAW/'pool4-development-summary.json',dict(status='ok',
            geometric_means=means,selected=max(means,key=means.get),
            scope='single-repeat all30 development only; not formal target attainment',epoch=time.time()))
    state.update(status='requested_phase_complete',finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
    raise
finally:serial.save(record,state)
