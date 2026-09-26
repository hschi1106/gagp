#pragma once

#include "gagp/evolution/grammar/compiled.hpp"

namespace gagp::evo::grammar {

struct GenerationRequest {
  std::uint32_t nonterminal = kNoGrammarId;
  RType type = RType::Invalid;
  // Ordered lexical bindings; global input/local declarations remain in grammar.
  std::vector<RegionBinding> visible_environment;
  // Complete native-program allowance, including an expression's envelope.
  GrammarLimits budget;
  // Construction policy only; membership and crossover use the union grammar.
  GenerationStage stage = GenerationStage::Initial;
};

// A mixed population remains a finite set of exact, closed root contracts.
// Each result type has one root; all roots share physical resource limits.
void validate_population_requests(const CompiledGrammar& grammar,
    const std::vector<GenerationRequest>& requests);
// Resolve ordered user-facing root IDs with the grammar's common search limits.
std::vector<GenerationRequest> named_population_requests(const CompiledGrammar& grammar,
    const std::vector<std::string>& root_names);

GenerationRequest entry_request(const CompiledGrammar& grammar);
// Checks exact type, visible scope, category and budgets; returns required-scope
// indexes into the supplied environment, preserving its order and identities.
std::vector<std::uint32_t> validate_request(const CompiledGrammar& grammar,
    const GenerationRequest& request);

}  // namespace gagp::evo::grammar
