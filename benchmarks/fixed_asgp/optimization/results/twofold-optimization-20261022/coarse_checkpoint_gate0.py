"""Event-driven gate: preserve complete coarse evidence, stop only unstarted tails.

Primary scope stays all30/bothP on the composed candidate and final R2 matrix.
Any individual full30 followup that already started is allowed to finish.
No active, successful or failed sample is interrupted or overwritten.
"""
import ctypes
import json
import os
from pathlib import Path
import select
import signal
import struct
import subprocess
import time
import traceback

import serial

name='coarse-gate0';record=serial.RAW/(name+'-controller.json');assert not record.exists()
specs={
    'private-launch0':('development-selection',16,('ordinary','bound512','bound1024'),'private_launch_pipeline0.py'),
    'projection-simple0':('development-selection',26,('shape','projected','simple','projected_simple'),'projection_simple_pipeline0.py'),
    'view-geometry1':('geometry-selection',26,('plain0','plain32','plain64','plain128','relaxed32','relaxed64','relaxed128'),'view_geometry_pipeline1.py')}
state=dict(pid=os.getpid(),status='waiting_for_complete_coarse',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
    rule='complete fixed coarse grids first; cancel only before any individual full30 sample starts; compose one full30 candidate, then full formal and independent quality',components={})
serial.save(record,state)


def alive(pid):
    path=Path(f'/proc/{pid}/stat')
    return path.exists() and path.read_text().split()[2]!='Z'


def handle(component):
    suffix,size,variants,script=specs[component]
    control_path=serial.RAW/(component+'-controller.json');control=serial.read(control_path)
    choice=serial.RAW/(component+'-'+suffix+'.json')
    if not choice.exists():
        if control['status']=='stopped' or not alive(control['pid']):raise RuntimeError('coarse prerequisite ended before selection: '+component)
        return False
    coarse_path=serial.RAW/(component+'-coarse-comparisons.json');rows=serial.read(coarse_path)
    assert len(rows)==size*len(variants)
    for variant in variants:
        group=[row for row in rows if row['variant']==variant]
        assert len(group)==size and len({(row['task'],row['population']) for row in group})==size
        assert all(all(row[key] for key in ('final_descriptors_equal','full_final_ast_equal','champion_ast_fitness_equal')) for row in group)
    proof=dict(selection=str(choice),selection_sha256=serial.sha(choice),coarse_comparisons=str(coarse_path),
        coarse_comparisons_sha256=serial.sha(coarse_path),coarse_pairs_per_variant=size,coarse_variants=list(variants))
    if control['status']=='requested_phase_complete' and not alive(control['pid']):
        state['components'][component]=dict(proof,outcome='individual_followup_already_completed');return True
    if control['status']=='stopped':raise RuntimeError('component stopped after selection; inspect preserved failure: '+component)
    pid=control['pid'];proc=Path(f'/proc/{pid}')
    assert script in (proc/'cmdline').read_bytes().decode()
    os.kill(pid,signal.SIGSTOP)
    try:
        # STOP closes the race between checking children and a possible Popen.
        children=subprocess.run(['ps','-o','pid=','--ppid',str(pid)],capture_output=True,text=True).stdout.strip()
        following=[];started=bool(children)
        for population in (1024,16384):
            for folder in serial.RAW.glob(f'{component}-p{population}-*'):
                manifest=folder/'manifest.json'
                if not manifest.exists():following.append(dict(folder=str(folder),partial_initialization=True));continue
                data=serial.read(manifest)
                if any(job.get('status')!='waiting' or 'pid' in job for job in data['jobs'].values()):started=True
                following.append(dict(folder=str(folder),manifest=data))
        if started:
            os.kill(pid,signal.SIGCONT)
            state['components'][component]=dict(proof,outcome='individual_followup_already_started_preserved',
                live_children=children,followup_stages=following,controller_pid=pid)
            return True
        assert Path(f'/proc/{pid}/stat').read_text().split()[2]=='T'
        cancellation=serial.RAW/(component+'-unstarted-tail-cancellation0.json');assert not cancellation.exists()
        serial.save(cancellation,dict(epoch=time.time(),original_controller=control,process_stat=(proc/'stat').read_text(),
            following_stages=following,proof=proof,reason='unstarted individual all30 tail redirected to composed full30/formal/fresh quality; zero sample processes'))
        os.kill(pid,signal.SIGTERM);os.kill(pid,signal.SIGCONT)
        for _ in range(100):
            if not alive(pid):break
            time.sleep(.05)
        assert not alive(pid)
        control.update(status='stopped',finished_epoch=time.time(),coarse_checkpoint_complete=True,
            intentional_unstarted_tail_cancellation=str(cancellation),
            reason='complete coarse preserved; unstarted individual full30 followup redirected to combined full30 and formal/quality')
        serial.save(control_path,control)
        for entry in following:
            if 'manifest' not in entry:continue
            data=entry['manifest']
            for job in data['jobs'].values():
                assert job['status']=='waiting' and 'pid' not in job
                job.update(status='not_run_budget_redirect',reason='no child started; see '+str(cancellation))
            data.update(status='stopped_before_samples',intentional_unstarted_tail_cancellation=str(cancellation))
            serial.save(Path(entry['folder'])/'manifest.json',data)
        state['components'][component]=dict(proof,outcome='unstarted_individual_followup_cancelled',cancellation=str(cancellation))
        return True
    except BaseException:
        if alive(pid):os.kill(pid,signal.SIGCONT)
        raise


fd=-1
try:
    library=ctypes.CDLL(None,use_errno=True)
    fd=library.inotify_init1(os.O_NONBLOCK|os.O_CLOEXEC);assert fd>=0
    assert library.inotify_add_watch(fd,str(serial.RAW).encode(),0x80|0x8)>=0
    pending=set(specs)
    while pending:
        for component in list(pending):
            if handle(component):pending.remove(component);serial.save(record,state)
        if not pending:break
        if time.time()>serial.DEADLINE-10800:raise RuntimeError('coarse gate exceeded original experiment budget')
        ready,_,_=select.select([fd],[],[],30)
        if ready:
            # Consume directory notifications; no sample polling or GPU work.
            try:os.read(fd,262144)
            except BlockingIOError:pass
    state.update(status='requested_phase_complete',finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
finally:
    if fd>=0:os.close(fd)
    serial.save(record,state)
