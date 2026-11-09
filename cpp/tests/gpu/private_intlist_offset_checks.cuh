#pragma once
#include "../../src/runtime/gpu/device/resident_score_device.cuh"
#include "../../src/runtime/gpu/view_profile.hpp"
#include "../../src/runtime/gpu/private_global_offsets.hpp"
#include "../../src/runtime/gpu/private_tuple_policy.hpp"

__global__ void inspect_private_intlist_offsets(Value* cells, std::int64_t* int_cells, Value* source, bool compact, int* failure) {
  using namespace gagp::gpu_detail;
  const int lane=threadIdx.x;
  failure+=lane;*failure=0;
  cells+=static_cast<std::size_t>(lane)*65538;
  source+=static_cast<std::size_t>(lane)*65537;
  int_cells+=static_cast<std::size_t>(lane)*65538;
  DArenaPayloadState st;
  DListPayloadEntry entries[4]{};
  st.private_intlists=true;st.compact_intlist_cells=compact;
  st.list_values=compact?nullptr:cells;st.intlist_cells=compact?int_cells:nullptr;st.kMaxListValues=65536;
  st.list_entries=entries;st.list_entry_capacity=4;st.logical_list_limit=65536;
  DPayloadTables tables{};
  ErrCode error=ErrCode::Value;
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
     !d_private_intlist_span(tables,st,b,span) || span.size!=2 || span.at(0)!=1) {*failure=14;return;}
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
  DIndexedArenaPayloadState local;local.private_intlists=true;local.compact_intlist_cells=compact;
  local.private_warp_copy=true;
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
     !d_private_intlist_span(tables,local,replacement,span) || span.size!=1 || (compact?span.integers!=int_cells+saved:span.boxed!=cells+saved) || span.at(0)!=8 ||
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

