"""Audit pinned baseline/source reconstruction and the new build's provenance."""
import fcntl
import hashlib
import io
import os
from pathlib import Path
import subprocess
import time
import traceback

import serial


def git_blobs(head, paths):
    queries=[head+':'+path for path in paths]
    result=subprocess.run(['git','cat-file','--batch'],cwd=serial.ROOT,
        input=('\n'.join(queries)+'\n').encode(),capture_output=True,check=True)
    stream=io.BytesIO(result.stdout);hashes={};missing=[]
    for path,query in zip(paths,queries):
        header=stream.readline().decode().rstrip('\n')
        if header==query+' missing':missing.append(path);continue
        identity,kind,size=header.split();assert kind=='blob'
        data=stream.read(int(size));assert len(data)==int(size) and stream.read(1)==b'\n'
        hashes[path]=hashlib.sha256(data).hexdigest()
    assert not stream.read()
    return hashes,missing


name='provenance-build16-attempt1';out=serial.RAW/name;out.mkdir()
record=out/'manifest.json'
state=dict(status='waiting_for_build',pid=os.getpid(),started_epoch=time.time(),deadline_epoch=serial.DEADLINE)
serial.save(record,state)
try:
    while True:
        path=serial.RAW/'build-attempt16/manifest.json'
        build=serial.read(path) if path.exists() else {}
        if (serial.read(serial.RAW/'elite-candidate3-controller.json') if (serial.RAW/'elite-candidate3-controller.json').exists() else {}).get('status')=='stopped':raise RuntimeError('elite candidate prerequisite stopped')
        if build.get('status')=='failed':raise RuntimeError('build16 failed; no successful provenance claim')
        if build.get('status')=='ok':break
        if time.time()>serial.DEADLINE-10800:raise RuntimeError('source audit missed budget')
        time.sleep(10)
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        state['status']='auditing';serial.save(record,state)
        protocol=serial.read(serial.BASE/'protocol.json')
        previous_path=serial.ROOT/'logs/scaling-optimization-20261018/formal-freeze.json'
        previous=serial.read(previous_path)
        assert serial.sha(protocol['baseline_binary'])==protocol['baseline_sha256']==previous['binary_sha256']
        assert serial.sha(build['binary'])==build['binary_sha256']
        blobs,missing=git_blobs(protocol['source_head'],list(previous['source_sha256']))
        different=[path for path,digest in previous['source_sha256'].items() if path in blobs and blobs[path]!=digest]
        external={path:serial.sha(path) for path in previous['external_source_sha256']}
        assert external==previous['external_source_sha256'],'external ASGP sources/overlay changed'
        for path,digest in build['source_sha256'].items():
            assert serial.sha(serial.ROOT/path)==digest,'candidate source changed after native admission'
        linked={}
        for path in previous['linked_input_sha256']:
            if Path(path).exists():linked[path]=serial.sha(path)
        flags={str(path):dict(sha256=serial.sha(path),text=path.read_text()) for path in [
            serial.ROOT/'cpp/build/release/CMakeFiles/gagp_fixed_asgp_bench.dir/link.txt',
            serial.ROOT/'cpp/build/release/CMakeFiles/gagp_gpu.dir/flags.make',
            serial.ROOT/'cpp/build/release/CMakeFiles/gagp_fixed_asgp_bench.dir/flags.make']}
        allocator=protocol['settings']['LD_PRELOAD']
        state.update(status='ok' if not missing and not different else 'source_reconstruction_incomplete',
            baseline_binary=protocol['baseline_binary'],baseline_binary_sha256=protocol['baseline_sha256'],
            baseline_source_commit=protocol['source_head'],baseline_source_sha256=previous['source_sha256'],
            baseline_source_missing=missing,baseline_source_different=different,
            baseline_prior_freeze=str(previous_path),baseline_prior_freeze_sha256=serial.sha(previous_path),
            baseline_validation='binary hash matches previously audited study; current git blobs independently compared with its source map',
            baseline_linked_objects='historical objects overwritten by new build; old hashes are historical evidence, not freshly revalidated objects',
            candidate_binary=build['binary'],candidate_binary_sha256=build['binary_sha256'],
            candidate_source_sha256=build['source_sha256'],candidate_linked_inputs_sha256=linked,
            external_source_sha256=external,compiler_flags=flags,
            allocator=dict(path=allocator,sha256=serial.sha(allocator)),finished_epoch=time.time())
        serial.save(record,state)
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
    serial.save(record,state)
    raise
