#pragma once
#include "phase_variation.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/bounded_region.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/repro/pack.hpp"
#include <map>
#include <set>

namespace gagp::evo::repro {
// Cold import/export adapter for the GPU genotype prototype. These host buffers
// are transport data, not admitted executable handles and not CPU reproduction.
struct HostPhaseGene {
  std::vector<PhaseTreeNode> nodes;
  std::vector<int> origins;
  std::vector<Value> constants;
};
struct NativePhaseSlot {
  unsigned nonterminal = 0;
  std::size_t begin = 0, end = 0, ordinal = 0;
  unsigned max_depth = 0;
  std::vector<int> binders;
};
struct NativePhaseLayout {
  AstProgram prototype;
  BytecodeProgram executable;
  std::vector<NativePhaseSlot> slots;
  std::vector<HostPhaseGrammar> profiles;
  std::vector<HostPhaseGene> genes;
  std::size_t fixed_nodes = 0;
  grammar::GrammarLimits budget;
};
inline void phase_layout_require(bool condition, const char* reason) {
  if (!condition) throw std::invalid_argument(std::string("native GPU phase layout: ") + reason);
}

inline NativePhaseLayout import_native_phase_layout(const grammar::CompiledGrammar& grammar,
    const ProgramGenome& external) {
  using namespace grammar;
  NativePhaseLayout result;
  VerifiedAst verified; std::vector<std::vector<int>> environments;
  // External provenance is not an import certificate: drop it explicitly.
  auto source = external; source.derivation.reset();
  const auto witness = reconstruct_derivation(grammar, source, entry_request(grammar), &verified, &environments);
  const auto& ast = source.ast;
  phase_layout_require(ast.bounded_region_specs.size() == 1 && ast.lexical_regions.empty() &&
      ast.traversal_specs.empty(), "requires one bounded region and no outer lexical/traversal regions");
  phase_layout_require(witness.templates.size() == 1, "requires one independent root template");
  for (const auto& value : ast.consts)
    phase_layout_require(value.tag == ValueTag::Int || value.tag == ValueTag::Bool,
                         "registry/noninteger constants require native fallback");
  const auto& entry = grammar.nonterminals()[grammar.entry()];
  phase_layout_require(entry.category == NodeCategory::Expression && entry.productions.size() == 1,
                       "requires a single expression-root production");
  const auto root_expression = grammar.productions()[entry.productions[0]].expression;
  phase_layout_require(grammar.expressions()[root_expression].kind == ExpressionKind::Template,
                       "requires a root template");
  std::map<unsigned, unsigned> variable_holes;
  std::set<unsigned> holes;
  std::function<void(unsigned,bool)> inspect = [&](unsigned id, bool root) {
    const auto& e = grammar.expressions().at(id);
    if (e.kind == ExpressionKind::Template) phase_layout_require(root, "nested templates require fallback");
    if (e.kind == ExpressionKind::Hole) {
      phase_layout_require(holes.insert(e.target).second && e.children.size() == 1,
                           "repeated holes require atomic-group fallback");
      const auto& child = grammar.expressions().at(e.children[0]);
      if (child.kind == ExpressionKind::Reference) {
        variable_holes.emplace(e.target, child.target); return;
      }
    }
    phase_layout_require(e.kind != ExpressionKind::Reference && e.kind != ExpressionKind::Local,
                         "variable skeleton requires native fallback");
    if (e.kind == ExpressionKind::Constant) {
      const auto& domain = grammar.constants().at(e.target);
      phase_layout_require(!domain.float_range && !domain.elements &&
          (domain.integer_range ? domain.minimum == domain.maximum : domain.values.size() == 1),
          "non-singleton skeleton constant requires fallback");
    }
    for (auto child : e.children) inspect(child, false);
  };
  inspect(root_expression, true);
  phase_layout_require(!variable_holes.empty(), "no variable phases");
  const auto& region = ast.bounded_region_specs[0];
  std::map<std::size_t, std::pair<std::size_t, std::size_t>> phases;
  std::size_t cursor = region.node_index + 1;
  for (std::size_t i = 0; i < region.plan.state_types.size() + region.plan.bound_operand_count; ++i)
    cursor = verified.subtree_end.at(cursor);
  for (std::size_t i = 0; i < region.phases.size(); ++i) {
    const auto end = verified.subtree_end.at(cursor); phases.emplace(cursor, std::make_pair(end,i)); cursor = end;
  }
  std::vector<unsigned> depths(ast.nodes.size()); std::vector<int> pending{1};
  for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
    while (!pending.empty() && !pending.back()) pending.pop_back();
    phase_layout_require(!pending.empty(), "invalid admitted shape");
    depths[i] = pending.size(); --pending.back();
    const auto arity = node_prefix_arity(ast.nodes[i]); if (arity) pending.push_back(arity);
  }
  std::set<unsigned> found_holes;
  for (const auto& hole : witness.holes) {
    const auto found = variable_holes.find(hole.slot);
    if (found == variable_holes.end()) continue;
    phase_layout_require(found_holes.insert(hole.slot).second, "coupled hole instance requires fallback");
    const auto phase = phases.find(hole.ast_begin);
    phase_layout_require(phase != phases.end() && phase->second.first == hole.ast_end,
                         "variable hole must cover one complete bounded phase");
    const auto& nt = grammar.nonterminals()[found->second];
    const auto& bindings = region.phases.at(phase->second.second).bindings;
    phase_layout_require(bindings.size() == nt.scope.size(), "phase scope must exactly match formal scope");
    NativePhaseSlot slot{found->second, hole.ast_begin, hole.ast_end, phase->second.second,
        grammar.search_limits().max_depth - depths.at(hole.ast_begin) + 1, {}};
    for (const auto& binding : bindings) slot.binders.push_back(binding.binder_id);
    auto choice = std::find_if(witness.choices.begin(), witness.choices.end(), [&](const auto& c) {
      return c.nonterminal == slot.nonterminal && c.ast_begin == slot.begin && c.ast_end == slot.end;
    });
    phase_layout_require(choice != witness.choices.end() &&
        environments.at(choice - witness.choices.begin()) == slot.binders,
        "phase/formal lexical order requires explicit normalization fallback");
    result.slots.push_back(std::move(slot));
  }
  phase_layout_require(found_holes.size() == variable_holes.size(), "variable hole missing from admission witness");
  std::sort(result.slots.begin(), result.slots.end(), [](const auto& a,const auto& b){return a.begin < b.begin;});
  result.fixed_nodes = ast.nodes.size(); result.budget = grammar.search_limits();
  phase_layout_require(result.budget.max_nodes <= 1024 && result.budget.max_depth <= 64,
                       "GPU genotype capacity requires fallback");
  for (const auto& slot : result.slots) {
    result.fixed_nodes -= slot.end - slot.begin;
    auto profile = compile_phase_variation_grammar(grammar, slot.nonterminal, result.budget.max_depth);
    HostPhaseGene gene;
    for (auto i = slot.begin; i < slot.end; ++i) {
      const auto expression = witness.nodes.at(i).expression;
      phase_layout_require(expression != kNoGrammarId && expression < profile.expressions.size(), "missing expression origin");
      const auto& e = grammar.expressions().at(expression); auto node = profile.expressions[expression].node;
      const auto& actual = ast.nodes[i];
      phase_layout_require(actual.kind == static_cast<NodeKind>(profile.expressions[expression].native_kind), "origin/native node mismatch");
      if (e.kind == ExpressionKind::Constant) {
        node.operand = gene.constants.size(); gene.constants.push_back(ast.consts.at(actual.i0));
      } else if (e.kind == ExpressionKind::Bound) {
        const auto binder = std::find(slot.binders.begin(), slot.binders.end(), actual.i0);
        phase_layout_require(binder != slot.binders.end(), "uncaptured lexical reference");
        node.operand = binder - slot.binders.begin();
      }
      gene.nodes.push_back(node); gene.origins.push_back(expression);
    }
    result.profiles.push_back(std::move(profile)); result.genes.push_back(std::move(gene));
  }
  std::vector<std::string> inputs; for (const auto& input : grammar.inputs()) inputs.push_back(input.name);
  result.executable = compile_for_eval(source, verified, inputs);
  result.prototype = std::move(source.ast);
  return result;
}

