"""Wait for the recorded build/check, then run serialized development pilots."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import time
import traceback

import register_pilot as pilot
import serial


def main(name,build_name,wait_controller):
    controller=serial.RAW/(name+'-controller.json')
    lock=(serial.RAW/(name+'-controller.lock')).open('a')
    fcntl.flock(lock,fcntl.LOCK_EX | fcntl.LOCK_NB)
    if controller.exists():raise RuntimeError('pipeline already recorded; preserve existing attempt')
    state=dict(pid=os.getpid(),status='waiting_for_build',started_epoch=time.time(),deadline_epoch=serial.DEADLINE)
    serial.save(controller,state)
    try:
        state['status']='waiting_for_prior';serial.save(controller,state)
        while True:
            prior=serial.read(wait_controller)
            if prior['status'] in ('requested_phase_complete','stopped'):
                proc=Path(f"/proc/{prior['pid']}/stat")
                if not proc.exists() or proc.read_text().split()[2]=='Z':
                    if prior['status']!='requested_phase_complete':raise RuntimeError('prior shape pilot stopped; inspect preserved failure')
                    break
            if time.time()>serial.DEADLINE-10800:raise RuntimeError('register pilot waiting exceeded budget')
            time.sleep(10)
        state['status']='waiting_for_build';serial.save(controller,state)
        while True:
            build_path=serial.RAW/build_name/'manifest.json'
            if not build_path.exists():
                if time.time()>serial.DEADLINE-10800:raise RuntimeError('build not available within budget')
                time.sleep(10);continue
            build=serial.read(build_path)
            if build['status']=='ok':break
            if build['status']=='failed':raise RuntimeError('prerequisite build/check failed; inspect preserved logs')
            if time.time()>serial.DEADLINE-serial.read(serial.BASE/'protocol.json')['reserve_s']:
                raise RuntimeError('development budget expired before build/check')
            time.sleep(10)
        assert serial.sha(build['binary'])==build['binary_sha256']
        protocol=serial.read(serial.BASE/'protocol.json')
        state.update(status='pilot1024',binary=build['binary'],binary_sha256=build['binary_sha256'])
        serial.save(controller,state)
        pilot.run(build['binary'],name+'-p1024',[1024],protocol['tasks'])
        state['status']='pilot16384';serial.save(controller,state)
        pilot.run(build['binary'],name+'-p16384',[16384],protocol['tasks'])
        state.update(status='requested_phase_complete',finished_epoch=time.time())
    except BaseException as error:
        state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
        raise
    finally:
        serial.save(controller,state)


if __name__=='__main__':
    parser=ArgumentParser(description=__doc__)
    parser.add_argument('--name',required=True)
    parser.add_argument('--build',required=True)
    parser.add_argument('--wait-controller',type=Path,required=True)
    args=parser.parse_args()
    main(args.name,args.build,args.wait_controller)
