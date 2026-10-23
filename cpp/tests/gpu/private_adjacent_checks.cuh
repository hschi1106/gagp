#pragma once
#include <cuda_runtime.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>
#include "gagp/runtime/gpu/device_types_gpu.hpp"
#include "../../src/runtime/gpu/device/builtins_device.cuh"

namespace private_adjacent_checks {
#define PROBE_OPT

using namespace gagp;
using namespace gagp::gpu_detail;
constexpr int Arena=4096, Global=256, MaxHandles=160, Lanes=128, TimedLanes=4096;
enum Kind {Single,Join,Window,Save,Restore,External,OddFiller};
struct Op {int kind,out,a,b;long long value;};
struct Result {std::uint64_t hash,logical;int reserved,physical,ok,overflow;};
__host__ __device__ long long sample(int lane,int index) {
  if(lane%7==0)return INT64_MIN+index;
  if(lane%7==1)return INT64_MAX-index;
  const long long value=(INT64_C(1)<<53)+lane*131+index*17+1;
  return lane%2?-value:value;
}
__host__ __device__ std::uint64_t mix(std::uint64_t h,std::uint64_t x) {
  return (h^x)*UINT64_C(0x100000001b3);
}

std::vector<Op> sequence(int kind,int n) {
  std::vector<Op> ops;int next=3;
  auto single=[&](int value,int op=Single){const int id=next++;ops.push_back({op,id,0,0,value});return id;};
  auto join=[&](int a,int b){const int id=next++;ops.push_back({Join,id,a,b,0});return id;};
  if(kind==0 || kind==5) {
    int list=2;
    for(int i=0;i<n;++i) {
      if(kind==5)ops.push_back({OddFiller,-1,0,0,100+i});
      const int item=single(i);list=join(list,item);
    }
  } else if(kind==1) {
    std::vector<int> items;
    for(int i=0;i<n;++i)items.push_back(single(i));
    int list=2;for(int i=n-1;i>=0;--i)list=join(items[i],list);
  } else if(kind==2) {
    const int combined=join(0,1),window=next++;
    ops.push_back({Window,window,combined,-6,-1});join(window,0);
  } else {
    const int a=single(0),b=single(1),pair=join(a,b);
    if(kind==3) {
      const int checkpoint=next;ops.push_back({Save,-1,checkpoint,0,0});
      const int temporary=single(2);join(pair,temporary);
      ops.push_back({Restore,-1,checkpoint,0,0});next=checkpoint;
      const int item=single(3);join(pair,item);
    } else {
      const int external=single(2,External),item=single(3);
      join(pair,external);join(external,item);
    }
  }
  return ops;
}

Result reference(const std::vector<Op>& ops,int lane,int capacity,unsigned limit,bool all) {
  std::vector<std::vector<long long>> values(3);
  for(int i=0;i<8;++i){values[0].push_back(sample(lane,1000+i));values[1].push_back(sample(lane,1008+i));}
  Result out{};out.ok=1;int saved=0;
  auto allocate=[&](int n) {
    if(limit) {
      if(out.logical+n>limit){out.ok=0;return false;}
      out.logical+=n;
    }
    if(n>capacity-out.reserved){out.ok=0;out.overflow=1;return false;}
    out.reserved+=n;return true;
  };
  for(const auto& op:ops) {
    if(op.kind==OddFiller){if(lane%2 && !allocate(1))break;continue;}
    if(op.kind==Save){saved=out.reserved;continue;}
    if(op.kind==Restore){out.reserved=saved;values.resize(op.a);continue;}
    std::vector<long long> value;
    if(op.kind==Single || op.kind==External) {
      if(!allocate(1))break;value.push_back(sample(lane,static_cast<int>(op.value)));
    } else if(op.kind==Join) {
      const auto a=values[op.a],b=values[op.b];
      if(!a.empty() && !b.empty() && !allocate(static_cast<int>(a.size()+b.size())))break;
      value=a;value.insert(value.end(),b.begin(),b.end());
    } else {
      const auto a=values[op.a];const long long size=a.size();
      const auto clamp=[&](long long i){return std::min(size,std::max(0LL,i<0?size+i:i));};
      const auto first=clamp(op.b),last=clamp(op.value);
      if(last>first)value.assign(a.begin()+first,a.begin()+last);
    }
    if(static_cast<int>(values.size())<=op.out)values.resize(op.out+1);
    values[op.out]=value;
  }
  out.hash=UINT64_C(0xcbf29ce484222325);
  const auto first=all?0:static_cast<int>(values.size())-1;
  for(int i=first;i<static_cast<int>(values.size());++i) {
    out.hash=mix(out.hash,values[i].size());
    for(auto value:values[i])out.hash=mix(out.hash,static_cast<std::uint64_t>(value));
  }
  return out;
}

template<bool Verify>
__global__ void exercise(const Op* ops,int count,int capacity,unsigned limit,
    const Value* globals,std::int64_t* cells,Result* results,int lanes,bool enabled) {
  const int lane=blockIdx.x*blockDim.x+threadIdx.x;if(lane>=lanes)return;
  DArenaPayloadState state;state.private_intlists=true;state.compact_intlist_cells=true;
  state.private_warp_copy=true;state.intlist_cells=cells+static_cast<std::size_t>(lane)*Arena;
  state.kMaxListValues=capacity;state.logical_list_limit=limit;
#ifdef PROBE_OPT
  state.private_adjacent_concat=enabled;
#endif
  DPayloadTables tables;tables.list_values=globals;tables.list_value_count=lanes*Global;
  Value handles[MaxHandles];
  handles[0]=d_private_intlist(lane*Global,8,true);
  handles[1]=d_private_intlist(lane*Global+8,8,true);
  handles[2]=d_private_intlist(lane*Global+32,0,true);
  Value last=handles[2];int live=3,saved=0;bool ok=true;ErrCode err=ErrCode::Value;
  for(int k=0;k<count;++k) {
    const auto op=ops[k];Value value;
    if(op.kind==Save){saved=state.list_values_used;continue;}
    if(op.kind==Restore){d_restore_local_list_storage(state,0,saved);live=op.a;last=handles[live-1];continue;}
    if(op.kind==OddFiller) {
      if(lane%2) {
        const Value arg=Value::from_int(sample(lane,static_cast<int>(op.value)));
        ok=d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Singleton,&arg,1,tables,state,value,err);
        if(!ok)break;
      }
      continue;
    }
    if(op.kind==Single || op.kind==External) {
      const Value arg=Value::from_int(sample(lane,static_cast<int>(op.value)));
#ifdef PROBE_OPT
      const bool old=state.private_adjacent_concat;
      if(op.kind==External)state.private_adjacent_concat=false;
#endif
      ok=d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Singleton,&arg,1,tables,state,value,err);
#ifdef PROBE_OPT
      state.private_adjacent_concat=old;
#endif
    } else if(op.kind==Join) {
      const Value args[2]={handles[op.a],handles[op.b]};
      ok=d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Concat,args,2,tables,state,value,err);
    } else {
      const Value args[3]={handles[op.a],Value::from_int(op.b),Value::from_int(op.value)};
      ok=d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Slice,args,3,tables,state,value,err);
    }
    if(!ok)break;
    handles[op.out]=value;live=max(live,op.out+1);last=value;
  }
  Result out{};out.hash=UINT64_C(0xcbf29ce484222325);out.ok=ok;
  const int first=Verify?0:live-1;
  for(int i=first;i<live;++i) {
    DPrivateIntListSpan span;
    const Value v=Verify?handles[i]:last;
    if(!d_private_intlist_span(tables,state,v,span)){out.ok=-1;break;}
    out.hash=mix(out.hash,span.size);
    for(int j=0;j<span.size;++j)out.hash=mix(out.hash,static_cast<std::uint64_t>(span.at(j)));
  }
  out.logical=state.logical_list_used;out.reserved=state.list_values_used;
  out.overflow=state.physical_capacity_exceeded;
