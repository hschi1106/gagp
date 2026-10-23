#include <cuda_runtime.h>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>
#include "gagp/core/value_semantics.hpp"

namespace gagp::gpu_detail {
#include "../../src/runtime/gpu/device/phase_register_bank.cuh"
}
namespace {
constexpr int programs=4096,steps=96;
struct Operation {int code,destination,left,right;long long literal;};
void checked(cudaError_t error){if(error!=cudaSuccess)throw std::runtime_error(cudaGetErrorString(error));}

template<unsigned Count>
__global__ void execute(const Operation* operations,long long* output) {
  const int row=blockIdx.x*blockDim.x+threadIdx.x;if(row>=programs)return;
  gagp::gpu_detail::DPhaseRegisterBank<Count> bank;
  for(unsigned i=0;i<Count;++i)bank[i]=static_cast<long long>(i)-3;
  for(int step=0;step<steps;++step) {
    const auto op=operations[row*steps+step];
    const int destination=op.destination%Count,left=op.left%Count,right=op.right%Count;
    switch(op.code) {
      case 0:bank[destination]=op.literal;break;
      case 1:bank[destination]=bank[left];break;
      case 2:bank[destination]=gagp::vm_semantics::wrap_int_add(bank[left],bank[right]);break;
      case 3:bank[destination]=gagp::vm_semantics::wrap_int_mul(bank[left],bank[right]);break;
      case 4:bank[destination]=bank[left]<=bank[right]?bank[left]:bank[right];break;
      case 5:bank[destination]=gagp::vm_semantics::wrap_int_sub(bank[left],bank[right]);break;
      case 6:{const long long before=bank[left];bank[destination]=bank[right];bank[right]=before;break;}
      case 7:{int cursor=destination;bank[cursor++]=bank[left];bank[cursor%Count]=bank[(cursor-1)%Count];break;}
    }
    for(unsigned i=0;i<Count;++i)output[(row*steps+step)*Count+i]=bank[i];
  }
}

template<unsigned Count>
void check(const std::vector<Operation>& operations) {
  std::vector<long long> expected(programs*steps*Count),actual(expected.size());
  for(int row=0;row<programs;++row) {
    long long memory[Count];for(unsigned i=0;i<Count;++i)memory[i]=static_cast<long long>(i)-3;
    for(int step=0;step<steps;++step) {
      const auto op=operations[row*steps+step];
      const int destination=op.destination%Count,left=op.left%Count,right=op.right%Count;
      switch(op.code) {
        case 0:memory[destination]=op.literal;break;
        case 1:memory[destination]=memory[left];break;
        case 2:memory[destination]=gagp::vm_semantics::wrap_int_add(memory[left],memory[right]);break;
        case 3:memory[destination]=gagp::vm_semantics::wrap_int_mul(memory[left],memory[right]);break;
        case 4:memory[destination]=memory[left]<=memory[right]?memory[left]:memory[right];break;
        case 5:memory[destination]=gagp::vm_semantics::wrap_int_sub(memory[left],memory[right]);break;
        case 6:{const auto before=memory[left];memory[destination]=memory[right];memory[right]=before;break;}
        case 7:{const int destination2=(destination+1)%Count;memory[destination]=memory[left];memory[destination2]=memory[destination];break;}
      }
      for(unsigned i=0;i<Count;++i)expected[(row*steps+step)*Count+i]=memory[i];
    }
  }
  Operation* input=nullptr;long long* output=nullptr;
  checked(cudaMalloc(&input,operations.size()*sizeof(Operation)));
  checked(cudaMalloc(&output,actual.size()*sizeof(long long)));
  checked(cudaMemcpy(input,operations.data(),operations.size()*sizeof(Operation),cudaMemcpyHostToDevice));
  execute<Count><<<(programs+127)/128,128>>>(input,output);checked(cudaGetLastError());
  checked(cudaMemcpy(actual.data(),output,actual.size()*sizeof(long long),cudaMemcpyDeviceToHost));
  checked(cudaFree(output));checked(cudaFree(input));
  for(std::size_t i=0;i<actual.size();++i)if(actual[i]!=expected[i])
    throw std::runtime_error("register bank"+std::to_string(Count)+" differs from CPU at observation"+std::to_string(i));
}
}
int main() try {
  std::mt19937_64 random(8264301);std::vector<Operation> operations;
  operations.reserve(programs*steps);
  for(int i=0;i<programs*steps;++i)operations.push_back({static_cast<int>(random()%8),static_cast<int>(random()%8),
      static_cast<int>(random()%8),static_cast<int>(random()%8),i%7==0?INT64_MIN:i%7==1?INT64_MAX:static_cast<long long>(random())});
  check<4>(operations);check<8>(operations);
  std::cout<<"register bank operations="<<programs*steps*2<<" exact CPU slot observations="<<programs*steps*12<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
