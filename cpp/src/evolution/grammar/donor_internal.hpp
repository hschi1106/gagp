#pragma once

#include "gagp/evolution/grammar/donor.hpp"
#include "gagp/evolution/grammar/variation_cache.hpp"

namespace gagp::evo::grammar {

// Private preparation continuation. Each certificate was produced by this
// context for the unchanged, owned destination at the corresponding job index.
// Public donor APIs never accept caller-supplied analysis handoffs.
std::optional<std::vector<DonorPool>> try_generate_warmed_donor_pools(
    VariationContext& context, const std::vector<DonorPoolJob>& jobs,
    const std::vector<WarmPopulationMember>& certificates,
    std::size_t maximum_attempts = 64);

}  // namespace gagp::evo::grammar
