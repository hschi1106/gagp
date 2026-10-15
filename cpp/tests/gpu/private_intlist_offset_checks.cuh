#pragma once
#include "../../src/runtime/gpu/device/resident_score_device.cuh"
#include "../../src/runtime/gpu/view_profile.hpp"
#include "../../src/runtime/gpu/private_global_offsets.hpp"

__global__ void inspect_private_intlist_offsets(Value* cells, std::int64_t* int_cells, Value* source, bool compact, int stride, int* failure) {
  using namespace gagp::gpu_detail;
  *failure=0;
  DArenaPayloadState st;
  DListPayloadEntry entries[4]{};
  st.private_intlists=true;st.compact_intlist_cells=compact;st.intlist_cell_stride=stride;
  st.list_values=compact?nullptr:cells;st.intlist_cells=compact?int_cells:nullptr;st.kMaxListValues=65536;
  st.list_entries=entries;st.list_entry_capacity=4;st.logical_list_limit=65536;
  DPayloadTables tables{};
  ErrCode error=ErrCode::Value;
  // Empty descriptors may have null backing in either origin, including SoA.
  auto empty_state=st;empty_state.intlist_cells=nullptr;empty_state.list_values=nullptr;
  DPrivateIntListSpan empty_span;
  if(!d_private_intlist_span(tables,empty_state,d_private_intlist(0,0),empty_span) || empty_span.size ||
      !d_private_intlist_span(tables,empty_state,d_private_intlist(0,0,true),empty_span) ||
      empty_span.size || empty_span.integer_stride!=1 || empty_span.boxed || empty_span.integers) {*failure=51;return;}
  Value one=Value::from_int(7),two=Value::from_int(8),a,b,c;
  if(!d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Singleton,&one,1,tables,st,a,error) ||
     !d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Singleton,&two,1,tables,st,b,error) ||
     !d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Singleton,&one,1,tables,st,c,error) ||
     a.i==c.i || st.list_entry_count || st.logical_list_used!=3) {*failure=1;return;}
  bool exact=false;
  FitnessPolicy policy;policy.metric=FitnessMetric::Exact;
  if(d_score_resident(a,c,1000,policy,tables,st,&exact)!=0 || !exact ||
     d_score_resident(a,b,1000,policy,tables,st,&exact)!=-1 || exact) {*failure=2;return;}
  policy.metric=FitnessMetric::Levenshtein;
  if(d_score_resident(a,b,1000,policy,tables,st,&exact)!=-1 || exact) {*failure=3;return;}
  // Fabricate a canonical identity collision with the private handle's bits.
  // Private resolution must never select that registry's different contents.
  entries[0]={ValueTag::IntList,a.i,1,1};st.list_entry_count=1;
  DPrivateIntListSpan span;
  const Value* boxed_ptr=nullptr;int boxed_count=0;
  if(d_lookup_list_payload(tables,st,a,boxed_ptr,boxed_count)==compact) {*failure=31;return;}
  if(!d_private_intlist_span(tables,st,a,span) || span.size!=1 || span.at(0)!=7) {*failure=4;return;}
  auto invalid=d_private_intlist(st.list_values_used,1);
  if(d_private_intlist_span(tables,st,invalid,span)) {*failure=5;return;}
  invalid=a;invalid.tag=ValueTag::FloatList;
  if(d_private_intlist_span(tables,st,invalid,span)) {*failure=6;return;}
  invalid=a;invalid.i|=1ULL<<40;
  if(d_private_intlist_span(tables,st,invalid,span)) {*failure=7;return;}
  invalid=d_private_intlist(0,1,true);
  if(d_private_intlist_span(tables,st,invalid,span)) {*failure=8;return;}
  d_restore_local_list_storage(st,0,0);
  if(d_private_intlist_span(tables,st,a,span)) {*failure=9;return;}
  // Physical overflow and logical overflow remain distinct.
  st.kMaxListValues=0;
  if(d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Singleton,&one,1,tables,st,a,error) ||
     !st.physical_capacity_exceeded || error!=ErrCode::Value) {*failure=10;return;}
  st.physical_capacity_exceeded=false;st.logical_list_used=65536;
  if(d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Singleton,&one,1,tables,st,a,error) ||
     st.physical_capacity_exceeded || error!=ErrCode::Value) {*failure=11;return;}
  // Full-length backing survives the public low-16-bit length encoding.
  for(int i=0;i<65536;++i)source[i]=Value::from_int(i);
  Value left=Value::from_int_list_hash_len(17,65535),right=Value::from_int_list_hash_len(19,1);
  DListPayloadEntry globals[2]={{ValueTag::IntList,left.i,0,65535},{ValueTag::IntList,right.i,65535,1}};
  tables.list_values=source;tables.list_value_count=65536;
  tables.list_entries=globals;tables.list_entry_count=2;
  st.list_values_used=0;st.logical_list_used=0;st.kMaxListValues=65536;
  Value args[2]={left,right};
  if(!d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Concat,args,2,tables,st,a,error) ||
     Value::container_len(a)!=0 || !d_private_intlist_span(tables,st,a,span) ||
     span.size!=65536 || span.at(65535)!=65535 || st.logical_list_used!=65536 || st.list_entry_count) {*failure=12;return;}
  Value encoded_index[2]={a,Value::from_int(0)};
  if(d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Index,encoded_index,2,tables,st,b,error) ||
     error!=ErrCode::Value || !d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::IndexClampedOrZero,
         encoded_index,2,tables,st,b,error) || b.tag!=ValueTag::Int || b.i!=0) {*failure=32;return;}
  st.logical_list_limit=65535;st.logical_list_used=0;
  if(d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Concat,args,2,tables,st,b,error) ||
     st.physical_capacity_exceeded) {*failure=13;return;}
  Value slice[3]={left,Value::from_int(1),Value::from_int(3)};
  if(!d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Slice,slice,3,tables,st,b,error) ||
     !d_private_intlist_span(tables,st,b,span) || span.size!=2 || span.integer_stride!=1 || span.integers || span.at(0)!=1) {*failure=14;return;}
  // Host clamping hides one element: transport65535 + constant1 charges65536
  // while copying65537. The private high-length bit must retain that last cell.
  source[65536]=Value::from_int(65536);
  globals[0].len=65536;globals[1].offset=65536;tables.list_value_count=65537;
  st.logical_list_limit=65536;st.logical_list_used=0;st.list_values_used=0;st.kMaxListValues=65538;
  if(!d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Concat,args,2,tables,st,a,error) ||
     Value::container_len(a)!=1 || !d_private_intlist_span(tables,st,a,span) ||
     span.size!=65537 || span.at(65536)!=65536 || st.logical_list_used!=65536) {*failure=24;return;}
  encoded_index[0]=a;
  if(!d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Index,encoded_index,2,tables,st,b,error) || b.i!=0) {*failure=33;return;}
  encoded_index[1]=Value::from_int(1);
  if(d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Index,encoded_index,2,tables,st,b,error) ||
     !d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::IndexClampedOrZero,encoded_index,2,tables,st,b,error) || b.i!=0) {*failure=34;return;}
  policy.metric=FitnessMetric::Exact;
  const auto prefix=d_private_intlist(0,65536);
  if(d_score_resident(a,a,1000,policy,tables,st,&exact)!=0 || !exact ||
     d_score_resident(a,prefix,1000,policy,tables,st,&exact)!=-1 || exact) {*failure=25;return;}
  // Both malformed handles fit their supplied physical backing; rejection is
  // the profile's origin/length bound, rather than an incidental capacity bound.
  st.list_values_used=65538;
  invalid=d_private_intlist(0,65538);
  if(d_private_intlist_span(tables,st,invalid,span)) {*failure=26;return;}
  invalid=d_private_intlist(0,65537,true);
  if(d_private_intlist_span(tables,st,invalid,span)) {*failure=27;return;}
  st.list_values_used=65537;
  Value exhausted_args[2]={a,right};
  if(d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Concat,exhausted_args,2,tables,st,b,error) ||
     st.physical_capacity_exceeded || error!=ErrCode::Value) {*failure=28;return;}
  Value masked_slice[3]={a,Value::from_int(0),Value::from_int(65537)};
  if(!d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Slice,masked_slice,3,tables,st,b,error) ||
     !d_private_intlist_span(tables,st,b,span) || span.size!=1 || st.logical_list_used!=65536) {*failure=29;return;}
  d_restore_local_list_storage(st,0,0);
  if(d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Singleton,&one,1,tables,st,b,error) ||
     st.logical_list_used!=65536 || st.physical_capacity_exceeded) {*failure=30;return;}
  // Local windows retain their origin through nested slices and later copies.
  // No registry, legacy index, or epoch buffers exist for active private lists.
  DIndexedArenaPayloadState local;local.private_intlists=true;local.compact_intlist_cells=compact;local.intlist_cell_stride=stride;
  local.list_values=compact?nullptr:cells;local.intlist_cells=compact?int_cells:nullptr;
  local.kMaxListValues=65536;local.logical_list_limit=65536;
  Value x,y,pair,window,nested,joined;
  if(!d_builtin_call<DPayloadFlavor::IndexedArena>(BuiltinId::Singleton,&one,1,tables,local,x,error) ||
     !d_builtin_call<DPayloadFlavor::IndexedArena>(BuiltinId::Singleton,&two,1,tables,local,y,error)) {*failure=15;return;}
  Value pair_args[2]={x,y};
  if(!d_builtin_call<DPayloadFlavor::IndexedArena>(BuiltinId::Concat,pair_args,2,tables,local,pair,error)) {*failure=16;return;}
  Value local_slice[3]={pair,Value::from_int(0),Value::from_int(2)};
  if(!d_builtin_call<DPayloadFlavor::IndexedArena>(BuiltinId::Slice,local_slice,3,tables,local,window,error)) {*failure=17;return;}
  local_slice[0]=window;local_slice[1]=Value::from_int(1);
  if(!d_builtin_call<DPayloadFlavor::IndexedArena>(BuiltinId::Slice,local_slice,3,tables,local,nested,error) ||
     !d_private_intlist_span(tables,local,nested,span) || span.size!=1 || span.at(0)!=8) {*failure=18;return;}
  Value join_args[2]={nested,x};
  if(!d_builtin_call<DPayloadFlavor::IndexedArena>(BuiltinId::Concat,join_args,2,tables,local,joined,error) ||
     !d_private_intlist_span(tables,local,joined,span) || span.size!=2 || span.at(0)!=8 || span.at(1)!=7) {*failure=19;return;}
  local_slice[0]=x;local_slice[1]=Value::from_int(0);local_slice[2]=Value::from_int(1);
  if(!d_builtin_call<DPayloadFlavor::IndexedArena>(BuiltinId::Slice,local_slice,3,tables,local,window,error) ||
     !d_private_intlist_span(tables,local,window,span) || span.size!=1 || span.at(0)!=7 ||
     local.list_values_used!=6 || local.logical_list_used!=6 || local.list_entry_count) {*failure=20;return;}
  // A scalar region's discarded suffix is invalid before reuse. Offsets may be
  // reused after that lifetime ends; no persistent ABA identity is promised.
  const int saved=local.list_values_used;
  Value discarded,replacement;
  if(!d_builtin_call<DPayloadFlavor::IndexedArena>(BuiltinId::Singleton,&one,1,tables,local,discarded,error)) {*failure=21;return;}
  d_restore_local_list_storage(local,0,saved);
  if(d_private_intlist_span(tables,local,discarded,span) || local.logical_list_used!=7) {*failure=22;return;}
  if(!d_builtin_call<DPayloadFlavor::IndexedArena>(BuiltinId::Singleton,&two,1,tables,local,replacement,error) ||
     !d_private_intlist_span(tables,local,replacement,span) || span.size!=1 || (compact?span.integers!=int_cells+static_cast<std::size_t>(saved)*stride:span.boxed!=cells+saved) || span.at(0)!=8 ||
     local.list_values_used!=saved+1 || local.logical_list_used!=8 ||
     !d_private_intlist_span(tables,local,joined,span) || span.size!=2 || span.at(0)!=8 || span.at(1)!=7) {*failure=23;return;}

  constexpr std::int64_t wide[]={INT64_MIN,INT64_MAX,
      INT64_C(9007199254740993),-INT64_C(9007199254740993),0,-1};
  for(int i=0;i<6;++i)source[i]=Value::from_int(wide[i]);
  const Value external=Value::from_int_list_hash_len(123,6);
  DListPayloadEntry wide_entry{ValueTag::IntList,external.i,0,6};
  tables.list_entries=&wide_entry;tables.list_entry_count=1;tables.list_value_count=6;
  for(int i=0;i<6;++i) {
    Value item,element=source[i];
    if(!d_builtin_call<DPayloadFlavor::IndexedArena>(BuiltinId::Singleton,&element,1,tables,local,item,error)) {*failure=35;return;}
    for(auto bid:{BuiltinId::Index,BuiltinId::IndexNonnegative,BuiltinId::IndexClampedOrZero}) {
      Value index_args[2]={item,Value::from_int(bid==BuiltinId::Index?-1:bid==BuiltinId::IndexNonnegative?0:999)};
      Value indexed;
      if(!d_builtin_call<DPayloadFlavor::IndexedArena>(bid,index_args,2,tables,local,indexed,error) ||
          indexed.tag!=ValueTag::Int || indexed.i!=wide[i]) {*failure=36;return;}
    }
  }
  Value mixed=external;
  for(int origin=0;origin<4;++origin) {
    Value mix_args[2]={origin==0 || origin==1?external:mixed,origin==0 || origin==2?external:mixed};
    Value result;
    if(!d_builtin_call<DPayloadFlavor::IndexedArena>(BuiltinId::Concat,mix_args,2,tables,local,result,error) ||
        !d_private_intlist_span(tables,local,result,span)) {*failure=37;return;}
    for(int i=0;i<span.size;++i)if(span.at(i)!=wide[i%6]) {*failure=38;return;}
    Value reference[96];
    for(int i=0;i<span.size;++i)reference[i]=Value::from_int(wide[i%6]);
    for(auto proximity:{FitnessProximity::Capped,FitnessProximity::Discounted}) {
      FitnessPolicy distance{FitnessMetric::Levenshtein,proximity};unsigned row[kFitnessMaxSequence+1];
      const auto expected=fitness_sequence_score(reference,span.size,source,6,2.5,distance,row);
      const auto score=d_score_resident(result,external,2.5,distance,tables,local,&exact);
      if(score!=expected || exact) {*failure=39;return;}
      if(d_score_resident(external,result,2.5,distance,tables,local,&exact)!=expected || exact) {*failure=40;return;}
    }
    mixed=result;
  }
  Value negative[2]={mixed,Value::from_int(-1)},indexed;
  if(d_builtin_call<DPayloadFlavor::IndexedArena>(BuiltinId::IndexNonnegative,negative,2,tables,local,indexed,error) ||
      error!=ErrCode::Value) {*failure=41;return;}
  Value not_integer=Value::from_float(1.0);
  const auto charged=local.logical_list_used;
  if(d_builtin_call<DPayloadFlavor::IndexedArena>(BuiltinId::Singleton,&not_integer,1,tables,local,indexed,error) ||
      error!=ErrCode::Type || local.logical_list_used!=charged) {*failure=42;return;}

}

