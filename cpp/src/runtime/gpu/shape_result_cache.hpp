#pragma once

#include "gagp/core/builtin.hpp"
#include "gagp/runtime/gpu/host_pack_gpu.hpp"

namespace gagp::gpu_detail {

// Additional dependency proof AFTER complete typed-view and direct-root
// admission. Scalar result/error/fuel can depend only on root inputs and LOADed
// parameter captures. Element reads and list-key memoization remain excluded.
inline bool shape_result_program_supported(const PackResult& packed,const DProgramMeta& meta,
    std::uint64_t* required_inputs=nullptr) {
  if(required_inputs)*required_inputs=0;
  if(!meta.is_valid || meta.region_count!=1 || meta.region_offset<0 ||
      meta.region_offset>=static_cast<int>(packed.region_segments.size()))return false;
  const auto& segment=packed.region_segments[meta.region_offset];
  if(segment.result_type!=ValueTag::Int && segment.result_type!=ValueTag::Bool)return false;
  if(segment.memoized)for(unsigned i=0;i<segment.state_count;++i)
    if(segment.state_types[i]==ValueTag::IntList)return false;
  std::uint64_t inputs=0;
  const auto include=[&](int local) {
    if(local<0 || local>=MAX_LOCALS)return false;
    inputs|=std::uint64_t{1}<<local;return true;
  };
  const auto code_supported=[](const DInstr* code,int count) {
    if(count<=0)return false;
    for(int i=0;i<count;++i)if(code[i].op==OP_CALL_BUILTIN) {
      switch(static_cast<BuiltinId>(code[i].a)) {
        case BuiltinId::Len:case BuiltinId::Slice:case BuiltinId::Clip:
        case BuiltinId::TruncDiv0:case BuiltinId::TruncMod0:case BuiltinId::IDiv0:
        case BuiltinId::IMod0:case BuiltinId::Min:case BuiltinId::Max:
        case BuiltinId::IsLetter:break;
        default:return false;
      }
    }
    return true;
  };
  const auto* root=packed.all_code.data()+meta.code_offset;
  if(!code_supported(root,meta.code_len))return false;
  // Direct-root admission excludes STORE and computed caller locals. Include
  // every root LOAD even if a later phase happens to ignore that operand.
  for(int i=0;i<meta.code_len;++i)
    if(root[i].op==OP_LOAD && !include(root[i].a))return false;
  const auto phase_supported=[&](int index) {
    if(index<0 || index>=static_cast<int>(packed.region_phases.size()))return false;
    const auto& phase=packed.region_phases[index];const auto& p=phase.program;
    const auto* code=packed.all_phase_code.data()+p.code_offset;
    if(!code_supported(code,p.code_len))return false;
    for(int i=0;i<p.code_len;++i)if(code[i].op==OP_LOAD) {
      for(int j=0;j<phase.binding_count;++j) {
        const auto& binding=packed.region_bindings[phase.binding_offset+j];
        if(binding.local==code[i].a && binding.bank==RegionSlotBank::Parameter) {
          if(binding.slot>=segment.parameter_count || !include(segment.parameter_caller_locals[binding.slot]))return false;
        }
      }
    }
    return true;
  };
  if(!phase_supported(segment.base_predicate_phase) || !phase_supported(segment.base_body_phase))return false;
  if(segment.boundary_phase>=0 && !phase_supported(segment.boundary_phase))return false;
  const auto& predicate=packed.region_phases[segment.base_predicate_phase].program;
  const auto* predicate_code=packed.all_phase_code.data()+predicate.code_offset;
  bool always_base=false;
  if(predicate.code_len==2 && predicate_code[0].op==OP_PUSH_CONST && predicate_code[1].op==OP_RETURN) {
    const auto& value=packed.all_phase_consts[predicate.const_offset+predicate_code[0].a];
    always_base=value.tag==ValueTag::Bool && value.b;
  }
  // A literal-true predicate retains its own fuel but never calls these phases.
  if(!always_base) {
    if(!phase_supported(segment.combine_phase))return false;
    for(unsigned i=0;i<segment.preparation_count;++i)if(!phase_supported(segment.preparation_phases[i]))return false;
    for(unsigned i=0;i<segment.request_expression_count;++i)if(!phase_supported(segment.request_expression_phases[i]))return false;
  }
  if(required_inputs)*required_inputs=inputs;
  return true;
}

} // namespace gagp::gpu_detail
