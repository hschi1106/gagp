#pragma once
#include "gagp/evolution/repro/prep.hpp"
#include "gagp/evolution/grammar/variation_cache.hpp"

namespace gagp::evo::repro {
// Private backend continuation: the caller owns the unmodified population and
// the same context used to warm it. Public preprocessing never accepts handoffs.
PreprocessOutput preprocess_warmed_population(const std::vector<ProgramGenome>& population,
    const GpuReproConfig& config, grammar::VariationContext& context,
    std::shared_ptr<const ConstantMutationDomains> domains, bool prepare_donors,
    const std::vector<grammar::WarmPopulationMember>& handoff);
}
