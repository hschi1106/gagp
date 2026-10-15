#pragma once

namespace private_typed_phase_test {

struct Observation {
  gagp::gpu_detail::DResult result;int fuel,used;unsigned long long charged;
  bool overflow;unsigned reason;int size=-1;std::int64_t first=0,last=0;
};

template<gagp::gpu_detail::DPayloadFlavor Flavor>
__global__ void inspect(Value* cells,std::int64_t* integers,int compact,int* failure) {
  using namespace gagp::gpu_detail;
  *failure=0;
  Value globals[4]={Value::from_int(INT64_MIN),Value::from_int((INT64_C(1)<<53)+1),
      Value::from_int(-19),Value::from_int(INT64_MAX)};
  DPayloadTables payloads{};payloads.list_values=globals;payloads.list_value_count=4;
  Value constants[5]={Value::from_int(INT64_MAX),Value::from_int(1),Value::from_int(0),
      Value::from_int(-1),Value::from_bool(true)};
  DInstr code[32];DRegionPhase phase{};auto& meta=phase.program;
  meta.const_len=5;meta.n_locals=2;meta.verified_stack_bound=4;phase.binding_count=2;
  DRegionPhaseBinding bindings[2]={{RegionSlotBank::State,0,0},{RegionSlotBank::Parameter,0,1}};
  DRegionSegment segment{};segment.sequence_state=0;segment.parameter_count=1;
  segment.parameter_types[0]=ValueTag::Int;segment.parameter_caller_locals[0]=0;
  DExecutionTables tables{};tables.phase_code=code;tables.phase_consts=constants;
  tables.region_phases=&phase;tables.region_phase_count=1;
  tables.region_bindings=bindings;tables.region_binding_count=2;
  DRegionFrame frame{};Value caller[1];
  for(int recipe=0;recipe<18;++recipe)for(int captures=0;captures<3;++captures)
  for(int capacity=0;capacity<3;++capacity)for(int shape=0;shape<3;++shape)
  for(int initial=-1;initial<=38;++initial) {
    int count=0;
    const auto emit=[&](unsigned char op,int a=0,int b=0) {
      code[count++]={op,static_cast<unsigned char>(DINSTR_HAS_A|DINSTR_HAS_B),a,b,2};
    };
    const auto builtin=[&](BuiltinId bid,int argc){emit(OP_CALL_BUILTIN,int(bid),argc);};
    auto expected=ValueTag::Int;
    const bool escaped=recipe>=6 && recipe<=9;
    frame.state[0]=d_private_intlist(0,escaped?65537:4,!escaped);
    caller[0]=captures==2?Value::from_float(1.5):Value::from_int(-7);
    meta.n_locals=shape==1?9:2;meta.verified_stack_bound=shape==2?17:4;
    if(recipe<=2) {
      emit(OP_LOAD,0);emit(OP_PUSH_CONST,3);
      builtin(recipe==0?BuiltinId::Index:recipe==1?BuiltinId::IndexNonnegative:BuiltinId::IndexClampedOrZero,2);
    } else if(recipe==3) {
      emit(OP_LOAD,0);emit(OP_PUSH_CONST,2);emit(OP_PUSH_CONST,1);builtin(BuiltinId::Slice,3);
      emit(OP_PUSH_CONST,2);emit(OP_PUSH_CONST,1);builtin(BuiltinId::Slice,3);expected=ValueTag::IntList;
    } else if(recipe==4 || recipe==5) {
      emit(OP_PUSH_CONST,0);builtin(BuiltinId::Singleton,1);
      if(recipe==5) {
        emit(OP_PUSH_CONST,1);builtin(BuiltinId::Singleton,1);builtin(BuiltinId::Concat,2);
        emit(OP_PUSH_CONST,2);emit(OP_PUSH_CONST,1);builtin(BuiltinId::Slice,3);
        emit(OP_PUSH_CONST,3);builtin(BuiltinId::Singleton,1);builtin(BuiltinId::Concat,2);
      }
      expected=ValueTag::IntList;
    } else if(escaped) {
      emit(OP_LOAD,0);
      if(recipe==7)builtin(BuiltinId::Len,1);
      else if(recipe==8){emit(OP_PUSH_CONST,2);builtin(BuiltinId::Index,2);}
      else if(recipe==9){emit(OP_PUSH_CONST,2);emit(OP_PUSH_CONST,1);builtin(BuiltinId::Slice,3);expected=ValueTag::IntList;}
      else expected=ValueTag::IntList;
    } else if(recipe==10) {
      emit(OP_PUSH_CONST,0);emit(OP_PUSH_CONST,1);emit(OP_ADD);emit(OP_NEG);
      emit(OP_PUSH_CONST,3);builtin(BuiltinId::TruncMod0,2);
    } else if(recipe==11) {
      emit(OP_PUSH_CONST,4);emit(OP_NOT);emit(OP_JMP_IF_TRUE,5);
      emit(OP_PUSH_CONST,0);emit(OP_JMP,6);emit(OP_PUSH_CONST,3);
    } else if(recipe==12)emit(OP_LOAD,1);
    else if(recipe==13)emit(OP_PUSH_CONST,0); // The declared capture is unused.
    else if(recipe==14) { // Store clears a present parameter's lazy type error.
      emit(OP_PUSH_CONST,0);emit(OP_STORE,1);emit(OP_LOAD,1);
    } else if(recipe==15) {
      emit(OP_PUSH_CONST,0);emit(OP_PUSH_CONST,1);emit(OP_CALL_BUILTIN,0x7fffffff,2);
    } else if(recipe==16)emit(255);
    else {
      emit(OP_LOAD,0);emit(OP_CHECK_LIST);builtin(BuiltinId::Len,1);emit(OP_CHECK_INT);
    }
    emit(OP_RETURN);meta.code_len=count;
    Observation seen[2];
    for(int typed=0;typed<2;++typed) {
      typename DPayloadFlavorTraits<Flavor>::State state{};
      state.private_intlists=true;state.private_typed_phases=typed;state.compact_intlist_cells=compact;
      state.list_values=compact?nullptr:cells;state.intlist_cells=compact?integers:nullptr;
      state.kMaxListValues=capacity==2?0:65538;state.logical_list_limit=capacity==1?1:65536;
      state.list_values_used=escaped?65537:0;
      cells[0]=Value::from_int((INT64_C(1)<<53)+3);integers[0]=cells[0].i;
      int remaining=initial;
      const auto result=d_region_phase<Flavor>(segment,0,expected,frame,caller,captures?1:0,
          payloads,state,tables,remaining);
      auto& snapshot=seen[typed];snapshot.result=result;snapshot.fuel=remaining;
      snapshot.used=state.list_values_used;snapshot.charged=state.logical_list_used;
      snapshot.overflow=state.physical_capacity_exceeded;snapshot.reason=state.physical_capacity_reason;
      if(!result.is_error && result.value.tag==ValueTag::IntList) {
        DPrivateIntListSpan span;
        if(!d_private_intlist_span(payloads,state,result.value,span)){*failure=100+recipe;return;}
        snapshot.size=span.size;
        if(span.size){snapshot.first=span.at(0);snapshot.last=span.at(span.size-1);}
      }
    }
    const auto& a=seen[0];const auto& b=seen[1];
    if(!trivial_private_phase_test::same_result(a.result,b.result) || a.fuel!=b.fuel ||
        a.used!=b.used || a.charged!=b.charged || a.overflow!=b.overflow || a.reason!=b.reason ||
        a.size!=b.size || a.first!=b.first || a.last!=b.last){*failure=1+recipe;return;}
  }
}

bool test_contract() {
  using namespace gagp::gpu_detail;
  Value* cells=nullptr;std::int64_t* integers=nullptr;int* failure=nullptr;
  if(cudaMallocManaged(&cells,65538*sizeof(Value))!=cudaSuccess)return false;
  if(cudaMallocManaged(&integers,65538*sizeof(std::int64_t))!=cudaSuccess){cudaFree(cells);return false;}
  if(cudaMallocManaged(&failure,sizeof(int))!=cudaSuccess){cudaFree(cells);cudaFree(integers);return false;}
  // Initialize the escaped backing as well as the small allocation scratch.
  for(int i=0;i<65538;++i){cells[i]=Value::from_int(-i);integers[i]=-i;}
  bool ok=true;
  for(int compact=0;compact<2 && ok;++compact) {
    *failure=-1;inspect<DPayloadFlavor::ArenaMixed><<<1,1>>>(cells,integers,compact,failure);
    ok=cudaDeviceSynchronize()==cudaSuccess &&
        check(*failure==0,"private typed phase full-width/list/fuel/lazy-capture guard "+std::to_string(*failure));
    if(ok) {
      *failure=-1;inspect<DPayloadFlavor::IndexedArena><<<1,1>>>(cells,integers,compact,failure);
      ok=cudaDeviceSynchronize()==cudaSuccess &&
          check(*failure==0,"indexed private typed phase guard "+std::to_string(*failure));
    }
  }
  cudaFree(failure);cudaFree(integers);cudaFree(cells);return ok;
}

} // namespace private_typed_phase_test
