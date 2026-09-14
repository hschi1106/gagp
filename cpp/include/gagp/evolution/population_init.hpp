#pragma once

#include <cstdint>
#include <vector>

#include "gagp/evolution/case_set.hpp"
#include "gagp/evolution/genome.hpp"

namespace gagp::evo {

struct EvolutionConfig;
namespace grammar { class CompiledGrammar; struct GenerationRequest; }

struct PopulationInitialization {
  std::vector<ProgramGenome> population;
  bool replayed = false;
};

PopulationInitialization initialize_population(
    const grammar::CompiledGrammar& grammar,
    const CaseSet& case_set,
    int population_size,
    std::uint64_t seed);

PopulationInitialization initialize_population(
    const grammar::CompiledGrammar& grammar,
    const CaseSet& case_set,
    int population_size,
    std::uint64_t seed,
    const grammar::GenerationRequest& request);

PopulationInitialization initialize_population(
    const EvolutionConfig& config,
    const CaseSet& case_set,
    const std::vector<ProgramGenome>* replay_population = nullptr);

}  // namespace gagp::evo
