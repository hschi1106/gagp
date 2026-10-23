"""Replace only zero-sample waiting queues with the final global recipe chain."""
import fcntl
import os
from pathlib import Path
import select
import signal
import subprocess
import time
import traceback

import serial

record=serial.RAW/'queued-final-chain13-replacement0.json';assert not record.exists()
new_scripts=['training_best_final_recipe.py','training_timing8.py','preparation_timing6.py','final_pipeline1.py',
             'best_selection_final_recipe.py','best_selection_extra_recipe.py','combined3_pilot.py']
state=dict(pid=os.getpid(),status='waiting_for_checks',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
           new_source_sha256={str(serial.BASE/p):serial.sha(serial.BASE/p) for p in new_scripts},
           reason='wait for all complete global recipes before one G64 training/cost/freeze chain; no fresh heldout yet')
serial.save(record,state)
handles=[];cancelled=False


def abort(signum,frame):
    raise RuntimeError('queue replacement interrupted: '+str(signum))


signal.signal(signal.SIGTERM,abort);signal.signal(signal.SIGALRM,abort)
try:
    while True:
        checks=serial.read(serial.RAW/'harness-checks-attempt19/manifest.json')
        if checks['status']=='failed':raise RuntimeError('new final recipe/harness checks failed')
        if checks['status']=='ok':break
        if time.time()>serial.DEADLINE-10800:raise RuntimeError('queue prerequisites missed original budget')
        time.sleep(10)
    for name in ('fused-cold-admission1',):
        assert serial.read(serial.RAW/(name+'-controller.json'))['status']=='ok'
    for name in ('provenance-build22','resources-build22'):
        assert serial.read(serial.RAW/name/'manifest.json')['status']=='ok'
    signal.alarm(600)
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX);signal.alarm(0)
        assert not list(serial.RAW.glob('formal0-*')),'formal phase already exists; preserve it'
        for path,digest in state['new_source_sha256'].items():assert serial.sha(path)==digest
        old_specs=[('training12','training_best_coarse_recipe.py','waiting_for_full30_development'),
                   ('training-timing7','training_timing7.py','waiting_for_training12'),
                   ('preparation-timing5','preparation_timing5.py','waiting_for_training12'),
                   ('final-pipeline0','final_pipeline0.py','waiting_for_selection_and_training_costs')]
        originals={}
        for name,script,expected in old_specs:
            path=serial.RAW/(name+'-controller.json');data=serial.read(path);pid=data['pid']
            assert data['status']==expected,('old work started; preserve it',name,data['status'])
            command=Path(f'/proc/{pid}/cmdline').read_bytes().replace(b'\0',b' ').decode()
            assert script in command
            assert not Path(f'/proc/{pid}/task/{pid}/children').read_text().split()
            jobs=[]
            for manifest in serial.RAW.glob(name+'*/manifest.json'):
                jobs.extend(serial.read(manifest).get('jobs',{}).values())
            assert not jobs,('old native samples already exist',name)
            fd=os.pidfd_open(pid);handles.append((pid,fd))
            signal.pidfd_send_signal(fd,signal.SIGSTOP)
            for unused in range(100):
                if Path(f'/proc/{pid}/stat').read_text().split()[2]=='T':break
                time.sleep(.01)
            else:raise RuntimeError('waiting controller did not park')
            assert serial.read(path)==data and not Path(f'/proc/{pid}/task/{pid}/children').read_text().split()
            originals[name]=dict(record=str(path),record_sha256=serial.sha(path),data=data,command=command,
                                 pid=pid,children=[],native_samples=0,gpu_exclusion_owned_by=os.getpid())
        assert not list(serial.RAW.glob('formal0-*'))
        state.update(status='verified_unstarted',originals=originals);serial.save(record,state)
        for pid,fd in handles:
            signal.pidfd_send_signal(fd,signal.SIGTERM);signal.pidfd_send_signal(fd,signal.SIGCONT)
            assert select.select([fd],[],[],5)[0],('idle controller failed to exit',pid)
        cancelled=True
        for name,original in originals.items():
            data=dict(original['data'],status='stopped',intentional=True,native_samples=0,
                      reason=state['reason'],cancellation_record=str(record),finished_epoch=time.time())
            serial.save(original['record'],data)
        state['status']='old_waiting_queues_preserved';serial.save(record,state)
    commands=[('training13',['python3',str(serial.BASE/'training_best_final_recipe.py'),
                             '--name','training13','--prior-name','combined3','--build-name','build-attempt22']),
              ('training-timing8',['python3',str(serial.BASE/'training_timing8.py')]),
              ('preparation-timing6',['python3',str(serial.BASE/'preparation_timing6.py')]),
              ('final-pipeline1',['python3',str(serial.BASE/'final_pipeline1.py')])]
    children={}
    for name,command in commands:
        assert not (serial.RAW/(name+'-controller.json')).exists()
        with (serial.RAW/(name+'.log')).open('x') as log:
            child=subprocess.Popen(command,cwd=serial.ROOT,env=dict(os.environ,PYTHONPATH='tools'),
                                   stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        children[name]=dict(pid=child.pid,command=command)
        state['new_children']=children;serial.save(record,state)
    state.update(status='ok',finished_epoch=time.time())
except BaseException as error:
    state.update(status='failed',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
finally:
    signal.alarm(0)
    for pid,fd in handles:
        if not cancelled:
            try:signal.pidfd_send_signal(fd,signal.SIGCONT)
            except ProcessLookupError:pass
        os.close(fd)
    serial.save(record,state)
