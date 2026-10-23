"""Admitted20 private launch bounds, one global TRAINING choice then full30."""
import fcntl
import os
from pathlib import Path
import time
import traceback

import serial
import private_launch_pilot as helper
import study_statistics

name='private-launch0';record=serial.RAW/(name+'-controller.json');assert not record.exists()
paths=[Path(__file__),serial.BASE/'private_launch_pilot.py',serial.BASE/'stage_pilot.py',serial.BASE/'vm_pilot.py',
       serial.BASE/'serial.py',serial.BASE/'study_statistics.py',serial.BASE/'protocol.json']
state=dict(pid=os.getpid(),status='waiting_for_build20',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
    harness_sha256={str(p):serial.sha(p) for p in paths},
    rule='fixed8developmenttasks/bothP; require2% overordinary for global512/1024, otherwise0; full30 bothP candidate/freshbaseline; training only')
serial.save(record,state)
try:
    while True:
        path=serial.RAW/'build-attempt20/manifest.json';build=serial.read(path) if path.exists() else {}
        prior=serial.read(serial.RAW/'private-launch-candidate0-controller.json')
        if prior['status']=='stopped' or build.get('status')=='failed':raise RuntimeError('build20 not admitted; preserve failure')
        if prior['status']=='ok' and build.get('status')=='ok':break
        if time.time()>serial.DEADLINE-10800:raise RuntimeError('compilation prerequisite missed budget')
        time.sleep(10)
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest
        assert serial.sha(build['binary'])==build['binary_sha256']
        protocol=serial.read(serial.BASE/'protocol.json')
    tasks=protocol['development']['tasks']
    state['status']='coarse';serial.save(record,state)
    helper.run(build['binary'],name+'-coarse',[1024,16384],tasks,'coarse',pool_mode=0)
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        rows=serial.read(serial.RAW/(name+'-coarse-comparisons.json'));means={}
        expected={(task,p) for task in tasks for p in (1024,16384)}
        for variant in helper.VARIANTS[1:]:
            group=[r for r in rows if r['variant']==variant]
            assert len(group)==16 and {(r['task'],r['population']) for r in group}==expected
            assert all(all(r[k] for k in ('final_descriptors_equal','full_final_ast_equal','champion_ast_fitness_equal')) for r in group)
            means[variant]=study_statistics.geometric_mean(r['speedup'] for r in group)
        selected=max(means,key=means.get)
        if means[selected]<means['ordinary']*1.02:selected='ordinary'
        selection=dict(selected=selected,geometric_means=means,extra=helper.execution_settings(selected),
            binary=build['binary'],binary_sha256=build['binary_sha256'],
            scope='one global launch compilation setting from old TRAINING; physical32/logical512 unchanged; no heldout',epoch=time.time())
        serial.save(serial.RAW/(name+'-development-selection.json'),selection)
    for population in (1024,16384):
        state['status']=f'all30_p{population}';serial.save(record,state)
        helper.run(build['binary'],f'{name}-p{population}',[population],protocol['tasks'],selected,pool_mode=0)
    state.update(status='requested_phase_complete',selection=selection,finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
finally:serial.save(record,state)
