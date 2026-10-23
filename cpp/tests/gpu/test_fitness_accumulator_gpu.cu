#include <cuda_runtime.h>
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "gagp/runtime/gpu/device_types_gpu.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

#include "../../src/runtime/gpu/device/fitness_accumulator.cuh"
#include "../../src/runtime/gpu/device/integer_arithmetic.cuh"

namespace {
__device__ double original(double value) {
  if (!isfinite(value) || value == 0.0) return value == 0.0 ? 0.0 : value;
  int exponent = 0;
  const double mantissa = frexp(value, &exponent);
  const long long quantized = llround(ldexp(mantissa, 48));
  return ldexp(static_cast<double>(quantized), exponent - 48);
}
__global__ void compare(const double* input, int count, double* output) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < count) {
    output[2*i] = original(input[i]);
    output[2*i+1] = gagp::gpu_detail::d_canonicalize_fitness_accumulator(input[i]);
  }
}
void checked(cudaError_t error) {
  if (error != cudaSuccess) throw std::runtime_error(cudaGetErrorString(error));
}
struct IntegerPair {long long a,b;};
__global__ void integer_results(const IntegerPair* input,int count,long long* output) {
  const int i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=count)return;
  const auto a=input[i].a,b=input[i].b;
  output[i*4]=gagp::gpu_detail::d_trunc_int_div0(a,b);
  output[i*4+1]=gagp::gpu_detail::d_trunc_int_mod0(a,b);
  output[i*4+2]=b?gagp::gpu_detail::d_wrap_int_div(a,b):0;
  output[i*4+3]=b?gagp::gpu_detail::d_py_int_mod(a,b):0;
}
void check_shared_memory_boundary() {
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

void check_integer_arithmetic() {
  const long long boundaries[]={INT64_MIN,INT64_MIN+1,-4294967296LL,
      static_cast<long long>(INT_MIN)-1,INT_MIN,static_cast<long long>(INT_MIN)+1,
      -33,-3,-2,-1,0,1,2,3,33,static_cast<long long>(INT_MAX)-1,INT_MAX,
      static_cast<long long>(INT_MAX)+1,4294967296LL,INT64_MAX-1,INT64_MAX};
  std::vector<IntegerPair> pairs;
  for(auto a:boundaries)for(auto b:boundaries)pairs.push_back({a,b});
  std::mt19937_64 random(772419);
  for(int i=0;i<32768;++i) {
    const auto a=static_cast<long long>(random()),b=static_cast<long long>(random());
    pairs.push_back({a,b});pairs.push_back({static_cast<int>(a),static_cast<int>(b)});
    pairs.push_back({a,i%2?2:-2});pairs.push_back({a,i%2?1:-1});
  }
  IntegerPair* device_input=nullptr;long long* device_output=nullptr;
  checked(cudaMalloc(&device_input,pairs.size()*sizeof(IntegerPair)));
  checked(cudaMalloc(&device_output,pairs.size()*4*sizeof(long long)));
  checked(cudaMemcpy(device_input,pairs.data(),pairs.size()*sizeof(IntegerPair),cudaMemcpyHostToDevice));
  integer_results<<<(pairs.size()+255)/256,256>>>(device_input,pairs.size(),device_output);
  checked(cudaGetLastError());std::vector<long long> results(pairs.size()*4);
  checked(cudaMemcpy(results.data(),device_output,results.size()*sizeof(long long),cudaMemcpyDeviceToHost));
  checked(cudaFree(device_output));checked(cudaFree(device_input));
  for(std::size_t i=0;i<pairs.size();++i) {
    const auto a=pairs[i].a,b=pairs[i].b;
    const long long expected[]={gagp::vm_semantics::trunc_int_div0(a,b),
        gagp::vm_semantics::trunc_int_mod0(a,b),b?gagp::vm_semantics::wrap_int_div(a,b):0,
        b?gagp::vm_semantics::py_int_mod(a,b):0};
    for(int op=0;op<4;++op)if(results[i*4+op]!=expected[op])
      throw std::runtime_error("GPU signed64 arithmetic differs at pair "+std::to_string(i)+" operation "+std::to_string(op));
  }
  std::cout<<"exact signed64 arithmetic pairs="<<pairs.size()<<" operations="<<pairs.size()*4<<'\n';
}
}

int main() try {
  check_integer_arithmetic();
  check_shared_memory_boundary();
  std::vector<double> input{0.0, -0.0, 1.0, -1.0,
      std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
      std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::denorm_min()};
  const double boundary = 281474976710656.0;
  for (int i = -1024; i <= 1024; ++i) {
    input.push_back(boundary+i);
    input.push_back(-boundary+i);
    input.push_back(i);
    input.push_back(i/10.0);
  }
  std::mt19937_64 random(917623);
  for (int i = 0; i < 32768; ++i) {
    const auto bits = random();
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    input.push_back(value);
  }
  double *device_input = nullptr, *device_output = nullptr;
  checked(cudaMalloc(&device_input, input.size()*sizeof(double)));
  checked(cudaMalloc(&device_output, input.size()*2*sizeof(double)));
  checked(cudaMemcpy(device_input, input.data(), input.size()*sizeof(double), cudaMemcpyHostToDevice));
  compare<<<(input.size()+255)/256,256>>>(device_input, input.size(), device_output);
  checked(cudaGetLastError());
  std::vector<double> output(input.size()*2);
  checked(cudaMemcpy(output.data(), device_output, output.size()*sizeof(double), cudaMemcpyDeviceToHost));
  checked(cudaFree(device_output));checked(cudaFree(device_input));
  for (std::size_t i = 0; i < input.size(); ++i) {
    if (std::isnan(output[2*i]) && std::isnan(output[2*i+1])) continue;
    if (std::memcmp(&output[2*i], &output[2*i+1], sizeof(double)) != 0)
      throw std::runtime_error("fitness canonicalization differs at input " + std::to_string(i));
  }
  std::cout << "bitwise accumulator comparisons=" << input.size() << '\n';
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';return 1;
}
