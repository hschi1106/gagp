"""Build, check and pin one immutable candidate under the shared GPU lock."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import shutil
import subprocess
import time
import traceback

import serial


def run(name,wait_controller=None):
    attempt=serial.RAW/name
    attempt.mkdir()
    commands=[['cmake','-S','cpp','-B','cpp/build/release'],
        ['cmake','--build','cpp/build/release','--target','gagp_fixed_asgp_bench',
         'gagp_test_fitness_accumulator_gpu','gagp_test_gpu_phase_population',
         'gagp_test_fitness_cpu_gpu_parity','gagp_test_fitness_contract','gagp_test_bounded_region_gpu','gagp_test_phase_register_gpu','gagp_test_cuda_allocation_pool','gagp_test_linear_elite_gpu','gagp_test_cuda_stage_storage','gagp_test_grammar_membership','gagp_test_grammar_bounded_membership','gagp_test_compiler_lowering','gagp_test_region_bytecode_verify','-j','2'],
        ['ctest','--test-dir','cpp/build/release','-R',
         '^(gagp_test_fitness_accumulator_gpu|gagp_test_gpu_phase_population|gagp_test_fitness_cpu_gpu_parity|gagp_test_fitness_contract|gagp_test_bounded_region_gpu|gagp_test_phase_register_gpu|gagp_test_cuda_allocation_pool|gagp_test_linear_elite_gpu|gagp_test_cuda_stage_storage|gagp_test_grammar_membership|gagp_test_grammar_bounded_membership|gagp_test_compiler_lowering|gagp_test_region_bytecode_verify)$',
         '--output-on-failure']]
    state=dict(status='waiting',pid=os.getpid(),commands=commands,started_epoch=time.time(),
               deadline_epoch=serial.DEADLINE,harness=str(Path(__file__)),harness_sha256=serial.sha(Path(__file__)))
    serial.save(attempt/'manifest.json',state)
    try:
        if wait_controller:
            state['waiting_for_controller']=str(wait_controller);serial.save(attempt/'manifest.json',state)
            while True:
                previous=serial.read(wait_controller)
                if previous['status'] in ('requested_phase_complete','stopped'):
                    pid=previous['pid'];proc=Path(f'/proc/{pid}/stat')
                    if not proc.exists() or proc.read_text().split()[2]=='Z':break
                if time.time()>serial.DEADLINE-10800:raise RuntimeError('build waiting exceeded experiment budget')
                time.sleep(10)
        with serial.GPU_LOCK.open('a') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX)
            paths=[serial.ROOT/p for p in subprocess.check_output(
                ['git','ls-files','--cached','--others','--exclude-standard','cpp'],cwd=serial.ROOT,text=True).splitlines()]
            snapshot=attempt/'source-delta'
            snapshot.mkdir()
            (snapshot/'basecommit.txt').write_text(subprocess.check_output(
                ['git','rev-parse','HEAD'],cwd=serial.ROOT,text=True))
            (snapshot/'tracked.patch').write_bytes(subprocess.check_output(
                ['git','diff','--binary','HEAD','--','cpp'],cwd=serial.ROOT))
            for entry in subprocess.check_output(['git','ls-files','--others','--exclude-standard','cpp'],
                    cwd=serial.ROOT,text=True).splitlines():
                target=snapshot/entry;target.parent.mkdir(parents=True,exist_ok=True)
                shutil.copy2(serial.ROOT/entry,target)
            shutil.copy2(Path(__file__),snapshot/'build_candidate.py')
            state.update(status='running',source_sha256={str(p.relative_to(serial.ROOT)):serial.sha(p) for p in paths})
            serial.save(attempt/'manifest.json',state)
            for i,command in enumerate(commands):
                state['current_command']=i
                with (attempt/f'check{i}.log').open('w') as log:
                    child=subprocess.Popen(command,cwd=serial.ROOT,stdout=log,stderr=subprocess.STDOUT,
                                           start_new_session=True)
                    state['child_pid']=child.pid;serial.save(attempt/'manifest.json',state)
                    result=child.wait()
                state.setdefault('results',[]).append(result)
                serial.save(attempt/'manifest.json',state)
                if result:raise RuntimeError(f'command {i} failed ({result})')
            test_log=(attempt/'check2.log').read_text()
            assert 'gagp_test_cuda_stage_storage' in test_log and 'gagp_test_cuda_allocation_pool' in test_log and '***Skipped' not in test_log,'pool correctness test must actually run'
            assert '100% tests passed, 0 tests failed out of 13' in test_log,'thirteen targeted tests must pass'
            assert all(serial.sha(serial.ROOT/p)==h for p,h in state['source_sha256'].items()),'source changed during build/checks'
            shutil.copy2(serial.ROOT/'cpp/build/release/gagp_fixed_asgp_bench',attempt/'bench')
            state.update(status='ok',binary=str(attempt/'bench'),binary_sha256=serial.sha(attempt/'bench'),
                         finished_epoch=time.time())
            serial.save(attempt/'manifest.json',state)
    except BaseException as error:
        state.update(status='failed',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
        serial.save(attempt/'manifest.json',state)
        raise


if __name__=='__main__':
    parser=ArgumentParser(description=__doc__)
    parser.add_argument('--name',required=True)
    parser.add_argument('--wait-controller',type=Path)
    args=parser.parse_args()
    run(args.name,args.wait_controller)
