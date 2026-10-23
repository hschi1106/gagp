"""Forecast audit costs using old TRAINING, including a1000-case replica.

The replica repeats existing training inputs/labels. It is a timing diagnostic,
never heldout/generalization evidence or a search-quality selection criterion.
"""
import fcntl
import os
from pathlib import Path
import time
import traceback

import champion_audit_dedup
import quality
import serial

name='training-timing11';record=serial.RAW/(name+'-controller.json');assert not record.exists()
paths=[Path(__file__),serial.BASE/'champion_audit_dedup.py',serial.BASE/'quality.py',serial.BASE/'serial.py',serial.BASE/'protocol.json']
state=dict(pid=os.getpid(),status='waiting_for_training16',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
    harness_sha256={str(p):serial.sha(p) for p in paths},
    scope='old TRAINING audit runtime only; no heldout; no new search or candidate-quality selection')
serial.save(record,state)
try:
    while True:
        path=serial.RAW/'training16-controller.json';previous=serial.read(path) if path.exists() else {}
        if previous.get('status')=='stopped':raise RuntimeError('long-training prerequisite stopped')
        if previous.get('status')=='requested_phase_complete':
            proc=Path(f'/proc/{previous["pid"]}/stat')
            if not proc.exists() or proc.read_text().split()[2]=='Z':break
        if time.time()>serial.DEADLINE-10800:raise RuntimeError('training audit timing prerequisite missed budget')
        time.sleep(10)
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest
        protocol=serial.read(serial.BASE/'protocol.json')
        selection=serial.read(serial.RAW/'training16-development-selection.json')
        manifest={v:serial.read(serial.RAW/f'training16-{v}/manifest.json') for v in ('baseline','candidate')}
        binaries={'baseline':protocol['baseline_binary'],'candidate':selection['binary']}
        for variant in binaries:assert serial.sha(binaries[variant])==manifest[variant]['binary_sha256']
        stages={v:serial.Stage(name+'-'+v,binaries[v],settings=protocol['settings']) for v in binaries}
        for stage in stages.values():
            stage.m.update(scope=state['scope'],replica_cases=1000,heldout=False);stage.persist()
    rows=[];state['status']='auditing_old_training';serial.save(record,state)
    for task in protocol['tasks']:
        for variant,stage in stages.items():
            with serial.GPU_LOCK.open('a') as lock:
                fcntl.flock(lock,fcntl.LOCK_EX)
                search=manifest[variant]['jobs'][task+'.s0.g64'];assert search['status']=='ok'
                for field in ('output','history','export'):assert serial.sha(search[field])==search[field+'_sha256']
                old=serial.read(search['source']);count=len(old['cases']);assert count==search['cases']>0
                replica=dict(old,cases=[old['cases'][i%count] for i in range(1000)],population=[],
                    diagnostic='repeated old TRAINING for runtime forecast, not heldout',heldout=False)
                replica_path=stage.out/(task+'.training-repeat1000.source.json');assert not replica_path.exists()
                serial.save(replica_path,replica)
            for scope,source,cases in [('training',search['source'],count),('training_repeat1000',replica_path,1000)]:
                tag=f'{task}.{variant}.{scope}.common';output=stage.out/(tag+'.json')
                job=stage.run(tag,['quality-source',source,search['export'],protocol['inputs'][task]['grammar'],output,search['history']],
                    {'GAGP_BM_CASE_COUNT':str(cases)},timeout=2400,reserve=protocol['reserve_s'])
                with serial.GPU_LOCK.open('a') as lock:
                    fcntl.flock(lock,fcntl.LOCK_EX)
                    job.update(task=task,variant=variant,split=scope,heldout=False,output=str(output),cases=cases,scope=state['scope'])
                    if job['status']=='ok':
                        try:
                            doc=serial.read(output);quality.validate_judge(doc,cases,64,task)
                            job.update(output_sha256=serial.sha(output),unique_audited_programs=doc['unique_audited_programs'])
                        except Exception as error:job.update(status='invalid_output',error=repr(error))
                    stage.persist()
                if job['status']!='ok':raise RuntimeError('common training-runtime judge failed: '+tag)
            inputs=dict(metadata=dict(test_count=1000),grammar=protocol['inputs'][task]['grammar'],
                seeds={'0':dict(source=search['source'],prepared=search['prepared'])},test_source=str(replica_path))
            freeze=dict(variants=selection['variants'],reserve_s=protocol['reserve_s'])
            # The adapter's "test" mode omits comparison against the200-case
            # search score; this1000-case replicated training split has a
            # different score sum. Immediately label the recorded observation
            # as training_replica, never as heldout evidence.
            native=champion_audit_dedup.run(stage,search,'test',inputs,freeze)
            with serial.GPU_LOCK.open('a') as lock:
                fcntl.flock(lock,fcntl.LOCK_EX)
                native.update(split='training_repeat1000',heldout=False,scope=state['scope']);stage.persist()
                if native['status']!='ok':raise RuntimeError('native training-replica fidelity failed: '+task+'/'+variant)
                rows.append(dict(task=task,variant=variant,training_cases=count,
                    common_training_s=stage.m['jobs'][f'{task}.{variant}.training.common']['process_s'],
                    common_repeat1000_s=stage.m['jobs'][f'{task}.{variant}.training_repeat1000.common']['process_s'],
                    native_repeat1000_s=native['process_s'],
                    native_cpu_executed_programs=native['native_cpu_executed_programs'],
                    native_cpu_executed_cases=native['native_cpu_executed_cases'],heldout=False))
                serial.save(serial.RAW/(name+'-rows.json'),rows)
    for stage in stages.values():
        stage.m.update(status='requested_phase_complete',finished_epoch=time.time());stage.persist()
    state.update(status='requested_phase_complete',finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
    raise
finally:serial.save(record,state)
