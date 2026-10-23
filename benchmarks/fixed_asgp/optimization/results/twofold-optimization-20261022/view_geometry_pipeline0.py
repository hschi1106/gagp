"""Joint physical width/register-cap tuning with one global full-matrix winner."""
import fcntl
import os
from pathlib import Path
import time
import traceback

import serial
import full_population_audit
import view_geometry_pilot as helper
import study_statistics

name='view-geometry0';record=serial.RAW/(name+'-controller.json');assert not record.exists()
paths=[Path(__file__),serial.BASE/'view_geometry_pilot.py',serial.BASE/'stage_pilot.py',serial.BASE/'vm_pilot.py',
       serial.BASE/'full_population_audit.py',serial.BASE/'champion_audit_dedup.py',serial.BASE/'serial.py',
       serial.BASE/'study_statistics.py',serial.BASE/'protocol.json']
state=dict(pid=os.getpid(),status='waiting_for_stage1',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
    harness_sha256={str(p):serial.sha(p) for p in paths},
    rule='13fixedTRAININGtasks/bothP; plain0/32/64/128 andrelaxed32/64/128; pooled26-cell mean; require2% overplain0 then one globalwinner freshfull30')
serial.save(record,state)
try:
    while True:
        path=serial.RAW/'stage1-controller.json';prior=serial.read(path) if path.exists() else {}
        if prior.get('status')=='stopped':raise RuntimeError('shared-stage prerequisite stopped')
        if prior.get('status')=='requested_phase_complete':
            proc=Path(f'/proc/{prior["pid"]}/stat')
            if not proc.exists() or proc.read_text().split()[2]=='Z':break
        if time.time()>serial.DEADLINE-10800:raise RuntimeError('geometry prerequisite missed budget')
        time.sleep(10)
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest
        protocol=serial.read(serial.BASE/'protocol.json');build=serial.read(serial.RAW/'build-attempt19/manifest.json')
        assert build['status']=='ok' and serial.sha(build['binary'])==build['binary_sha256']
        admission=serial.Stage(name+'-plain32-admission',build['binary'],settings=protocol['settings'])
        previous=serial.read(serial.RAW/'simple1-p1024-baseline/manifest.json')
    state['status']='full_native_plain32_admission';serial.save(record,state)
    freeze=dict(variants={'candidate':dict(extra=helper.execution_settings('plain32'))},reserve_s=protocol['reserve_s'])
    verified_actual_plain32=0
    for task in ('sum_of_elements','is_sorted','filter_odd','house_robber','fibonacci'):
        original=previous['jobs'][task+'.p1024.s0.g8']
        job=dict(original,task=task,population=1024,seed=0,repetition=0,variant='candidate')
        result=full_population_audit.run(admission,job,protocol['inputs'][task],freeze)
        if result['status']!='ok':raise RuntimeError('plain32 fullnative admission failed: '+task)
        with serial.GPU_LOCK.open('a') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX)
            raw=serial.read(result['raw_output']);profile=raw['execution_profile']
            assert '-register-phase-stack' not in profile and '-view-reg512' not in profile
            verified_actual_plain32+=('-physical-view32' in profile)
            result['admission_execution_profile']=profile;admission.persist()
    assert verified_actual_plain32>0,'native admission did not actually exercise plain32 view launch'
    admission.m.update(status='requested_phase_complete',finished_epoch=time.time());admission.persist()
    tasks=protocol['development']['tasks']+['sum_of_squares','fibonacci','tribonacci','fibonacci_array','even_squares']
    assert len(tasks)==len(set(tasks))==13
    state['status']='coarse';serial.save(record,state)
    helper.run(build['binary'],name+'-coarse',[1024,16384],tasks,'coarse')
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        rows=serial.read(serial.RAW/(name+'-coarse-comparisons.json'));means={}
        expected={(task,p) for task in tasks for p in (1024,16384)}
        for variant in helper.VARIANTS[1:]:
            group=[r for r in rows if r['variant']==variant]
            assert len(group)==26 and {(r['task'],r['population']) for r in group}==expected
            assert all(all(r[k] for k in ('final_descriptors_equal','full_final_ast_equal','champion_ast_fitness_equal')) for r in group)
            means[variant]=study_statistics.geometric_mean(r['speedup'] for r in group)
        selected=max(means,key=means.get)
        if means[selected]<means['plain0']*1.02:selected='plain0'
        extra=helper.execution_settings(selected)
        selection=dict(selected=selected,physical_block=int(extra['GAGP_GPU_VIEW_PHYSICAL_BLOCK']),
                       relaxed_registers=int(extra['GAGP_GPU_RELAXED_VIEW_REGISTERS']),logical_block=512,geometric_means=means,
                       scope='old TRAINING only; one global geometry, no heldout',epoch=time.time())
        serial.save(serial.RAW/(name+'-geometry-selection.json'),selection)
    if selected!='plain0':
        for population in (1024,16384):
            state['status']=f'all30_p{population}';serial.save(record,state)
            helper.run(build['binary'],f'{name}-p{population}',[population],protocol['tasks'],selected)
    state.update(status='requested_phase_complete',selection=selection,finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
finally:serial.save(record,state)
