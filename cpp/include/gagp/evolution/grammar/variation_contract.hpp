#pragma once

#include <map>

#include "gagp/evolution/grammar/membership.hpp"

namespace gagp::evo::grammar {

struct VariationSpan {
  std::uint32_t begin = 0;
  std::uint32_t end = 0;
};

struct VariationSite {
  std::uint32_t nonterminal = kNoGrammarId;
  RType type = RType::Invalid;
  NodeCategory category = NodeCategory::Expression;
  std::uint32_t context = kNoGrammarId;
  std::uint32_t template_id = kNoGrammarId;
  std::uint32_t slot = kNoGrammarId;
  std::string crossover_group;
  bool crossover_closed = false;
  // All occurrences of this logical choice must be replaced atomically.
  std::vector<VariationSpan> occurrences;
  // Physical IDs in ordered formal scope, aligned with occurrences. IDs are
  // remapped during copying and are deliberately absent from compatibility keys.
  std::vector<std::vector<int>> occurrence_binder_ids;
  // Subtree allowance, without an expression's standalone four-node envelope.
  GrammarLimits replacement_budget;
  std::uint32_t remaining_template_nesting = 0;
  std::uint32_t materialized_nodes = 0;
  std::uint32_t materialized_depth = 0;
  std::uint32_t template_nesting = 0;
  // Per-occurrence cost from the reconstructed membership witness.
  ProjectedResources projected_resources;
  // Only populated when the grammar certifies context-independent charges.
  bool has_projected_allowance = false;
  ProjectedAllowance projected_allowance;
  std::vector<RegionBinding> visible_environment;
  std::vector<RegionBinding> available_locals;
  std::vector<RegionBinding> free_locals;
  // Exact equality key; numeric IDs are comparable only in the same registry.
  std::string compatibility_key;
  std::uint32_t compatibility_id = kNoGrammarId;
};

class CompatibilityRegistry {
 public:
  std::uint32_t intern(const std::string& key);
  const std::vector<std::string>& keys() const { return keys_; }
 private:
  std::map<std::string, std::uint32_t> ids_;
  std::vector<std::string> keys_;
};

struct VariationAnalysis {
  DerivationMetadata witness;
  VerifiedAst verified;
  std::vector<VariationSite> sites;
};

VariationAnalysis analyze_variation(const CompiledGrammar& grammar, const ProgramGenome& genome,
    const GenerationRequest& request, CompatibilityRegistry* registry = nullptr,
    const ProjectedBudget* local_projected_budget = nullptr);
VariationAnalysis analyze_variation(const CompiledGrammar& grammar, const ProgramGenome& genome,
    CompatibilityRegistry* registry = nullptr);

VariationAnalysis analyze_population_variation(const CompiledGrammar& grammar,
    const ProgramGenome& genome, const std::vector<GenerationRequest>& requests,
    CompatibilityRegistry* registry = nullptr, const ProjectedBudget* local_projected_budget = nullptr);

// Equality of contracts and fit of the donor are separate predicates.
bool compatible_sites(const VariationSite& left, const VariationSite& right);
// True only when every lexical reference is declared inside the payload.
bool lexically_closed(const AstProgram& ast, VariationSpan payload);
bool donor_fits(const VariationSite& destination, std::uint32_t nodes,
    std::uint32_t depth, std::uint32_t template_nesting);
bool donor_fits(const VariationSite& destination, const VariationSite& donor);
GenerationRequest donor_request(const VariationSite& site);

}  // namespace gagp::evo::grammar
