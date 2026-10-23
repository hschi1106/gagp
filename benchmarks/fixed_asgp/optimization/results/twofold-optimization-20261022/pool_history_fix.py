"""Preserve14, correct the history test's G versus G+final expectation, rebuild15."""
import fcntl
import os
from pathlib import Path
import shutil
import time
import traceback

import build_pool
import serial

record=serial.RAW/'pool-corrected0-controller.json';assert not record.exists()
state=dict(pid=os.getpid(),status='waiting_for_expected14_failure',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
    reason='source review: history_best has G pre-reproduction champions; final champion is result.best; G4 history size is4, not5',
    harness_sha256={str(p):serial.sha(p) for p in (Path(__file__),serial.BASE/'build_pool.py',serial.BASE/'serial.py')})
serial.save(record,state)
try:
    while True:
        path=serial.RAW/'build-attempt14/manifest.json';prior=serial.read(path) if path.exists() else {}
        if prior.get('status') in ('ok','failed'):
            proc=Path(f'/proc/{prior["pid"]}/stat')
            if not proc.exists() or proc.read_text().split()[2]=='Z':break
        if time.time()>serial.DEADLINE-10800:raise RuntimeError('test correction prerequisite missed budget')
        time.sleep(10)
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest
        assert prior['status']=='failed' and prior['results']==[0,0,8],'unexpected14 result; inspect instead of assuming test-only failure'
        text=(serial.RAW/'build-attempt14/check2.log').read_text()
        assert 'CUDA pool changed complete history/score coverage' in text
        assert '1 tests failed out of 7' in text and 'gagp_test_gpu_phase_population' in text
        for path,digest in prior['source_sha256'].items():assert serial.sha(serial.ROOT/path)==digest,path
        archived=serial.RAW/'build-attempt14/unadmitted-bench';assert not archived.exists()
        shutil.copy2(serial.ROOT/'cpp/build/release/gagp_fixed_asgp_bench',archived)
        path=serial.ROOT/'cpp/tests/gpu/test_gpu_phase_population.cu';text=path.read_text();before=serial.sha(path)
        old='before.history_best.size()==5 && before.history_best_fitness==after.history_best_fitness'
        assert text.count(old)==1
        path.write_text(text.replace(old,'before.history_best.size()==4 && before.history_best_fitness==after.history_best_fitness'))
        serial.save(serial.RAW/'pool-history-test-correction.json',dict(reason=state['reason'],epoch=time.time(),
            source_before={str(path.relative_to(serial.ROOT)):before},source_after={str(path.relative_to(serial.ROOT)):serial.sha(path)},
            unadmitted_binary=str(archived),unadmitted_binary_sha256=serial.sha(archived)))
        state.update(status='building');serial.save(record,state)
    build_pool.run('build-attempt15')
    build=serial.read(serial.RAW/'build-attempt15/manifest.json');assert build['status']=='ok'
    state.update(status='ok',binary=build['binary'],binary_sha256=build['binary_sha256'],finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
    raise
finally:serial.save(record,state)
