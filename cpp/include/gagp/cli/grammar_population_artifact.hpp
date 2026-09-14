#pragma once

#include "gagp/cli/grammar_artifact.hpp"

namespace gagp::cli_detail {

inline constexpr const char* kGeneratedGrammarPopulationArtifactVersion = "grammar-population-v1";

// Population artifacts describe unchanged, same-version generated members.
std::string encode_generated_population_artifact(const evo::grammar::CompiledGrammar& grammar,
    const std::vector<evo::ProgramGenome>& population);

std::vector<evo::ProgramGenome> replay_generated_population_artifact(const std::string& artifact,
    const evo::grammar::CompiledGrammar* required_grammar = nullptr);

}  // namespace gagp::cli_detail
