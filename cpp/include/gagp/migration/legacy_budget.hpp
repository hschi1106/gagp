#pragma once

#include <cstddef>

#include "gagp/migration/legacy_ast_v1.hpp"
#include "gagp/evolution/grammar/resource_projection.hpp"

namespace gagp::migration {

enum class LegacyAcceptanceStage { Initialization, Variation };

struct LegacyBudgetMetrics {
  std::size_t nodes = 0;
  std::size_t physical_depth = 0;
  std::size_t expression_depth = 0;
};

struct LegacyExpansionMetrics {
  std::size_t nodes = 0;
  std::size_t physical_depth = 0;
};

struct LegacyExpansionSite {
  // Half-open target prefix span corresponding to one complete source subtree.
  std::size_t begin = 0;
  std::size_t end = 0;
  // One-based physical depth of its target root.
  std::size_t depth = 0;
};

struct LegacyExpansionLayout {
  LegacyExpansionMetrics metrics;
  // Indexed by source prefix node, not target emission order. Administrative
  // target nodes have no source entry. In particular, LinearRec emits its last
  // child before its step child.
  std::vector<LegacyExpansionSite> source_nodes;
};

struct LegacyReplacementResources {
  bool is_expression = false;
  // False if any unchanged part already exceeds a variation resource limit.
  // This is site-specific: replacing an over-depth branch may repair a parent
  // that was accepted at initialization without a metadata-depth predicate.
  bool surrounding_fits = false;
  std::size_t max_nodes = 0;
  std::size_t max_expression_depth = 0;

  // Donor metrics describe the expression alone, without a program envelope.
  // Type/scope/grammar compatibility and donor validity are not checked here.
  bool accepts(std::size_t donor_nodes, std::size_t donor_expression_depth) const;
};

// Source-indexed exact resource allowances for replacing one expression
// subtree. Does not make every expression an eligible legacy variation site.
std::vector<LegacyReplacementResources> legacy_replacement_resources(
    const legacy_v1::AstProgram& program, std::size_t max_nodes,
    std::size_t max_expression_depth, const std::vector<evo::InputSpec>& inputs = {});

// Independent structural witness for associating source resource accounting
// with lowered subtrees. Does not certify that a site is eligible for variation:
// legacy type/scope/binder/phase restrictions remain separate obligations.
LegacyExpansionLayout predict_legacy_expansion_layout(const legacy_v1::AstProgram& program,
    const std::vector<evo::InputSpec>& inputs = {});

// Offline projection certificate: source roots carry one node; expression roots
// carry one depth level, while source structural nodes reset expression depth.
// Administrative target nodes carry neither cost nor reset. Independently check
// the layout against the actual lowering before applying these charges to it.
std::vector<evo::grammar::ResourceCharge> legacy_expansion_resource_charges(
    const legacy_v1::AstProgram& program, const std::vector<evo::InputSpec>& inputs = {});

// Predicts the exact physical shape produced by lower_bounded_regions from its
// rewrite rules, without lowering or using observed population maxima. This
// describes one source tree, not a search-space-wide allowance.
LegacyExpansionMetrics predict_legacy_expansion(const legacy_v1::AstProgram& program,
    const std::vector<evo::InputSpec>& inputs = {});

// Conservative ceiling for every source tree with at most max_source_nodes.
// Maximizes over rewrite shapes without type/scope/grammar/depth restrictions;
// those restrictions must still be enforced independently on source derivations.
// O(max_source_nodes^2) time and O(max_source_nodes) memory. No observed members
// or execution-fuel changes participate in this calculation.
LegacyExpansionMetrics legacy_expansion_ceiling(std::size_t max_source_nodes);

// Offline source-side metrics. Expression depth excludes statement and block
// ancestry; physical depth includes every prefix node. Invalid ASTs and
// intermediate lowering forms outside the frozen source node domain throw.
LegacyBudgetMetrics legacy_budget_metrics(const legacy_v1::AstProgram& program,
    const std::vector<evo::InputSpec>& inputs = {});

// Reproduces only the old final resource acceptance predicates. In particular,
// initialization's recursive generator depth is NOT a metadata-depth check.
// Passing this function does not prove grammar membership, generation provenance,
// semantic equivalence, or that a target physical budget preserves search space.
bool legacy_accepts_resources(const LegacyBudgetMetrics& metrics,
    std::size_t max_nodes, std::size_t max_expression_depth,
    LegacyAcceptanceStage stage);

}  // namespace gagp::migration