// A full boundary run in lane zero must not touch any neighboring lane.
__global__ void inspect_private_intlist_lane_guards(const std::int64_t* cells,int* failure) {
  if(threadIdx.x==0)return;
  for(int i=0;i<65538;++i)if(cells[static_cast<std::size_t>(i)*32+threadIdx.x]!=INT64_C(0x5a5a5a5a5a5a5a5a)) {
    atomicCAS(failure,0,43);return;
  }
}

__global__ void inspect_private_intlist_warp(std::int64_t* cells,int* failure) {
  using namespace gagp::gpu_detail;
  const int lane=threadIdx.x;
  DArenaPayloadState state;state.private_intlists=true;state.compact_intlist_cells=true;
  state.intlist_cells=cells+lane;state.intlist_cell_stride=32;
  state.kMaxListValues=7;state.logical_list_limit=7;
  DPayloadTables tables{};ErrCode error=ErrCode::Value;
  Value low=Value::from_int(INT64_MIN+lane),high=Value::from_int(INT64_MAX-lane),a,b,pair,window,joined;
  auto fail=[&](int code){atomicCAS(failure,0,code);};
  if(!d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Singleton,&low,1,tables,state,a,error) ||
      !d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Singleton,&high,1,tables,state,b,error)){fail(44);return;}
  Value args[3]={a,b};
  if(!d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Concat,args,2,tables,state,pair,error)){fail(45);return;}
  args[0]=pair;args[1]=Value::from_int(1);args[2]=Value::from_int(2);
  if(!d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Slice,args,3,tables,state,window,error)){fail(46);return;}
  args[0]=window;args[1]=pair;
  if(!d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Concat,args,2,tables,state,joined,error)){fail(47);return;}
  DPrivateIntListSpan span;
  if(!d_private_intlist_span(tables,state,joined,span) || span.size!=3 || span.integer_stride!=32 ||
      span.at(0)!=high.i || span.at(1)!=low.i || span.at(2)!=high.i){fail(48);return;}
  Value index_args[2]={joined,Value::from_int(1)},indexed;
  if(!d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Index,index_args,2,tables,state,indexed,error) ||
      indexed.tag!=ValueTag::Int || indexed.i!=low.i){fail(53);return;}
  // Validate the actual interleaved cell positions as well as typed reads.
  const std::int64_t expected[]={low.i,high.i,low.i,high.i,high.i,low.i,high.i};
  for(int i=0;i<7;++i)if(cells[i*32+lane]!=expected[i]){fail(49);return;}
  if(!d_private_intlist_span(tables,state,d_private_intlist(7,0),span) ||
      span.size || span.integers || span.integer_stride!=32){fail(52);return;}
  for(int stride:{0,2,33}) {
    state.intlist_cell_stride=stride;
    if(d_private_intlist_span(tables,state,joined,span) ||
        d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Singleton,&low,1,tables,state,a,error) ||
        error!=ErrCode::Value){fail(50);return;}
  }
}