bool test_private_intlist_offsets() {
  Value *cells=nullptr,*source=nullptr;std::int64_t* int_cells=nullptr;
  int *device_failure=nullptr,failure=-1;std::vector<int> failures(32,-1);
  if(cudaMalloc(reinterpret_cast<void**>(&cells),65538*32*sizeof(Value))!=cudaSuccess)return false;
  if(cudaMalloc(reinterpret_cast<void**>(&source),65537*32*sizeof(Value))!=cudaSuccess){cudaFree(cells);return false;}
  if(cudaMalloc(reinterpret_cast<void**>(&int_cells),65538*32*sizeof(std::int64_t))!=cudaSuccess){cudaFree(cells);cudaFree(source);return false;}
  if(cudaMalloc(reinterpret_cast<void**>(&device_failure),32*sizeof(int))!=cudaSuccess){cudaFree(cells);cudaFree(source);cudaFree(int_cells);return false;}
  bool ok=true;
  for(int width:{1,32})for(bool compact:{false,true}) {
    if(!ok)continue;
    inspect_private_intlist_offsets<<<1,width>>>(cells,int_cells,source,compact,device_failure);
    ok=cudaGetLastError()==cudaSuccess && cudaDeviceSynchronize()==cudaSuccess &&
        cudaMemcpy(failures.data(),device_failure,width*sizeof(int),cudaMemcpyDeviceToHost)==cudaSuccess;
    for(int lane=0;ok && lane<width;++lane)if(failures[lane]){failure=failures[lane];ok=false;}
    if(!ok)break;
    failure=0;
  }
  cudaFree(cells);cudaFree(source);cudaFree(int_cells);cudaFree(device_failure);
  return check(ok,"boxed/compact private IntList boundary/origin/precision "+std::to_string(failure));
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
  std::vector<CaseBindings> cases;std::vector<Value> answers;
  for(int i=0;i<1024;++i) {
    auto values=std::vector<Value>(i%4,Value::from_int(INT64_C(9007199254740993)+i%11));
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
    int callbacks=0;
    const auto result=session.eval_programs(programs,true,[&]{++callbacks;});
    const bool active=iteration==0 || iteration==3;
    if(!check(result.ok && result.fitness==cpu && result.wrong==wrong && callbacks==1 &&
        (result.execution_profile.find("-private-global-offsets-declined")==std::string::npos)==active,
        "private global full-case/session reinit/decline CPU parity"))return false;
    if(!check((result.execution_profile.find("-private-typed-phases-declined")==std::string::npos)==active,
        "typed private admission must require complete current normalization"))return false;
    for(unsigned p=0;p<programs.size();++p)
      if(!check(result.case_counts[p][0]==1024 && result.case_counts[p][1]==counts[p].errors &&
          result.case_counts[p][2]==counts[p].timeouts,"private global lazy capture case/status counts"))return false;
    if(!check(!programs[1].consts[0].b && !programs[2].bounded_region_segments[0].base_body.program.consts[0].b &&
        !cases.front()[0].value.b,"private global staging mutated external case/program ownership"))return false;
  }
  // Inputs and either constant pool with an actual65536 list decline only
  // typed execution. An expected-answer-only65536 list is outside the word VM.
  const auto full=payload::make_int_list_value(std::vector<Value>(65536,Value::from_int(17)));
  const auto small=payload::make_int_list_value({Value::from_int(17)});
  for(int location=0;location<4;++location) {
    BytecodeProgram program;std::vector<CaseBindings> input_cases(1);
    if(location==0) {
      program.n_locals=1;program.code={ins_a(Opcode::Load,0),ins(Opcode::Return)};
      input_cases[0]={{0,full}};
    } else if(location==2) {
      program=invocation(unary_segment(ValueTag::IntList,constant_phase(full),
          load_phase(RegionSlotBank::Result,0)),0);
    } else {
      program.consts={location==3?small:full};
      program.code={ins_a(Opcode::PushConst,0),ins(Opcode::Return)};
    }
    FitnessSessionGpu boundary;
    if(!boundary.init(input_cases,{full},1000,32,1000,policy).ok)return false;
    const auto result=boundary.eval_programs({program},true);
    std::vector<unsigned> wrong;
    const auto cpu=eval_fitness_cpu({program},input_cases,{full},1000,1000,32,policy,&wrong);
    if(!check(result.ok && result.fitness==cpu && result.wrong==wrong &&
        result.execution_profile.find("private-intlist-offset-v1-declined")==std::string::npos &&
        (result.execution_profile.find("-private-typed-phases-declined")==std::string::npos)==(location==3),
        "typed private boundary declines transport but preserves expected-only65536 answer"))return false;
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
  for(auto metric:{FitnessMetric::Exact,FitnessMetric::Levenshtein}) for(unsigned limit:{4u,5u}) {
    FitnessPolicy policy{metric};policy.list_allocation_limit=limit;
    std::vector<unsigned> wrong;std::vector<CpuFitnessCaseCounts> counts;
    const auto cpu=eval_fitness_cpu({program},cases,answers,1000,1000,33,policy,&wrong,&counts);
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
  return true;
}

bool test_private_tuple_fitness() {
  const auto tuple=[](std::initializer_list<std::int64_t> fields) {
    std::vector<Value> values;for(auto x:fields)values.push_back(Value::from_int(x));
    return payload::make_int_list_value(values);
  };
  for(int summaries:{0,1,2})for(unsigned limit:{0u,3u,65536u}) {
    FitnessPolicy policy{FitnessMetric::TupleSummary};policy.summary_count=summaries;
    policy.summary_weight=.17;policy.summary_cap=7;
    const auto expected=tuple({INT64_MIN,9007199254740993LL,INT64_MAX});
    std::vector<Value> answer_fields{Value::from_int(INT64_MIN),Value::from_int(9007199254740993LL),Value::from_int(INT64_MAX)};
    answer_fields.resize(summaries+1);
    const auto answer=payload::make_int_list_value(answer_fields);
    policy.list_allocation_limit=limit;
    // Include primary-equal/aux-different, signed extremes, invalid lengths,
    // generated backing, runtime error and an exhausted instruction budget.
    std::vector<BytecodeProgram> programs;
    for(const auto value:{answer,expected,tuple({INT64_MAX,INT64_MIN,9007199254740992LL}),tuple({}),tuple({INT64_MIN})}) {
      BytecodeProgram p;p.consts={value};p.code={ins_a(Opcode::PushConst,0),ins(Opcode::Return)};
      p.instruction_fuel={1,1};programs.push_back(p);
    }
    if(summaries==2) {
      auto p=programs[0];
      p.consts={tuple({INT64_MIN}),tuple({9007199254740992LL,INT64_MAX})};
      p.code={ins_a(Opcode::PushConst,0),ins_a(Opcode::PushConst,1),ins_ab(Opcode::CallBuiltin,int(BuiltinId::Concat),2),ins(Opcode::Return)};
      p.instruction_fuel={1,1,1,1};programs.push_back(p);
    }
    for(int fuel:{0,3,100}) {
      std::vector<unsigned> wrong;std::vector<CpuFitnessCaseCounts> counts;
      const auto cpu=eval_fitness_cpu(programs,{{}},{answer},fuel,1000,33,policy,&wrong,&counts);
      FitnessSessionGpu session;if(!session.init({{}},{answer},fuel,33,1000,policy).ok)return false;
      const auto gpu=session.eval_programs(programs,true);
      if(!check(gpu.ok && gpu.fitness==cpu && gpu.wrong==wrong,"private tuple scoring/fuel/primary correctness parity"))return false;
      const bool active=gpu.execution_profile.find("-private-tuple-score")!=std::string::npos &&
          gpu.execution_profile.find("-private-tuple-score-declined")==std::string::npos;
      if(!check(active,"private tuple admission"))return false;
      for(std::size_t i=0;i<counts.size();++i)
        if(!check(gpu.case_counts[i][1]==counts[i].errors && gpu.case_counts[i][2]==counts[i].timeouts &&
            gpu.case_counts[i][4]==0,"private tuple error/timeout counters"))return false;
    }
  }
  // A zero-cap program can allocate much more than the private descriptor can
  // represent, even if it later slices back to a tuple. Reject such allocation
  // paths before enabling private storage; inputs themselves remain permitted.
  BytecodeProgram oversized;oversized.n_locals=1;
  oversized.consts={Value::from_int(0),Value::from_int(3)};
  oversized.code={ins_a(Opcode::Load,0),ins_a(Opcode::Load,0),
      ins_ab(Opcode::CallBuiltin,int(BuiltinId::Concat),2),ins_a(Opcode::PushConst,0),
      ins_a(Opcode::PushConst,1),ins_ab(Opcode::CallBuiltin,int(BuiltinId::Slice),3),ins(Opcode::Return)};
  std::array<ValueTag,64> types;types.fill(ValueTag::Invalid);types[0]=ValueTag::IntList;
  if(!check(!gpu_detail::bounded_tuple_allocations(oversized,types),"unbounded zero-cap concat admitted"))return false;
  const auto input=tuple({1,2,3});FitnessPolicy narrow{FitnessMetric::TupleSummary};narrow.summary_count=2;
  FitnessSessionGpu ordinary;
  if(!ordinary.init({{{0,input}}},{input},100,32,1000,narrow).ok)return false;
  const auto rejected=ordinary.eval_programs({oversized},true);
  std::vector<unsigned> reference_wrong;
  const auto reference=eval_fitness_cpu({oversized},{{{0,input}}},{input},100,1000,32,narrow,&reference_wrong);
  if(!check(rejected.ok && rejected.fitness==reference && rejected.wrong==reference_wrong &&
      rejected.execution_profile.find("-private-tuple-score-declined")!=std::string::npos,
      "unsafe tuple allocation must preserve ordinary evaluation"))return false;
  // Container-identity observations still decline the whole private profile.
  auto list=tuple({1,2,3});BytecodeProgram identity;identity.consts={list};
  identity.code={ins_a(Opcode::PushConst,0),ins_a(Opcode::PushConst,0),ins(Opcode::Eq),ins(Opcode::Return)};
  FitnessPolicy policy{FitnessMetric::TupleSummary};policy.summary_count=2;
  FitnessSessionGpu session;if(!session.init({{}},{list},100,32,1000,policy).ok)return false;
  const auto declined=session.eval_programs({identity},true);
  return check(declined.ok && declined.execution_profile.find("-private-tuple-score-declined")!=std::string::npos,
      "tuple container equality must retain ordinary execution");
}

bool test_private_intlist_fitness() {
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
    if(!check(result.execution_profile.find("-compact-intlist-cells")!=std::string::npos &&
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
  }
  auto segment=unary_segment(ValueTag::IntList,constant_phase(list),load_phase(RegionSlotBank::Result,0));
  segment.plan.state_types.push_back(ValueTag::IntList);
  if(!check(!gagp::detail::view_region_supported(segment,true),"memoized subordinate IntList state admitted"))return false;
  // A large canonical constant concat may overflow the initial physical
  // capacity and replay on the GPU. Exact content scoring must retain actual
  // length 65536, not empty.
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
        result.wrong[0]==(limit==65536?0u:1u) && result.payload_replay_programs<=(limit==65536?1u:0u),
        "private length65536 logical boundary/replay (limit="+std::to_string(limit)+" ok="+std::to_string(result.ok)+
        " fitness="+(result.fitness.empty()?std::string("none"):std::to_string(result.fitness[0]))+
        " replay="+std::to_string(result.payload_replay_programs)+" err="+result.err.message+" profile="+result.execution_profile+")"))return false;
  }
  // A host-clamped full constant plus a constant singleton produces65537
  // under a65536 logical charge. Score against an admitted65536 answer; it must
  // be wrong by content and finish every case, replaying at most once when the
  // initial physical capacity overflows.
  concat.consts[0]=full;
  FitnessSessionGpu boundary;
  const std::vector<CaseBindings> boundary_cases(1024);
  const std::vector<Value> boundary_answers(1024,full);
  if(!boundary.init(boundary_cases,boundary_answers,100,32,1000,policy).ok)return false;
  const auto beyond=boundary.eval_programs({concat},true);
  if(!check(beyond.ok && beyond.fitness[0]==-1024 && beyond.wrong[0]==1024 &&
      beyond.case_counts[0][0]==1024 && beyond.case_counts[0][1]==0 && beyond.case_counts[0][2]==0 &&
      beyond.case_counts[0][4]==0 && beyond.payload_replay_programs<=1 && beyond.payload_replay_launches<=1 &&
      (beyond.payload_replay_programs==0 || beyond.payload_replay_value_capacity==65537) &&
      beyond.execution_profile.find("private-intlist-offset-v1-declined")==std::string::npos,
      "private generated65537 content mismatch/full-case replay (ok="+std::to_string(beyond.ok)+
      " fitness="+(beyond.fitness.empty()?std::string("none"):std::to_string(beyond.fitness[0]))+
      " wrong="+(beyond.wrong.empty()?std::string("none"):std::to_string(beyond.wrong[0]))+
      " replay="+std::to_string(beyond.payload_replay_programs)+"/"+std::to_string(beyond.payload_replay_launches)+
      " values="+std::to_string(beyond.payload_replay_value_capacity)+" profile="+beyond.execution_profile+")"))return false;
  // Unsupported policies retain precisely the existing view backend even when
  // this same session previously ran the allocating private profile.
  auto scalar=invocation(unary_segment(ValueTag::Int,constant_phase(integer),
      load_phase(RegionSlotBank::Result,0)),3);
  FitnessSessionGpu reused;
  if(!reused.init({{}},{list},100,32,1000,policy).ok || !reused.eval_programs({singleton}).ok)return false;
  for(auto metric:{FitnessMetric::Absolute,FitnessMetric::Exact}) {
    auto unsupported=policy;unsupported.metric=metric;
    if(metric==FitnessMetric::Exact)unsupported.list_allocation_limit=0;
    if(!reused.init({{}},{integer},100,32,1000,unsupported).ok)return false;
    const auto declined=reused.eval_programs({scalar},true);
    std::vector<unsigned> expected_wrong;
    const auto expected=eval_fitness_cpu({scalar},{{}},{integer},100,1000,32,unsupported,&expected_wrong);
    if(!check(declined.ok && declined.fitness==expected && declined.wrong==expected_wrong &&
        declined.execution_profile.find("-private-intlist-offset-v1-declined")!=std::string::npos &&
        declined.execution_profile.find("-compact-intlist-cells-declined")!=std::string::npos &&
        declined.execution_profile.find("int-list-views")==0,
        "declined policy/session reinit preserves ordinary view backend"))return false;
  }
  auto missing=constant;missing.consts={Value::from_int_list_hash_len(0xabcdef123456ULL,1)};missing.consts[0].b=true;
  if(!reused.init({{}},{list},100,32,1000,policy).ok)return false;
  const auto missing_result=reused.eval_programs({missing},true);
  if(!check(missing_result.ok && missing_result.execution_profile.find("private-intlist-offset-v1-declined")!=std::string::npos,
      "missing external payload/forged marker must decline private profile"))return false;
  {
    const std::vector<BytecodeProgram> population(65,singleton);
    FitnessSessionGpu session;if(!session.init({{}},{list},100,32,1000,policy).ok)return false;
    const auto result=session.eval_programs(population,true);
    if(!check(result.ok && result.fitness==std::vector<double>(65,0) && result.wrong==std::vector<unsigned>(65,0) &&
        result.payload_replay_programs==0,"compact workspace coverage"))return false;
  }
  for(int fuel:{0,1,2,3}) {
    FitnessSessionGpu session;if(!session.init({{}},{list},fuel,32,1000,policy).ok)return false;
    const auto result=session.eval_programs({singleton},true);
    if(!check(result.ok && result.case_counts[0][0]==1 && result.case_counts[0][2]==(fuel<3?1u:0u),
        "private fuel/error ordering"))return false;
  }
  return test_private_intlist_region_lifetime();
}

