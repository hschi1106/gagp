"""Correct the actual native fixture fuel failure plus reviewed history count."""
import fcntl
import os
from pathlib import Path
import shutil
import time
import traceback

import build_pool
import serial

record=serial.RAW/'pool-corrected1-controller.json';assert not record.exists()
state=dict(pid=os.getpid(),status='waiting_for_lock',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
    reason='test-only correction: evolve config must use compiled grammar fuel; G4 history has4 pre-reproduction champions, final is separate',
    harness_sha256={str(p):serial.sha(p) for p in (Path(__file__),serial.BASE/'build_pool.py',serial.BASE/'serial.py')})
serial.save(record,state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest
        prior=serial.read(serial.RAW/'build-attempt14/manifest.json')
        assert prior['status']=='failed' and prior['results']==[0,0,8]
        proc=Path(f'/proc/{prior["pid"]}/stat');assert not proc.exists() or proc.read_text().split()[2]=='Z'
        text=(serial.RAW/'build-attempt14/check2.log').read_text()
        assert 'compiled grammar execution fuel must match EvolutionConfig fuel' in text
        assert '1 tests failed out of 7' in text
        for path,digest in prior['source_sha256'].items():assert serial.sha(serial.ROOT/path)==digest,path
        archived=serial.RAW/'build-attempt14/unadmitted-bench';assert not archived.exists()
        shutil.copy2(serial.ROOT/'cpp/build/release/gagp_fixed_asgp_bench',archived)
        path=serial.ROOT/'cpp/tests/gpu/test_gpu_phase_population.cu';text=path.read_text();before=serial.sha(path)
        changes=[('before.history_best.size()==5 && before.history_best_fitness==after.history_best_fitness',
                  'before.history_best.size()==4 && before.history_best_fitness==after.history_best_fitness'),
                 ('  config.fuel=audit_fuel;config.penalty=1000;config.gpu_blocksize=32;config.fitness_policy=policy;',
                  '  config.fuel=grammar->execution_limits().fuel;config.penalty=1000;config.gpu_blocksize=32;config.fitness_policy=policy;'),
                 ('  check_pool_evolution(evolution_cases,config,parents);',
                  '  evolution_cases.resize(37); // Partial warp and the1024-element input remain in the pool fixture.\n  check_pool_evolution(evolution_cases,config,parents);')]
        for old,new in changes:
            assert text.count(old)==1,(old,text.count(old));text=text.replace(old,new)
        path.write_text(text)
        serial.save(serial.RAW/'pool-native-fixture-correction.json',dict(reason=state['reason'],epoch=time.time(),
            scope='only new pooled evolution fixture; unchanged original32-generation1024-case coverage and raw low-fuel tests',
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