bool test_private_intlist_offsets() {
  Value *cells=nullptr,*source=nullptr;std::int64_t* int_cells=nullptr;
  int *device_failure=nullptr,failure=-1;
  if(cudaMalloc(reinterpret_cast<void**>(&cells),65538*sizeof(Value))!=cudaSuccess)return false;
  if(cudaMalloc(reinterpret_cast<void**>(&source),65537*sizeof(Value))!=cudaSuccess){cudaFree(cells);return false;}
  if(cudaMalloc(reinterpret_cast<void**>(&int_cells),32*65538*sizeof(std::int64_t))!=cudaSuccess){cudaFree(cells);cudaFree(source);return false;}
  if(cudaMalloc(reinterpret_cast<void**>(&device_failure),sizeof(int))!=cudaSuccess){cudaFree(cells);cudaFree(source);cudaFree(int_cells);return false;}
  bool ok=true;
  for(int mode:{0,1,32}) {
    if(cudaMemset(int_cells,0x5a,32*65538*sizeof(std::int64_t))!=cudaSuccess){ok=false;break;}
    inspect_private_intlist_offsets<<<1,1>>>(cells,int_cells,source,mode!=0,mode==32?32:1,device_failure);
    ok=cudaGetLastError()==cudaSuccess && cudaDeviceSynchronize()==cudaSuccess &&
        cudaMemcpy(&failure,device_failure,sizeof(int),cudaMemcpyDeviceToHost)==cudaSuccess && failure==0;
    if(!ok)break;
    if(mode==32) {
      inspect_private_intlist_lane_guards<<<1,32>>>(int_cells,device_failure);
      ok=cudaGetLastError()==cudaSuccess && cudaDeviceSynchronize()==cudaSuccess &&
          cudaMemcpy(&failure,device_failure,sizeof(int),cudaMemcpyDeviceToHost)==cudaSuccess && failure==0;
      if(!ok)break;
    }
  }
  if(ok) {
    inspect_private_intlist_warp<<<1,32>>>(int_cells,device_failure);
    ok=cudaGetLastError()==cudaSuccess && cudaDeviceSynchronize()==cudaSuccess &&
        cudaMemcpy(&failure,device_failure,sizeof(int),cudaMemcpyDeviceToHost)==cudaSuccess && failure==0;
  }
  cudaFree(cells);cudaFree(source);cudaFree(int_cells);cudaFree(device_failure);
  return check(ok,"boxed/compact/interleaved private IntList boundary/origin/precision "+std::to_string(failure));
}

