"""Preserve interrupted10, remove unaccounted shared memory, build/check11."""
import fcntl
import os
from pathlib import Path
import subprocess
import time
import traceback

import build_projected
import serial

record=serial.RAW/'unchanged-corrected-controller.json'
assert not record.exists()
state=dict(pid=os.getpid(),status='waiting_for_lock',started_epoch=time.time(),deadline_epoch=serial.DEADLINE)
serial.save(record,state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        prior=serial.read(serial.RAW/'build-attempt10/manifest.json')
        stopped=serial.read(serial.RAW/'unchanged-candidate-controller.json')
        assert prior['status']=='failed' and stopped['status']=='stopped'
        assert prior['results']==[0,-15] and (serial.RAW/'build-attempt10/authorized-interruption.json').exists()
        for line in subprocess.check_output(['ps','-eo','pid,pgid,stat'],text=True).splitlines()[1:]:
            pid,group,status=line.split()
            assert not ((int(pid)==prior['pid'] or int(group)==prior['child_pid']) and not status.startswith('Z')),'old build process remains active'
        for path,digest in prior['source_sha256'].items():assert serial.sha(serial.ROOT/path)==digest,'source changed since interrupted10'
        kernel=serial.ROOT/'cpp/src/runtime/gpu/device/kernels.cuh';text=kernel.read_text()
        old='''      __shared__ int next_reused_program;
      if(tid==0)next_reused_program=atomicAdd(next_program,1);
      __syncthreads();logical_idx=static_cast<std::size_t>(next_reused_program);__syncthreads();'''
        new='''      // Reuse the already-admitted dynamic code/score scratch. Adding
      // static shared storage would reduce the public shared-memory limit.
      extern __shared__ unsigned char reuse_queue_scratch[];
      auto* next_reused_program=reinterpret_cast<int*>(reuse_queue_scratch);
      if(tid==0)*next_reused_program=atomicAdd(next_program,1);
      __syncthreads();logical_idx=static_cast<std::size_t>(*next_reused_program);__syncthreads();'''
        assert text.count(old)==1
        test=serial.ROOT/'cpp/tests/gpu/test_fitness_accumulator_gpu.cu';check=test.read_text()
        check=check.replace('#include <cuda_runtime.h>','#include <cuda_runtime.h>\n#include "gagp/runtime/gpu/fitness_gpu.hpp"\n#include "gagp/runtime/gpu/device_types_gpu.hpp"',1)
        addition=r'''void check_shared_memory_boundary() {
  const gagp::FitnessPolicy policy{gagp::FitnessMetric::Exact};
  gagp::FitnessSessionGpu session;
  if(!session.init({{}},{gagp::Value::from_int(0)},100000,1,1,policy).ok)
    throw std::runtime_error("shared-memory boundary session init failed");
  int device=0;checked(cudaGetDevice(&device));cudaDeviceProp props{};checked(cudaGetDeviceProperties(&props,device));
  const auto code_length=(props.sharedMemPerBlock-(alignof(double)-1)-sizeof(double))/sizeof(gagp::gpu_detail::DInstr);
  if(code_length<2)throw std::runtime_error("shared-memory limit cannot hold boundary probe");
  gagp::BytecodeProgram program;program.consts={gagp::Value::from_int(0)};
  program.code.push_back({gagp::Opcode::PushConst,0,0,true,false});
  for(std::size_t index=1;index+1<code_length;++index)program.code.push_back({gagp::Opcode::Neg});
  program.code.push_back({gagp::Opcode::Return});
  const auto cpu=gagp::eval_fitness_cpu({program},{{}},{gagp::Value::from_int(0)},100000,1,1,policy);
  const auto gpu=session.eval_programs({program},true);
  if(!gpu.ok || gpu.fitness!=cpu || cpu!=std::vector<double>{0} || gpu.case_counts.at(0)[0]!=1)
    throw std::runtime_error("queue bookkeeping reduced admitted shared-memory capacity: "+gpu.err.message);
}

'''
        assert check.count('void check_integer_arithmetic() {')==1
        check=check.replace('void check_integer_arithmetic() {',addition+'void check_integer_arithmetic() {')
        assert check.count('  check_integer_arithmetic();')==1
        check=check.replace('  check_integer_arithmetic();','  check_integer_arithmetic();\n  check_shared_memory_boundary();')
        before={str(path.relative_to(serial.ROOT)):serial.sha(path) for path in [kernel,test]}
        kernel.write_text(text.replace(old,new));test.write_text(check)
        serial.save(serial.RAW/'unchanged-queue-source-correction.json',dict(epoch=time.time(),
            reason='reuse existing admitted dynamic scratch; add near-device-limit generic GPU/CPU boundary regression',
            source_before=before,source_after={str(p.relative_to(serial.ROOT)):serial.sha(p) for p in [kernel,test]}))
        state['status']='building';serial.save(record,state)
    build_projected.run('build-attempt11')
    build=serial.read(serial.RAW/'build-attempt11/manifest.json');assert build['status']=='ok'
    state.update(status='ok',binary=build['binary'],binary_sha256=build['binary_sha256'],finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
    raise
finally:serial.save(record,state)
