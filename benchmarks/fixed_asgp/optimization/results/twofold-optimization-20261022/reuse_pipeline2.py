"""One global completed-fitness ablation after the prior VM development pilot."""
import fcntl
import os
from pathlib import Path
import time
import traceback

import reuse_pilot
import serial
import study_statistics

name='reuse2';record=serial.RAW/(name+'-controller.json')
lease=(serial.RAW/(name+'-controller.lock')).open('a');fcntl.flock(lease,fcntl.LOCK_EX | fcntl.LOCK_NB)
assert not record.exists(),'preserve previous attempts'
paths=[Path(__file__),serial.BASE/'reuse_pilot.py',serial.BASE/'vm_pilot.py',
       serial.BASE/'serial.py',serial.BASE/'study_statistics.py',serial.BASE/'protocol.json']
state=dict(pid=os.getpid(),status='waiting_for_build13',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
           harness_sha256={str(p):serial.sha(p) for p in paths})
serial.save(record,state)
try:
    while True:
        path=serial.RAW/'build-attempt13/manifest.json'
        build=serial.read(path) if path.exists() else {}
        corrected=serial.read(serial.RAW/'unchanged-linked-controller.json')
        if build.get('status')=='failed' or corrected['status']=='stopped':raise RuntimeError('corrected reuse candidate failed; preserve and diagnose')
        if build.get('status')=='ok':break
        if time.time()>serial.DEADLINE-10800:raise RuntimeError('reuse candidate prerequisite missed budget')
        time.sleep(10)
    state['status']='waiting_for_vm0';serial.save(record,state)
    while True:
        previous=serial.read(serial.RAW/'vm0-controller.json')
        if previous['status']=='stopped':raise RuntimeError('prior VM ablation stopped; inspect actual failure')
        if previous['status']=='requested_phase_complete':
            proc=Path(f'/proc/{previous["pid"]}/stat')
            if not proc.exists() or proc.read_text().split()[2]=='Z':break
        if time.time()>serial.DEADLINE-10800:raise RuntimeError('VM ablation prerequisite missed budget')
        time.sleep(10)
    for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest,'reuse pilot harness changed'
    assert serial.sha(build['binary'])==build['binary_sha256']
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        protocol=serial.read(serial.BASE/'protocol.json')
        rows=sum([serial.read(serial.RAW/f'vm0-all30-p{p}-comparisons.json') for p in (1024,16384)],[])
        means={}
        for variant in ('basic','full'):
            group=[row for row in rows if row['variant']==variant]
            assert len(group)==60 and all(all(r[k] for k in ('final_descriptors_equal','full_final_ast_equal','champion_ast_fitness_equal')) for r in group)
            means[variant]=study_statistics.geometric_mean(row['speedup'] for row in group)
        selected=max(means,key=means.get)
        physical=serial.read(serial.RAW/'vm0-width-selection.json')['physical_block']
        selection=dict(scope='old TRAINING/runtime only; no fresh heldout; one global upstream configuration',
            selected=selected,physical_block=physical,geometric_means=means,epoch=time.time(),
            binary=build['binary'],binary_sha256=build['binary_sha256'],
            rule='freeze prior global physical width and best pooled60-cell basic/full path; compare completed-score reuse off/on')
        serial.save(serial.RAW/'reuse2-development-selection.json',selection)
    for population in (1024,16384):
        state.update(status=f'pilot{population}',selection=selection);serial.save(record,state)
        reuse_pilot.run(build['binary'],f'{name}-p{population}',[population],protocol['tasks'],selected,physical)
    state.update(status='requested_phase_complete',finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
    raise
finally:serial.save(record,state)
