"""Check the champion-audit harness on existing development TRAINING only."""
import fcntl
import os
from pathlib import Path
import time
import traceback

import champion_audit_dedup as champion_audit
import serial
import vm_pilot


name='champion-audit-dedup-smoke0'
record=serial.RAW/(name+'-controller.json')
lease=(serial.RAW/(name+'-controller.lock')).open('a')
fcntl.flock(lease,fcntl.LOCK_EX | fcntl.LOCK_NB)
assert not record.exists()
paths=[Path(__file__),serial.BASE/'champion_audit_dedup.py',serial.BASE/'serial.py',serial.BASE/'vm_pilot.py']
state=dict(status='waiting_for_build',pid=os.getpid(),started_epoch=time.time(),
           deadline_epoch=serial.DEADLINE,harness_sha256={str(p):serial.sha(p) for p in paths})
serial.save(record,state)
try:
    while True:
        path=serial.RAW/'build-attempt9/manifest.json'
        build=serial.read(path) if path.exists() else {}
        if build.get('status')=='failed':raise RuntimeError('candidate9 failed; diagnose before smoke')
        if build.get('status')=='ok':break
        if time.time()>serial.DEADLINE-10800:raise RuntimeError('smoke missed budget')
        time.sleep(10)
    for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest
    assert serial.sha(build['binary'])==build['binary_sha256']
    protocol=serial.read(serial.BASE/'protocol.json')
    stage=serial.Stage(name,build['binary'],settings=protocol['settings'])
    stage.m.update(scope='existing development training; no new data or search; native champion audit smoke')
    stage.persist()
    freeze=dict(variants={'candidate':dict(extra=vm_pilot.execution_settings('full','full',128))},
                reserve_s=protocol['reserve_s'])
    state['status']='auditing';serial.save(record,state)
    for task in ('sum_of_elements','is_sorted','filter_odd','house_robber'):
        with serial.GPU_LOCK.open('a') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX)
            source=protocol['inputs'][task]
            previous=serial.read(serial.RAW/'simple1-p1024-baseline/manifest.json')
            run=dict(previous['jobs'][f'{task}.p1024.s0.g8'],variant='candidate')
            inputs=dict(metadata=dict(train_count=1024),grammar=source['grammar'],
                seeds={'0':dict(source=source['source'],prepared=source['prefixes']['1024']['prepared'])})
        job=champion_audit.run(stage,run,'train',inputs,freeze)
        if job['status']!='ok':raise RuntimeError(f'{task} native champion audit failed: {job}')
        with serial.GPU_LOCK.open('a') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX)
            old_stage=serial.read(serial.RAW/'champion-audit-smoke0/manifest.json')
            old_job=old_stage['jobs'][f'{task}.s0.candidate.train.native'];assert old_job['status']=='ok'
            assert serial.sha(old_job['output'])==old_job['output_sha256']
            a=serial.read(old_job['output'])['programs'];b=serial.read(job['output'])['programs']
            fields=('fitness','cpu_fitness','wrong','cpu_wrong','errors','cpu_errors','timeouts','cpu_timeouts','cases','cpu_cases')
            assert len(a)==len(b)==9
            assert all(all(x[k]==y[k] for k in fields) for x,y in zip(a,b)),'deduplicated native audit differs from original full audit'
            job['original_full_audit_sha256']=old_job['output_sha256'];job['expanded_matches_original_full']=True;stage.persist()
    stage.m.update(status='requested_phase_complete',finished_epoch=time.time());stage.persist()
    state.update(status='ok',finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
    raise
finally:serial.save(record,state)
