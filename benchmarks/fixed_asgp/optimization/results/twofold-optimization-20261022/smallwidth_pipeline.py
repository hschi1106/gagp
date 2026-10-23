"""Current18-binary global view width, not obsolete8/9-binary followups."""
import fcntl
import os
from pathlib import Path
import time
import traceback

import serial
import smallwidth_pilot
import study_statistics

name='smallwidth0';record=serial.RAW/(name+'-controller.json');assert not record.exists()
lease=(serial.RAW/(name+'-controller.lock')).open('a');fcntl.flock(lease,fcntl.LOCK_EX|fcntl.LOCK_NB)
paths=[Path(__file__),serial.BASE/'smallwidth_pilot.py',serial.BASE/'elite_pilot.py',serial.BASE/'vm_pilot.py',
    serial.BASE/'serial.py',serial.BASE/'study_statistics.py',serial.BASE/'protocol.json']
state=dict(pid=os.getpid(),status='waiting_for_lock',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
    harness_sha256={str(p):serial.sha(p) for p in paths},
    rule='13 predeclared TRAINING tasks/bothP/physical32vs64vs128; pooled26-cell mean; require>=2% over128 then full30 fresh controls; logical512 unchanged')
serial.save(record,state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        build=serial.read(serial.RAW/'build-attempt18/manifest.json');assert build['status']=='ok'
        assert serial.sha(build['binary'])==build['binary_sha256']
        for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest
        protocol=serial.read(serial.BASE/'protocol.json')
        tasks=protocol['development']['tasks']+['sum_of_squares','fibonacci','tribonacci','fibonacci_array','even_squares']
        assert len(tasks)==len(set(tasks))==13
        state.update(status='coarse',tasks=tasks);serial.save(record,state)
    smallwidth_pilot.run(build['binary'],name+'-coarse',[1024,16384],tasks,'coarse',128)
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        rows=serial.read(serial.RAW/(name+'-coarse-comparisons.json'));means={}
        expected={(task,p) for task in tasks for p in (1024,16384)}
        for variant in ('width32','width64','width128'):
            group=[r for r in rows if r['variant']==variant]
            assert len(group)==26 and {(r['task'],r['population']) for r in group}==expected
            assert all(all(r[k] for k in ('final_descriptors_equal','full_final_ast_equal','champion_ast_fitness_equal')) for r in group)
            means[variant]=study_statistics.geometric_mean(r['speedup'] for r in group)
        selected=max(means,key=means.get)
        if means[selected]<1.02*means['width128']:selected='width128'
        width=int(selected[5:])
        selection=dict(physical_block=width,logical_block=512,geometric_means=means,epoch=time.time(),
            scope='old TRAINING runtime only; one global width, no heldout; not primary all30 target attainment')
        serial.save(serial.RAW/(name+'-width-selection.json'),selection)
    if width!=128:
        for population in (1024,16384):
            state.update(status=f'full30_p{population}',selection=selection);serial.save(record,state)
            smallwidth_pilot.run(build['binary'],f'{name}-all30-p{population}',[population],protocol['tasks'],'full',width)
    else:
        state['full_matrix_reference']='elite5/bothP/elite_full128 (same binary/settings, already planned fresh full30 controls); no redundant repeat here'
    state.update(status='requested_phase_complete',selection=selection,finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
    raise
finally:serial.save(record,state)
