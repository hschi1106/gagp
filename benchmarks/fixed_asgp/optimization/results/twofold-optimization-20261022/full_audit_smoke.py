"""Admit the full-population audit on existing four-task TRAINING searches."""
import fcntl
import os
from pathlib import Path
import time
import traceback

import elite_pilot
import full_population_audit
import serial

name='full-population-audit-smoke0';record=serial.RAW/(name+'-controller.json');assert not record.exists()
paths=[Path(__file__),serial.BASE/'full_population_audit.py',serial.BASE/'champion_audit_dedup.py',
    serial.BASE/'elite_pilot.py',serial.BASE/'vm_pilot.py',serial.BASE/'serial.py',serial.BASE/'protocol.json']
state=dict(pid=os.getpid(),status='waiting_for_lock',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
    harness_sha256={str(p):serial.sha(p) for p in paths},scope='old TRAINING, full1024 final members; no new search or heldout')
serial.save(record,state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest
        build=serial.read(serial.RAW/'build-attempt18/manifest.json');assert build['status']=='ok'
        assert serial.sha(build['binary'])==build['binary_sha256']
        protocol=serial.read(serial.BASE/'protocol.json')
        stage=serial.Stage(name,build['binary'],settings=protocol['settings'])
        stage.m.update(scope=state['scope']);stage.persist()
        previous=serial.read(serial.RAW/'simple1-p1024-baseline/manifest.json')
    state['status']='auditing';serial.save(record,state)
    freeze=dict(variants={'candidate':dict(extra=elite_pilot.execution_settings('elite_full128','full',128))},reserve_s=protocol['reserve_s'])
    for task in ('sum_of_elements','is_sorted','filter_odd','house_robber'):
        original=previous['jobs'][task+'.p1024.s0.g8']
        job=dict(original,task=task,population=1024,seed=0,repetition=0,variant='candidate')
        result=full_population_audit.run(stage,job,protocol['inputs'][task],freeze)
        if result['status']!='ok':raise RuntimeError('full population fidelity smoke failed: '+task)
    stage.m.update(status='requested_phase_complete',finished_epoch=time.time());stage.persist()
    state.update(status='ok',finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
    raise
finally:serial.save(record,state)
