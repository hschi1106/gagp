"""Tune one global physical width on development data, then compare all30."""
from argparse import ArgumentParser
import fcntl
import math
import os
from pathlib import Path
import time
import traceback

import serial
import vm_pilot


def exited(pid):
    path=Path(f'/proc/{pid}/stat')
    return not path.exists() or path.read_text().split()[2]=='Z'


def main(name,wait_controller,build_name,apply_controller):
    record=serial.RAW/(name+'-controller.json')
    lock=(serial.RAW/(name+'-controller.lock')).open('a')
    fcntl.flock(lock,fcntl.LOCK_EX | fcntl.LOCK_NB)
    assert not record.exists(),'preserve existing pipeline; use a new name'
    state=dict(pid=os.getpid(),status='waiting_for_candidate',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
        harness_sha256={str(p):serial.sha(p) for p in (Path(__file__),serial.BASE/'vm_pilot.py',serial.BASE/'serial.py',serial.BASE/'protocol.json')})
    serial.save(record,state)
    try:
        while True:
            path=serial.RAW/build_name/'manifest.json'
            apply_path=apply_controller
            if apply_path and apply_path.exists() and serial.read(apply_path)['status']=='stopped':raise RuntimeError('candidate prerequisites stopped')
            build=serial.read(path) if path.exists() else {}
            if build.get('status')=='failed':raise RuntimeError('candidate native build/check failed')
            if build.get('status')=='ok':break
            if time.time()>serial.DEADLINE-10800:raise RuntimeError('candidate wait exceeded budget')
            time.sleep(10)
        if wait_controller:
            state['status']='waiting_for_prior';serial.save(record,state)
            while True:
                prior=serial.read(wait_controller)
                if prior['status'] in ('requested_phase_complete','stopped') and exited(prior['pid']):
                    if prior['status']=='stopped':raise RuntimeError('prior register ablation stopped; inspect failure')
                    break
                if time.time()>serial.DEADLINE-10800:raise RuntimeError('prior timing wait exceeded budget')
                time.sleep(10)
        for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest,'timing harness changed after launch'
        assert serial.sha(build['binary'])==build['binary_sha256']
        state.update(binary=build['binary'],binary_sha256=build['binary_sha256'])
        protocol=serial.read(serial.BASE/'protocol.json');tasks=protocol['development']['tasks']
        for population in (1024,16384):
            state['status']=f'coarse{population}';serial.save(record,state)
            vm_pilot.run(build['binary'],f'{name}-coarse-p{population}',[population],tasks)
        with serial.GPU_LOCK.open('a') as gpu:
            fcntl.flock(gpu,fcntl.LOCK_EX)
            rows=[]
            for population in (1024,16384):rows+=serial.read(serial.RAW/f'{name}-coarse-p{population}-comparisons.json')
            means={}
            for variant in ('bank512','bank128','bank256','full128'):
                current=[row for row in rows if row['variant']==variant]
                assert len(current)==16 and all(row['final_descriptors_equal'] and row['full_final_ast_equal'] and row['champion_ast_fitness_equal'] for row in current)
                means[variant]=math.exp(sum(math.log(row['speedup']) for row in current)/len(current))
            chosen=max(('bank512','bank128','bank256'),key=lambda v:means[v])
            physical={'bank512':0,'bank128':128,'bank256':256}[chosen]
            selection=dict(format='gagp-twofold-development-width-v1',epoch=time.time(),data_scope='old development TRAINING only; no fresh heldout',
                rule='maximize pooled paired geometric mean over the frozen8 tasks and both populations; choose one global width',
                physical_block=physical,logical_block=512,geometric_means=means,binary=build['binary'],binary_sha256=build['binary_sha256'])
            serial.save(serial.RAW/(name+'-width-selection.json'),selection)
        state.update(status='full30',width_selection=str(serial.RAW/(name+'-width-selection.json')));serial.save(record,state)
        for population in (1024,16384):
            state['status']=f'full30-p{population}';serial.save(record,state)
            vm_pilot.run(build['binary'],f'{name}-all30-p{population}',[population],protocol['tasks'],mode='full',physical=physical)
        state.update(status='requested_phase_complete',finished_epoch=time.time())
    except BaseException as error:
        state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
        raise
    finally:serial.save(record,state)


if __name__=='__main__':
    parser=ArgumentParser(description=__doc__);parser.add_argument('--name',required=True)
    parser.add_argument('--wait-controller',type=Path)
    parser.add_argument('--build-name',default='build-attempt8')
    parser.add_argument('--apply-controller',type=Path)
    args=parser.parse_args();main(args.name,args.wait_controller,args.build_name,args.apply_controller)
