"""Drain two priority rows by parking idle controllers at an exclusion boundary.

No benchmark child, active sample, binary, setting, input or result is altered.
The common GPU lock must be owned before an idle controller can be parked.
Every parked process is resumed in finally; acquisition and draining are bounded.
"""
import fcntl
import os
from pathlib import Path
import select
import signal
import time
import traceback

import serial

record=serial.RAW/'scheduler-priority-drain0.json';assert not record.exists()
state=dict(pid=os.getpid(),status='waiting_for_gpu_exclusion',started_epoch=time.time(),
           deadline_epoch=serial.DEADLINE,priority='stage1',parked=[])
serial.save(record,state)
handles=[]


def interrupted(signum,frame):
    raise RuntimeError('scheduler interrupted by signal '+str(signum))


def identity(pid,suffix):
    proc=Path(f'/proc/{pid}')
    command=(proc/'cmdline').read_bytes().replace(b'\0',b' ').decode()
    assert suffix in command,('unexpected controller identity',pid,command)
    return dict(pid=pid,command=command,start_ticks=(proc/'stat').read_text().split()[21])


signal.signal(signal.SIGTERM,interrupted)
signal.signal(signal.SIGALRM,interrupted)
try:
    signal.alarm(600)
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        signal.alarm(0)
        assert time.time()<serial.DEADLINE-10800
        priority=serial.read(serial.RAW/'stage1-controller.json')
        state['priority_identity']=identity(priority['pid'],'stage_pipeline1.py')
        priority_fd=os.pidfd_open(priority['pid'])
        state['priority_before']=priority
        if priority['status']=='requested_phase_complete':
            state.update(status='already_complete',finished_epoch=time.time())
        else:
            assert priority['status']=='all30_p16384'
            for name,script in [('repro-block0','repro_block_pipeline0.py'),
                                ('parent-cache0','parent_cache_pipeline0.py')]:
                controller=serial.read(serial.RAW/(name+'-controller.json'))
                if controller['status']=='requested_phase_complete':continue
                assert controller['status']=='coarse'
                pid=controller['pid'];proof=identity(pid,script)
                # The exclusion boundary prevents any Stage benchmark from
                # starting between this proof and SIGSTOP. Require no live child
                # and no active record, including outside the timing wrapper.
                children=Path(f'/proc/{pid}/task/{pid}/children').read_text().split()
                assert not children,('controller has child at exclusion boundary',pid,children)
                counts={}
                for manifest in serial.RAW.glob(name+'-coarse-*/manifest.json'):
                    jobs=serial.read(manifest)['jobs']
                    assert all(j['status']!='running' for j in jobs.values())
                    for job in jobs.values():counts[job['status']]=counts.get(job['status'],0)+1
                fd=os.pidfd_open(pid)
                handles.append((pid,fd))
                signal.pidfd_send_signal(fd,signal.SIGSTOP)
                for unused in range(100):
                    if Path(f'/proc/{pid}/stat').read_text().split()[2]=='T':break
                    time.sleep(.01)
                else:raise RuntimeError('idle controller did not park')
                proof.update(controller=name,sample_counts=counts,children=[],
                             gpu_exclusion_owned_by=os.getpid(),epoch=time.time())
                state['parked'].append(proof);serial.save(record,state)
            state.update(status='draining_stage1',gpu_exclusion_released_epoch=time.time())
            serial.save(record,state)
    if state['status']=='draining_stage1':
        end=min(time.time()+600,serial.DEADLINE-10800)
        while time.time()<end:
            if select.select([priority_fd],[],[],min(55,end-time.time()))[0]:break
        else:raise RuntimeError('priority drain timeout; resume all idle controllers')
        completed=serial.read(serial.RAW/'stage1-controller.json')
        assert completed['status']=='requested_phase_complete',completed
        state.update(status='priority_complete',priority_after=completed,finished_epoch=time.time())
        print('priority stage1 complete; resuming parked idle controllers',flush=True)
    os.close(priority_fd)
except BaseException as error:
    state.update(status='failed',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
    raise
finally:
    signal.alarm(0)
    resumed=[]
    for pid,fd in handles:
        try:
            signal.pidfd_send_signal(fd,signal.SIGCONT)
            resumed.append(dict(pid=pid,epoch=time.time(),signal='SIGCONT'))
        except ProcessLookupError:
            resumed.append(dict(pid=pid,epoch=time.time(),already_exited=True))
        finally:os.close(fd)
    state['resumed']=resumed;serial.save(record,state)