bool test_private_global_offset_preparation() {
  using namespace gpu_detail;
  const auto canonical=Value::from_int_list_hash_len(123,2);
  for(int offset:{3,0,7}) {
    std::vector<DListPayloadEntry> entries{{ValueTag::IntList,canonical.i,offset,2}};
    auto value=canonical;value.b=true; // External markers cannot carry an old table offset.
    if(!check(prepare_private_global_intlist(value,entries,offset+2) && value.b &&
        Value::container_len(value)==2 && Value::container_hash48(value)==(DPRIVATE_LIST_GLOBAL|offset),
        "private global preparation must use current checked table offset"))return false;
    for(int capacity:{-1,offset,offset+1}) {
      value=canonical;
      if(!check(!prepare_private_global_intlist(value,entries,capacity) && !value.b && value.i==canonical.i,
          "private global preparation accepted incomplete backing"))return false;
    }
  }
  for(unsigned encoded:{0u,65535u}) {
    auto value=Value::from_int_list_hash_len(37,encoded);const auto original=value;
    if(!check(!prepare_private_global_intlist(value,{{ValueTag::IntList,value.i,0,65536}},65536) &&
        !value.b && value.i==original.i,"full-length global must retain canonical length encoding"))return false;
  }
  auto maximum=Value::from_int_list_hash_len(99,65535);
  if(!check(prepare_private_global_intlist(maximum,{{ValueTag::IntList,maximum.i,0,65535}},65535) &&
      maximum.b && Value::container_len(maximum)==65535,"maximum converted global lost public length"))return false;
  for(const auto& entry:std::vector<DListPayloadEntry>{{ValueTag::IntList,canonical.i,-1,2},
      {ValueTag::IntList,canonical.i,0,1},{ValueTag::FloatList,canonical.i,0,2}}) {
    auto value=canonical;
    if(!check(!prepare_private_global_intlist(value,{entry},2) && !value.b && value.i==canonical.i,
        "invalid private global entry manufactured a descriptor"))return false;
  }
  auto missing=canonical;
  return check(!prepare_private_global_intlist(missing,{},2) && missing.i==canonical.i && !missing.b,
      "missing private global must retain canonical lazy lookup");
}

