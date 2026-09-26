#pragma once

#include <cstddef>
#include <vector>

#include "gagp/evolution/grammar/request.hpp"

namespace gagp::evo::grammar {

struct JointResourceCost {
  std::uint32_t physical_nodes = 0;
  ProjectedResources projected;
};

// Nondominated structural construction costs within BOTH request physical limits
// and projected limits. Each row represents one joint derivation, never a mix of
// independent minima. Stage and shared template-hole choices are respected.
// Does not replace native validity, membership or contextual-frame admission.
// Capacity exhaustion throws; feasible states are never silently truncated.
std::vector<JointResourceCost> joint_resource_frontier(const CompiledGrammar& grammar,
    const GenerationRequest& request, ProjectedBudget projected_budget,
    std::size_t maximum_states = 1'000'000);

}  // namespace gagp::evo::grammar
