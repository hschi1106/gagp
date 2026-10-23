"""Reconstruct the selected immutable C++ source and run real full native checks.

Run explicitly AFTER timed controllers exit. An older selected build must not
be described as validated by tests of the current, different worktree source.
Every command still runs from the support worktree; no checkout is switched.
"""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tarfile
import time
import traceback

import serial


def exited_controller(name):
    state=serial.read(serial.RAW/(name+'-controller.json'))
    assert state['status'] in ('requested_phase_complete','stopped','budget_insufficient'),('timed work is not terminal',name)
    for key in ('pid','child_pid'):
        pid=state.get(key);proc=Path(f'/proc/{pid}/stat')
        assert not proc.exists() or proc.read_text().split()[2]=='Z',('timed process still alive',name,pid)


def main(selection_path,name,controllers):
    out=serial.RAW/name;out.mkdir()
    record=out/'manifest.json'
    state=dict(pid=os.getpid(),status='checking_timed_work_exited',started_epoch=time.time(),
        deadline_epoch=serial.DEADLINE,selection=str(selection_path),selection_sha256=serial.sha(selection_path))
    serial.save(record,state)

    def command(args,tag,timeout=1800):
        remaining=serial.DEADLINE-7200-time.time()
        if remaining<=0:raise RuntimeError('two-hour reporting reserve reached before source validation command')
        log=out/(tag+'.log');assert not log.exists()
        with log.open('x') as stream:
            child=subprocess.Popen(list(map(str,args)),cwd=serial.ROOT,stdout=stream,stderr=subprocess.STDOUT,
                start_new_session=True,env=dict(
                    {k:v for k,v in os.environ.items() if not k.startswith('GAGP_')
                     and k not in ('LD_PRELOAD','MALLOC_CONF')},
                    GAGP_CUDA_DEVICE='0',GAGP_HOST_THREADS='1',OMP_NUM_THREADS='1',
                    OPENBLAS_NUM_THREADS='1',MKL_NUM_THREADS='1'))
            state.update(child_pid=child.pid,current_command=list(map(str,args)),current_log=str(log));serial.save(record,state)
            try:result=child.wait(timeout=min(timeout,remaining))
            except subprocess.TimeoutExpired:
                os.killpg(child.pid,signal.SIGTERM)
                try:child.wait(timeout=3)
                except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait()
                raise
        state.setdefault('commands',[]).append(dict(command=list(map(str,args)),log=str(log),
            log_sha256=serial.sha(log),returncode=result))
        serial.save(record,state)
        if result:raise RuntimeError('source validation command failed: '+tag)
        return log

    try:
        assert controllers,'explicit completed timed controllers required'
        for controller in controllers:exited_controller(controller)
        with serial.GPU_LOCK.open('a') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX)
            for controller in controllers:exited_controller(controller)
            selection=serial.read(selection_path);selected=selection['selected']
            build=serial.read(serial.RAW/selected['build_name']/'manifest.json')
            assert build['status']=='ok'
            assert serial.sha(selected['binary'])==selected['binary_sha256']==build['binary_sha256']
            state.update(status='reconstructing_selected_source',candidate_binary_sha256=build['binary_sha256'],
                candidate_source_sha256=build['source_sha256'],build_name=selected['build_name'])
            serial.save(record,state)
            snapshot=Path(build['binary']).parent/'source-delta'
            base=(snapshot/'basecommit.txt').read_text().strip()
            archive=out/'base-source.tar'
            with archive.open('xb') as stream:
                subprocess.run(['git','archive','--format=tar',base],cwd=serial.ROOT,stdout=stream,check=True)
            mirror=out/'source-mirror';mirror.mkdir()
            with tarfile.open(archive) as source:
                source.extractall(mirror,filter='data')
            patch=snapshot/'tracked.patch';directory=str(mirror.relative_to(serial.ROOT))
            command(['git','apply','--check','--no-index','--directory='+directory,patch],'patch-check',120)
            command(['git','apply','--no-index','--directory='+directory,patch],'patch-apply',120)
            if (snapshot/'cpp').exists():shutil.copytree(snapshot/'cpp',mirror/'cpp',dirs_exist_ok=True)
            for path,digest in build['source_sha256'].items():assert serial.sha(mirror/path)==digest,('reconstructed source differs',path)
            state.update(status='full_release_build',source_archive_sha256=serial.sha(archive),
                source_patch_sha256=serial.sha(patch),source_mirror=str(mirror))
            serial.save(record,state)
            build_dir=out/'build'
            # Match the original recorded Release/CUDA/benchmark contract.
            # ASGP sources stay the original read-only external checkout.
            configure=['cmake','-S',mirror/'cpp','-B',build_dir,'-DCMAKE_BUILD_TYPE=Release',
                '-DCMAKE_CUDA_COMPILER=/usr/local/cuda-12.6/bin/nvcc','-DCMAKE_CUDA_ARCHITECTURES=86',
                '-DGAGP_ENABLE_CUDA=ON','-DGAGP_BUILD_BENCHMARKS=ON',
                '-DGAGP_ASGP_SOURCE_DIR=/home/hschi1106/r13921069','-DGAGP_ASGP_LIST_COMBINE_EXTENSION=ON']
            command(configure,'configure',300)
            # A clean CUDA build recompiles both the VM and the large bounded
            # regression kernels. Their actual23 ptxas stages each exceed ten
            # minutes; allow an hour while retaining the original absolute
            # deadline and two-hour final-report reserve in command().
            command(['cmake','--build',build_dir,'-j','2'],'full-release-build',3600)
            command(['ctest','--test-dir',build_dir,'-N'],'native-test-list',120)
            native=command(['ctest','--test-dir',build_dir,'--output-on-failure'],'full-native')
            text=native.read_text()
            assert '100% tests passed, 0 tests failed out of ' in text
            assert '***Skipped' not in text and 'The following tests did not run' not in text
            for path,digest in build['source_sha256'].items():assert serial.sha(mirror/path)==digest
            rebuilt=build_dir/'gagp_fixed_asgp_bench'
            evidence={str(p):serial.sha(p) for p in [selection_path,Path(__file__),
                serial.RAW/selected['build_name']/'manifest.json',snapshot/'basecommit.txt',patch]}
            evidence.update({row['log']:row['log_sha256'] for row in state['commands']})
            state.update(status='ok',finished_epoch=time.time(),evidence_sha256=evidence,
                validation_rebuilt_binary_sha256=serial.sha(rebuilt),native_summary=text.split('100% tests passed',1)[1][:160],
                scope='full Release/native checks on exact reconstructed selected source; pinned selected binary remains immutable; rebuilt file paths may change its hash')
    except BaseException as error:
        state.update(status='failed',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
    finally:serial.save(record,state)


if __name__=='__main__':
    parser=ArgumentParser(description=__doc__)
    parser.add_argument('--selection',required=True,type=Path);parser.add_argument('--name',required=True)
    parser.add_argument('--controller',required=True,action='append')
    args=parser.parse_args();main(args.selection,args.name,args.controller)
