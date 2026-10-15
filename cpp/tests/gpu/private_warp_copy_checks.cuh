#pragma once

namespace private_warp_copy_test {

constexpr int kSource=65537+64,kDestination=65537+96,kStride=kSource+kDestination;

__global__ void inspect(std::int64_t* workspaces,std::int64_t* serial,
    const Value* globals,unsigned allowed,unsigned* failure,int* visits) {
  using namespace gagp::gpu_detail;
  const unsigned lane=threadIdx.x & 31;
  if(!(allowed & (1u<<lane))){visits[lane]=0;return;}
  auto* local=workspaces+static_cast<std::size_t>(lane)*kStride;
  auto* destination=local+kSource;
  auto* expected=serial+static_cast<std::size_t>(lane)*kDestination;
  const unsigned masks[]={0xffffffffu,0xaaaaaaaau,0x80000005u,0x80000000u,0x84210842u,0x7fffffffu};
  const int lengths[]={0,1,127,128,255,256,257,511,512,513,1024,65536,65537};
  constexpr std::int64_t sentinel=INT64_C(0x123456789abcdef);
  int count=0;
  // Different loop counts and masked entrants may meet at different dynamic
  // copies. Each captured group's fields must describe its actual owners only.
  for(int round=0;round<7+int(lane%4);++round) {
    if(!(masks[round%6] & (1u<<lane)))continue;
    const int size=round==0?1024:lengths[(lane+round*3)%(sizeof(lengths)/sizeof(lengths[0]))];
    const bool global=round!=0 && (lane&1) && size<=65536;
    const int source_offset=lane%17,destination_offset=32+lane%19;
    for(int i=0;i<kDestination;++i)destination[i]=expected[i]=sentinel;
    // These owner writes need the helper's initial memory-ordering barrier
    // before foreign lanes may read a compact local source.
    if(!global)for(int i=0;i<size;++i)local[source_offset+i]=
        static_cast<std::int64_t>(UINT64_C(0xfedcba9876543210) ^
            (static_cast<std::uint64_t>(lane)<<43) ^ (static_cast<std::uint64_t>(round)<<33) ^ i);
    DArenaPayloadState state;state.private_intlists=true;state.compact_intlist_cells=true;
    state.intlist_cells=local;state.list_values_used=kSource;state.kMaxListValues=kStride;
    DPayloadTables tables{};tables.list_values=globals;tables.list_value_count=kSource;
    auto value=d_private_intlist(source_offset,size,global);
    DListPayloadEntry entry{};
    if(global && size==65536) { // Full globals retain canonical transport.
      value=Value::from_int_list_hash_len(19,65535);
      entry={ValueTag::IntList,value.i,source_offset,size};tables.list_entries=&entry;tables.list_entry_count=1;
    }
    DPrivateIntListSpan source;
    if(!d_private_intlist_span(tables,state,value,source) || source.size!=size) {
      atomicOr(failure,1u<<lane);continue;
    }
    // All buffers, including boxed sources and per-case compact prefix/append
    // storage, are CUDA global allocations. Thread-local pointers cannot be shared.
    d_copy_private_intlist(expected+destination_offset,source,false);
    d_copy_private_intlist(destination+destination_offset,source,true);
    ++count;
    // Compare the entire append slice, not only copied cells: redzones and
    // neighboring unused cells must remain exactly equal to the serial snapshot.
    for(int i=0;i<kDestination;++i)if(destination[i]!=expected[i])atomicOr(failure,1u<<lane);
  }
  visits[lane]=count;
}

bool test_contract() {
  std::int64_t *workspaces=nullptr,*serial=nullptr;Value* globals=nullptr;
  unsigned* failure=nullptr;int* visits=nullptr;
  const auto cleanup=[&]{cudaFree(workspaces);cudaFree(serial);cudaFree(globals);cudaFree(failure);cudaFree(visits);};
  if(cudaMallocManaged(&workspaces,32ULL*kStride*sizeof(std::int64_t))!=cudaSuccess ||
      cudaMallocManaged(&serial,32ULL*kDestination*sizeof(std::int64_t))!=cudaSuccess ||
      cudaMallocManaged(&globals,kSource*sizeof(Value))!=cudaSuccess ||
      cudaMallocManaged(&failure,sizeof(unsigned))!=cudaSuccess ||
      cudaMallocManaged(&visits,32*sizeof(int))!=cudaSuccess){cleanup();return false;}
  for(int i=0;i<kSource;++i)globals[i]=Value::from_int(INT64_MIN+INT64_C(9007199254740993)+i);
  bool ok=true;
  const unsigned masks[]={0xffffffffu,0xaaaaaaaau,0x80000005u,0x80000000u,0x84210842u,0x7fffffffu};
  for(unsigned allowed:{0xffffffffu,0x80000005u,0x80000000u}) {
    *failure=0;
    inspect<<<1,32>>>(workspaces,serial,globals,allowed,failure,visits);
    ok=cudaGetLastError()==cudaSuccess && cudaDeviceSynchronize()==cudaSuccess &&
        check(*failure==0,"opportunistic private copy divergent owners/full snapshots/redzones: mask="+std::to_string(*failure));
    if(!ok)break;
    for(unsigned lane=0;lane<32;++lane) {
      int expected=0;
      if(allowed & (1u<<lane))for(unsigned round=0;round<7+lane%4;++round)expected+=(masks[round%6]>>lane)&1u;
      ok=check(visits[lane]==expected,"opportunistic copy omitted a divergent owner") && ok;
    }
    if(!ok)break;
  }
  cleanup();return ok;
}

} // namespace private_warp_copy_test