bool test_private_global_offset_cases() {
  ScopedEnvironment private_mode("GAGP_GPU_PRIVATE_INTLIST_OFFSETS"),global("GAGP_GPU_GLOBAL_PAYLOAD"),
      mapping("GAGP_GPU_PRIVATE_GLOBAL_OFFSETS"),trivial("GAGP_GPU_TRIVIAL_PHASES");
  setenv("GAGP_GPU_PRIVATE_INTLIST_OFFSETS","1",1);setenv("GAGP_GPU_GLOBAL_PAYLOAD","1",1);
  setenv("GAGP_GPU_TRIVIAL_PHASES","1",1);
  std::vector<CaseBindings> cases;std::vector<Value> answers;
  for(int i=0;i<1024;++i) {
    auto values=std::vector<Value>(i%4+1,Value::from_int(INT64_C(9007199254740993)+i%11));
    const auto input=payload::make_int_list_value(values);cases.push_back({{0,input}});answers.push_back(input);
  }
  FitnessPolicy policy{FitnessMetric::Exact};policy.list_allocation_limit=65536;
  FitnessSessionGpu session;
  for(int iteration=0;iteration<4;++iteration) {
    auto current=cases;
    if(iteration==1)current.back().clear();
    if(iteration==2)current.back()[0].value=Value::from_float(1.5);
    const auto literal=payload::make_int_list_value({Value::from_int(iteration),Value::from_int(INT64_MIN)});
    auto expected=answers;
    for(unsigned i=0;i<expected.size();i+=3)expected[i]=literal;
    BytecodeProgram pass;pass.n_locals=1;pass.code={ins_a(Opcode::Load,0),ins(Opcode::Return)};
    BytecodeProgram constant;constant.consts={literal};constant.code={ins_a(Opcode::PushConst,0),ins(Opcode::Return)};
    auto phase=constant_phase(literal);phase.program.n_locals=1;
    phase.bindings={{slot(RegionSlotBank::Parameter,0),0}}; // Unused capture stays lazy.
    auto segment=unary_segment(ValueTag::IntList,std::move(phase),load_phase(RegionSlotBank::Result,0));
    segment.plan.parameter_types={ValueTag::IntList};segment.parameter_locals={0};
    auto phased=invocation(std::move(segment),0);phased.n_locals=1;
    BytecodeProgram mixed;mixed.n_locals=1;
    mixed.consts={Value::from_int(0),Value::from_int(1),literal};
    mixed.code={ins_a(Opcode::Load,0),ins_a(Opcode::PushConst,0),ins_a(Opcode::PushConst,1),
        ins_ab(Opcode::CallBuiltin,int(BuiltinId::Slice),3),ins_a(Opcode::PushConst,2),
        ins_ab(Opcode::CallBuiltin,int(BuiltinId::Concat),2),ins(Opcode::Return)};
    BytecodeProgram indexed;indexed.n_locals=1;indexed.consts={Value::from_int(0)};
    indexed.code={ins_a(Opcode::Load,0),ins_a(Opcode::PushConst,0),
        ins_ab(Opcode::CallBuiltin,int(BuiltinId::Index),2),
        ins_ab(Opcode::CallBuiltin,int(BuiltinId::Singleton),1),ins(Opcode::Return)};
    const std::vector<BytecodeProgram> programs{pass,constant,phased,mixed,indexed};
    std::vector<unsigned> wrong;std::vector<CpuFitnessCaseCounts> counts;
    const auto cpu=eval_fitness_cpu(programs,current,expected,1000,1000,33,policy,&wrong,&counts);
    if(!session.init(current,expected,1000,33,1000,policy).ok)return false;
    FitnessEvalResult baseline;
    for(const char* enabled:{"0","1"}) {
      setenv("GAGP_GPU_PRIVATE_GLOBAL_OFFSETS",enabled,1);
      int callbacks=0;
      const auto result=session.eval_programs(programs,true,[&]{++callbacks;});
      const bool active=enabled[0]=='1' && (iteration==0 || iteration==3);
      if(!check(result.ok && result.fitness==cpu && result.wrong==wrong && callbacks==1 &&
          (result.execution_profile.find("-private-global-offsets")!=std::string::npos)==(enabled[0]=='1') &&
          (result.execution_profile.find("-private-global-offsets-declined")==std::string::npos)==
              (enabled[0]=='0' || active),"private global full-case/session reinit/decline CPU parity"))return false;
      for(unsigned p=0;p<programs.size();++p)
        if(!check(result.case_counts[p][0]==1024 && result.case_counts[p][1]==counts[p].errors &&
            result.case_counts[p][2]==counts[p].timeouts,"private global lazy capture case/status counts"))return false;
      if(enabled[0]=='0')baseline=result;
      else if(!check(result.fitness==baseline.fitness && result.wrong==baseline.wrong &&
          result.case_counts==baseline.case_counts && result.payload_replay_programs==baseline.payload_replay_programs,
          "private global mapping changed canonical mode result"))return false;
    }
    if(!check(!programs[1].consts[0].b && !programs[2].bounded_region_segments[0].base_body.program.consts[0].b &&
        !cases.front()[0].value.b,"private global staging mutated external case/program ownership"))return false;
  }
  return true;
}

