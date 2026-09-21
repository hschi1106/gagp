#pragma once

#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/input_spec.hpp"

namespace gagp::cli_detail {

inline constexpr const char* kGeneratedGrammarArtifactVersion = "grammar-generated-v2";
inline constexpr const char* kMaterializedGrammarArtifactVersion = "grammar-materialized-v2";

std::string encode_generated_artifact(const evo::grammar::CompiledGrammar& grammar,
    const evo::grammar::GeneratedDerivation& generated);

struct MaterializedGrammarArtifact {
  evo::ProgramGenome genome;
  std::vector<evo::InputSpec> inputs;
  evo::RType return_type = evo::RType::Invalid;
  evo::grammar::GrammarLimits search_limits;
  evo::grammar::ExecutionLimits execution_limits;
};

// Retains the execution contract without requiring the recorded generator.
MaterializedGrammarArtifact decode_materialized_program(const std::string& artifact);

// Decode the package-independent output of the release-1 offline migrator.
MaterializedGrammarArtifact decode_migrated_materialized_program(
    const std::string& artifact);

// Decode the materialized prefix AST without executing a generator or loading a
// grammar package. This proves native validity, not evolutionary membership.
evo::ProgramGenome decode_materialized_artifact(const std::string& artifact);

// Same-version replay validates the complete stored materialization/provenance.
evo::grammar::GeneratedDerivation replay_generated_artifact(const std::string& artifact,
    const evo::grammar::CompiledGrammar* required_grammar = nullptr);

}  // namespace gagp::cli_detail
