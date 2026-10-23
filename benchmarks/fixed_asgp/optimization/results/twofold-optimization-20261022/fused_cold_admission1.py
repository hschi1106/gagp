"""Admit actual build22; preserve candidate0's mistaken completion reference.

candidate0 correctly builds/tests22, but its final metadata read names build21.
No build or search is repeated. Only the actual22 manifest can authorize trials.
"""
import fcntl
import os
from pathlib import Path
import time
import traceback

import serial

name='fused-cold-admission1';record=serial.RAW/(name+'-controller.json');assert not record.exists()
state=dict(pid=os.getpid(),status='waiting_for_actual_build22',started_epoch=time.time(),deadline_epoch=serial.DEADLINE)
serial.save(record,state)
try:
    original_path=serial.RAW/'fused-cold-candidate0-controller.json'
    while True:
        original=serial.read(original_path)
        path=serial.RAW/'build-attempt22/manifest.json';build=serial.read(path) if path.exists() else {}
        if original['status']=='stopped' or build.get('status')=='failed':
            raise RuntimeError('actual22 build/native checks failed; preserve attempt, no admission')
        proc=Path(f'/proc/{original["pid"]}/stat')
        if build.get('status')=='ok' and original['status']=='ok' and (
                not proc.exists() or proc.read_text().split()[2]=='Z'):break
        if time.time()>serial.DEADLINE-10800:raise RuntimeError('admission missed original budget')
        time.sleep(10)
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        assert build['results']==[0,0,0]
        assert '100% tests passed, 0 tests failed out of 13' in (path.parent/'check2.log').read_text()
        assert serial.sha(build['binary'])==build['binary_sha256']
        for source,digest in build['source_sha256'].items():assert serial.sha(serial.ROOT/source)==digest
        assert original['binary_sha256']==serial.read(serial.RAW/'build-attempt21/manifest.json')['binary_sha256']
        correction=serial.RAW/'fused-cold-completion-metadata-correction0.json';assert not correction.exists()
        serial.save(correction,dict(original_controller=str(original_path),original_sha256=serial.sha(original_path),
            error='candidate0 post-build metadata read references21 after correctly running actual build22',
            corrected_build_manifest=str(path),corrected_build_manifest_sha256=serial.sha(path),
            actual_binary=build['binary'],actual_binary_sha256=build['binary_sha256'],
            no_build_or_search_repeated=True,epoch=time.time()))
        state.update(status='ok',binary=build['binary'],binary_sha256=build['binary_sha256'],
                     source_sha256=build['source_sha256'],correction=str(correction),
                     correction_sha256=serial.sha(correction),finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
finally:serial.save(record,state)
