"""Wait for the existing native-only recovery, then validate the pinned source.

This local one-shot pipeline launches no search and acquires no GPU lock while
waiting. Source validation itself owns the shared lock and absolute reserves.
"""
import os
from pathlib import Path
import select
import subprocess
import sys
import time
import traceback

import serial


def main():
    record=serial.RAW/'after-native-source0-controller.json'
    assert not record.exists(), 'one-shot source validation already scheduled'
    target='selected-source-validation0'
    assert not (serial.RAW/target).exists(), 'preserve prior source validation'
    recovery=serial.read(serial.RAW/'native-resume2-controller.json')
    pid=recovery['pid']
    state=dict(pid=os.getpid(),status='waiting_for_existing_native_recovery',
        started_epoch=time.time(),deadline_epoch=serial.DEADLINE,wait_pid=pid,
        target=target,harness_sha256={str(Path(__file__).resolve()):serial.sha(__file__)})
    serial.save(record,state)
    try:
        if Path(f'/proc/{pid}').exists():
            fd=os.pidfd_open(pid)
            try:
                poll=select.poll();poll.register(fd,select.POLLIN)
                while not poll.poll(30000):
                    if time.time()>=serial.DEADLINE-7200:
                        raise RuntimeError('two-hour source/report reserve reached while native recovery is still live')
            finally:os.close(fd)
        recovery=serial.read(serial.RAW/'native-resume2-controller.json')
        assert recovery['status'] in ('requested_phase_complete','stopped'), 'native recovery has no terminal record'
        # Check every currently active job independently of its controller.
        for variant in ('baseline','candidate'):
            stage=serial.read(serial.RAW/('best-effort1-speed-full-native-'+variant)/'manifest.json')
            for job in stage['jobs'].values():
                assert job['status'] not in ('running','waiting'), 'native job remains nonterminal'
                if job.get('pid'):
                    proc=Path(f'/proc/{job["pid"]}/stat')
                    assert not proc.exists() or proc.read_text().split()[2]=='Z', 'native child still alive'
        controllers=['best-effort1','best-effort1-quality','best-effort1-speed','native-resume2']
        command=[sys.executable,str(serial.BASE/'validate_selected_source.py'),
            '--selection',str(serial.RAW/'training17-development-selection.json'),'--name',target]
        for name in controllers:command+=['--controller',name]
        log_path=serial.RAW/'selected-source-validation0-launch.log'
        assert not log_path.exists()
        with log_path.open('x') as log:
            child=subprocess.Popen(command,cwd=serial.ROOT,stdout=log,stderr=subprocess.STDOUT,
                                   stdin=subprocess.DEVNULL,start_new_session=True)
            state.update(status='source_validation_running',child_pid=child.pid,command=command)
            serial.save(record,state)
            result=child.wait()
        state.update(status='ok' if result==0 else 'failed',returncode=result,
                     finished_epoch=time.time(),validation=str(serial.RAW/target/'manifest.json'))
    except BaseException as error:
        state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
        raise
    finally:serial.save(record,state)


if __name__=='__main__':main()