// Export only. The caller supplies a private run's current device copyback;
// external/imported gene buffers must never bypass independent admission.
inline ProgramGenome export_native_phase_layout(const grammar::CompiledGrammar& grammar,
    const NativePhaseLayout& layout, const std::vector<HostPhaseGene>& genes) {
  phase_layout_require(genes.size() == layout.slots.size(), "wrong gene count");
  ProgramGenome out; auto& ast = out.ast; ast = layout.prototype; ast.nodes.clear(); ast.fuel_specs.clear();
  std::vector<std::size_t> remap(layout.prototype.nodes.size(), std::size_t(-1));
  std::size_t slot_index = 0;
  for (std::size_t i = 0; i < layout.prototype.nodes.size();) {
    if (slot_index < layout.slots.size() && layout.slots[slot_index].begin == i) {
      const auto& slot = layout.slots[slot_index]; const auto& gene = genes[slot_index];
      phase_layout_require(!gene.nodes.empty() && gene.nodes.size() <= layout.budget.max_nodes &&
          gene.nodes.size() == gene.origins.size(), "invalid gene shape");
      for (std::size_t j = 0; j < gene.nodes.size(); ++j) {
        const int id = gene.origins[j];
        phase_layout_require(id >= 0 && std::size_t(id) < grammar.expressions().size(), "unknown expression origin");
        const auto& e = grammar.expressions()[id]; const auto& model = layout.profiles[slot_index].expressions.at(id);
        const auto node = gene.nodes[j]; AstNode native{static_cast<NodeKind>(model.native_kind)};
        phase_layout_require(node.kind == model.node.kind && node.opcode == model.node.opcode &&
            node.arity == model.node.arity && node.fuel == model.node.fuel && node.merge_fuel == model.node.merge_fuel,
            "gene/expression contract mismatch");
        if (e.kind == grammar::ExpressionKind::Constant) {
          phase_layout_require(node.operand >= 0 && std::size_t(node.operand) < gene.constants.size(), "constant index");
          native.i0 = ast.consts.size(); ast.consts.push_back(gene.constants[node.operand]);
        } else if (e.kind == grammar::ExpressionKind::Bound) {
          phase_layout_require(node.operand >= 0 && std::size_t(node.operand) < slot.binders.size(), "lexical index");
          native.i0 = slot.binders[node.operand];
        } else phase_layout_require(node.operand == model.node.operand, "operation operand");
        if (!e.fuel_charges.empty()) ast.fuel_specs.push_back({ast.nodes.size(),e.fuel_charges});
        ast.nodes.push_back(native);
      }
      i = slot.end; ++slot_index;
    } else {
      remap[i] = ast.nodes.size(); ast.nodes.push_back(layout.prototype.nodes[i]); ++i;
    }
  }
  for (const auto& fuel : layout.prototype.fuel_specs)
    if (remap.at(fuel.node_index) != std::size_t(-1)) ast.fuel_specs.push_back({remap[fuel.node_index],fuel.charges});
  for (auto& region : ast.bounded_region_specs) {
    phase_layout_require(remap.at(region.node_index) != std::size_t(-1), "region overwritten by phase");
    region.node_index = remap[region.node_index];
  }
  // Compaction is only serialization cleanup; the independent caller audits
  // this output before treating it as an imported executable/member.
  return compact_genome_tables(std::move(out));
}
} // namespace gagp::evo::repro
