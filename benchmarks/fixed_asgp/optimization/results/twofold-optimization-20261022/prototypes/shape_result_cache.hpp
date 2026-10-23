#pragma once

#include "gagp/core/builtin.hpp"
#include "gagp/runtime/gpu/host_pack_gpu.hpp"

namespace gagp::gpu_detail {

// Additional syntactic proof AFTER complete typed-view admission. With no
// element read or container-identity comparison, a scalar result/error depends
// on scalar bindings, list lengths, immutable constants and the original fuel.
// Reject list-key memoization because identity-dependent hits could change fuel.
inline bool shape_result_program_supported(const PackResult& packed,const DProgramMeta& meta) {
  if(!meta.is_valid || meta.region_count!=1 || meta.region_offset<0 ||
      meta.region_offset>=static_cast<int>(packed.region_segments.size()))return false;
  const auto& segment=packed.region_segments[meta.region_offset];
  if(segment.result_type!=ValueTag::Int && segment.result_type!=ValueTag::Bool)return false;
  if(segment.memoized)for(unsigned i=0;i<segment.state_count;++i)
    if(segment.state_types[i]==ValueTag::IntList)return false;
  const auto code_supported=[](const DInstr* code,int count) {
    if(count<=0)return false;
    for(int i=0;i<count;++i)if(code[i].op==OP_CALL_BUILTIN) {
      // Closed list of shape-preserving primitives; typed proof supplies arity,
      // stack types and control flow. Payload-producing/observing ops decline.
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
  if(!code_supported(packed.all_code.data()+meta.code_offset,meta.code_len))return false;
  const auto phase_supported=[&](int index) {
    if(index<0 || index>=static_cast<int>(packed.region_phases.size()))return false;
    const auto& p=packed.region_phases[index].program;
    return code_supported(packed.all_phase_code.data()+p.code_offset,p.code_len);
  };
  if(!phase_supported(segment.base_predicate_phase) || !phase_supported(segment.base_body_phase) ||
      !phase_supported(segment.combine_phase))return false;
  if(segment.boundary_phase>=0 && !phase_supported(segment.boundary_phase))return false;
  for(unsigned i=0;i<segment.preparation_count;++i)if(!phase_supported(segment.preparation_phases[i]))return false;
  for(unsigned i=0;i<segment.request_expression_count;++i)if(!phase_supported(segment.request_expression_phases[i]))return false;
  return true;
}

} // namespace gagp::gpu_detail
