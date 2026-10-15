#pragma once

namespace trivial_private_phase_test {

__device__ bool same_result(const gagp::gpu_detail::DResult& a,const gagp::gpu_detail::DResult& b) {
  return a.is_error==b.is_error && (a.is_error?a.err_code==b.err_code:
      a.value.tag==b.value.tag && a.value.i==b.value.i && a.value.b==b.value.b);
}

__global__ void inspect(int* failure) {
  using namespace gagp::gpu_detail;
  *failure=0;
  DInstr code[2]={{OP_PUSH_CONST,DINSTR_HAS_A,0,0,3},{OP_RETURN,0,0,0,2}};
  Value constants[1]={Value::from_int_list_hash_len(0x123456,7)};
  DRegionPhase phase{};auto& meta=phase.program;
  meta.code_len=2;meta.const_len=1;meta.n_locals=8;meta.verified_stack_bound=1;
  meta.trivial_constant_index=0;phase.binding_count=8;
  DRegionPhaseBinding bindings[8]{};Value caller[8];
  DRegionSegment segment{};segment.parameter_count=8;segment.sequence_state=0;
  for(int i=0;i<8;++i) {
    bindings[i]={RegionSlotBank::Parameter,static_cast<unsigned>(i),i};
    segment.parameter_caller_locals[i]=i;segment.parameter_types[i]=ValueTag::Int;
    caller[i]=Value::from_float(1.5); // Incorrect, but never read by the constant phase.
  }
  DExecutionTables tables{};tables.phase_code=code;tables.phase_consts=constants;
  tables.region_phases=&phase;tables.region_phase_count=1;
  tables.region_bindings=bindings;tables.region_binding_count=8;
  DRegionFrame frame{};frame.state[0]=Value::from_int_list_hash_len(456,7);
  DPayloadTables payloads{}; // A constant return must not resolve even this missing token.
  DArenaPayloadState state;state.private_intlists=true;
  for(int length=1;length<=2;++length)for(int capture_case=0;capture_case<2;++capture_case)for(int initial=0;initial<=6;++initial) {
    const int captures=capture_case?255:0;
    meta.code_len=length;
    int a_fuel=initial,b_fuel=initial;state.trivial_phases=false;
    const auto a=d_region_phase<DPayloadFlavor::ArenaMixed>(segment,0,ValueTag::IntList,frame,
        caller,captures,payloads,state,tables,a_fuel,true);
    state.trivial_phases=true;
    const auto b=d_region_phase<DPayloadFlavor::ArenaMixed>(segment,0,ValueTag::IntList,frame,
        caller,captures,payloads,state,tables,b_fuel,true);
    if(!same_result(a,b) || a_fuel!=b_fuel || (!b.is_error &&
        (b.value.i!=constants[0].i || b.value.b!=constants[0].b || state.list_entry_count))) {
      *failure=1;return;
    }
  }
  // Exact result guards, including the result-phase-only fallback exception.
  meta.code_len=2;
  for(int invalid=0;invalid<2;++invalid)for(int result_phase=0;result_phase<2;++result_phase) {
    constants[0]=invalid?Value::invalid():Value::from_fallback_token(91);
    int a_fuel=5,b_fuel=5;state.trivial_phases=false;
    const auto a=d_region_phase<DPayloadFlavor::ArenaMixed>(segment,0,ValueTag::IntList,frame,
        caller,0,payloads,state,tables,a_fuel,result_phase);
    state.trivial_phases=true;
    const auto b=d_region_phase<DPayloadFlavor::ArenaMixed>(segment,0,ValueTag::IntList,frame,
        caller,0,payloads,state,tables,b_fuel,result_phase);
    if(!same_result(a,b) || a_fuel!=b_fuel){*failure=2;return;}
  }
  // Unused formal bindings preserve lazy capture errors. The selected Load may
  // be away from binding zero and its local number need not match its index.
  code[0].op=OP_LOAD;meta.n_locals=1;phase.binding_count=1;
  meta.trivial_constant_index=-1;meta.trivial_load_binding=0;
  frame.prepared[0]=Value::from_int(-9);
  frame.results[0]=Value::from_fallback_token(71);
  frame.state[0]=d_private_intlist(11,7);
  const RegionSlotBank banks[]={RegionSlotBank::State,RegionSlotBank::Prepared,RegionSlotBank::Result,RegionSlotBank::Measure};
  for(int binding_count:{1,8})for(int bank=0;bank<4;++bank)
  for(int length=1;length<=2;++length)for(int captures:{0,255})
  for(int initial=0;initial<=6;++initial)for(int result_phase=0;result_phase<2;++result_phase) {
    phase.binding_count=binding_count;meta.n_locals=binding_count;meta.code_len=length;
    if(binding_count==8) {
      for(int j=0;j<8;++j)bindings[j]={RegionSlotBank::Parameter,static_cast<unsigned>(j),(j*3)%8};
      bindings[1]={RegionSlotBank::State,1,3};
      bindings[2]={RegionSlotBank::Prepared,1,6};
      bindings[3]={RegionSlotBank::Result,1,1};
      bindings[5]={banks[bank],0,7};code[0].a=7;meta.trivial_load_binding=5;
    } else {bindings[0]={banks[bank],0,0};code[0].a=0;meta.trivial_load_binding=0;}
    const auto expected=bank==0 || bank==2?ValueTag::IntList:ValueTag::Int;
    int a_fuel=initial,b_fuel=initial;state.trivial_phases=false;
    const auto a=d_region_phase<DPayloadFlavor::ArenaMixed>(segment,0,expected,frame,
        caller,captures,payloads,state,tables,a_fuel,result_phase);
    state.trivial_phases=true;
    const auto b=d_region_phase<DPayloadFlavor::ArenaMixed>(segment,0,expected,frame,
        caller,captures,payloads,state,tables,b_fuel,result_phase);
    if(!same_result(a,b) || a_fuel!=b_fuel){*failure=3;return;}
  }
  // New constant metadata never bypasses the view VM's canonical resolution.
  code[0].op=OP_PUSH_CONST;meta.trivial_load_binding=-1;meta.trivial_constant_index=0;
  meta.n_locals=0;phase.binding_count=0;meta.code_len=2;code[0].a=0;
  constants[0]=Value::from_int_list_hash_len(0x123456,7);
  DPayloadFlavorTraits<DPayloadFlavor::IntListViews>::State views{};
  int fuel=10;
  const auto declined=d_region_phase<DPayloadFlavor::IntListViews>(segment,0,ValueTag::IntList,frame,
      caller,0,payloads,views,tables,fuel,true);
  if(!declined.is_error || declined.err_code!=ErrCode::Value || fuel!=7){*failure=4;return;}
}

bool test_contract() {
  int* failure=nullptr;
  if(cudaMallocManaged(&failure,sizeof(int))!=cudaSuccess)return false;
  *failure=-1;inspect<<<1,1>>>(failure);
  const bool ok=cudaDeviceSynchronize()==cudaSuccess &&
      check(*failure==0,"private trivial phase instruction/fuel/lazy-capture/identity guard "+std::to_string(*failure));
  cudaFree(failure);return ok;
}

} // namespace trivial_private_phase_test
