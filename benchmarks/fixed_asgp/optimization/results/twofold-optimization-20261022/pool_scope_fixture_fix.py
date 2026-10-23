"""Make the new evolution fixture explicitly request the native owner it tests."""
import fcntl
import os
from pathlib import Path
import shutil
import time
import traceback

import build_pool
import serial

record=serial.RAW/'pool-corrected2-controller.json';assert not record.exists()
state=dict(pid=os.getpid(),status='waiting_for_failed15_exit',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
    reason='test-only: original private fixture used NativePhasePopulation directly; new evolve fixture must explicitly set/restore GAGP_GPU_PHASE_POPULATION',
    harness_sha256={str(p):serial.sha(p) for p in (Path(__file__),serial.BASE/'build_pool.py',serial.BASE/'serial.py')})
serial.save(record,state)
try:
    while True:
        prior=serial.read(serial.RAW/'build-attempt15/manifest.json')
        if prior['status'] in ('ok','failed'):
            proc=Path(f'/proc/{prior["pid"]}/stat')
            if not proc.exists() or proc.read_text().split()[2]=='Z':break
        if time.time()>serial.DEADLINE-10800:raise RuntimeError('native fixture correction missed budget')
        time.sleep(10)
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest
        assert prior['status']=='failed' and prior['results']==[0,0,8]
        text=(serial.RAW/'build-attempt15/check2.log').read_text()
        assert 'CUDA pool did not use the strict native GPU reproduction path' in text and '1 tests failed out of 7' in text
        for path,digest in prior['source_sha256'].items():assert serial.sha(serial.ROOT/path)==digest,path
        archived=serial.RAW/'build-attempt15/unadmitted-bench';assert not archived.exists()
        shutil.copy2(serial.ROOT/'cpp/build/release/gagp_fixed_asgp_bench',archived)
        path=serial.ROOT/'cpp/tests/gpu/test_gpu_phase_population.cu';text=path.read_text();before=serial.sha(path)
        start=text.index('void check_pool_evolution(');end=text.index('void check_pool_session_lifetime(',start)
        fixture=text[start:end]
        fixture=fixture.replace('"GAGP_GPU_PHASE_STAGED_BUFFERS"};','"GAGP_GPU_PHASE_STAGED_BUFFERS","GAGP_GPU_PHASE_POPULATION"};')
        fixture=fixture.replace('saved[2];bool present[2]','saved[3];bool present[3]').replace('i<2','i<3')
        fixture=fixture.replace(' config.generations=4;', ' setenv(flags[2],"1",1);\n config.generations=4;')
        assert 'saved[3]' in fixture and 'setenv(flags[2],"1",1)' in fixture
        path.write_text(text[:start]+fixture+text[end:])
        serial.save(serial.RAW/'pool-native-scope-fixture-correction.json',dict(reason=state['reason'],epoch=time.time(),
            source_before={str(path.relative_to(serial.ROOT)):before},source_after={str(path.relative_to(serial.ROOT)):serial.sha(path)},
            unadmitted_binary=str(archived),unadmitted_binary_sha256=serial.sha(archived)))
        state['status']='building';serial.save(record,state)
    build_pool.run('build-attempt16')
    build=serial.read(serial.RAW/'build-attempt16/manifest.json');assert build['status']=='ok'
    state.update(status='ok',binary=build['binary'],binary_sha256=build['binary_sha256'],finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
    raise
finally:serial.save(record,state)
