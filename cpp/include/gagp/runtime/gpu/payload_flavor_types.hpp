#pragma once

#include <cstdint>

namespace gagp::gpu_detail {

enum class DPayloadFlavor : std::uint8_t {
  None = 0,
  StringOnly = 1,
  ListOnly = 2,
  Mixed = 3,
  IntListViews = 4,
  BoundIntListViews = 5,
};

}  // namespace gagp::gpu_detail
