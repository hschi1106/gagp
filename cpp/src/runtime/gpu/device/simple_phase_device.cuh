#pragma once

// Included inside gpu_detail. Fresh host metadata proves a straight-line phase
// with one operation and up to three literal/bound leaves. Private type-flow
// and complete global normalization are still required at dispatch.
template<DPayloadFlavor Flavor,class Frame>
__device__ DResult d_run_simple_phase(const DRegionSegment& segment,
    const DRegionPhase& phase,ValueTag expected,const Frame& frame,
    const Value* caller_locals,std::uint64_t caller_set,
    const DPayloadTables& payload_tables,
    typename DPayloadFlavorTraits<Flavor>::State& payload_state,
    const DExecutionTables& tables,int& fuel,bool result_phase) {
  const auto& p=phase.program;
  const auto* code=tables.phase_code+p.code_offset;
  const auto spend=[&](unsigned amount) {
    if(fuel<0 || amount>static_cast<unsigned>(fuel))return false;
    fuel-=static_cast<int>(amount);return true;
  };
  Value args[3];int pc=0;
  for(int leaf=0;leaf<p.simple_arity;++leaf) {
    const auto ins=code[pc++];
    if(!spend(ins.fuel))return d_error(ErrCode::Timeout);
    Value value;
    if(ins.op==OP_PUSH_CONST)value=tables.phase_consts[p.const_offset+ins.a];
    else {
      int selected=-1;
      for(int i=0;i<phase.binding_count;++i)
        if(tables.region_bindings[phase.binding_offset+i].local==ins.a)selected=phase.binding_offset+i;
      if(selected<0)return d_error(ErrCode::Name);
      const auto& binding=tables.region_bindings[selected];
      switch(binding.bank) {
        case RegionSlotBank::State:value=frame.state[binding.slot];break;
        case RegionSlotBank::Prepared:value=frame.prepared[binding.slot];break;
        case RegionSlotBank::Result:value=frame.results[binding.slot];break;
        case RegionSlotBank::Measure:value=Value::from_int(Value::container_len(frame.state[segment.sequence_state]));break;
        case RegionSlotBank::Parameter: {
          const int local=segment.parameter_caller_locals[binding.slot];
          if(!(caller_set & (std::uint64_t{1}<<local)))return d_error(ErrCode::Name);
          value=caller_locals[local];
          if(segment.parameter_types[binding.slot]!=ValueTag::Invalid &&
              value.tag!=segment.parameter_types[binding.slot])return d_error(ErrCode::Type);
          break;
        }
      }
    }
    if(pc<p.simple_operation_ip && (code[pc].op==OP_CHECK_INT || code[pc].op==OP_CHECK_LIST)) {
      const auto check=code[pc++];
      if(!spend(check.fuel))return d_error(ErrCode::Timeout);
      if((check.op==OP_CHECK_INT && value.tag!=ValueTag::Int) ||
          (check.op==OP_CHECK_LIST && value.tag!=ValueTag::IntList))return d_error(ErrCode::Type);
    }
    args[leaf]=value;
  }
  const auto operation=code[p.simple_operation_ip];
  if(!spend(operation.fuel))return d_error(ErrCode::Timeout);
  Value value;
  if(operation.op==OP_CALL_BUILTIN) {
    ErrCode error=ErrCode::Value;
    if(!d_builtin_call<Flavor>(static_cast<BuiltinId>(operation.a),args,p.simple_arity,
        payload_tables,payload_state,value,error))return d_error(error);
  } else {
    const auto a=args[0].tag==ValueTag::Bool?static_cast<std::int64_t>(args[0].b):args[0].i;
    const auto b=p.simple_arity==2?(args[1].tag==ValueTag::Bool?static_cast<std::int64_t>(args[1].b):args[1].i):0;
    switch(operation.op) {
      case OP_NEG:value=Value::from_int(vm_semantics::wrap_int_neg(a));break;
      case OP_NOT:value=Value::from_bool(!a);break;
      case OP_ADD:value=Value::from_int(vm_semantics::wrap_int_add(a,b));break;
      case OP_SUB:value=Value::from_int(vm_semantics::wrap_int_sub(a,b));break;
      case OP_MUL:value=Value::from_int(vm_semantics::wrap_int_mul(a,b));break;
      case OP_MOD:if(!b)return d_error(ErrCode::ZeroDiv);value=Value::from_int(d_py_int_mod(a,b));break;
      case OP_EQ:value=Value::from_bool(a==b);break;
      case OP_NE:value=Value::from_bool(a!=b);break;
      case OP_LT:value=Value::from_bool(a<b);break;
      case OP_LE:value=Value::from_bool(a<=b);break;
      case OP_GT:value=Value::from_bool(a>b);break;
      case OP_GE:value=Value::from_bool(a>=b);break;
      default:return d_error(ErrCode::Type);
    }
  }
  if(p.simple_operation_ip+1<p.code_len && !spend(code[p.simple_operation_ip+1].fuel))
    return d_error(ErrCode::Timeout);
  if(value.tag!=expected && !(result_phase && value.tag==ValueTag::FallbackToken))return d_error(ErrCode::Type);
  return d_ok(value);
}
