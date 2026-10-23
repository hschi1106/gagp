"""Wait for a successful pinned base and storage oracle, apply and check a new candidate."""
from argparse import ArgumentParser
import fcntl
import importlib.util
import os
from pathlib import Path
import time
import traceback

import build_projected
import serial


def main(name,build_name,previous_build):
    record=serial.RAW/(name+'-controller.json')
    lease=(serial.RAW/(name+'-controller.lock')).open('a');fcntl.flock(lease,fcntl.LOCK_EX | fcntl.LOCK_NB)
    assert not record.exists(),'preserve failed attempts; use a new controller name'
    names=['apply_projected_shape.py','shape_partition_cache.hpp','projected_shape_result_cache.hpp',
           'test_projected_host.patch.py','test_projected_gpu.patch.py','register_phase.patch.py',
           'virtual_view_lanes.patch.py','authority.patch.py','phase_register_bank.cuh','test_phase_register_gpu.cu']
    paths=[serial.BASE/'prototypes'/n for n in names]+[Path(__file__),serial.BASE/'build_projected.py',serial.BASE/'serial.py']
    state=dict(pid=os.getpid(),status='waiting_for_base_and_storage_probe',started_epoch=time.time(),
        deadline_epoch=serial.DEADLINE,harness_sha256={str(p):serial.sha(p) for p in paths},build_name=build_name,previous_build=previous_build)
    serial.save(record,state)
    try:
        while True:
            build=serial.read(serial.RAW/previous_build/'manifest.json')
            probe=serial.read(serial.RAW/'projected-preflight-attempt3/manifest.json')
            if build['status']=='failed':raise RuntimeError('base build failed; inspect actual cause before applying next candidate')
            if probe['status']=='failed':raise RuntimeError('staged source/storage probe failed; preserve and diagnose')
            if build['status']==probe['status']=='ok':break
            if time.time()>serial.DEADLINE-10800:raise RuntimeError('prerequisite wait exceeded budget')
            time.sleep(10)
        with serial.GPU_LOCK.open('a') as gpu:
            fcntl.flock(gpu,fcntl.LOCK_EX)
            if time.time()>serial.DEADLINE-10800:raise RuntimeError('source apply exceeded budget')
            for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest,'staged followup changed after launch'
            for filename,relative in [('phase_register_bank.cuh','cpp/src/runtime/gpu/device/phase_register_bank.cuh'),
                                      ('test_phase_register_gpu.cu','cpp/tests/gpu/test_phase_register_gpu.cu')]:
                assert serial.sha(serial.BASE/'prototypes'/filename)==serial.sha(serial.RAW/'projected-preflight-attempt3/proposed'/relative),'storage oracle differs from staged source'
            path=serial.BASE/'prototypes/apply_projected_shape.py'
            spec=importlib.util.spec_from_file_location('projected_candidate_apply',path)
            module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
            state.update(status='applying_source',applied_paths=module.apply(previous_build=previous_build));serial.save(record,state)
        state['status']='building';serial.save(record,state)
        build_projected.run(build_name)
        build=serial.read(serial.RAW/build_name/'manifest.json')
        assert build['status']=='ok'
        state.update(status='ok',binary=build['binary'],binary_sha256=build['binary_sha256'],finished_epoch=time.time())
    except BaseException as error:
        state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
        raise
    finally:serial.save(record,state)


if __name__=='__main__':
    parser=ArgumentParser(description=__doc__);parser.add_argument('--name',required=True)
    parser.add_argument('--build-name',required=True)
    parser.add_argument('--previous-build',required=True)
    args=parser.parse_args();main(args.name,args.build_name,args.previous_build)
