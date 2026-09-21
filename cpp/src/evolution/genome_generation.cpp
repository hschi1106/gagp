#include "gagp/evolution/genome_generation.hpp"

#include "gagp/evolution/grammar/generate.hpp"

namespace gagp::evo {

ProgramGenome generate_random_genome(std::uint64_t seed,
                                     const grammar::CompiledGrammar& grammar) {
  return grammar::generate_derivation(grammar, seed).genome;
}

ProgramGenome generate_random_genome(
    std::uint64_t seed,
    const grammar::CompiledGrammar& grammar,
    const grammar::GenerationRequest& request) {
  return grammar::generate_derivation(grammar, seed, request).genome;
}

}  // namespace gagp::evo
