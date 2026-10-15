#pragma once

namespace private_bound_phase_test {

template <gagp::gpu_detail::DPayloadFlavor Flavor>
__global__ void inspect(int* failure) {
  using namespace gagp::gpu_detail;
  *failure=0;
  DInstr code[4]={{OP_LOAD,DINSTR_HAS_A,0,0,2},{OP_LOAD,DINSTR_HAS_A,1,0,3},
      {OP_ADD,0,0,0,5},{OP_RETURN,0,0,0,7}};
  Value constants[1]={Value::from_int(0)};
  DRegionPhase phase{};auto& meta=phase.program;
  meta.const_len=1;meta.n_locals=3;meta.verified_stack_bound=2;phase.binding_count=3;
  DRegionPhaseBinding bindings[3]={{RegionSlotBank::State,0,0},
      {RegionSlotBank::Prepared,0,1},{RegionSlotBank::Parameter,0,2}};
  DExecutionTables tables{};tables.phase_code=code;tables.phase_consts=constants;
  tables.region_phases=&phase;tables.region_phase_count=1;
  tables.region_bindings=bindings;tables.region_binding_count=3;
  DRegionSegment segment{};segment.parameter_count=1;segment.sequence_state=0;
  segment.parameter_caller_locals[0]=0;segment.parameter_types[0]=ValueTag::Int;
  Value caller[1]={Value::from_float(1.5)};
  DRegionFrame frame{};DPayloadTables payloads{};
  typename DPayloadFlavorTraits<Flavor>::State state{};state.private_intlists=true;
  // Metadata is supplied directly to probe dispatch guards. Host recognition
  // still requires its original strict binding counts; it is not widened here.
  const Value left[]={Value::from_int(INT64_MAX),Value::from_int(INT64_MIN),
      Value::from_int((INT64_C(1)<<53)+1),Value::from_int(-17),Value::from_bool(true),
      Value::from_float(1.5),Value::from_fallback_token(71)};
  const Value right[]={Value::from_int(1),Value::from_int(-1),
      Value::from_int(-(INT64_C(1)<<53)),Value::from_int(8),Value::from_bool(false),
      Value::from_float(2.5),Value::from_fallback_token(72)};
  const unsigned char comparisons[]={OP_LT,OP_LE,OP_GT,OP_GE};
  for(int operation=0;operation<5;++operation)for(int reverse=0;reverse<2;++reverse)
  for(int sample=0;sample<7;++sample)for(int length=3;length<=4;++length)
  for(int captures=0;captures<=1;++captures)for(int wrong_expected=0;wrong_expected<2;++wrong_expected)
  for(int initial=-1;initial<=18;++initial) {
    meta.code_len=length;meta.add_left_binding=-1;meta.add_right_binding=-1;meta.compare_binding=-1;
    frame.state[0]=left[sample];frame.prepared[0]=right[sample];
    const bool add=operation==0;
    if(add) {
      code[0].op=OP_LOAD;code[0].a=reverse?1:0;
      code[1].op=OP_LOAD;code[1].a=reverse?0:1;code[2].op=OP_ADD;
      meta.add_left_binding=reverse?1:0;meta.add_right_binding=reverse?0:1;
    } else {
      constants[0]=Value::from_int(sample==0?INT64_MAX:sample==1?INT64_MIN:-3);
      code[0].op=reverse?OP_PUSH_CONST:OP_LOAD;code[0].a=0;
      code[1].op=reverse?OP_LOAD:OP_PUSH_CONST;code[1].a=0;
      code[2].op=comparisons[operation-1];meta.compare_binding=0;
    }
    const auto expected=(add!=bool(wrong_expected))?ValueTag::Int:ValueTag::Bool;
    int ordinary_fuel=initial,bound_fuel=initial;
    state.private_bound_phases=false;
    const auto ordinary=d_region_phase<Flavor>(segment,0,expected,
        frame,caller,captures,payloads,state,tables,ordinary_fuel);
    state.private_bound_phases=true;
    const auto bound=d_region_phase<Flavor>(segment,0,expected,
        frame,caller,captures,payloads,state,tables,bound_fuel);
    if(!trivial_private_phase_test::same_result(ordinary,bound) || ordinary_fuel!=bound_fuel) {
      *failure=1+operation;return;
    }
  }
  // Enabled metadata cannot activate the shortcut outside the private profile.
  meta.code_len=4;meta.compare_binding=-1;meta.add_left_binding=0;meta.add_right_binding=1;
  code[0].op=OP_LOAD;code[0].a=0;code[1].op=OP_LOAD;code[1].a=1;code[2].op=OP_ADD;
  frame.state[0]=Value::from_bool(true);frame.prepared[0]=Value::from_int(1);
  state.private_intlists=false;int a_fuel=17,b_fuel=17;
  state.private_bound_phases=false;
  const auto a=d_region_phase<Flavor>(segment,0,ValueTag::Int,frame,caller,0,payloads,state,tables,a_fuel);
  state.private_bound_phases=true;
  const auto b=d_region_phase<Flavor>(segment,0,ValueTag::Int,frame,caller,0,payloads,state,tables,b_fuel);
  if(!trivial_private_phase_test::same_result(a,b) || a_fuel!=b_fuel)*failure=6;
}

bool test_contract() {
  using namespace gagp::gpu_detail;
  int* failure=nullptr;
  if(cudaMallocManaged(&failure,sizeof(int))!=cudaSuccess)return false;
  bool ok=true;
  *failure=-1;inspect<DPayloadFlavor::ArenaMixed><<<1,1>>>(failure);
  ok=cudaDeviceSynchronize()==cudaSuccess &&
      check(*failure==0,"private bound phase wrap/compare/fuel/lazy-capture guard "+std::to_string(*failure));
  if(ok) {
    *failure=-1;inspect<DPayloadFlavor::IndexedArena><<<1,1>>>(failure);
    ok=cudaDeviceSynchronize()==cudaSuccess &&
        check(*failure==0,"indexed private bound phase guard "+std::to_string(*failure));
  }
  cudaFree(failure);return ok;
}

} // namespace private_bound_phase_test