#ifdef PROBE_OPT
  out.physical=enabled?state.private_cells_used:state.list_values_used;
#else
  out.physical=state.list_values_used;
#endif
  results[lane]=out;
}

bool equal(const Result& a,const Result& b) {
  return a.hash==b.hash && a.logical==b.logical && a.reserved==b.reserved &&
      a.ok==b.ok && a.overflow==b.overflow;
}
bool run() {
  if(cudaDeviceSetLimit(cudaLimitStackSize,16384)!=cudaSuccess)return false;
  std::int64_t* cells=nullptr;Value* globals=nullptr;Result* output=nullptr;Op* operations=nullptr;
  if(cudaMalloc(reinterpret_cast<void**>(&cells),std::size_t(Lanes)*Arena*sizeof(std::int64_t))!=cudaSuccess ||
     cudaMalloc(reinterpret_cast<void**>(&globals),std::size_t(Lanes)*Global*sizeof(Value))!=cudaSuccess ||
     cudaMalloc(reinterpret_cast<void**>(&output),Lanes*sizeof(Result))!=cudaSuccess ||
     cudaMalloc(reinterpret_cast<void**>(&operations),256*sizeof(Op))!=cudaSuccess)return false;
  std::vector<Value> input;
  for(int lane=0;lane<Lanes;++lane)for(int j=0;j<Global;++j)
    input.push_back(Value::from_int(sample(lane,1000+j)));
  if(cudaMemcpy(globals,input.data(),input.size()*sizeof(Value),cudaMemcpyHostToDevice)!=cudaSuccess)return false;
  std::vector<Result> actual(Lanes);
  int traces=0;
  for(int enabled=0;enabled<2;++enabled)for(int kind=0;kind<6;++kind)
    for(int n:{0,1,2,3,7,8,31,32,63,64}) {
      if(kind>=2 && kind<=4 && n!=3)continue;
      const auto ops=sequence(kind,n);
      if(cudaMemcpy(operations,ops.data(),ops.size()*sizeof(Op),cudaMemcpyHostToDevice)!=cudaSuccess)return false;
      for(int capacity:{0,1,2,3,4,7,8,16,64,Arena})for(unsigned limit:{0u,8u,65536u}) {
        if(cudaMemset(cells,0xA5,std::size_t(Lanes)*Arena*sizeof(std::int64_t))!=cudaSuccess)return false;
        exercise<true><<<Lanes/32,32>>>(operations,ops.size(),capacity,limit,globals,cells,output,Lanes,enabled);
        if(cudaGetLastError()!=cudaSuccess || cudaDeviceSynchronize()!=cudaSuccess ||
           cudaMemcpy(actual.data(),output,Lanes*sizeof(Result),cudaMemcpyDeviceToHost)!=cudaSuccess)return false;
        for(int lane=0;lane<Lanes;++lane) {
          ++traces;
          if(!equal(actual[lane],reference(ops,lane,capacity,limit,true)))return false;
        }
      }
    }
  // Positive witness: both append and recursive prepend physically store64
  // cells, retaining all immutable prefixes, while reserving/charging2143.
  // This asserts the mechanism rather than inferring it from timing.
  for(int kind:{0,1})for(int enabled=0;enabled<2;++enabled) {
    const auto ops=sequence(kind,64);
    if(cudaMemcpy(operations,ops.data(),ops.size()*sizeof(Op),cudaMemcpyHostToDevice)!=cudaSuccess)return false;
    exercise<true><<<Lanes/32,32>>>(operations,ops.size(),Arena,65536,globals,cells,output,Lanes,enabled);
    if(cudaGetLastError()!=cudaSuccess || cudaDeviceSynchronize()!=cudaSuccess ||
       cudaMemcpy(actual.data(),output,Lanes*sizeof(Result),cudaMemcpyDeviceToHost)!=cudaSuccess)return false;
    for(int lane=0;lane<Lanes;++lane)if(!equal(actual[lane],reference(ops,lane,Arena,65536,true)) ||
        actual[lane].reserved!=2143 || actual[lane].logical!=2143 ||
        actual[lane].physical!=(enabled?64:2143))return false;
  }
  if(cudaFree(cells)!=cudaSuccess || cudaFree(globals)!=cudaSuccess ||
     cudaFree(output)!=cudaSuccess || cudaFree(operations)!=cudaSuccess)return false;
  std::cout<<"PASS private-adjacent traces="<<traces
           <<"; CPU vectors; exact charge/overflow; physical64/reserved2143 append+prepend; immutable prefixes\n";
  return true;
}

#undef PROBE_OPT
}
