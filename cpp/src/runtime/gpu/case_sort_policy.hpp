#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include "gagp/core/fitness_policy.hpp"

namespace gagp::gpu_detail {
// Levenshtein distances and failures have integral capped scores independently
// of the program's output tag. Bound every partial sum below exact integer range.
inline bool arena_case_sort_is_exact(FitnessPolicy policy,double penalty,std::size_t cases) {
  const double ceiling=std::fabs(penalty);
  return policy.metric==FitnessMetric::Levenshtein && policy.proximity==FitnessProximity::Capped &&
      std::isfinite(ceiling) && std::floor(ceiling)==ceiling &&
      cases*std::max(1.0,ceiling)<1099511627776.0;
}
} // namespace gagp::gpu_detail
