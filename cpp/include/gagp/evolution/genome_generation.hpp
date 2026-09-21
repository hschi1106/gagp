#pragma once

#include <cstdint>
#include "gagp/evolution/genome.hpp"

namespace gagp::evo {

namespace grammar { class CompiledGrammar; struct GenerationRequest; }

ProgramGenome generate_random_genome(std::uint64_t seed, const grammar::CompiledGrammar& grammar);
ProgramGenome generate_random_genome(std::uint64_t seed, const grammar::CompiledGrammar& grammar,
    const grammar::GenerationRequest& request);

}  // namespace gagp::evo
