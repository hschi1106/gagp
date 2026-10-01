#pragma once

#include <functional>
#include "gagp/evolution/grammar/variation_contract.hpp"

namespace gagp::evo::grammar::variation_detail {
// Internal selected-site result, deliberately distinct from complete public
// VariationAnalysis. All occurrences of each selected logical group are kept.
struct SelectedVariationAnalysis {
  DerivationMetadata witness;
  VerifiedAst verified;
  std::vector<VariationSite> sites;
  std::vector<std::size_t> original_indices;
  std::size_t total_sites = 0;
};
using SiteSelector = std::function<std::vector<std::size_t>(std::size_t)>;
SelectedVariationAnalysis analyze_selected_variation(const CompiledGrammar& grammar,
    const ProgramGenome& genome, const std::vector<GenerationRequest>& requests,
    const SiteSelector& select, CompatibilityRegistry* registry = nullptr,
    const ProjectedBudget* local_projected_budget = nullptr);
}  // namespace gagp::evo::grammar::variation_detail
