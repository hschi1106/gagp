#include <cuda_runtime.h>
#include <cstdint>
#include <iostream>
#include <vector>
#include "gagp/runtime/gpu/device_types_gpu.hpp"
#include "runtime/gpu/device/resident_score_device.cuh"

using namespace gagp;
using namespace gagp::gpu_detail;

// Distinct full-width values per lane are essential: equal per-lane fixtures
// could accidentally pass despite scratch aliasing. Barriers expose a wrong
// column mapping after every other lane has completed its writes.
__global__ void check_columns(std::int64_t* cells, const Value* expected,
                             int* failures, bool interleaved) {
  constexpr int capacity=128;
  const int lane=threadIdx.x,slot=blockIdx.x*blockDim.x+lane;
  DArenaPayloadState state;
  state.private_intlists=true;state.compact_intlist_cells=true;
  state.private_warp_copy=true;
  state.private_cell_stride=interleaved?blockDim.x:1;
  state.intlist_cells=cells+(interleaved?
      static_cast<std::size_t>(blockIdx.x)*blockDim.x*capacity+lane:
      static_cast<std::size_t>(slot)*capacity);
  state.kMaxListValues=capacity;state.logical_list_limit=65536;
  const auto positive=(INT64_C(1)<<53)+slot*17+3;
  const auto negative=-positive;
  Value left=Value::from_int(positive),right=Value::from_int(negative);
  Value a,b,pair,window,joined;
  ErrCode error=ErrCode::Value;
  DPayloadTables tables;
  const auto answer=Value::from_int_list_hash_len(100+slot,2);
  DListPayloadEntry entry{ValueTag::IntList,answer.i,slot*2,2};
  tables.list_entries=&entry;tables.list_entry_count=1;
  tables.list_values=expected;tables.list_value_count=gridDim.x*blockDim.x*2;
  int failed=0;
  failed|=!d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Singleton,&left,1,tables,state,a,error);
  failed|=!d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Singleton,&right,1,tables,state,b,error);
  Value args[2]={a,b};
  failed|=!d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Concat,args,2,tables,state,pair,error);
  __syncthreads();
  DPrivateIntListSpan span;
  if(!d_private_intlist_span(tables,state,pair,span) || span.size!=2 ||
     span.at(0)!=positive || span.at(1)!=negative)failed|=2;
  Value slice[3]={pair,Value::from_int(1),Value::from_int(2)};
  failed|=!d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Slice,slice,3,tables,state,window,error);
  Value combine[2]={window,a};
  failed|=!d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Concat,combine,2,tables,state,joined,error);
  __syncthreads();
  if(!d_private_intlist_span(tables,state,joined,span) || span.size!=2 ||
     span.at(0)!=negative || span.at(1)!=positive)failed|=4;
  bool exact=false;
  FitnessPolicy policy;policy.metric=FitnessMetric::Exact;
  if(d_score_resident(joined,answer,1000,policy,tables,state,&exact)!=0 || !exact)failed|=8;
  policy.metric=FitnessMetric::Levenshtein;
  if(d_score_resident(joined,answer,1000,policy,tables,state,&exact)!=0 || !exact)failed|=16;
  state.logical_list_used=state.logical_list_limit;
  if(d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Singleton,&left,1,tables,state,a,error) ||
     state.physical_capacity_exceeded)failed|=32;
  for(int repeat=0;repeat<4;++repeat) {
    d_restore_local_list_storage(state,0,0);state.logical_list_used=0;
    left=Value::from_int(positive+repeat*10000);
    failed|=!d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Singleton,&left,1,tables,state,a,error);
    __syncthreads();
    if(!d_private_intlist_span(tables,state,a,span) || span.size!=1 || span.at(0)!=left.i)failed|=64;
    __syncthreads();
  }
  state.kMaxListValues=0;
  if(d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Singleton,&left,1,tables,state,a,error) ||
     !state.physical_capacity_exceeded)failed|=128;
  failures[slot]=failed;
}

int main() {
  constexpr int blocks=3,width=32,count=blocks*width;
  if(cudaSetDevice(0)!=cudaSuccess)return 2;
  std::int64_t* cells=nullptr;Value* expected=nullptr;int* failures=nullptr;
  if(cudaMalloc(reinterpret_cast<void**>(&cells),count*128*sizeof(std::int64_t))!=cudaSuccess ||
     cudaMalloc(reinterpret_cast<void**>(&expected),count*2*sizeof(Value))!=cudaSuccess ||
     cudaMalloc(reinterpret_cast<void**>(&failures),count*sizeof(int))!=cudaSuccess)return 3;
  std::vector<Value> answers;
  for(int slot=0;slot<count;++slot) {
    const auto value=(INT64_C(1)<<53)+slot*17+3;
    answers.push_back(Value::from_int(-value));answers.push_back(Value::from_int(value));
  }
  if(cudaMemcpy(expected,answers.data(),answers.size()*sizeof(Value),cudaMemcpyHostToDevice)!=cudaSuccess)return 4;
  std::vector<int> result(count,-1);
  for(bool interleaved:{false,true}) {
    check_columns<<<blocks,width>>>(cells,expected,failures,interleaved);
    if(cudaGetLastError()!=cudaSuccess || cudaDeviceSynchronize()!=cudaSuccess ||
       cudaMemcpy(result.data(),failures,count*sizeof(int),cudaMemcpyDeviceToHost)!=cudaSuccess)return 5;
    for(int slot=0;slot<count;++slot)if(result[slot]) {
      std::cerr<<"layout="<<interleaved<<" lane="<<slot<<" failure="<<result[slot]<<'\n';return 6;
    }
  }
  if(cudaFree(cells)!=cudaSuccess || cudaFree(expected)!=cudaSuccess || cudaFree(failures)!=cudaSuccess)return 7;
  std::cout<<"PASS 96 distinct lanes x2 layouts; singleton/concat/window/score/rewind/overflow\n";
}