bool test_reusable_private_workspace() {
  const auto value=Value::from_int(INT64_MIN+7);
  const auto answer=payload::make_int_list_value({value});
  BytecodeProgram program;program.n_locals=1;
  program.code={ins_a(Opcode::Load,0),ins_ab(Opcode::CallBuiltin,int(BuiltinId::Singleton),1),ins(Opcode::Return)};
  const std::vector<BytecodeProgram> population(65,program);
  const std::vector<CaseBindings> cases{{{0,value}}};const std::vector<Value> answers{answer};
  FitnessPolicy policy;policy.metric=FitnessMetric::Exact;policy.list_allocation_limit=65536;
  const auto cpu=eval_fitness_cpu(population,cases,answers,100,1000,1024,policy);
  FitnessSessionGpu session;if(!session.init(cases,answers,100,1024,1000,policy).ok)return false;
  bool nested_rejected=false;int callbacks=0;
  const auto first=session.eval_programs(population,true,[&] {
    ++callbacks;
    const auto nested=session.eval_programs(population,true);
    const auto reset=session.init(cases,answers,100,1024,1000,policy);
    nested_rejected=!nested.ok && !reset.ok && nested.err.message.find("in use")!=std::string::npos &&
        reset.err.message.find("in use")!=std::string::npos;
  });
  const auto budget=first.payload_workspace_budget_bytes;
  if(!check(first.ok && first.fitness==cpu && callbacks==1 && nested_rejected &&
      first.payload_workspace_pool_allocations==1 && first.payload_workspace_retained_bytes>0 &&
      first.payload_workspace_pool_peak_bytes<=budget &&
      first.execution_profile.find("-reuse-private-workspace-declined")==std::string::npos,
      "single bounded backing/callback lease must preserve complete work"))return false;
  auto different=program;different.consts={Value::from_int(INT64_MAX)};different.code[0]=ins_a(Opcode::PushConst,0);
  for(const auto& current:{std::vector<BytecodeProgram>(65,different),population}) {
    const auto expected=eval_fitness_cpu(current,cases,answers,100,1000,1024,policy);
    const auto result=session.eval_programs(current,true);
    if(!check(result.ok && result.fitness==expected && result.payload_workspace_pool_allocations==0 &&
        result.payload_workspace_retained_bytes==first.payload_workspace_retained_bytes &&
        result.payload_workspace_pool_peak_bytes<=budget,"dirty pooled cells changed CPU content fitness"))return false;
  }
  bool threw=false;
  try {(void)session.eval_programs(population,true,[]{throw std::runtime_error("pooled callback test");});}
  catch(const std::runtime_error&){threw=true;}
  const auto recovered=session.eval_programs(population,true);
  if(!check(threw && recovered.ok && recovered.fitness==cpu && recovered.payload_workspace_pool_allocations==0,
      "throwing callback left pooled storage leased or unsafe"))return false;
  const std::vector<BytecodeProgram> many(4096,program);
  const auto larger=session.eval_programs(many,true);
  if(!check(larger.ok && larger.fitness==std::vector<double>(4096,0) &&
      larger.payload_workspace_pool_peak_bytes<=budget,"pooled backing exceeded the payload workspace cap"))return false;
  for(const auto& counts:larger.case_counts)if(!check(counts[0]==1 && !counts[1] && !counts[2],"pooled metadata resize omitted cases"))return false;
  FitnessSessionGpu moved(std::move(session));
  const auto after_move=moved.eval_programs(population,true);
  if(!check(after_move.ok && after_move.fitness==cpu,"session move lost reusable storage ownership"))return false;
  if(!moved.init(cases,answers,100,1024,1000,policy).ok)return false;
  const auto reset=moved.eval_programs(population,true);
  return check(reset.ok && reset.fitness==cpu && reset.payload_workspace_pool_allocations==1 &&
      reset.payload_workspace_pool_peak_bytes<=budget,"session reset retained old arena allocation");
}
