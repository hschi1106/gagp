#include <algorithm>
#include <iostream>
#include <functional>
#include <stdexcept>

#include "gagp/migration/legacy_budget.hpp"
#include "../../src/evolution/subtree_utils.hpp"
#include "gagp/migration/legacy_constants.hpp"
#include "gagp/evolution/node_descriptor.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/transition/bounded_regions.hpp"
#include "gagp/runtime/payload/payload.hpp"

using namespace gagp;
using namespace gagp::migration;
using legacy_v1::NodeKind;

namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}

// Give every materialized constant occurrence its own prefix-ordered slot.
// Compare the complete AST identity, including metadata and fuel, independently
// of append order or value-table sharing introduced by lowering.
std::string materialized_key(evo::AstProgram ast) {
  std::vector<Value> values;
  for (auto& node : ast.nodes) {
    const auto& descriptor = evo::node_descriptor(node.kind);
    const auto remap = [&](int& index) {
      values.push_back(ast.consts.at(index)); index = static_cast<int>(values.size()-1);
    };
    if (descriptor.i0_role == evo::NodeIndexRole::Constant) remap(node.i0);
    if (descriptor.i1_role == evo::NodeIndexRole::Constant) remap(node.i1);
  }
  ast.consts = std::move(values);
  return evo::ast_cache_key(ast);
}

void check_constant_origins(const legacy_v1::AstProgram& source) {
  const auto mapped = lower_with_constant_origins(source);
  std::size_t rank = 0;
  for (std::size_t i = 0; i < source.nodes.size(); ++i) {
    if (source.nodes[i].kind != NodeKind::CONST) continue;
    const auto& site = mapped.sites.at(rank++);
    require(site.source_node == i && mapped.source_by_target.at(site.target_node) == i,
            "source constant selection order changed");
    Value replacement = source.consts.at(source.nodes[i].i0);
    if (replacement.tag == ValueTag::Int)
      replacement.i = replacement.i == INT64_MAX ? replacement.i-1 : replacement.i+1;
    else if (replacement.tag == ValueTag::Float) replacement.f += 0.125;
    else if (replacement.tag == ValueTag::Bool) replacement.b = !replacement.b;
    auto before = source;
    before.nodes[i].i0 = static_cast<int>(before.consts.size()); before.consts.push_back(replacement);
    auto after = mapped.genome.ast;
    after.nodes[site.target_node].i0 = static_cast<int>(after.consts.size()); after.consts.push_back(replacement);
    require(materialized_key(evo::transition::lower_bounded_regions(before).ast) == materialized_key(after),
            "source constant perturbation did not commute with lowering");
  }
  require(rank == mapped.sites.size(),"extra source constant candidate");
  for (std::size_t i = 0; i < mapped.source_by_target.size(); ++i) {
    const auto origin = mapped.source_by_target[i];
    if (origin != kNoLegacyConstant)
      require(source.nodes.at(origin).kind == NodeKind::CONST && mapped.genome.ast.nodes[i].kind == evo::NodeKind::CONST,
              "administrative node acquired source constant identity");
  }
}

void check_layout(const legacy_v1::AstProgram& source) {
  check_constant_origins(source);
  const auto layout = predict_legacy_expansion_layout(source);
  const auto target = evo::transition::lower_bounded_regions(source).ast;
  const auto verified = evo::verify_ast(target, {});
  require(verified.ok, "layout target must pass native verification");
  require(layout.metrics.nodes == target.nodes.size() &&
          layout.source_nodes.size() == source.nodes.size(), "layout size mismatch");
  std::vector<std::size_t> depths(target.nodes.size()), ends;
  std::vector<bool> occupied(target.nodes.size());
  std::size_t max_depth = 0;
  for (std::size_t i = 0; i < target.nodes.size(); ++i) {
    while (!ends.empty() && ends.back() <= i) ends.pop_back();
    depths[i] = ends.size() + 1;
    max_depth = std::max(max_depth, depths[i]);
    ends.push_back(verified.verified.subtree_end.at(i));
  }
  require(layout.metrics.physical_depth == max_depth, "layout maximum depth mismatch");
  const auto source_metrics = legacy_budget_metrics(source);
  const auto allowances = legacy_replacement_resources(source, 80, 7);
  const evo::grammar::ResourceProjection projection(verified.verified.subtree_end,
      legacy_expansion_resource_charges(source));
  require(projection.subtree().nodes == source_metrics.nodes &&
          projection.subtree().peak() == source_metrics.expression_depth,
          "target resource projection differs from source budgets");
  for (std::size_t i = 0; i < source.nodes.size(); ++i) {
    const auto& site = layout.source_nodes[i];
    require(site.begin < site.end && site.end <= target.nodes.size(), "invalid mapped span");
    require(!occupied.at(site.begin), "distinct source roots alias one target root");
    occupied[site.begin] = true;
    if (allowances[i].is_expression) {
      const auto projected = projection.replacement({site.begin}, 80, 7);
      for (std::size_t nodes = 1; nodes <= 81; ++nodes)
        for (std::size_t depth = 1; depth <= 8; ++depth)
          require(projected.accepts({nodes, depth, 0}) == allowances[i].accepts(nodes, depth),
                  "target replacement projection differs from source acceptance");
    }
    require(verified.verified.subtree_end.at(site.begin) == site.end &&
            depths.at(site.begin) == site.depth, "mapped span/depth is not a target subtree");
    if (source.nodes[i].kind == NodeKind::CONST) {
      const auto& leaf = target.nodes.at(site.begin);
      require(leaf.kind == evo::NodeKind::CONST && leaf.i0 == source.nodes[i].i0,
              "mapped source constant lost identity");
    }
  }
}

