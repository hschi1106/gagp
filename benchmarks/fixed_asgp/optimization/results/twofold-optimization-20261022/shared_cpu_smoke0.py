"""Fresh GPU-only snapshots checked against exact OLD TRAINING CPU references."""
import fcntl
import os
import time
import traceback

import full_population_audit_shared as shared_audit
import full_population_audit_v2 as admitted
import serial
import stage_pilot

name='shared-cpu-smoke0';record=serial.RAW/(name+'-controller.json');assert not record.exists()
state=dict(pid=os.getpid(),status='waiting_for_lock',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
    scope='old TRAINING native-fitness and runtime diagnostic only; no fresh heldout, no search reruns; four oldCPUreferences plus one fresh P16384 reference')
serial.save(record,state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        protocol=serial.read(serial.BASE/'protocol.json');build=serial.read(serial.RAW/'build-attempt20/manifest.json')
        assert build['status']=='ok' and serial.sha(build['binary'])==build['binary_sha256']
        candidate=serial.Stage(name+'-candidate',build['binary'],settings=protocol['settings'])
        baseline=serial.Stage(name+'-baseline-reference',protocol['baseline_binary'],settings=protocol['settings'])
        old=serial.read(serial.RAW/'full-population-audit-smoke0/manifest.json')
        searches=serial.read(serial.RAW/'simple1-p1024-baseline/manifest.json')
        large=serial.read(serial.RAW/'stage1-p16384-baseline/manifest.json')
        assert large['jobs']['sum_of_elements.p16384.s0.g8']['status']=='ok'
        extra=dict(stage_pilot.execution_settings('pool_shape'),GAGP_GPU_CUDA_POOL='0',GAGP_GPU_PRIVATE_LAUNCH_BOUND='512')
        freeze=dict(variants=dict(baseline=dict(extra=stage_pilot.execution_settings('baseline')),candidate=dict(extra=extra)),reserve_s=10800)
        state.update(status='checking_old_training_references',rows=[]);serial.save(record,state)
    for task in ('sum_of_elements','is_sorted','filter_odd','house_robber'):
        old_reference=old['jobs'][f'{task}.p1024.candidate.final.full-native']
        search=dict(searches['jobs'][f'{task}.p1024.s0.g8'],variant='candidate',seed=0,repetition=0)
        row=shared_audit.run(candidate,search,protocol['inputs'][task],freeze,reference=old_reference)
        assert row['status']=='ok' and row.get('cpu_reference_verified') is True
        assert row['native_cpu_executed_programs']==row['native_cpu_executed_cases']==0
        state['rows'].append(dict(task=task,population=1024,process_s=row['process_s'],
            cpu_reference_programs=row['native_cpu_reference_programs'],new_cpu_executions=0));serial.save(record,state)
    task='sum_of_elements';original=large['jobs'][f'{task}.p16384.s0.g8']
    search=dict(original,variant='baseline',seed=0,repetition=0)
    reference=admitted.run(baseline,search,protocol['inputs'][task],freeze)
    assert reference['status']=='ok'
    search=dict(original,variant='candidate',seed=0,repetition=0)
    row=shared_audit.run(candidate,search,protocol['inputs'][task],freeze,reference=reference)
    assert row['status']=='ok' and row.get('cpu_reference_verified') is True
    assert row['native_cpu_executed_programs']==0
    state['rows'].append(dict(task=task,population=16384,process_s=row['process_s'],
        actual_reference_process_s=reference['process_s'],
        actual_reference_cpu_executions=reference['native_cpu_executed_programs'],new_candidate_cpu_executions=0))
    for stage in (candidate,baseline):stage.m.update(status='requested_phase_complete',finished_epoch=time.time());stage.persist()
    state.update(status='ok',finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
finally:serial.save(record,state)
