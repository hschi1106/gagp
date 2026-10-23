"""Measure existing reproduction launch options using one old immutable binary."""
import fcntl
import os
from pathlib import Path
import time
import traceback

import serial
import study_statistics
import thread_pilot

record=serial.RAW/'threads0-controller.json';assert not record.exists()
paths=[Path(__file__),serial.BASE/'thread_pilot.py',serial.BASE/'vm_pilot.py',
    serial.BASE/'serial.py',serial.BASE/'study_statistics.py',serial.BASE/'protocol.json']
state=dict(pid=os.getpid(),status='waiting_for_lock',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
    harness_sha256={str(p):serial.sha(p) for p in paths},
    rule='all three arms use the exact baseline binary; development8 tasks/bothP/global1vs8vs16; select pooled16-cell mean; no heldout')
serial.save(record,state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest
        protocol=serial.read(serial.BASE/'protocol.json')
        assert serial.sha(protocol['baseline_binary'])==protocol['baseline_sha256']
    state['status']='coarse';serial.save(record,state)
    thread_pilot.run(protocol['baseline_binary'],'threads0-coarse',[1024,16384],protocol['development']['tasks'])
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        rows=serial.read(serial.RAW/'threads0-coarse-comparisons.json')
        means={}
        for variant in ('threads8','threads16'):
            group=[r for r in rows if r['variant']==variant]
            assert len(group)==16 and all(all(r[k] for k in ('final_descriptors_equal','full_final_ast_equal','champion_ast_fitness_equal')) for r in group)
            means[variant]=study_statistics.geometric_mean(r['speedup'] for r in group)
        selected=max(means,key=means.get)
        if means[selected]<1.02:selected='baseline'
        serial.save(serial.RAW/'threads0-development-selection.json',dict(status='ok',selected=selected,
            threads={'baseline':1,'threads8':8,'threads16':16}[selected],geometric_means=means,epoch=time.time(),
            scope='old TRAINING only, not final global freeze; 2% minimum coarse benefit or retain1'))
    state.update(status='requested_phase_complete',finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
    raise
finally:serial.save(record,state)
