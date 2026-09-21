#pragma once

#include <cstdint>
#include <utility>

#include "gagp/evolution/genome.hpp"

namespace gagp::evo {

namespace grammar { class VariationContext; }

// Compiled-grammar typed_subtree crossover; every returned child is certified.
std::pair<ProgramGenome, ProgramGenome> crossover(const ProgramGenome& parent_a,
    const ProgramGenome& parent_b, std::uint64_t seed, grammar::VariationContext& context);

}  // namespace gagp::evo
