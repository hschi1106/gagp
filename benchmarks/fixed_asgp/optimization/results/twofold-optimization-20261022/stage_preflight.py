"""Standalone stage-storage lifetime/fragmentation probe, without CPP mutation."""
import fcntl
import os
import shutil
import subprocess
import time
import traceback

import serial

out=serial.RAW/'stage-storage-preflight0';out.mkdir();record=out/'manifest.json'
state=dict(pid=os.getpid(),status='waiting_for_lock',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
    scope='standalone allocator ordering/capacity only; no native evolution or speed claim; CPP unchanged')
serial.save(record,state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        proposed=out/'proposed';proposed.mkdir()
        inputs=[serial.BASE/'prototypes'/n for n in ['cuda_stage_storage.hpp','stage_storage_gpu.hpp','stage_storage.cu','test_stage_storage.cu']]
        inputs += [serial.ROOT/'cpp/src/runtime/gpu/allocation_pool.cu',serial.ROOT/'cpp/src/runtime/gpu/cuda_allocation_scope.hpp',serial.ROOT/'cpp/include/gagp/runtime/gpu/allocation_gpu.hpp']
        state.update(status='running',source_sha256={str(p):serial.sha(p) for p in inputs})
        for path in inputs:
            target=proposed/path.name if path.name!='allocation_gpu.hpp' else proposed/'include/gagp/runtime/gpu/allocation_gpu.hpp'
            target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
        path=proposed/'allocation_pool.cu';text=path.read_text()
        text=text.replace('#include "cuda_allocation_scope.hpp"','#include "cuda_allocation_scope.hpp"\n#include "stage_storage_gpu.hpp"')
        text=text.replace('cudaError_t cuda_pool_malloc(void** pointer,std::size_t bytes) {',
            'cudaError_t cuda_pool_malloc(void** pointer,std::size_t bytes) {\n  if(cuda_stage_reproduction_active())return cuda_stage_workspace_malloc(pointer,bytes);')
        text=text.replace('cudaError_t cuda_pool_free(void* pointer) {',
            'cudaError_t cuda_pool_free(void* pointer) {\n  cudaError_t stage_status;\n  if(cuda_stage_free_if_owned(pointer,&stage_status))return stage_status;')
        path.write_text(text)
        compiler=next(x.split('=',1)[1] for x in (serial.ROOT/'cpp/build/release/CMakeCache.txt').read_text().splitlines() if x.startswith('CMAKE_CUDA_COMPILER:STRING='))
        commands=[[compiler,'-std=c++17','-arch=sm_86','-I'+str(proposed/'include'),str(path),str(proposed/'stage_storage.cu'),str(proposed/'test_stage_storage.cu'),'-o',str(out/'stage-probe')],[str(out/'stage-probe')]]
        state['commands']=commands
        for index,command in enumerate(commands):
            with (out/f'check{index}.log').open('x') as log:
                child=subprocess.Popen(command,cwd=serial.ROOT,stdout=log,stderr=subprocess.STDOUT)
                state.update(child_pid=child.pid,current_command=index);serial.save(record,state);result=child.wait()
            state.setdefault('results',[]).append(result);serial.save(record,state)
            if result:raise RuntimeError(f'stage allocator probe{index} failed({result})')
        state.update(status='ok',binary_sha256=serial.sha(out/'stage-probe'),finished_epoch=time.time());serial.save(record,state)
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());serial.save(record,state)
    raise
