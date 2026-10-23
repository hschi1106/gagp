"""Preserve failed12 and add the CPU library required by its new oracle."""
import fcntl
import os
from pathlib import Path
import shutil
import subprocess
import time
import traceback

import build_projected
import serial

record=serial.RAW/'unchanged-linked-controller.json';assert not record.exists()
state=dict(pid=os.getpid(),status='waiting_for_lock',started_epoch=time.time(),deadline_epoch=serial.DEADLINE)
serial.save(record,state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        previous=serial.read(serial.RAW/'build-attempt12/manifest.json')
        assert previous['status']=='failed' and previous['results']==[0,2]
        proc=Path(f'/proc/{previous["pid"]}/stat');assert not proc.exists() or proc.read_text().split()[2]=='Z'
        error=(serial.RAW/'build-attempt12/check1.log').read_text()
        assert 'undefined reference to `gagp::eval_fitness_cpu(' in error
        for path,digest in previous['source_sha256'].items():assert serial.sha(serial.ROOT/path)==digest
        built=serial.ROOT/'cpp/build/release/gagp_fixed_asgp_bench'
        archived=serial.RAW/'build-attempt12/unadmitted-bench';assert not archived.exists();shutil.copy2(built,archived)
        path=serial.ROOT/'cpp/CMakeLists.txt';text=path.read_text();before=serial.sha(path)
        old='target_link_libraries(gagp_test_fitness_accumulator_gpu PRIVATE gagp_gpu)'
        assert text.count(old)==1
        path.write_text(text.replace(old,'target_link_libraries(gagp_test_fitness_accumulator_gpu PRIVATE gagp_gpu gagp_runtime_cpu)'))
        serial.save(serial.RAW/'unchanged-cpu-oracle-link-correction.json',dict(epoch=time.time(),
            reason='new near-limit oracle calls eval_fitness_cpu; add its actual CMake dependency; runtime source unchanged from12',
            source_before={str(path.relative_to(serial.ROOT)):before},source_after={str(path.relative_to(serial.ROOT)):serial.sha(path)},
            unadmitted_binary=str(archived),unadmitted_binary_sha256=serial.sha(archived)))
        state['status']='building';serial.save(record,state)
    build_projected.run('build-attempt13')
    build=serial.read(serial.RAW/'build-attempt13/manifest.json');assert build['status']=='ok'
    state.update(status='ok',binary=build['binary'],binary_sha256=build['binary_sha256'],finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
    raise
finally:serial.save(record,state)
