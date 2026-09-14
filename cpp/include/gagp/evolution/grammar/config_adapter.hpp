#pragma once

#include "gagp/evolution/grammar/compiled.hpp"
#include "gagp/evolution/grammar_config.hpp"
#include "gagp/evolution/input_spec.hpp"

namespace gagp::evo::grammar {

inline constexpr const char* kGrammarConfigConversionVersion = "grammar-config-typed-v1";

// Explicit bounded conversion policy, independent of legacy generation limits.
// Every enabled type requires one owned constant domain. Declared locals are
// initialized from those domains before the generated body executes.
struct GrammarConfigConversion {
  RType return_type = RType::Invalid;
  std::vector<InputSpec> inputs;
  std::vector<RegionBinding> locals;
  std::vector<ConstantDomain> constants;
  GrammarLimits search_limits;
  ExecutionLimits execution_limits;
  std::uint32_t max_statements_per_block = 6;
  std::int64_t max_for_k = 16;
};

ResolvedDefinition convert_grammar_config(const GrammarConfig& config,
    const GrammarConfigConversion& options);

}  // namespace gagp::evo::grammar
