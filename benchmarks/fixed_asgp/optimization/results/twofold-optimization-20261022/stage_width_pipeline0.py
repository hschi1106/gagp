"""Measure physical width independently of register VM, old TRAINING only."""
import fcntl
import os
from pathlib import Path
import time
import traceback

import serial
import stage_width_pilot as helper
import study_statistics

name='stage-width0';record=serial.RAW/(name+'-controller.json');assert not record.exists()
paths=[Path(__file__),serial.BASE/'stage_width_pilot.py',serial.BASE/'stage_pilot.py',serial.BASE/'vm_pilot.py',serial.BASE/'serial.py',serial.BASE/'study_statistics.py',serial.BASE/'protocol.json']
state=dict(pid=os.getpid(),status='waiting_for_stage1',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
    harness_sha256={str(p):serial.sha(p) for p in paths},
    rule='13 fixed TRAINING tasks/bothP; physical0/32/64/128 without register VM; require2% over0 then full30 selected width controls')
serial.save(record,state)
try:
    while True:
        path=serial.RAW/'stage1-controller.json';prior=serial.read(path) if path.exists() else {}
        if prior.get('status')=='stopped':raise RuntimeError('shared-stage prerequisite stopped')
        if prior.get('status')=='requested_phase_complete':
            proc=Path(f'/proc/{prior["pid"]}/stat')
            if not proc.exists() or proc.read_text().split()[2]=='Z':break
        if time.time()>serial.DEADLINE-10800:raise RuntimeError('width prerequisite missed budget')
        time.sleep(10)
    for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest
    protocol=serial.read(serial.BASE/'protocol.json');build=serial.read(serial.RAW/'build-attempt19/manifest.json')
    tasks=protocol['development']['tasks']+['sum_of_squares','fibonacci','tribonacci','fibonacci_array','even_squares']
    assert len(tasks)==len(set(tasks))==13
    state['status']='coarse';serial.save(record,state)
    helper.run(build['binary'],name+'-coarse',[1024,16384],tasks)
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
        if means[selected]<means['width0']*1.02:selected='width0'
        selection=dict(selected=selected,physical_block=int(selected[5:]),logical_block=512,geometric_means=means,
                       scope='old TRAINING only; one global width, no heldout',epoch=time.time())
        serial.save(serial.RAW/(name+'-width-selection.json'),selection)
    if selected!='width0':
        # Fresh full30 controls for the one coarse-selected global width.
        for population in (1024,16384):
            state['status']=f'all30_p{population}';serial.save(record,state)
            helper.run(build['binary'],f'{name}-p{population}',[population],protocol['tasks'],physical=int(selected[5:]))
    state.update(status='requested_phase_complete',selection=selection,finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
finally:serial.save(record,state)