bool test_private_intlist_region_lifetime() {
  auto base=phase({Value::from_int(91)},
      {ins_a(Opcode::PushConst,0),ins_ab(Opcode::CallBuiltin,int(BuiltinId::Singleton),1),
       ins_ab(Opcode::CallBuiltin,int(BuiltinId::Len),1),ins(Opcode::Return)});
  auto program=invocation(unary_segment(ValueTag::Int,std::move(base),constant_phase(Value::from_int(0))),0);
  program.n_locals=3;program.consts={Value::from_int(0),Value::from_int(1)};
  // Root [input] survives a scalar phase's temporary [91]. The new [input+1]
  // reuses that phase's discarded storage, then a root window joins both lists.
  program.code={ins_a(Opcode::Load,0),ins_ab(Opcode::CallBuiltin,int(BuiltinId::Singleton),1),ins_a(Opcode::Store,1),
      ins_a(Opcode::PushConst,0),ins_a(Opcode::BoundedRegion,0),ins_a(Opcode::Store,2),
      ins_a(Opcode::Load,0),ins_a(Opcode::PushConst,1),ins(Opcode::Add),
      ins_ab(Opcode::CallBuiltin,int(BuiltinId::Singleton),1),ins_a(Opcode::Store,2),
      ins_a(Opcode::Load,1),ins_a(Opcode::PushConst,0),ins_a(Opcode::PushConst,1),
      ins_ab(Opcode::CallBuiltin,int(BuiltinId::Slice),3),ins_a(Opcode::Load,2),
      ins_ab(Opcode::CallBuiltin,int(BuiltinId::Concat),2),ins(Opcode::Return)};
  program.instruction_fuel.assign(program.code.size(),1);
  std::vector<CaseBindings> cases;std::vector<Value> answers;
  for(int i=0;i<1024;++i) {
    const int input=i%17-8;cases.push_back({{0,Value::from_int(input)}});
    answers.push_back(payload::make_int_list_value({Value::from_int(input),Value::from_int(input+1)}));
  }
  ScopedEnvironment restore_tiles("GAGP_GPU_TILED_INITIAL_PAYLOAD"),restore_epoch("GAGP_GPU_PAYLOAD_EPOCH_INDEX");
  for(auto metric:{FitnessMetric::Exact,FitnessMetric::Levenshtein}) for(unsigned limit:{4u,5u}) {
    FitnessPolicy policy{metric};policy.list_allocation_limit=limit;
    std::vector<unsigned> wrong;std::vector<CpuFitnessCaseCounts> counts;
    const auto cpu=eval_fitness_cpu({program},cases,answers,1000,1000,33,policy,&wrong,&counts);
    for(const char* tiled:{"0","1"}) for(const char* epoch:{"0","1"}) {
      setenv("GAGP_GPU_TILED_INITIAL_PAYLOAD",tiled,1);
      setenv("GAGP_GPU_PAYLOAD_EPOCH_INDEX",epoch,1);
      FitnessSessionGpu session;if(!session.init(cases,answers,1000,33,1000,policy).ok)return false;
      int callbacks=0;cudaError_t callback_status=cudaSuccess;
      const auto result=session.eval_programs({program},true,[&]{++callbacks;callback_status=cudaDeviceSynchronize();});
      if(!check(result.ok && result.fitness==cpu && result.wrong==wrong && callbacks==1 && callback_status==cudaSuccess &&
          result.execution_profile.find("private-intlist-offset-v1-declined")==std::string::npos &&
          result.execution_profile.find("private-intlist-offset-v1")!=std::string::npos &&
          result.case_counts[0][0]==1024 && result.case_counts[0][1]==counts[0].errors &&
          counts[0].errors==(limit==4?1024u:0u) && result.case_counts[0][2]==counts[0].timeouts &&
          result.payload_replay_programs==0,"private scalar phase lifetime/full-case CPU/charge parity"))return false;
    }
  }
  return true;
}

