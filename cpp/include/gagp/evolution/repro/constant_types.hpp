#pragma once

#include <cstdint>
#include "gagp/evolution/grammar/constant_policy.hpp"

namespace gagp::evo::repro {

inline constexpr int kNoConstantMutationGroup = -1;

// Device-neutral rows. These types intentionally contain no owning state or
// host pointers so the same layout can be copied by a later GPU integration.
struct ConstantMutationDomain {
  int type = -1;
  int value_offset = 0;
  int value_count = 0;
  int integer_range = 0;
  std::int64_t minimum = 0;
  std::int64_t maximum = 0;
  int float_range = 0;
  double float_minimum = 0;
  double float_maximum = 0;
  double float_quantization_scale = 0;
  grammar::ConstantMutationPolicy mutation = grammar::ConstantMutationPolicy::Resample;
  grammar::ConstantMutationDelta delta;
};

struct ConstantMutationGroup {
  std::uint32_t logical_instance = UINT32_MAX;
  int domain = -1;
  int node_offset = 0;
  int node_count = 0;
};

struct ConstantMutationStream {
  int node_origin_offset = 0;
  int node_count = 0;
  int group_offset = 0;
  int group_count = 0;
  int metadata_root_offset = 0;
  int metadata_root_count = 0;
};

}  // namespace gagp::evo::repro