void test_expression_and_acceptance() {
  legacy_v1::AstProgram ast;
  ast.consts = {Value::from_int(1)};
  ast.nodes = {{NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
               {NodeKind::ADD}, {NodeKind::NEG}, {NodeKind::CONST, 0},
               {NodeKind::CONST, 0}, {NodeKind::BLOCK_NIL}};
  const auto m = legacy_budget_metrics(ast);
  const auto expansion = predict_legacy_expansion(ast);
  check_layout(ast);
  require(expansion.nodes == m.nodes && expansion.physical_depth == m.physical_depth,
          "ordinary source acquired expansion overhead");
  require(m.nodes == 8 && m.physical_depth == 6 && m.expression_depth == 3,
          "source depth metrics disagree with prefix structure");
  require(legacy_accepts_resources(m, 8, 2, LegacyAcceptanceStage::Initialization),
          "initialization must not invent a metadata-depth rejection");
  require(!legacy_accepts_resources(m, 8, 2, LegacyAcceptanceStage::Variation),
          "variation must retain expression-depth rejection");
  require(legacy_accepts_resources(m, 8, 3, LegacyAcceptanceStage::Variation),
          "exact node/depth boundary rejected");
  for (auto stage : {LegacyAcceptanceStage::Initialization, LegacyAcceptanceStage::Variation})
    require(!legacy_accepts_resources(m, 7, 100, stage), "node overflow accepted");
  ast.nodes[4].kind = NodeKind::CHECK_INT;
  try { (void)legacy_budget_metrics(ast); }
  catch (const std::invalid_argument& error) {
    require(std::string(error.what()).find("unlowered source") != std::string::npos,
            "intermediate AST rejection lost its diagnostic");
    return;
  }
  throw std::runtime_error("intermediate lowering nodes received a source budget");
}

void test_statement_ancestry_and_invalid_input() {
  legacy_v1::AstProgram ast;
  ast.consts = {Value::from_bool(true), Value::from_int(1)};
  ast.nodes = {{NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::IF_STMT},
               {NodeKind::CONST, 0}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
               {NodeKind::CONST, 1}, {NodeKind::BLOCK_NIL},
               {NodeKind::BLOCK_CONS}, {NodeKind::RETURN}, {NodeKind::NEG},
               {NodeKind::CONST, 1}, {NodeKind::BLOCK_NIL}, {NodeKind::BLOCK_NIL}};
  const auto m = legacy_budget_metrics(ast);
  check_layout(ast);
  require(m.nodes == 14 && m.physical_depth == 7 && m.expression_depth == 2,
          "statement/block ancestry leaked into expression depth");
  ast.nodes.pop_back();
  try { (void)legacy_budget_metrics(ast); }
  catch (const std::invalid_argument&) { return; }
  throw std::runtime_error("malformed source accepted for budget evidence");
}

void test_map_filter_expansion() {
  for (auto kind : {NodeKind::MAP_LIST, NodeKind::FILTER_LIST}) {
    legacy_v1::AstProgram ast;
    ast.consts = {payload::make_int_list_value({Value::from_int(1), Value::from_int(2)}),
                  Value::from_bool(true)};
    ast.nodes = {{NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
                 {kind, 17, 1}, {NodeKind::CONST, 0},
                 {kind == NodeKind::MAP_LIST ? NodeKind::BOUND_VAR : NodeKind::CONST,
                  kind == NodeKind::MAP_LIST ? 17 : 1}, {NodeKind::BLOCK_NIL}};
    const auto predicted = predict_legacy_expansion(ast);
    check_layout(ast);
    const auto lowered = evo::transition::lower_bounded_regions(ast);
    require(predicted.nodes == lowered.ast.nodes.size(), "sequence expansion node count differs");
    require(predicted.nodes == (kind == NodeKind::MAP_LIST ? 14U : 15U) &&
            predicted.physical_depth == 7, "sequence skeleton metric changed");
  }
}

void test_expansion_ceiling() {
  require(legacy_expansion_ceiling(0).nodes == 0, "empty allowance has nonzero size");
  const auto three = legacy_expansion_ceiling(3);
  require(three.nodes == 11 && three.physical_depth == 4,
          "three-node allowance must cover the Filter skeleton");
  const auto six = legacy_expansion_ceiling(6);
  require(six.nodes == 35 && six.physical_depth == 10,
          "six-node allowance must cover the LinearRec skeleton");
  for (int nesting = 0; nesting <= 30; ++nesting) {
    legacy_v1::AstProgram ast;
    ast.consts = {payload::make_int_list_value({Value::from_int(1)}), Value::from_bool(true)};
    ast.nodes = {{NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN}};
    for (int i = 0; i < nesting; ++i) ast.nodes.push_back({NodeKind::FILTER_LIST, i, 1});
    ast.nodes.push_back({NodeKind::CONST, 0});
    for (int i = 0; i < nesting; ++i) ast.nodes.push_back({NodeKind::CONST, 1});
    ast.nodes.push_back({NodeKind::BLOCK_NIL});
    const auto predicted = predict_legacy_expansion(ast);
    check_layout(ast);
    const auto lowered = evo::transition::lower_bounded_regions(ast);
    const auto bound = legacy_expansion_ceiling(ast.nodes.size());
    require(predicted.nodes == lowered.ast.nodes.size() && bound.nodes >= predicted.nodes &&
            bound.physical_depth >= predicted.physical_depth,
            "ceiling excludes an actual nested lowering");
  }
  try { (void)legacy_expansion_ceiling(static_cast<std::size_t>(-1)); }
  catch (const std::overflow_error&) { return; }
  throw std::runtime_error("overflowing allowance accepted");
}

void test_large_valid_source_expansion() {
  legacy_v1::AstProgram ast;
  ast.names = {"element", "accumulator", "index"};
  ast.consts = {payload::make_int_list_value({Value::from_int(1)}), Value::from_int(0)};
  ast.nodes = {{NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN}};
  // A balanced source expression keeps source depth small while exercising
  // repeated LinearRec skeleton overhead. This proves resource admissibility,
  // not derivability under any particular legacy structured grammar profile.
  std::function<void(int)> emit = [&](int recurrences) {
    if (!recurrences) {
      ast.nodes.push_back({NodeKind::CONST, 1});
      return;
    }
    const auto owner = ast.nodes.size();
    ast.nodes.push_back({NodeKind::LINEAR_REC});
    ast.linear_rec_binders.push_back({owner, 0, 1, 2});
    ast.nodes.push_back({NodeKind::CONST, 0});
    ast.nodes.push_back({NodeKind::CONST, 1});
    --recurrences;
    for (int child = 0; child < 3; ++child)
      emit(recurrences / 3 + (child < recurrences % 3 ? 1 : 0));
  };
  emit(15);
  ast.nodes.push_back({NodeKind::BLOCK_NIL});
  const auto source = legacy_budget_metrics(ast);
  require(source.nodes == 80 &&
          legacy_accepts_resources(source, 80, 7, LegacyAcceptanceStage::Variation),
          "balanced source violates the tested legacy resource limits");
  const auto prediction = predict_legacy_expansion(ast);
  check_layout(ast);
  const auto lowered = evo::transition::lower_bounded_regions(ast);
  require(prediction.nodes == 515 && lowered.ast.nodes.size() == prediction.nodes,
          "large source expansion no longer matches the skeleton proof");
  require(prediction.nodes <= legacy_expansion_ceiling(80).nodes,
          "derived ceiling excludes large valid source");
}

void test_layout_child_order_and_bounded_forms() {
  legacy_v1::AstProgram ast;
  ast.names = {"element", "accumulator", "index", "a", "b", "c", "d"};
  ast.consts = {payload::make_int_list_value({Value::from_int(1)}),
      Value::from_int(0), Value::from_int(11), Value::from_int(22), Value::from_int(33)};
  ast.nodes = {{NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
      {NodeKind::LINEAR_REC}, {NodeKind::CONST, 0}, {NodeKind::CONST, 1},
      {NodeKind::CONST, 2}, {NodeKind::NEG}, {NodeKind::CONST, 3},
      {NodeKind::CONST, 4}, {NodeKind::BLOCK_NIL}};
  ast.linear_rec_binders = {{3, 0, 1, 2}};
  check_layout(ast);
  const auto layout = predict_legacy_expansion_layout(ast);
  require(layout.source_nodes[9].end == layout.source_nodes[7].begin,
          "LinearRec last must precede step in target prefix order");
  ast.linear_rec_binders.clear();
  ast.nodes = {{NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
      {NodeKind::ASGP_DC}, {NodeKind::CONST, 0}, {NodeKind::CONST, 2},
      {NodeKind::CONST, 3}, {NodeKind::BOUND_VAR, 4}, {NodeKind::BLOCK_NIL}};
  ast.asgp_dc_binders = {{3, 0, 1, 2, 3, 4, 5}};
  check_layout(ast);
  ast.asgp_dc_binders.clear();
  ast.nodes = {{NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
      {NodeKind::ASGP_DP1D}, {NodeKind::CONST, 1}, {NodeKind::CONST, 2},
      {NodeKind::BOUND_VAR, 3}, {NodeKind::BLOCK_NIL}};
  ast.asgp_dp1d_specs = {{3, 0, 5, 0, 1, NodeKind::DP1_BACKWARD1, {1}, 1, 2, {3}}};
  check_layout(ast);
  ast.asgp_dp1d_specs.clear();
  ast.nodes = {{NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
      {NodeKind::ASGP_DP2D}, {NodeKind::CONST, 1}, {NodeKind::CONST, 1},
      {NodeKind::CONST, 2}, {NodeKind::BOUND_VAR, 6}, {NodeKind::BLOCK_NIL}};
  ast.asgp_dp2d_specs = {{3, 0, 3, 0, 3, 0, 0, 1,
      NodeKind::DP2_DIAGONAL_BACKWARD, 2, 3, 4, 5, {6}}};
  check_layout(ast);
}

void test_replacement_resources_against_actual_splices() {
  legacy_v1::AstProgram ast;
  ast.consts = {Value::from_int(1)};
  ast.nodes = {{NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
      {NodeKind::ADD}, {NodeKind::NEG}, {NodeKind::NEG}, {NodeKind::CONST, 0},
      {NodeKind::NEG}, {NodeKind::CONST, 0}, {NodeKind::BLOCK_NIL}};
  const auto verified = legacy_v1::verify(ast, {});
  for (std::size_t max_nodes = 0; max_nodes <= 16; ++max_nodes) {
    for (std::size_t max_depth = 0; max_depth <= 7; ++max_depth) {
      const auto budgets = legacy_replacement_resources(ast, max_nodes, max_depth);
      require(budgets.size() == ast.nodes.size(), "replacement source index coverage lost");
      for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
        const bool expression = i >= 3 && i <= 8;
        require(budgets[i].is_expression == expression, "statement offered expression budget");
        require(!budgets[i].accepts(0, 1) && !budgets[i].accepts(1, 0),
                "empty replacement expression accepted");
        if (!expression) {
          require(!budgets[i].accepts(1, 1), "non-expression replacement accepted");
          continue;
        }
        for (std::size_t donor_size = 1; donor_size <= 6; ++donor_size) {
          auto replaced = ast;
          std::vector<legacy_v1::AstNode> donor(donor_size - 1, {NodeKind::NEG});
          donor.push_back({NodeKind::CONST, 0});
          replaced.nodes.erase(replaced.nodes.begin() + i,
              replaced.nodes.begin() + verified.subtree_end[i]);
          replaced.nodes.insert(replaced.nodes.begin() + i, donor.begin(), donor.end());
          const auto metrics = legacy_budget_metrics(replaced);
          require(budgets[i].accepts(donor_size, donor_size) ==
              legacy_accepts_resources(metrics, max_nodes, max_depth,
                  LegacyAcceptanceStage::Variation),
              "replacement allowance differs from verified post-splice resource check");
        }
      }
    }
  }
  // This source's depth is four. Variation at its deep left branch can repair
  // it to depth three; changing the right branch cannot repair the left branch.
  const auto budgets = legacy_replacement_resources(ast, 80, 3);
  require(budgets[4].accepts(1, 1), "repairing an initial over-depth parent was rejected");
  require(!budgets[7].surrounding_fits, "unchanged over-depth branch was ignored");
  // Width and depth are independent: a shallow binary donor costs three nodes.
  const auto tight = legacy_replacement_resources(ast, 10, 4);
  require(!tight[8].accepts(3, 2) && tight[4].accepts(3, 2),
          "replacement source node allowance conflates depth and size");
}
// Compare real source and lowered splices, rather than only synthetic donor
// size/depth pairs. Nested traversal wrappers must not acquire source charges.
void test_projected_resources_after_structured_splices() {
  const std::vector<Value> lists{
      payload::make_int_list_value({Value::from_int(1)}),
      payload::make_float_list_value({Value::from_float(1.0)}),
      payload::make_string_list_value({payload::make_string_value("a")})};
  for (const auto list : lists) {
    const auto make = [&](int nesting) {
      legacy_v1::AstProgram ast;
      ast.consts = {list, Value::from_bool(true)};
      ast.nodes = {{NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN}};
      for (int i = 0; i < nesting; ++i) ast.nodes.push_back({NodeKind::FILTER_LIST, i, 1});
      ast.nodes.push_back({NodeKind::CONST, 0});
      for (int i = 0; i < nesting; ++i) ast.nodes.push_back({NodeKind::CONST, 1});
      ast.nodes.push_back({NodeKind::BLOCK_NIL});
      return ast;
    };
    for (int parent_depth = 1; parent_depth <= 3; ++parent_depth) {
      const auto parent = make(parent_depth);
      const auto parent_verified = legacy_v1::verify(parent, {});
      const auto parent_layout = predict_legacy_expansion_layout(parent);
      const auto parent_ast = evo::transition::lower_bounded_regions(parent).ast;
      const auto parent_charges = legacy_expansion_resource_charges(parent);
      for (int donor_depth = 0; donor_depth <= 4; ++donor_depth) {
        const auto donor = make(donor_depth);
        const auto donor_layout = predict_legacy_expansion_layout(donor);
        const auto donor_ast = evo::transition::lower_bounded_regions(donor).ast;
        const auto donor_charges = legacy_expansion_resource_charges(donor);
        const auto donor_span = donor_layout.source_nodes.at(3);
        for (std::size_t target : {std::size_t{3}, static_cast<std::size_t>(3 + parent_depth)}) {
          const auto span = parent_layout.source_nodes.at(target);
          auto source_child = parent;
          source_child.nodes.erase(source_child.nodes.begin() + target,
              source_child.nodes.begin() + parent_verified.subtree_end.at(target));
          source_child.nodes.insert(source_child.nodes.begin() + target,
              donor.nodes.begin() + 3, donor.nodes.end() - 1);
          const auto child = evo::subtree::replace_subtree(parent_ast, span.begin, span.end,
              donor_ast, donor_span.begin, donor_span.end);
          const auto verified = evo::verify_ast(child, {});
          require(verified.ok, "structured resource splice failed native verification");
          auto charges = parent_charges;
          charges.erase(charges.begin() + span.begin, charges.begin() + span.end);
          charges.insert(charges.begin() + span.begin,
              donor_charges.begin() + donor_span.begin, donor_charges.begin() + donor_span.end);
          const evo::grammar::ResourceProjection projection(verified.verified.subtree_end, charges);
          const auto expected = legacy_budget_metrics(source_child);
          const auto actual = projection.subtree();
          require(actual.nodes == expected.nodes && actual.peak() == expected.expression_depth,
                  "structured splice source/target resource composition differs");
          const evo::grammar::ResourceProjection before(
              evo::verify_ast(parent_ast, {}).verified.subtree_end, parent_charges);
          const evo::grammar::ResourceProjection replacement(
              evo::verify_ast(donor_ast, {}).verified.subtree_end, donor_charges);
          for (std::size_t nodes = expected.nodes - 1; nodes <= expected.nodes + 1; ++nodes)
            for (std::size_t depth = expected.expression_depth - 1;
                 depth <= expected.expression_depth + 1; ++depth)
              require(before.replacement({span.begin}, nodes, depth).accepts(
                          replacement.subtree(donor_span.begin)) ==
                      legacy_accepts_resources(expected, nodes, depth, LegacyAcceptanceStage::Variation),
                      "structured splice boundary allowance differs from actual source child");
        }
      }
    }
  }
}
}  // namespace

int main() {
  try {
    test_expression_and_acceptance();
    test_statement_ancestry_and_invalid_input();
    test_map_filter_expansion();
    test_expansion_ceiling();
    test_large_valid_source_expansion();
    test_layout_child_order_and_bounded_forms();
    test_replacement_resources_against_actual_splices();
    test_projected_resources_after_structured_splices();
    std::cout << "legacy budget tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
