#pragma once

#include <cstdint>

namespace gagp::evo::grammar {

enum class ConstantMutationPolicy : int { Resample = 0, Keep = 1, Flip = 2, Add = 3 };

struct ConstantMutationDelta {
  std::int64_t integer_minimum = 0;
  std::int64_t integer_maximum = 0;
  double float_minimum = 0;
  double float_maximum = 0;
  // Zero: continuous half-open sampling. Positive: GPU alone samples an
  // equally weighted closed grid with steps + 1 points.
  std::uint32_t gpu_grid_steps = 0;
};

}  // namespace gagp::evo::grammar
