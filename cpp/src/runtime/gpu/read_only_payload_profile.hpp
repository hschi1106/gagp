#pragma once
#include "gagp/core/builtin.hpp"
#include "gagp/runtime/gpu/host_pack_gpu.hpp"

namespace gagp::gpu_detail {
// Inspect the current packed root instructions, not an owned population's
// potentially cold host reference. This exact table is uploaded for execution.
inline bool read_only_payload_supported(const PackResult& packed) {
  if (!packed.region_segments.empty() || packed.metas.empty()) return false;
  for (const auto& meta : packed.metas) if (!meta.is_valid || meta.region_count) return false;
  for (const auto& ins : packed.all_code) {
    switch (ins.op) {
      case OP_PUSH_CONST: case OP_LOAD: case OP_STORE:
      case OP_NEG: case OP_NOT: case OP_ADD: case OP_SUB: case OP_MUL:
      case OP_DIV: case OP_MOD: case OP_LT: case OP_LE: case OP_GT:
      case OP_GE: case OP_EQ: case OP_NE: case OP_JMP:
      case OP_JMP_IF_FALSE: case OP_JMP_IF_TRUE: case OP_RETURN:
      case OP_CHECK_LIST: case OP_CHECK_INT: break;
      case OP_CALL_BUILTIN:
        // These implementations neither allocate nor consult thread payload.
        // Index/Find/StringToChar are deliberately excluded: None changes
        // their payload-read behavior despite their lack of allocation.
        switch (static_cast<BuiltinId>(ins.a)) {
          case BuiltinId::Abs: case BuiltinId::Min: case BuiltinId::Max:
          case BuiltinId::Clip: case BuiltinId::Len: case BuiltinId::IsInt:
          case BuiltinId::IDiv0: case BuiltinId::IMod0:
          case BuiltinId::TruncDiv0: case BuiltinId::TruncMod0: break;
          default: return false;
        }
        break;
      default: return false;
    }
  }
  return true;
}
}  // namespace gagp::gpu_detail
