"""Measure seed0 source/preparation costs using only already-used TRAINING data."""
import fcntl
import os
from pathlib import Path
import time
import traceback

import serial

name='preparation-timing0';record=serial.RAW/(name+'-controller.json');assert not record.exists()
paths=[Path(__file__),serial.BASE/'serial.py',serial.BASE/'protocol.json']
state=dict(pid=os.getpid(),status='waiting_for_training7',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
           harness_sha256={str(p):serial.sha(p) for p in paths},
           scope='old initialization seed0 and original TRAINING only; runtime diagnostic, no fresh heldout or new seed')
serial.save(record,state)
try:
    while True:
        path=serial.RAW/'training7-controller.json';prior=serial.read(path) if path.exists() else {}
        if prior.get('status')=='stopped':raise RuntimeError('training prerequisite stopped')
        if prior.get('status')=='requested_phase_complete':
            proc=Path(f'/proc/{prior["pid"]}/stat')
            if not proc.exists() or proc.read_text().split()[2]=='Z':break
        if time.time()>serial.DEADLINE-10800:raise RuntimeError('preparation forecast prerequisite missed budget')
        time.sleep(10)
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest
        protocol=serial.read(serial.BASE/'protocol.json');old=serial.read(serial.ROOT/'logs/day24-20261017/manifest.json')
        selection=serial.read(serial.RAW/'training7-development-selection.json')
        assert serial.sha(selection['binary'])==selection['binary_sha256']
        stage=serial.Stage(name,selection['binary'],settings=protocol['settings'])
        stage.m.update(scope=state['scope'],heldout=False,initialization_seed=0);stage.persist()
    state['status']='old_seed0_preparation';serial.save(record,state);rows=[]
    for task in protocol['tasks']:
        frozen=stage.out/(task+'.s0.frozen.json')
        job=stage.run(task+'.freeze',['freeze',task,frozen],
                      dict(GAGP_FREEZE_POPULATION='1024',GAGP_FREEZE_POPULATION_SEED='0'),reserve=protocol['reserve_s'])
        if job['status']!='ok':raise RuntimeError('old seed0 freeze failed: '+task)
        with serial.GPU_LOCK.open('a') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX)
            previous=old['inputs'][task]['quality']['0'];old_source=Path(previous['source'])
            assert serial.sha(old_source)==previous['source_sha256']
            original=serial.read(old_source);document=serial.read(frozen)
            assert document['population_seed']==0 and len(document['population'])==1024
            assert document['population']==original['population'],'old source generator seed0 no longer matches recorded seed0'
            document.update(cases=original['cases'],heldout=False,
                            diagnostic='already-used old seed0/TRAINING; timing only, not a quality outcome')
            source=stage.out/(task+'.s0.train.source.json');assert not source.exists();serial.save(source,document)
            job.update(task=task,heldout=False,output=str(frozen),output_sha256=serial.sha(frozen));stage.persist()
        prepared=stage.out/(task+'.s0.prepared.json')
        prepared_job=stage.run(task+'.prepare',['prepare',source,protocol['inputs'][task]['grammar'],1024,prepared],reserve=protocol['reserve_s'])
        with serial.GPU_LOCK.open('a') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX)
            if prepared_job['status']=='ok':
                try:
                    document=serial.read(prepared)
                    assert len(document['programs'])==1024 and not document.get('admission_rejections',[])
                    assert document['source_indices']==list(range(1024))
                    prepared_job.update(output=str(prepared),output_sha256=serial.sha(prepared))
                except Exception as error:prepared_job.update(status='native_unadmitted',error=repr(error))
            prepared_job.update(task=task,heldout=False);stage.persist()
            if prepared_job['status']!='ok':raise RuntimeError('old seed0 preparation failed: '+task)
            rows.append(dict(task=task,heldout=False,seed=0,freeze_s=job['process_s'],prepare_s=prepared_job['process_s'],
                             source_sha256=serial.sha(source),old_source_sha256=previous['source_sha256']))
            serial.save(serial.RAW/(name+'-rows.json'),rows)
    stage.m.update(status='requested_phase_complete',finished_epoch=time.time());stage.persist()
    state.update(status='requested_phase_complete',finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
finally:serial.save(record,state)
