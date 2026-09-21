#pragma once

#include <cstdint>

#include "gagp/core/errors.hpp"
#include "gagp/core/value.hpp"
#include "gagp/runtime/gpu/constants_gpu.hpp"
#include "gagp/runtime/gpu/payload_flavor_types.hpp"

namespace gagp::gpu_detail {

struct DInstr {
  std::uint8_t op = 0;
  std::uint8_t flags = 0;
  std::int32_t a = 0;
  std::int32_t b = 0;
  std::uint32_t fuel = 1;
};

struct DResult {
  int is_error = 0;
  ErrCode err_code = ErrCode::Value;
  Value value = Value::invalid();
};

struct DPhaseMeta {
  int code_offset = 0;
  int code_len = 0;
  int const_offset = 0;
  int const_len = 0;
  int n_locals = 0;
};

struct DProgramMeta {
  int code_offset = 0;
  int code_len = 0;
  int const_offset = 0;
  int const_len = 0;
  int n_locals = 0;
  int case_offset = 0;
  int case_count = 0;
  int case_local_offset = 0;
  int is_valid = 0;
  DPayloadFlavor payload_flavor = DPayloadFlavor::None;
  ErrCode err_code = ErrCode::Value;
  int region_offset = 0;
  int region_count = 0;
};

struct DStringPayloadEntry {
  std::int64_t packed = 0;
  int offset = 0;
  int len = 0;
};

struct DListPayloadEntry {
  ValueTag tag = ValueTag::Invalid;
  std::int64_t packed = 0;
  int offset = 0;
  int len = 0;
};

__host__ __device__ inline bool d_has_a(const DInstr& ins) { return (ins.flags & DINSTR_HAS_A) != 0; }
__host__ __device__ inline bool d_has_b(const DInstr& ins) { return (ins.flags & DINSTR_HAS_B) != 0; }

}  // namespace gagp::gpu_detail
