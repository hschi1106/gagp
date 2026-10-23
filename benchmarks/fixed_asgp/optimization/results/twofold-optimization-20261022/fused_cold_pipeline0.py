"""Admit exact fused-cold21, then fixed global old-TRAINING coarse comparison."""
import fcntl
import os
from pathlib import Path
import time
import traceback

import fused_cold_pilot as helper
import serial
import study_statistics

name='fused-cold0';record=serial.RAW/(name+'-controller.json');assert not record.exists()
paths=[Path(__file__),serial.BASE/'fused_cold_pilot.py',serial.BASE/'stage_pilot.py',
       serial.BASE/'vm_pilot.py',serial.BASE/'serial.py',serial.BASE/'study_statistics.py',
       serial.BASE/'protocol.json']
state=dict(pid=os.getpid(),status='waiting_for_admission',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
           harness_sha256={str(p):serial.sha(p) for p in paths},
           rule='fixed8developmenttasks/bothP/G8/seed0; same global shape/poolON/block1; '
                '19reference/22OFF/22ON; require >=2% vs both controls; no heldout; '
                'coarse only, not eligible as full30 or final result')
serial.save(record,state)
try:
    prerequisites=[serial.RAW/'build-attempt22/manifest.json',
                   serial.RAW/'provenance-build22/manifest.json',
                   serial.RAW/'resources-build22/manifest.json']
    while True:
        if serial.read(serial.RAW/'fused-cold-admission1-controller.json')['status']=='stopped':
            raise RuntimeError('fused cold admission native admission failed; preserve attempt')
        data=[serial.read(p) if p.exists() else {} for p in prerequisites]
        if any(d.get('status') in ('failed','stopped') for d in data):
            raise RuntimeError('fused cold admission provenance/resource prerequisite failed')
        if all(d.get('status')=='ok' for d in data):break
        if time.time()>serial.DEADLINE-10800:raise RuntimeError('fused cold admission admission missed budget')
        time.sleep(10)
    build=data[0]
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        for p,digest in state['harness_sha256'].items():assert serial.sha(p)==digest
        assert serial.sha(build['binary'])==build['binary_sha256']
        previous=serial.read(serial.RAW/'build-attempt19/manifest.json')
        assert previous['status']=='ok' and serial.sha(previous['binary'])==previous['binary_sha256']
        protocol=serial.read(serial.BASE/'protocol.json')
        state.update(status='coarse',binary_sha256=build['binary_sha256'],
                     control_binary_sha256=previous['binary_sha256']);serial.save(record,state)
    helper.run(build['binary'],name+'-coarse',[1024,16384],protocol['development']['tasks'])
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        rows=serial.read(serial.RAW/(name+'-coarse-comparisons.json'))
        expected={(t,p) for t in protocol['development']['tasks'] for p in (1024,16384)}
        means={}
        for variant in helper.VARIANTS[1:]:
            group=[r for r in rows if r['variant']==variant]
            assert len(group)==16 and {(r['task'],r['population']) for r in group}==expected
            assert all(all(r[k] for k in ('final_descriptors_equal','full_final_ast_equal',
                                         'champion_ast_fitness_equal')) for r in group)
            means[variant]=study_statistics.geometric_mean(r['speedup'] for r in group)
        selected='fused' if means['fused']>=max(1.0,means['ordinary'])*1.02 else 'baseline'
        selection=dict(selected=selected,enabled=selected=='fused',geometric_means=means,
                       epoch=time.time(),scope=state['rule'],extra=helper.execution_settings(selected),
                       binary_sha256=build['binary_sha256'],
                       comparisons_sha256=serial.sha(serial.RAW/(name+'-coarse-comparisons.json')))
        serial.save(serial.RAW/(name+'-development-selection.json'),selection)
    state.update(status='requested_phase_complete',selection=selection,finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
finally:serial.save(record,state)
