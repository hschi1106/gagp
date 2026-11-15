#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "gagp/evolution/case_set.hpp"
#include "gagp/evolution/genome.hpp"

namespace gagp::evo {

struct EvolutionConfig;
namespace grammar { class CompiledGrammar; struct GenerationRequest; }

void validate_grammar_case_set(const grammar::CompiledGrammar& grammar,
    const CaseSet& case_set, const grammar::GenerationRequest& request);

// Rejects a configuration without a compiled grammar, with a fuel above the
// grammar's execution fuel, a nonpositive population or a mutation
// probability outside [0,1].
void validate_evolution_config(const EvolutionConfig& config);

// Checks the case set against the grammar entry.
void validate_grammar_case_set(const EvolutionConfig& config, const CaseSet& case_set);

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

// Returns an empty string for an admissible generated member, otherwise the
// capacity it exceeds.
using MemberCapacityCheck = std::function<std::string(const ProgramGenome&)>;

// Generates member i from seed + i, or replays a saved population. With a
// capacity check, a generated member over a capacity is redrawn from a seed
// derived from (seed + i, attempt). A member admissible at its first draw
// consumes nothing more, so such runs are unchanged. After
// kMemberCapacityAttempts draws of one member the initialization fails,
// naming the capacity. Replayed members are never redrawn.
constexpr int kMemberCapacityAttempts = 256;
PopulationInitialization initialize_population(
    const EvolutionConfig& config,
    const CaseSet& case_set,
    const std::vector<ProgramGenome>* replay_population = nullptr,
    const MemberCapacityCheck& capacity = {});

}  // namespace gagp::evo
