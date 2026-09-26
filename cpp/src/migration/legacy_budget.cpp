#include "gagp/migration/legacy_budget.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <vector>

namespace gagp::migration {
std::vector<evo::grammar::ResourceCharge> legacy_expansion_resource_charges(
    const legacy_v1::AstProgram& program, const std::vector<evo::InputSpec>& inputs) {
  const auto layout = predict_legacy_expansion_layout(program, inputs);
  std::vector<evo::grammar::ResourceCharge> charges(layout.metrics.nodes, {0, 0, false});
  for (std::size_t i = 0; i < program.nodes.size(); ++i) {
    const bool expression = static_cast<int>(program.nodes[i].kind) >=
        static_cast<int>(legacy_v1::NodeKind::CONST);
    auto& charge = charges.at(layout.source_nodes[i].begin);
    if (charge.nodes) throw std::logic_error("source resource projection roots overlap");
    charge = {1, expression ? 1U : 0U, !expression};
  }
  return charges;
}

namespace {
std::size_t add(std::size_t a, std::size_t b) {
  if (b > std::numeric_limits<std::size_t>::max() - a)
    throw std::overflow_error("legacy expansion resource overflow");
  return a + b;
}

struct ExpansionRule {
  LegacyExpansionMetrics fixed;
  std::vector<std::size_t> offsets;
  // Cumulative fixed skeleton nodes preceding each source child, excluding
  // all recursively copied children. Entries follow source argument order.
  std::vector<std::size_t> preceding;
  std::vector<std::size_t> emission_order;
};

ExpansionRule expansion_rule(legacy_v1::NodeKind kind, std::size_t arity) {
  using K = legacy_v1::NodeKind;
  switch (kind) {
    case K::MAP_LIST: return {{8, 4}, {2, 2}, {2, 5}, {0, 1}};
    case K::FILTER_LIST: return {{9, 4}, {2, 2}, {2, 5}, {0, 1}};
    case K::LINEAR_REC:
      return {{30, 10}, {1, 3, 5, 9, 9}, {1, 3, 13, 30, 30}, {0, 1, 2, 4, 3}};
    case K::ASGP_DC: return {{6, 3}, {1, 1, 1, 1}, {1, 3, 3, 6}, {0, 1, 2, 3}};
    case K::ASGP_DP1D: return {{6, 3}, {2, 1, 1}, {2, 5, 5}, {0, 1, 2}};
    case K::ASGP_DP2D: return {{16, 6}, {1, 2, 3, 3}, {1, 2, 15, 15}, {0, 1, 2, 3}};
    default:
      if (arity > 3) throw std::logic_error("unknown ordinary source arity");
      std::vector<std::size_t> order;
      for (std::size_t i = 0; i < arity; ++i) order.push_back(i);
      return {{1, 1}, std::vector<std::size_t>(arity, 1),
              std::vector<std::size_t>(arity, 1), std::move(order)};
  }
}
}  // namespace

LegacyBudgetMetrics legacy_budget_metrics(const legacy_v1::AstProgram& program,
    const std::vector<evo::InputSpec>& inputs) {
  // The offline AST type also carries intermediate lowering nodes. Those were
  // not recognized by the frozen source metadata walker and are not evidence
  // of a release-1 search budget.
  for (const auto& node : program.nodes)
    if (static_cast<int>(node.kind) >
        static_cast<int>(legacy_v1::NodeKind::ASGP_DP2D))
      throw std::invalid_argument("legacy budget metrics require unlowered source nodes");
  const auto verified = legacy_v1::verify(program, inputs);
  struct Ancestor {
    std::size_t end;
    std::size_t expression_depth;
  };
  std::vector<Ancestor> ancestors;
  LegacyBudgetMetrics result;
  result.nodes = program.nodes.size();
  for (std::size_t i = 0; i < program.nodes.size(); ++i) {
    while (!ancestors.empty() && ancestors.back().end <= i)
      ancestors.pop_back();
    const bool expression = static_cast<int>(program.nodes[i].kind) >=
                            static_cast<int>(legacy_v1::NodeKind::CONST);
    const auto expression_depth = expression
        ? 1 + (ancestors.empty() ? 0 : ancestors.back().expression_depth) : 0;
    result.physical_depth = std::max(result.physical_depth, ancestors.size() + 1);
    result.expression_depth = std::max(result.expression_depth, expression_depth);
    ancestors.push_back({verified.subtree_end.at(i), expression_depth});
  }
  return result;
}

namespace {
std::vector<LegacyExpansionMetrics> subtree_expansions(
    const legacy_v1::AstProgram& program, const legacy_v1::VerifiedAst& verified) {
  std::vector<LegacyExpansionMetrics> metrics(program.nodes.size());
  for (std::size_t i = program.nodes.size(); i-- > 0;) {
    const int arity = legacy_v1::prefix_arity(program, i);
    // Fixed skeleton nodes exclude the recursively copied source children.
    // Offsets count target edges from the replacement root to each child root.
    // Each source child occurs once in these migration rewrites.
    const auto rule = expansion_rule(program.nodes[i].kind, static_cast<std::size_t>(arity));
    auto current = rule.fixed;
    if (rule.offsets.size() != static_cast<std::size_t>(arity))
      throw std::logic_error("legacy expansion rule arity mismatch");
    std::size_t child = i + 1;
    for (const auto offset : rule.offsets) {
      current.nodes = add(current.nodes, metrics.at(child).nodes);
      current.physical_depth = std::max(current.physical_depth,
          add(offset, metrics.at(child).physical_depth));
      child = verified.subtree_end.at(child);
    }
    metrics[i] = current;
  }
  return metrics;
}
}  // namespace

LegacyExpansionMetrics predict_legacy_expansion(const legacy_v1::AstProgram& program,
    const std::vector<evo::InputSpec>& inputs) {
  (void)legacy_budget_metrics(program, inputs);
  return subtree_expansions(program, legacy_v1::verify(program, inputs)).front();
}

bool LegacyReplacementResources::accepts(std::size_t donor_nodes,
    std::size_t donor_expression_depth) const {
  return is_expression && surrounding_fits && donor_nodes > 0 &&
      donor_expression_depth > 0 && donor_nodes <= max_nodes &&
      donor_expression_depth <= max_expression_depth;
}

std::vector<LegacyReplacementResources> legacy_replacement_resources(
    const legacy_v1::AstProgram& program, std::size_t max_nodes,
    std::size_t max_expression_depth, const std::vector<evo::InputSpec>& inputs) {
  (void)legacy_budget_metrics(program, inputs);
  const auto verified = legacy_v1::verify(program, inputs);
  const auto size = program.nodes.size();
  std::vector<std::size_t> depths(size), ancestors;
  std::vector<std::size_t> prefix(size + 1), suffix(size + 1);
  for (std::size_t i = 0; i < size; ++i) {
    while (!ancestors.empty() && verified.subtree_end[ancestors.back()] <= i)
      ancestors.pop_back();
    if (static_cast<int>(program.nodes[i].kind) >=
        static_cast<int>(legacy_v1::NodeKind::CONST))
      depths[i] = 1 + (ancestors.empty() ? 0 : depths[ancestors.back()]);
    prefix[i + 1] = std::max(prefix[i], depths[i]);
    ancestors.push_back(i);
  }
  for (std::size_t i = size; i-- > 0;)
    suffix[i] = std::max(suffix[i + 1], depths[i]);
  std::vector<LegacyReplacementResources> result(size);
  for (std::size_t i = 0; i < size; ++i) {
    auto& allowance = result[i];
    allowance.is_expression = depths[i] != 0;
    if (!allowance.is_expression) continue;
    const auto end = verified.subtree_end[i];
    const auto outside_nodes = size - (end - i);
    const auto ancestor_depth = depths[i] - 1;
    const auto outside_depth = std::max(prefix[i], suffix[end]);
    if (outside_nodes >= max_nodes || ancestor_depth >= max_expression_depth ||
        outside_depth > max_expression_depth) continue;
    allowance.surrounding_fits = true;
    allowance.max_nodes = max_nodes - outside_nodes;
    allowance.max_expression_depth = max_expression_depth - ancestor_depth;
  }
  return result;
}

LegacyExpansionLayout predict_legacy_expansion_layout(const legacy_v1::AstProgram& program,
    const std::vector<evo::InputSpec>& inputs) {
  (void)legacy_budget_metrics(program, inputs);
  const auto verified = legacy_v1::verify(program, inputs);
  const auto metrics = subtree_expansions(program, verified);
  LegacyExpansionLayout result{metrics.front(),
      std::vector<LegacyExpansionSite>(program.nodes.size())};
  result.source_nodes[0] = {0, metrics.front().nodes, 1};
  // Source prefix order visits every parent before its children even when the
  // target rewrite permutes the children. No inverse-lowering heuristics needed.
  for (std::size_t i = 0; i < program.nodes.size(); ++i) {
    const auto rule = expansion_rule(program.nodes[i].kind,
        static_cast<std::size_t>(legacy_v1::prefix_arity(program, i)));
    std::vector<std::size_t> children;
    std::size_t child = i + 1;
    for (std::size_t argument = 0; argument < rule.offsets.size(); ++argument) {
      children.push_back(child);
      child = verified.subtree_end.at(child);
    }
    std::size_t preceding_children = 0;
    for (const auto argument : rule.emission_order) {
      child = children.at(argument);
      const auto begin = add(result.source_nodes[i].begin,
          add(rule.preceding.at(argument), preceding_children));
      result.source_nodes[child] = {begin, add(begin, metrics[child].nodes),
          add(result.source_nodes[i].depth, rule.offsets.at(argument))};
      preceding_children = add(preceding_children, metrics[child].nodes);
    }
  }
  return result;
}

LegacyExpansionMetrics legacy_expansion_ceiling(std::size_t max_source_nodes) {
  using K = legacy_v1::NodeKind;
  std::vector<ExpansionRule> rules;
  for (std::size_t arity = 0; arity <= 3; ++arity)
    rules.push_back(expansion_rule(K::CONST, arity));
  for (auto kind : {K::MAP_LIST, K::FILTER_LIST, K::LINEAR_REC,
                    K::ASGP_DC, K::ASGP_DP1D, K::ASGP_DP2D})
    rules.push_back(expansion_rule(kind, 0));
  const auto extent = add(max_source_nodes, 1);
  std::vector<LegacyExpansionMetrics> trees(extent);
  // forests[a][n] maximizes target nodes over a nonempty source trees whose
  // source sizes sum to n. Zero denotes an impossible forest (a >= 1).
  std::array<std::vector<std::size_t>, 6> forests;
  for (auto& forest : forests) forest.resize(extent);
  for (std::size_t n = 1; n <= max_source_nodes; ++n) {
    for (const auto& rule : rules) {
      const auto arity = rule.offsets.size();
      if (!arity) {
        if (n == 1) trees[n] = rule.fixed;
        continue;
      }
      if (n <= arity || !forests[arity][n - 1]) continue;
      trees[n].nodes = std::max(trees[n].nodes,
          add(rule.fixed.nodes, forests[arity][n - 1]));
      // Reserve one source node for every sibling and place the remaining
      // subtree on the longest target path. Ignoring types is conservative.
      const auto offset = *std::max_element(rule.offsets.begin(), rule.offsets.end());
      trees[n].physical_depth = std::max(trees[n].physical_depth,
          std::max(rule.fixed.physical_depth,
                   add(offset, trees[n - arity].physical_depth)));
    }
    forests[1][n] = trees[n].nodes;
    for (std::size_t count = 2; count < forests.size() && count <= n; ++count)
      for (std::size_t child = 1; child <= n - count + 1; ++child)
        if (forests[count - 1][n - child])
          forests[count][n] = std::max(forests[count][n],
              add(trees[child].nodes, forests[count - 1][n - child]));
  }
  // Ordinary unary nodes make both maxima monotone in the source node count.
  return trees.back();
}

bool legacy_accepts_resources(const LegacyBudgetMetrics& metrics,
    std::size_t max_nodes, std::size_t max_expression_depth,
    LegacyAcceptanceStage stage) {
  switch (stage) {
    case LegacyAcceptanceStage::Initialization:
      return metrics.nodes <= max_nodes;
    case LegacyAcceptanceStage::Variation:
      return metrics.nodes <= max_nodes &&
             metrics.expression_depth <= max_expression_depth;
  }
  throw std::invalid_argument("unknown legacy resource acceptance stage");
}

}  // namespace gagp::migration