bool test_private_intlist_fitness() {
  ScopedEnvironment restore_private("GAGP_GPU_PRIVATE_INTLIST_OFFSETS"),restore_global("GAGP_GPU_GLOBAL_PAYLOAD");
  setenv("GAGP_GPU_PRIVATE_INTLIST_OFFSETS","1",1);setenv("GAGP_GPU_GLOBAL_PAYLOAD","1",1);
  ScopedEnvironment fused("GAGP_GPU_FUSED_LIST_CONCAT"),small_hash("GAGP_GPU_SMALL_INT_HASH"),
      index("GAGP_GPU_PAYLOAD_INDEX"),epoch("GAGP_GPU_PAYLOAD_EPOCH_INDEX");
  setenv("GAGP_GPU_FUSED_LIST_CONCAT","1",1);setenv("GAGP_GPU_SMALL_INT_HASH","1",1);
  setenv("GAGP_GPU_PAYLOAD_INDEX","1",1);setenv("GAGP_GPU_PAYLOAD_EPOCH_INDEX","1",1);
  const bool compact=std::getenv("GAGP_GPU_COMPACT_INTLIST_CELLS") &&
      std::string(std::getenv("GAGP_GPU_COMPACT_INTLIST_CELLS"))=="1";
  const bool soa=std::getenv("GAGP_GPU_INTLIST_CELLS_SOA") &&
      std::string(std::getenv("GAGP_GPU_INTLIST_CELLS_SOA"))=="1";
  auto integer=Value::from_int(7);
  BytecodeProgram singleton;singleton.consts={integer};
  singleton.code={ins_a(Opcode::PushConst,0),ins_ab(Opcode::CallBuiltin,int(BuiltinId::Singleton),1),ins(Opcode::Return)};
  auto list=payload::make_int_list_value({integer});list.b=true; // external marker must be normalized
  BytecodeProgram input;input.n_locals=1;input.code={ins_a(Opcode::Load,0),ins(Opcode::Return)};
  BytecodeProgram constant;constant.consts={list};constant.code={ins_a(Opcode::PushConst,0),ins(Opcode::Return)};
  FitnessPolicy policy;policy.metric=FitnessMetric::Exact;policy.list_allocation_limit=65536;
  for(auto metric:{FitnessMetric::Exact,FitnessMetric::Levenshtein}) {
    policy.metric=metric;
    FitnessSessionGpu session;
    if(!session.init({{{0,list}}},{list},100,32,1000,policy).ok)return false;
    auto result=session.eval_programs({singleton,input,constant},true);
    if(!check(result.ok && result.fitness==std::vector<double>(3,0) && result.wrong==std::vector<unsigned>(3,0) &&
        result.execution_profile.find("private-intlist-offset-v1-declined")==std::string::npos &&
        result.execution_profile.find("private-intlist-offset-v1")!=std::string::npos,
        "private full scoring/external normalization"))return false;
    if(!check((result.execution_profile.find("-intlist-cells-soa")!=std::string::npos)==soa &&
        (result.execution_profile.find("-intlist-cells-soa-declined")!=std::string::npos)==(soa && !compact),
        "interleaved compact cells require both private and compact admission"))return false;
    if(!check((result.execution_profile.find("-compact-intlist-cells")!=std::string::npos)==compact &&
        result.execution_profile.find("-compact-intlist-cells-declined")==std::string::npos &&
        result.execution_profile.find("-fused-concat")==std::string::npos &&
        result.execution_profile.find("-small-int-hash")==std::string::npos &&
        result.execution_profile.find("-list-index")==std::string::npos &&
        result.execution_profile.find("-epoch")==std::string::npos,"private storage diagnostics report executed work"))return false;
    auto inequality=singleton;
    inequality.consts={list};inequality.code={ins_a(Opcode::PushConst,0),ins_a(Opcode::PushConst,0),ins(Opcode::Eq),ins(Opcode::Return)};
    result=session.eval_programs({singleton,inequality},true);
    if(!check(result.ok && result.execution_profile.find("private-intlist-offset-v1-declined")!=std::string::npos,
        "mixed population containing container Eq must decline whole profile"))return false;
    if(soa && !check(result.execution_profile.find("-intlist-cells-soa-declined")!=std::string::npos,
        "mixed population must decline interleaved cells"))return false;
  }
  auto segment=unary_segment(ValueTag::IntList,constant_phase(list),load_phase(RegionSlotBank::Result,0));
  segment.plan.state_types.push_back(ValueTag::IntList);
  if(!check(!gagp::detail::view_region_supported(segment,true),"memoized subordinate IntList state admitted"))return false;
  // A large canonical constant concat forces initial physical overflow and GPU
  // replay. Exact content scoring must retain actual length 65536, not empty.
  std::vector<Value> large(65535,integer);
  auto almost=payload::make_int_list_value(large);large.push_back(integer);
  auto full=payload::make_int_list_value(large);list.b=false;
  BytecodeProgram concat;concat.consts={almost,list};concat.code={ins_a(Opcode::PushConst,0),ins_a(Opcode::PushConst,1),
      ins_ab(Opcode::CallBuiltin,int(BuiltinId::Concat),2),ins(Opcode::Return)};
  policy.metric=FitnessMetric::Exact;
  for(unsigned limit:{65535u,65536u}) {
    policy.list_allocation_limit=limit;
    FitnessSessionGpu session;if(!session.init({{}},{full},100,32,1000,policy).ok)return false;
    const auto result=session.eval_programs({concat},true);
    if(!check(result.ok && result.fitness[0]==(limit==65536?0:-1) &&
        result.wrong[0]==(limit==65536?0u:1u) && result.payload_replay_programs==(limit==65536?1u:0u),
        "private length65536 logical boundary/replay"))return false;
  }
  // A host-clamped full constant plus a constant singleton produces65537
  // under a65536 logical charge. Score against an admitted65536 answer; it must
  // be wrong by content and finish every case after exactly one physical replay.
  concat.consts[0]=full;
  FitnessSessionGpu boundary;
  const std::vector<CaseBindings> boundary_cases(1024);
  const std::vector<Value> boundary_answers(1024,full);
  if(!boundary.init(boundary_cases,boundary_answers,100,32,1000,policy).ok)return false;
  const auto beyond=boundary.eval_programs({concat},true);
  if(!check(beyond.ok && beyond.fitness[0]==-1024 && beyond.wrong[0]==1024 &&
      beyond.case_counts[0][0]==1024 && beyond.case_counts[0][1]==0 && beyond.case_counts[0][2]==0 &&
      beyond.case_counts[0][4]==0 && beyond.payload_replay_programs==1 && beyond.payload_replay_launches==1 &&
      beyond.payload_replay_value_capacity==65537 &&
      beyond.execution_profile.find("private-intlist-offset-v1-declined")==std::string::npos,
      "private generated65537 content mismatch/full-case single replay"))return false;
  // Unsupported policies retain precisely the existing view backend even when
  // this same session previously ran the allocating private profile.
  ScopedEnvironment restore_view("GAGP_VIEW_PROFILE"),restore_typed("GAGP_TYPED_VIEW_PHASE");
  setenv("GAGP_VIEW_PROFILE","1",1);setenv("GAGP_TYPED_VIEW_PHASE","1",1);
  auto scalar=invocation(unary_segment(ValueTag::Int,constant_phase(integer),
      load_phase(RegionSlotBank::Result,0)),3);
  FitnessSessionGpu reused;
  if(!reused.init({{}},{list},100,32,1000,policy).ok || !reused.eval_programs({singleton}).ok)return false;
  for(auto metric:{FitnessMetric::Absolute,FitnessMetric::Exact}) {
    auto unsupported=policy;unsupported.metric=metric;
    if(metric==FitnessMetric::Exact)unsupported.list_allocation_limit=0;
    setenv("GAGP_GPU_PRIVATE_INTLIST_OFFSETS","0",1);
    if(!reused.init({{}},{integer},100,32,1000,unsupported).ok)return false;
    const auto control=reused.eval_programs({scalar},true);
    setenv("GAGP_GPU_PRIVATE_INTLIST_OFFSETS","1",1);
    if(!reused.init({{}},{integer},100,32,1000,unsupported).ok)return false;
    const auto declined=reused.eval_programs({scalar},true);
    auto profile=declined.execution_profile;
    const std::string suffix="-private-intlist-offset-v1-declined";
    const auto position=profile.find(suffix);if(position!=std::string::npos)profile.erase(position,suffix.size());
    if(!check((!compact || (control.execution_profile.find("-compact-intlist-cells-declined")!=std::string::npos &&
        declined.execution_profile.find("-compact-intlist-cells-declined")!=std::string::npos)) &&
        control.ok && declined.ok && position!=std::string::npos && profile==control.execution_profile &&
        profile.find("int-list-views")==0 && control.fitness==declined.fitness && control.wrong==declined.wrong &&
        control.case_counts==declined.case_counts,"declined policy/session reinit preserves ordinary view backend"))return false;
  }
  setenv("GAGP_GPU_PRIVATE_INTLIST_OFFSETS","1",1);
  auto missing=constant;missing.consts={Value::from_int_list_hash_len(0xabcdef123456ULL,1)};missing.consts[0].b=true;
  if(!reused.init({{}},{list},100,32,1000,policy).ok)return false;
  const auto missing_result=reused.eval_programs({missing},true);
  if(!check(missing_result.ok && missing_result.execution_profile.find("private-intlist-offset-v1-declined")!=std::string::npos,
      "missing external payload/forged marker must decline private profile"))return false;
  // Real workspace admission must account for eight-byte cells: under the
  // same64MiB bound, more physical lanes fit without changing value capacities.
  {
    ScopedEnvironment budget("GAGP_GPU_PAYLOAD_WORKSPACE_MIB"),tiles("GAGP_GPU_TILED_INITIAL_PAYLOAD"),
        restore_mode("GAGP_GPU_COMPACT_INTLIST_CELLS"),restore_soa("GAGP_GPU_INTLIST_CELLS_SOA");
    setenv("GAGP_GPU_PAYLOAD_WORKSPACE_MIB","64",1);setenv("GAGP_GPU_TILED_INITIAL_PAYLOAD","0",1);
    const std::vector<BytecodeProgram> population(65,singleton);
    std::size_t boxed_blocks=0,compact_blocks=0;
    for(int mode:{0,1,2}) {
      setenv("GAGP_GPU_COMPACT_INTLIST_CELLS",mode?"1":"0",1);
      setenv("GAGP_GPU_INTLIST_CELLS_SOA",mode==2?"1":"0",1);
      FitnessSessionGpu session;if(!session.init({{}},{list},100,32,1000,policy).ok)return false;
      const auto result=session.eval_programs(population,true);
      if(!check(result.ok && result.fitness==std::vector<double>(65,0) && result.wrong==std::vector<unsigned>(65,0) &&
          result.initial_payload_value_capacity==8192 && result.payload_replay_programs==0,
          "boxed/compact workspace budget/capacity/coverage"))return false;
      if(mode==0)boxed_blocks=result.initial_payload_physical_blocks;
      else {
        if(!check(result.initial_payload_physical_blocks>boxed_blocks,"compact typed bytes must admit more physical blocks"))return false;
        if(mode==1)compact_blocks=result.initial_payload_physical_blocks;
        else if(!check(result.initial_payload_physical_blocks==compact_blocks,"interleaving preserves physical allocation/admission"))return false;
      }
    }
  }
  for(int fuel:{0,1,2,3}) {
    FitnessSessionGpu session;if(!session.init({{}},{list},fuel,32,1000,policy).ok)return false;
    const auto result=session.eval_programs({singleton},true);
    if(!check(result.ok && result.case_counts[0][0]==1 && result.case_counts[0][2]==(fuel<3?1u:0u),
        "private fuel/error ordering"))return false;
  }
  return test_private_intlist_region_lifetime();
}
