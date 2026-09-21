#pragma once

#include <cstdint>

#include "gagp/evolution/genome.hpp"

namespace gagp::evo {

namespace grammar { class VariationContext; }

ProgramGenome mutate(const ProgramGenome& genome, std::uint64_t seed,
    grammar::VariationContext& context, double mutation_subtree_prob = 0.8);

}  // namespace gagp::evo
