#include "opcode_map_gpu.hpp"

#include "gagp/core/opcode.hpp"
#include "gagp/runtime/gpu/constants_gpu.hpp"

namespace gagp::gpu_detail {

int host_opcode(const Opcode op) {
  const int value = static_cast<int>(op);
  if ((value >= static_cast<int>(Opcode::PushConst) &&
       value <= static_cast<int>(Opcode::EmptyListLike)) ||
      value == static_cast<int>(Opcode::BoundedRegion)) {
    return value;
  }
  return -1;
}

}  // namespace gagp::gpu_detail
