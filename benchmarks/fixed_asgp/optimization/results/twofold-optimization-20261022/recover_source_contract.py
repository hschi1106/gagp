"""Recover an isolated-mirror missing historical doc fixture, without C++ edits.

Keep the original135/136 result. Copy the real ignored manifest byte-exact and
rerun only its repository-contract gate; never claim a single fresh136 pass.
"""
import copy
import fcntl
import os
from pathlib import Path
import shutil
import subprocess
import time
import traceback

import serial
from validate_selected_source import exited_controller


def main():
    out=serial.RAW/'selected-source-validation1';out.mkdir()
    record=out/'manifest.json'
    old_path=serial.RAW/'selected-source-validation0/manifest.json'
    old=serial.read(old_path)
    state=copy.deepcopy(old)
    state.update(pid=os.getpid(),status='checking_isolated_doc_fixture',started_epoch=time.time(),
        prior_validation=str(old_path),prior_validation_sha256=serial.sha(old_path),
        recovery_harness_sha256=serial.sha(__file__))
    serial.save(record,state)
    try:
        for name in ('best-effort1','best-effort1-quality','best-effort1-speed','native-resume2','after-native-source0'):
            exited_controller(name) if name!='after-native-source0' else None
        for obj in (old,serial.read(serial.RAW/'after-native-source0-controller.json'),serial.read(serial.RAW/'closeout0/manifest.json')):
            for key in ('pid','child_pid'):
                pid=obj.get(key);proc=Path(f'/proc/{pid}/stat')
                assert not proc.exists() or proc.read_text().split()[2]=='Z',('prior source/check process live',pid)
        assert old['status']=='failed' and old['commands'][-1]['returncode']==8
        with serial.GPU_LOCK.open('a') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX)
            mirror=Path(old['source_mirror'])
            for path,digest in old['candidate_source_sha256'].items():assert serial.sha(mirror/path)==digest
            native=Path(old['commands'][-1]['log']);text=native.read_text()
            assert '99% tests passed, 1 tests failed out of 136' in text
            assert '\t 60 - gagp_test_repository_contract (Failed)' in text
            assert '***Skipped' not in text
            relative=Path('logs/all-task-repair-20261015/final-all30-g4/manifest.json')
            assert str(relative) in text
            source=serial.ROOT/relative;target=mirror/relative
            assert source.is_file() and not target.exists(), 'missing real historical doc fixture must be the cause'
            fixture_sha=serial.sha(source)
            target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
            assert serial.sha(target)==fixture_sha
            build=serial.RAW/'selected-source-validation0/build'
            args=['ctest','--test-dir',str(build),'-R','^gagp_test_repository_contract$','--output-on-failure']
            log=out/'repository-contract.log'
            env={k:v for k,v in os.environ.items() if not k.startswith('GAGP_') and k not in ('LD_PRELOAD','MALLOC_CONF')}
            with log.open('x') as stream:
                child=subprocess.Popen(args,cwd=serial.ROOT,env=env,stdout=stream,stderr=subprocess.STDOUT,start_new_session=True)
                state.update(status='repository_contract_recheck',child_pid=child.pid,current_command=args);serial.save(record,state)
                result=child.wait(timeout=min(120,serial.DEADLINE-time.time()))
            state['commands'].append(dict(command=args,log=str(log),log_sha256=serial.sha(log),returncode=result))
            assert result==0 and '100% tests passed, 0 tests failed out of 1' in log.read_text()
            for path,digest in old['candidate_source_sha256'].items():assert serial.sha(mirror/path)==digest
            snapshot=Path(serial.read(old['selection'])['selected']['binary']).parent/'source-delta'
            evidence={str(p):serial.sha(p) for p in [old_path,Path(__file__).resolve(),Path(old['selection']),
                serial.RAW/old['build_name']/'manifest.json',snapshot/'basecommit.txt',snapshot/'tracked.patch',
                native,source,target,log]}
            evidence.update({row['log']:row['log_sha256'] for row in state['commands']})
            state.update(status='ok',finished_epoch=time.time(),evidence_sha256=evidence,
                validation_rebuilt_binary_sha256=serial.sha(build/'gagp_fixed_asgp_bench'),
                native_summary='Original full native135/136; isolated missing historical doc fixture staged byte-exact; repository contract targeted1/1 PASS. No single fresh136/136 claim.',
                recovery=dict(kind='missing_ignored_historical_doc_fixture',fixture=str(relative),fixture_sha256=fixture_sha,
                    cpp_unchanged=True,original_full_native_passed=135,original_full_native_total=136,targeted_repository_passed=1,
                    prior_failure_preserved=True),
                scope='exact selected C++ clean Release build; all runtime/native tests passed in original full run; unchanged repository contract passed after restoring real historical fixture; pinned measured binary unchanged')
    except BaseException as error:
        state.update(status='failed',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
    finally:serial.save(record,state)


if __name__=='__main__':main()
