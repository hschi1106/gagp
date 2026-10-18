#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "gagp/evolution/selection.hpp"
#include "gagp/evolution/timing.hpp"

namespace gagp::evo::detail {

// Reconcile an explicitly selected subset. The caller then re-ranks the entire
// population; candidates outside this subset retain their original GPU score.
inline void reconcile_reference_fitness(const std::vector<std::size_t>& indices,
    const std::vector<double>& reference, const std::vector<unsigned>& reference_wrong,
    std::vector<double>& fitness, std::vector<unsigned>& wrong, EvaluationTiming& stats,
    const std::vector<double>& raw_gpu = {}) {
  if (indices.size() != reference.size() || indices.size() != reference_wrong.size() ||
      fitness.size() != wrong.size() || (!raw_gpu.empty() && raw_gpu.size() != fitness.size()))
    throw std::invalid_argument("reference fitness vector size mismatch");
  for (std::size_t j = 0; j < indices.size(); ++j) {
    const auto i = indices[j];
    const auto exact = canonicalize_fitness_for_ranking(reference[j]);
    if (!std::isfinite(exact)) throw std::runtime_error("CPU reference produced non-finite fitness");
    stats.verification_max_abs_error = std::max(stats.verification_max_abs_error,
        std::abs((raw_gpu.empty() ? fitness.at(i) : raw_gpu.at(i)) - reference[j]));
    stats.verification_fitness_corrections += fitness.at(i) != exact;
    stats.verification_wrong_corrections += wrong.at(i) != reference_wrong[j];
    fitness.at(i) = exact;
    wrong.at(i) = reference_wrong[j];
  }
}

}  // namespace gagp::evo::detail
