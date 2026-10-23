"""One-shot checks/report after the existing native/source validation pipeline.

No searches, retries, candidate changes, commits, or publication. Human-facing
plot/browser inspection and final git review remain explicit pending steps.
"""
import fcntl
import os
from pathlib import Path
import select
import subprocess
import sys
import time
import traceback

import serial


def wait_exit(pid):
    if not Path(f'/proc/{pid}').exists(): return
    fd=os.pidfd_open(pid)
    try:
        poll=select.poll();poll.register(fd,select.POLLIN)
        while not poll.poll(30000):
            if time.time()>=serial.DEADLINE:
                raise RuntimeError('original deadline reached while waiting for prerequisite')
    finally:os.close(fd)


def main():
    out=serial.RAW/'closeout1';out.mkdir()
    record=out/'manifest.json'
    state=dict(pid=os.getpid(),status='waiting_for_existing_source_pipeline',
        started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
        source_sha256=serial.sha(__file__),commands=[],
        pending_manual_steps=['actual plots inspection','desktop/mobile browser interactions',
                              'render original dashboard','affected docs/diff review','local commit; no push'])
    serial.save(record,state)

    def command(tag,args,shared_lock=False,timeout=1800):
        assert time.time()<serial.DEADLINE,'original deadline reached'
        if shared_lock:args=['flock',str(serial.GPU_LOCK),*args]
        path=out/(tag+'.log')
        with path.open('x') as log:
            child=subprocess.Popen(list(map(str,args)),cwd=serial.ROOT,
                stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
            state.update(status=tag,child_pid=child.pid,current_command=list(map(str,args)))
            serial.save(record,state)
            try: result=child.wait(timeout=min(timeout,serial.DEADLINE-time.time()))
            except subprocess.TimeoutExpired:
                import signal
                os.killpg(child.pid,signal.SIGTERM)
                try:child.wait(timeout=3)
                except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait()
                raise
        state['commands'].append(dict(tag=tag,command=list(map(str,args)),log=str(path),
                                      log_sha256=serial.sha(path),returncode=result))
        serial.save(record,state)
        assert result==0,('closeout step failed; preserve attempt',tag,result)

    try:
        prior=serial.read(serial.RAW/'selected-source-validation1/manifest.json')
        state['wait_pid']=prior['pid'];serial.save(record,state)
        wait_exit(prior['pid'])
        prior=serial.read(serial.RAW/'selected-source-validation1/manifest.json')
        assert prior['status']=='ok','source prerequisite did not pass'
        validation=serial.RAW/'selected-source-validation1/manifest.json'
        assert serial.read(validation)['status']=='ok'
        controllers=['best-effort1','best-effort1-quality','best-effort1-speed','native-resume2']
        control_args=[]
        for name in controllers:control_args+=['--controller',name]
        command('source-alignment',[sys.executable,serial.BASE/'align_selected_source.py',
            '--validation',validation,'--prior-build','build-attempt26','--name','selected-source-alignment0',
            *control_args,'--execute'])
        command('docs-alignment',[sys.executable,serial.BASE/'align_selected_docs.py',
            '--alignment',serial.RAW/'selected-source-alignment0/manifest.json',
            '--name','selected-docs-alignment0','--execute'])
        checked=sorted(serial.BASE.glob('*.py'))
        state['study_check_source_sha256']={str(p):serial.sha(p) for p in checked}
        serial.save(record,state)
        for p in checked:compile(p.read_text(),str(p),'exec')
        command('study-tests',[sys.executable,'-m','unittest','discover','-s',serial.BASE,
            '-p','test_*.py','-v'],shared_lock=True,timeout=300)
        assert all(serial.sha(p)==h for p,h in state['study_check_source_sha256'].items()),'study code changed during checks'
        command('tools-tests',[sys.executable,'-m','unittest','discover','-s','tools/tests',
            '-p','test_*.py','-v'],shared_lock=True,timeout=600)
        command('repository-tests',[sys.executable,'-m','unittest','discover','-s','tests/repository',
            '-p','test_*.py','-v'],shared_lock=True,timeout=300)
        command('report-help',[sys.executable,serial.BASE/'report_compact.py','--help'],shared_lock=True,timeout=120)
        report=serial.BASE/'final'
        assert not report.exists(),'preserve every report attempt'
        command('report',[sys.executable,serial.BASE/'report_compact.py',
            '--speed-freeze',serial.RAW/'best-effort1-speed-freeze.json',
            '--quality-freeze',serial.RAW/'best-effort1-quality-freeze.json',
            '--speed-name','best-effort1-speed','--quality-name','best-effort1-quality',
            '--regressions',validation,'--out',report],timeout=3600)
        command('preset-export',[sys.executable,serial.BASE/'winner_preset2.py',
            '--freeze',serial.RAW/'best-effort1-speed-freeze.json',
            '--out',report/'winner_preset.json'],shared_lock=True,timeout=120)
        state.update(status='artifacts_ready_manual_review_pending',finished_epoch=time.time(),
                     report=str(report),target_reached=False)
    except BaseException as error:
        state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
        raise
    finally:serial.save(record,state)


if __name__=='__main__':main()
