"""Build19 admission, P16384 capacity gate, then complete global development arms."""
import fcntl
import os
from pathlib import Path
import time
import traceback

import serial
import stage_pilot
import study_statistics

name='stage0';record=serial.RAW/(name+'-controller.json');assert not record.exists()
lease=(serial.RAW/(name+'-controller.lock')).open('a');fcntl.flock(lease,fcntl.LOCK_EX|fcntl.LOCK_NB)
paths=[Path(__file__),serial.BASE/'stage_pilot.py',serial.BASE/'vm_pilot.py',serial.BASE/'serial.py',serial.BASE/'study_statistics.py',serial.BASE/'protocol.json']
state=dict(pid=os.getpid(),status='waiting_for_build19',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
    harness_sha256={str(p):serial.sha(p) for p in paths},
    rule='capacity gate fixed3 old TRAINING tasks P16384 then all30/bothP/all5 global arms G8; no fresh heldout')
serial.save(record,state)
try:
    while True:
        path=serial.RAW/'build-attempt19/manifest.json';build=serial.read(path) if path.exists() else {}
        path=serial.RAW/'stage-candidate1-controller.json';candidate=serial.read(path) if path.exists() else {}
        if build.get('status')=='failed' or candidate.get('status')=='stopped':raise RuntimeError('shared-stage build/check prerequisite failed')
        if build.get('status')=='ok':break
        if time.time()>serial.DEADLINE-10800:raise RuntimeError('shared-stage build prerequisite missed budget')
        time.sleep(10)
    for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest
    assert serial.sha(build['binary'])==build['binary_sha256']
    protocol=serial.read(serial.BASE/'protocol.json')
    state.update(status='capacity_gate',binary=build['binary'],binary_sha256=build['binary_sha256']);serial.save(record,state)
    stage_pilot.run(build['binary'],name+'-capacity',[16384],['filter_odd','is_sorted','house_robber'])
    for population in (1024,16384):
        state.update(status=f'all30_p{population}');serial.save(record,state)
        stage_pilot.run(build['binary'],f'{name}-p{population}',[population],protocol['tasks'])
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        rows=sum([serial.read(serial.RAW/f'{name}-p{p}-comparisons.json') for p in (1024,16384)],[])
        means={};by_population={}
        for variant in stage_pilot.VARIANTS[1:]:
            group=[r for r in rows if r['variant']==variant]
            assert len(group)==60 and len({(r['task'],r['population']) for r in group})==60
            assert all(all(r[k] for k in ('final_descriptors_equal','full_final_ast_equal','champion_ast_fitness_equal')) for r in group)
            means[variant]=study_statistics.geometric_mean(r['speedup'] for r in group)
            by_population[variant]={str(p):study_statistics.geometric_mean(r['speedup'] for r in group if r['population']==p) for p in (1024,16384)}
        serial.save(serial.RAW/(name+'-development-summary.json'),dict(status='ok',geometric_means=means,by_population=by_population,
            selected=max(means,key=means.get),scope='single-repeat complete all30 development; not formal target or newseed quality',epoch=time.time()))
    state.update(status='requested_phase_complete',finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
finally:serial.save(record,state)
