#pragma once
#include "phase_variation.hpp"
#include "../../grammar/cold_admission.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/bounded_region.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/core/bytecode_verify.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/repro/pack.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <cstdlib>

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
  // Whole-program mode: one phase is the returned expression of a program
  // without regions, or the complete Program of a Program-entry grammar. The
  // executable main code is the lowered phase, followed by this terminator for
  // an expression entry; preset locals are the evaluator inputs.
  bool whole_program = false;
  bool program_terminator = true;
  Instr program_return;
  std::uint32_t program_return_fuel = 1;
  std::vector<std::string> input_names, local_names;
};
inline void phase_layout_require(bool condition, const char* reason) {
  if (!condition) throw std::invalid_argument(std::string("resident phase layout: ") + reason);
}
inline void phase_layout_require(bool condition, const std::string& reason) { phase_layout_require(condition, reason.c_str()); }
// A member over a per-program capacity of the genotype (nodes of a phase row,
// bounded regions per program). Initial generation redraws such a member.
struct PhaseMemberCapacityError : std::invalid_argument {
  explicit PhaseMemberCapacityError(const std::string& capacity)
      : std::invalid_argument("resident phase layout: " + capacity), reason(capacity) {}
  std::string reason;
};
inline void phase_member_capacity_require(bool condition, const std::string& reason) {
  if (!condition) throw PhaseMemberCapacityError(reason);
}
// Native name of a grammar local read or a named control target; null otherwise.
inline const std::string* phase_target_name(const grammar::CompiledGrammar& grammar, const grammar::CompiledExpression& e) {
  if (e.kind == grammar::ExpressionKind::Local) return &grammar.locals().at(e.target).name;
  if (e.kind != grammar::ExpressionKind::Control ||
      !grammar::PrimitiveCatalog::standard().control_signatures().at(e.target).requires_name) return nullptr;
  return e.target_input ? &grammar.inputs().at(e.local).name : &grammar.locals().at(e.local).name;
}

// Pure grammar compilation cache, bound to one immutable grammar owner. No AST
// admission or per-program witness is cached here; every import is still checked.
class NativePhaseGrammarProfiles final {
 public:
  // `inputs` fixes the evaluator input-local order of Input expressions.
  explicit NativePhaseGrammarProfiles(std::shared_ptr<const grammar::CompiledGrammar> owner,
      std::vector<std::string> inputs={})
      : owner_(std::move(owner)),inputs_(std::move(inputs)) { phase_layout_require(bool(owner_),"missing profile grammar owner"); }
  const HostPhaseGrammar& get(const grammar::CompiledGrammar& grammar,unsigned nonterminal,unsigned depth) {
    phase_layout_require(owner_.get()==&grammar,"phase profile grammar owner mismatch");
    const auto key=std::make_pair(nonterminal,depth);const auto found=profiles_.find(key);
    if(found!=profiles_.end())return found->second;
    return profiles_.emplace(key,compile_phase_variation_grammar(grammar,nonterminal,depth,0,
        inputs_.empty()?nullptr:&inputs_)).first->second;
  }
 private:
  std::shared_ptr<const grammar::CompiledGrammar> owner_;
  std::vector<std::string> inputs_;
  std::map<std::pair<unsigned,unsigned>,HostPhaseGrammar> profiles_;
};

// A grammar with let or traversal primitives runs whole programs: only the
// ASGP shape (one root template around one bounded region) keeps region mode.
inline bool phase_grammar_binds_lexically(const grammar::CompiledGrammar& grammar) {
  for (const auto& e : grammar.expressions())
    if (e.kind == grammar::ExpressionKind::Primitive && !e.regions.empty()) return true;
  return false;
}

// Region-template candidates: the entry's only production is a template and
// no let or traversal primitive exists (the ASGP shape). Every other grammar
// imports whole programs.
inline bool phase_region_template_candidate(const grammar::CompiledGrammar& grammar) {
  const auto& entry = grammar.nonterminals()[grammar.entry()];
  return entry.category == NodeCategory::Expression && entry.productions.size() == 1 &&
      grammar.expressions()[grammar.productions()[entry.productions[0]].expression].kind ==
          grammar::ExpressionKind::Template && !phase_grammar_binds_lexically(grammar);
}

// A secondary population import may omit duplicate profile storage: it is used
// only as transport and must not itself serve as an export template. Admission
// still runs against its own AST. A matching private certificate may reuse the
// witness and executable; otherwise ordinary reconstruction/lowering applies.
inline NativePhaseLayout import_native_phase_layout(const grammar::CompiledGrammar& grammar,
    const ProgramGenome& external, NativePhaseGrammarProfiles* profiles=nullptr,
    const std::vector<std::string>* evaluator_inputs=nullptr, bool retain_profiles=true,
    bool whole_program_only=false) {
  using namespace grammar;
  NativePhaseLayout result;
  VerifiedAst verified; std::vector<std::vector<int>> environments;
  // Public provenance is never trusted. The optional reuse still enters the
  // membership boundary: it checks a private certificate's grammar, request and
  // complete runtime identity, and reconstructs on a miss. Population
  // initialization has already created such certificates for unchanged inputs.
  auto source = external;
  std::shared_ptr<const BytecodeProgram> cold_executable;
  const auto witness = grammar::detail::reconstruct_cold_derivation(grammar, source, entry_request(grammar),
      &verified, &environments,&cold_executable,evaluator_inputs);
  const auto& ast = source.ast;
  // Cold canonical membership already checked physical node/depth budgets.
  std::vector<unsigned> depths(ast.nodes.size()); std::vector<int> pending{1};
  for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
    while (!pending.empty() && !pending.back()) pending.pop_back();
    phase_layout_require(!pending.empty(), "invalid admitted shape");
    depths[i] = pending.size(); --pending.back();
    const auto arity = node_prefix_arity(ast.nodes[i]); if (arity) pending.push_back(arity);
  }
  // Region mode needs a region-template candidate grammar, so members of any
  // other grammar all import in whole-program mode.
  if (whole_program_only || !phase_region_template_candidate(grammar) || witness.templates.empty() ||
      ast.bounded_region_specs.size() != 1) {
    // Whole-program mode: the entry nonterminal's outermost choice is the phase.
    // Templates, lets, traversals and bounded regions are phase nodes; the
    // phase grammar rejects every other lexical region primitive.
    for (const auto& region : ast.lexical_regions) {
      const auto kind = ast.nodes.at(region.node_index).kind;
      phase_layout_require(kind == NodeKind::LET_REGION || kind == NodeKind::TRAVERSE || kind == NodeKind::TRAVERSE_RANGE,
          "whole-program lexical region");
    }
    const auto& entry = grammar.nonterminals()[grammar.entry()];
    phase_layout_require(entry.category == NodeCategory::Expression || entry.category == NodeCategory::Program,
        "whole-program phase requires an expression or program entry");
    result.program_terminator = entry.category == NodeCategory::Expression;
    std::size_t best = witness.choices.size();
    for (std::size_t i = 0; i < witness.choices.size(); ++i) {
      const auto& c = witness.choices[i];
      if (c.nonterminal != grammar.entry()) continue;
      if (best == witness.choices.size() || c.ast_begin < witness.choices[best].ast_begin) best = i;
    }
    phase_layout_require(best < witness.choices.size(), "entry choice missing from admission witness");
    const auto& choice = witness.choices[best];
    phase_layout_require(environments.at(best).empty(), "whole-program phase has no lexical binders");
    phase_layout_require(choice.ast_end <= ast.nodes.size() && choice.ast_begin < choice.ast_end, "entry choice span");
    NativePhaseSlot slot{grammar.entry(), choice.ast_begin, choice.ast_end, 0,
        grammar.search_limits().max_depth - depths.at(choice.ast_begin) + 1, {}};
    result.slots.push_back(std::move(slot));
    result.whole_program = true;
  } else {
  phase_layout_require(ast.bounded_region_specs.size() == 1 && ast.lexical_regions.empty() &&
      ast.traversal_specs.empty(), "requires one bounded region and no outer lexical/traversal regions");
  phase_layout_require(witness.templates.size() == 1, "requires one independent root template");
  for (const auto& value : ast.consts)
    phase_layout_require(value.tag == ValueTag::Int || value.tag == ValueTag::Bool ||
        (value.tag == ValueTag::IntList && Value::container_len(value) == 0),
                         "region-template layout holds only Int, Bool and empty IntList constants");
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
      if (e.kind == ExpressionKind::Template) phase_layout_require(root, "region-template layout has no nested template");
    if (e.kind == ExpressionKind::Hole) {
      phase_layout_require(holes.insert(e.target).second && e.children.size() == 1,
                           "region-template layout has no repeated hole");
      const auto& child = grammar.expressions().at(e.children[0]);
      if (child.kind == ExpressionKind::Reference) {
        variable_holes.emplace(e.target, child.target); return;
      }
    }
    phase_layout_require(e.kind != ExpressionKind::Reference && e.kind != ExpressionKind::Local,
                         "region-template layout has a fixed skeleton");
    if (e.kind == ExpressionKind::Constant) {
      const auto& domain = grammar.constants().at(e.target);
      phase_layout_require(!domain.float_range && !domain.elements &&
          (domain.integer_range ? domain.minimum == domain.maximum : domain.values.size() == 1),
          "region-template layout has only singleton skeleton constants");
    }
    for (auto child : e.children) inspect(child, false);
  };
  inspect(root_expression, true);
  phase_layout_require(!variable_holes.empty(), "region-template layout needs a variable phase");
  const auto& region = ast.bounded_region_specs[0];
  std::map<std::size_t, std::pair<std::size_t, std::size_t>> phases;
  std::size_t cursor = region.node_index + 1;
  for (std::size_t i = 0; i < region.plan.state_types.size() + region.plan.bound_operand_count; ++i)
    cursor = verified.subtree_end.at(cursor);
  for (std::size_t i = 0; i < region.phases.size(); ++i) {
    const auto end = verified.subtree_end.at(cursor); phases.emplace(cursor, std::make_pair(end,i)); cursor = end;
  }
  std::set<unsigned> found_holes;
  for (const auto& hole : witness.holes) {
    const auto found = variable_holes.find(hole.slot);
    if (found == variable_holes.end()) continue;
    phase_layout_require(found_holes.insert(hole.slot).second, "region-template layout fills each hole once");
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
        "region-template layout needs phase bindings in formal scope order");
    result.slots.push_back(std::move(slot));
  }
  phase_layout_require(found_holes.size() == variable_holes.size(), "variable hole missing from admission witness");
  phase_layout_require(result.slots.size() <= std::size_t(kPhaseSlots), "region-template layout holds at most " +
      std::to_string(kPhaseSlots) + " variable phases, this template has " + std::to_string(result.slots.size()));
  }
  std::sort(result.slots.begin(), result.slots.end(), [](const auto& a,const auto& b){return a.begin < b.begin;});
  result.fixed_nodes = ast.nodes.size(); result.budget = grammar.search_limits();
  phase_layout_require(result.budget.max_nodes <= kPhaseProgramNodes && result.budget.max_depth <= kPhaseProgramDepth,
      "search_limits {max_nodes " + std::to_string(result.budget.max_nodes) + ", max_depth " +
      std::to_string(result.budget.max_depth) + "} exceed the resident capacity of " + std::to_string(kPhaseProgramNodes) +
      " nodes and depth " + std::to_string(kPhaseProgramDepth));
  for (const auto& slot : result.slots) {
    result.fixed_nodes -= slot.end - slot.begin;
    HostPhaseGrammar uncached_profile;
    if (!profiles) uncached_profile = compile_phase_variation_grammar(grammar,slot.nonterminal,result.budget.max_depth,0,evaluator_inputs);
    const auto& profile = profiles ? profiles->get(grammar,slot.nonterminal,result.budget.max_depth)
                                   : uncached_profile;
    HostPhaseGene gene;
    for (auto i = slot.begin; i < slot.end; ++i) {
      const auto expression = witness.nodes.at(i).expression;
      phase_layout_require(expression != kNoGrammarId && expression < profile.expressions.size(), "missing expression origin");
      if (i == slot.begin)
        phase_layout_require(!profile.statements || (result.whole_program && !result.program_terminator &&
            result.budget.max_nodes <= kPhaseStatementNodes), result.whole_program && !result.program_terminator ?
            "statement program search_limits max_nodes " + std::to_string(result.budget.max_nodes) +
            " exceeds the statement program capacity of " + std::to_string(kPhaseStatementNodes) + " nodes" :
            std::string("statements occur only in a Program-entry grammar"));
      const auto& e = grammar.expressions().at(expression); auto node = profile.expressions[expression].node;
      const auto& actual = ast.nodes[i];
      phase_layout_require(actual.kind == static_cast<NodeKind>(profile.expressions[expression].native_kind), "origin/native node mismatch");
      if (e.kind == ExpressionKind::Constant) {
        node.operand = gene.constants.size(); gene.constants.push_back(ast.consts.at(actual.i0));
      } else if (e.kind == ExpressionKind::Input) {
        phase_layout_require(actual.i0 >= 0 && std::size_t(actual.i0) < ast.names.size() &&
            ast.names[actual.i0] == grammar.inputs().at(e.target).name, "input name mismatch");
      } else if (e.kind == ExpressionKind::Bound) {
        // Binder numbers are static (see compile_phase_grammar); membership
        // has already bound every reference to its innermost binder.
        if (!profile.lets) {
          const auto binder = std::find(slot.binders.begin(), slot.binders.end(), actual.i0);
          phase_layout_require(binder != slot.binders.end(), "uncaptured lexical reference");
          node.operand = binder - slot.binders.begin();
        }
      } else if (const auto* name = phase_target_name(grammar, e)) {
        phase_layout_require(actual.i0 >= 0 && std::size_t(actual.i0) < ast.names.size() &&
            ast.names[actual.i0] == *name, "local or target name mismatch");
      }
      gene.nodes.push_back(node); gene.origins.push_back(expression);
    }
    phase_member_capacity_require(gene.nodes.size() <= kPhaseRowNodes, "a phase of " + std::to_string(gene.nodes.size()) +
        " nodes exceeds the phase capacity of " + std::to_string(kPhaseRowNodes) + " nodes");
    const int regions = phase_region_count(gene.nodes.data(), static_cast<int>(gene.nodes.size()));
    phase_member_capacity_require(regions <= kPhaseMaxRegions, "a program of " + std::to_string(regions) +
        " bounded regions exceeds the capacity of " + std::to_string(kPhaseMaxRegions) + " bounded regions per program");
    if (!profile.shared.empty()) {
      // A native derivation copies a repeated hole's first fill with its own
      // expression ids; the phase genotype names every copy after its
      // occurrence (see share_phase_tree), which this rebuild establishes.
      const int count = gene.nodes.size(), constants = gene.constants.size();
      std::vector<PhaseTreeNode> nodes(1024); std::vector<int> origins(1024);
      std::vector<PhaseGeneSite> sites(1024); std::vector<Value> values(1024);
      const PhaseTreeView in{gene.nodes.data(), gene.origins.data(), nullptr, gene.constants.data(), count, constants};
      const auto shared = share_phase_tree<true>(phase_structure_view(profile), int(profile.root_expression),
          int(grammar.nonterminals().at(slot.nonterminal).scope.size()), in, {nodes.data(), origins.data(), sites.data(), values.data(), 1024});
      phase_layout_require(shared.status == PhaseCompileStatus::Ok && shared.nodes == count && shared.constants == constants,
                           "repeated hole copies differ");
      gene.nodes.assign(nodes.begin(), nodes.begin() + count); gene.origins.assign(origins.begin(), origins.begin() + count);
      gene.constants.assign(values.begin(), values.begin() + constants);
    }
    phase_layout_require(result.whole_program || !profile.lets, "region-template layout has no let binder inside a phase");
    // Variation draws from the phase grammar, so its constant domains obey the
    // same restriction as the member's constants.
    if (!result.whole_program) {
      for (const auto& domain : profile.domains)
        phase_layout_require(!domain.float_range, "region-template layout holds only Int, Bool and empty IntList constant domains");
      for (const auto& value : profile.values)
        phase_layout_require(value.tag == ValueTag::Int || value.tag == ValueTag::Bool ||
            (value.tag == ValueTag::IntList && Value::container_len(value) == 0),
            "region-template layout holds only Int, Bool and empty IntList constant domains");
    }
    if (retain_profiles) {
      if (profiles) result.profiles.push_back(profile);
      else result.profiles.push_back(std::move(uncached_profile));
    }
    result.genes.push_back(std::move(gene));
  }
  std::vector<std::string> inputs; for (const auto& input : grammar.inputs()) inputs.push_back(input.name);
  const auto& execution_inputs = evaluator_inputs ? *evaluator_inputs : inputs;
  // Admission above still checks this grammar/request. Executable reuse adds
  // its own exact AST, scalar-constant, input-order and fuel identity check.
  const auto executable = !cold_executable
      ? admitted_bytecode_for_eval(source, execution_inputs, grammar.execution_limits().fuel)
      : nullptr;
  result.executable = cold_executable ? *cold_executable : executable ? *executable : compile_for_eval(source, verified, execution_inputs);
  result.input_names = execution_inputs;
  for (const auto& local : grammar.locals()) result.local_names.push_back(local.name);
  if (result.whole_program && result.program_terminator) {
    const auto& code = result.executable.code;
    phase_layout_require(!code.empty() && code.back().op == Opcode::Return, "whole-program executable must end in Return");
    result.program_return = code.back();
    result.program_return_fuel = result.executable.instruction_fuel.empty() ? 1 : result.executable.instruction_fuel.back();
  }
  result.prototype = std::move(source.ast);
  return result;
}

// A cold structural key only. Neutral expression placeholders are never executed
// or offered as grammar members; their purpose is to compare the fixed skeleton.
inline std::string native_phase_skeleton_key(const NativePhaseLayout& layout) {
 ProgramGenome skeleton;skeleton.ast=layout.prototype;auto& ast=skeleton.ast;ast.nodes.clear();ast.fuel_specs.clear();
 const int marker=ast.consts.size();ast.consts.push_back(Value::from_int(0));
 std::vector<std::size_t> remap(layout.prototype.nodes.size(),std::size_t(-1));std::size_t slot=0;
 for(std::size_t i=0;i<layout.prototype.nodes.size();) {
  if(slot<layout.slots.size() && layout.slots[slot].begin==i){ast.nodes.push_back({NodeKind::CONST,marker});i=layout.slots[slot++].end;}
  else {remap[i]=ast.nodes.size();ast.nodes.push_back(layout.prototype.nodes[i]);++i;}
 }
 for(const auto& fuel:layout.prototype.fuel_specs)if(remap.at(fuel.node_index)!=std::size_t(-1))ast.fuel_specs.push_back({remap[fuel.node_index],fuel.charges});
 // Bounded regions and let binders inside a phase belong to that phase.
 std::vector<BoundedRegionSpec> fixed_regions;
 for(auto region:ast.bounded_region_specs)if(remap.at(region.node_index)!=std::size_t(-1)){region.node_index=remap[region.node_index];fixed_regions.push_back(std::move(region));}
 ast.bounded_region_specs=std::move(fixed_regions);
 std::vector<LexicalRegion> fixed_lexical;
 for(auto region:ast.lexical_regions)if(remap.at(region.node_index)!=std::size_t(-1)){region.node_index=remap[region.node_index];fixed_lexical.push_back(std::move(region));}
 ast.lexical_regions=std::move(fixed_lexical);
 std::vector<TraversalSpec> fixed_traversals;
 for(auto traversal:ast.traversal_specs)if(remap.at(traversal.node_index)!=std::size_t(-1)){traversal.node_index=remap[traversal.node_index];fixed_traversals.push_back(traversal);}
 ast.traversal_specs=std::move(fixed_traversals);
 // Stable table compaction alone retains parent-dependent interning order:
 // a variable phase can intern a constant before a later fixed phase uses it.
 // Serialize each fixed occurrence through a fresh, traversal-ordered table.
 // Copy complete Values (including exact integer and payload identities), not
 // floating conversions or hashes. This changes only the comparison artifact.
 std::vector<Value> constants;
 for(auto& node:ast.nodes)if(node.kind==NodeKind::CONST) {
  constants.push_back(ast.consts.at(node.i0));node.i0=static_cast<int>(constants.size()-1);
 }
 ast.consts=std::move(constants);
 return ast_cache_key(compact_genome_tables(std::move(skeleton)).ast);
}

// Imports a population under one layout, a function of the grammar and the
// members only. A region-template candidate first tries exactly the layout of
// its first member, as every single import chooses it; when a member cannot
// take that layout or the members do not share one fixed skeleton, the whole
// population imports whole programs instead. The first layout retains its
// profiles; later ones keep only their slots, genes and executable.
inline std::vector<NativePhaseLayout> import_native_phase_population(const grammar::CompiledGrammar& grammar,
    const std::vector<ProgramGenome>& input, NativePhaseGrammarProfiles* profiles,
    const std::vector<std::string>* evaluator_inputs) {
  phase_layout_require(!input.empty(), "empty population");
  const auto import_all = [&](bool whole_program_only) {
    std::vector<NativePhaseLayout> layouts; layouts.reserve(input.size());
    std::string key;
    for (std::size_t p = 0; p < input.size(); ++p) {
      auto imported = import_native_phase_layout(grammar, input[p], profiles, evaluator_inputs, p == 0, whole_program_only);
      if (p == 0) key = native_phase_skeleton_key(imported);
      else {
        const auto& first = layouts.front();
        phase_layout_require(imported.whole_program == first.whole_program && imported.slots.size() == first.slots.size() &&
            native_phase_skeleton_key(imported) == key, "members do not share one fixed skeleton");
        for (std::size_t k = 0; k < first.slots.size(); ++k) {
          const auto& a = imported.slots[k]; const auto& b = first.slots[k];
          phase_layout_require(a.nonterminal == b.nonterminal && a.ordinal == b.ordinal && a.binders == b.binders &&
              a.max_depth == b.max_depth, "members do not share one phase contract");
        }
        imported.prototype = {};
      }
      layouts.push_back(std::move(imported));
    }
    return layouts;
  };
  if (phase_region_template_candidate(grammar)) {
    try { return import_all(false); } catch (const std::invalid_argument&) {}
  }
  return import_all(true);
}

// Checks one member against the per-program capacities in the layout it
// takes on its own (import_native_phase_population tries the same order).
// Throws PhaseMemberCapacityError only. Any other rejection is left to the
// population import, which reports it unchanged.
inline void require_phase_member_capacity(const grammar::CompiledGrammar& grammar, const ProgramGenome& member,
    NativePhaseGrammarProfiles* profiles, const std::vector<std::string>* evaluator_inputs) {
  if (phase_region_template_candidate(grammar)) {
    try { (void)import_native_phase_layout(grammar, member, profiles, evaluator_inputs, false); return; }
    catch (const PhaseMemberCapacityError&) { throw; }
    catch (const std::invalid_argument&) {}
  }
  try { (void)import_native_phase_layout(grammar, member, profiles, evaluator_inputs, false, true); }
  catch (const PhaseMemberCapacityError&) { throw; }
  catch (const std::invalid_argument&) {}
}

// Export only. The caller supplies a private run's current device copyback;
// external/imported gene buffers must never bypass independent admission.
inline ProgramGenome export_native_phase_layout(const grammar::CompiledGrammar& grammar,
    const NativePhaseLayout& layout, const std::vector<HostPhaseGene>& genes) {
  phase_layout_require(genes.size() == layout.slots.size(), "wrong gene count");
  ProgramGenome out; auto& ast = out.ast; ast = layout.prototype; ast.nodes.clear(); ast.fuel_specs.clear();
  std::vector<std::size_t> remap(layout.prototype.nodes.size(), std::size_t(-1));
  std::size_t slot_index = 0;
  const auto inside_slot = [&](std::size_t index) {
    return std::any_of(layout.slots.begin(), layout.slots.end(), [&](const auto& slot) {
      return index >= slot.begin && index < slot.end; });
  };
  // Let and region binders live only inside phases; fresh IDs avoid every
  // fixed binder.
  int next_binder = 0;
  std::vector<BoundedRegionSpec> fixed_regions, phase_regions;
  for (const auto& region : ast.bounded_region_specs) {
    for (const auto& phase : region.phases)
      for (const auto& binding : phase.bindings) next_binder = std::max(next_binder, binding.binder_id + 1);
    if (!inside_slot(region.node_index)) fixed_regions.push_back(region);
  }
  for (const auto& region : ast.lexical_regions) {
    phase_layout_require(inside_slot(region.node_index), "fixed lexical region");
    for (const auto& binding : region.bindings) next_binder = std::max(next_binder, binding.id + 1);
  }
  for (const auto& traversal : ast.traversal_specs) phase_layout_require(inside_slot(traversal.node_index), "fixed traversal");
  ast.lexical_regions.clear(); ast.traversal_specs.clear();
  const auto name_index = [&](const std::string& name) {
    auto found = std::find(ast.names.begin(), ast.names.end(), name);
    if (found == ast.names.end()) found = ast.names.insert(ast.names.end(), name);
    return static_cast<int>(found - ast.names.begin());
  };
  for (std::size_t i = 0; i < layout.prototype.nodes.size();) {
    if (slot_index < layout.slots.size() && layout.slots[slot_index].begin == i) {
      const auto& slot = layout.slots[slot_index]; const auto& gene = genes[slot_index];
      const auto& profile = layout.profiles.at(slot_index);
      phase_layout_require(!gene.nodes.empty() && gene.nodes.size() <= layout.budget.max_nodes &&
          gene.nodes.size() == gene.origins.size(), "invalid gene shape");
      std::vector<std::size_t> ends(gene.nodes.size());
      {
        std::vector<std::size_t> sizes;
        for (std::size_t j = gene.nodes.size(); j-- > 0;) {
          std::size_t size = 1;
          for (int c = 0; c < gene.nodes[j].arity; ++c) {
            phase_layout_require(!sizes.empty(), "invalid gene shape");
            size += sizes.back(); sizes.pop_back();
          }
          sizes.push_back(size); ends[j] = j + size;
        }
        phase_layout_require(sizes.size() == 1 && sizes.back() == gene.nodes.size(), "invalid gene shape");
      }
      // `env` maps each binder number to its native binder: a let or
      // traversal body binds its numbers, and a region phase sees only its own
      // bindings. Recursion depth is bounded by the phase depth budget.
      using Environment = std::array<int, kPhaseBinders>;
      std::function<void(std::size_t, const Environment&)> emit = [&](std::size_t j, const Environment& env) {
        const int id = gene.origins[j];
        phase_layout_require(id >= 0 && std::size_t(id) < grammar.expressions().size(), "unknown expression origin");
        const auto& e = grammar.expressions()[id]; const auto& model = profile.expressions.at(id);
        const auto node = gene.nodes[j]; AstNode native{static_cast<NodeKind>(model.native_kind)};
        phase_layout_require(node.kind == model.node.kind && node.opcode == model.node.opcode &&
            node.arity == model.node.arity && node.fuel == model.node.fuel && node.merge_fuel == model.node.merge_fuel,
            "gene/expression contract mismatch");
        int let_binder = -1; std::optional<BoundedRegionSpec> region; std::array<int, 3> walk{-1, -1, -1};
        if (e.kind == grammar::ExpressionKind::Constant) {
          phase_layout_require(node.operand >= 0 && std::size_t(node.operand) < gene.constants.size(), "constant index");
          native.i0 = ast.consts.size(); ast.consts.push_back(gene.constants[node.operand]);
        } else if (e.kind == grammar::ExpressionKind::Input) {
          native.i0 = name_index(grammar.inputs().at(e.target).name);
        } else if (e.kind == grammar::ExpressionKind::Bound) {
          phase_layout_require(node.operand >= 0 && node.operand < kPhaseBinders && env[node.operand] >= 0, "lexical index");
          native.i0 = env[node.operand];
        } else if (node.kind == PhaseTreeKind::Traverse) {
          phase_layout_require(node.operand == model.node.operand && node.operand >= 0 &&
              std::size_t(node.operand) < profile.regions.size() && profile.regions[node.operand].traversal &&
              e.regions.size() == 1 && e.regions[0].bindings.size() == 3, "traversal operand");
          // Native generation records a traversal before its subtrees.
          LexicalRegion lexical{ast.nodes.size(), static_cast<int>(e.regions[0].argument), {}};
          for (std::size_t k = 0; k < 3; ++k) {
            walk[k] = next_binder++; lexical.bindings.push_back({walk[k], e.regions[0].bindings[k].type});
          }
          ast.lexical_regions.push_back(std::move(lexical));
          ast.traversal_specs.push_back({ast.nodes.size(), profile.regions[node.operand].traversal == 2 ?
              TraversalDirection::Reverse : TraversalDirection::Forward});
        } else if (model.bind_argument >= 0) {
          phase_layout_require(node.operand >= 0 && node.operand < kPhaseBinders && model.bind_argument < node.arity &&
              e.regions.size() == 1 && e.regions[0].bindings.size() == 1, "let binder");
          let_binder = next_binder++;
          ast.lexical_regions.push_back({ast.nodes.size(), model.bind_argument, {{let_binder, e.regions[0].bindings[0].type}}});
        } else if (e.kind == grammar::ExpressionKind::Structured) {
          phase_layout_require(node.operand == model.node.operand && node.operand >= 0 &&
              std::size_t(node.operand) < profile.region_sources.size() &&
              profile.region_sources[node.operand].expression == unsigned(id), "region operand");
          native.i0 = node.arity;
          region.emplace(); region->plan = profile.region_sources[node.operand].plan;
          for (std::size_t c = 0; c < e.captures.size(); ++c) {
            const auto& capture = e.captures[c];
            if (capture.kind == grammar::CompiledCaptureKind::Input)
              region->parameters.push_back({RegionCaptureKind::Name, name_index(grammar.inputs().at(capture.target).name)});
            else if (capture.kind == grammar::CompiledCaptureKind::Local)
              region->parameters.push_back({RegionCaptureKind::Name, name_index(grammar.locals().at(capture.target).name)});
            else {
              const int number = profile.regions[node.operand].capture_target[c];
              phase_layout_require(number >= 0 && number < kPhaseBinders && env[number] >= 0, "region capture");
              region->parameters.push_back({RegionCaptureKind::Lexical, env[number]});
            }
          }
          for (std::size_t p = 0; p < e.phases.size(); ++p) {
            RegionAstPhase phase; phase.argument = e.phases[p].argument;
            for (const auto& source : e.phases[p].sources) phase.bindings.push_back({source, next_binder++});
            region->phases.push_back(std::move(phase));
          }
        } else {
          phase_layout_require(node.operand == model.node.operand, "operation operand");
          if (const auto* name = phase_target_name(grammar, e)) native.i0 = name_index(*name);
        }
        const auto index = ast.nodes.size();
        if (!e.fuel_charges.empty()) ast.fuel_specs.push_back({index, e.fuel_charges});
        ast.nodes.push_back(native);
        std::size_t child = j + 1;
        for (int c = 0; c < node.arity; ++c) {
          phase_layout_require(child < gene.nodes.size(), "invalid gene shape");
          Environment inner = env;
          if (region && c >= int(region->plan.state_types.size() + region->plan.bound_operand_count)) {
            const auto& phase = region->phases.at(c - region->plan.state_types.size() - region->plan.bound_operand_count);
            phase_layout_require(phase.argument == unsigned(c) && phase.bindings.size() < 8, "region phase layout");
            inner.fill(-1);
            for (std::size_t b = 0; b < phase.bindings.size(); ++b) inner[b] = phase.bindings[b].binder_id;
          } else if (let_binder >= 0 && c == model.bind_argument) inner[node.operand] = let_binder;
          else if (walk[0] >= 0 && c == node.arity - 1)
            for (std::size_t k = 0; k < 3; ++k) inner[profile.regions[node.operand].binders[k]] = walk[k];
          emit(child, inner); child = ends[child];
        }
        // Native generation records a region after its subtrees.
        if (region) { region->node_index = index; phase_regions.push_back(std::move(*region)); }
      };
      Environment env; env.fill(-1);
      phase_layout_require(slot.binders.size() <= env.size(), "lexical capacity");
      for (std::size_t b = 0; b < slot.binders.size(); ++b) env[b] = slot.binders[b];
      emit(0, env);
      i = slot.end; ++slot_index;
    } else {
      remap[i] = ast.nodes.size(); ast.nodes.push_back(layout.prototype.nodes[i]); ++i;
    }
  }
  for (const auto& fuel : layout.prototype.fuel_specs)
    if (remap.at(fuel.node_index) != std::size_t(-1)) ast.fuel_specs.push_back({remap[fuel.node_index],fuel.charges});
  for (auto& region : fixed_regions) {
    phase_layout_require(remap.at(region.node_index) != std::size_t(-1), "region overwritten by phase");
    region.node_index = remap[region.node_index];
  }
  phase_layout_require(fixed_regions.empty() || phase_regions.empty(), "bounded region inside a region phase");
  ast.bounded_region_specs = fixed_regions.empty() ? std::move(phase_regions) : std::move(fixed_regions);
  // Compaction is only serialization cleanup; the independent caller audits
  // this output before treating it as an imported executable/member.
  return compact_genome_tables(std::move(out));
}
// Whole-program mode: replace the main code with lowered phase code plus the
// prototype terminator, and the locals with the phase compiler's allocation.
template<class Code>
inline void patch_whole_program(BytecodeProgram& program, const NativePhaseLayout& layout,
    const Code* code, int code_count, const Value* constants, int constant_count, const PhaseCompileResult& lowered) {
  phase_layout_require(layout.whole_program, "whole-program patch on a region layout");
  const auto instruction = [](const Code& x) {
    return Instr{static_cast<Opcode>(x.op), x.a, x.b, bool(x.flags & 1), bool(x.flags & 2)};
  };
  // The native compiler leaves an all-unit charge table empty unless the
  // program lowered a lexical binder or checked conversion.
  const auto settle_fuel = [](std::vector<std::uint32_t>& fuel, bool general) {
    if (!general && std::all_of(fuel.begin(), fuel.end(), [](std::uint32_t f) { return f == 1; })) fuel.clear();
  };
  const int main_code = lowered.region_count ? lowered.main_code_count : code_count;
  const int main_constants = lowered.region_count ? lowered.main_constant_count : constant_count;
  phase_layout_require(main_code >= 0 && main_code <= code_count && main_constants >= 0 &&
      main_constants <= constant_count, "whole-program region records");
  program.code.clear(); program.instruction_fuel.clear();
  program.code.reserve(main_code + 1); program.instruction_fuel.reserve(main_code + 1);
  for (int i = 0; i < main_code; ++i) {
    program.code.push_back(instruction(code[i])); program.instruction_fuel.push_back(code[i].fuel);
  }
  if (layout.program_terminator) {
    program.code.push_back(layout.program_return); program.instruction_fuel.push_back(layout.program_return_fuel);
  }
  settle_fuel(program.instruction_fuel, lowered.general_regions);
  program.consts.assign(constants, constants + main_constants);
  // Named locals enter the native variable map on first use, after the inputs.
  program.n_locals = lowered.local_count;
  program.var2idx.clear();
  for (std::size_t i = 0; i < layout.input_names.size(); ++i) program.var2idx[layout.input_names[i]] = int(i);
  for (int g = 0; g < kPhaseNamedLocals; ++g)
    if (lowered.named_slots[g]) program.var2idx[layout.local_names.at(g)] = lowered.named_slots[g] - 1;
  // Region records (see PhaseCompileResult) become the native segments, in the
  // native compiler's completion order.
  program.bounded_region_segments.clear();
  int cursor = main_code, value = main_constants;
  const auto next = [&]() -> const Code& {
    phase_layout_require(cursor < code_count, "whole-program region records");
    return code[cursor++];
  };
  for (int r = 0; r < lowered.region_count; ++r) {
    const auto& header = next();
    phase_layout_require(header.op == kPhaseRegionRecord && header.a >= 0 &&
        std::size_t(header.a) < layout.profiles.at(0).region_sources.size(), "whole-program region record");
    const auto& source = layout.profiles[0].region_sources[header.a];
    phase_layout_require(std::size_t(header.b) == source.sources.size() &&
        header.fuel == unsigned(layout.profiles[0].regions[header.a].captures), "whole-program region record");
    BoundedRegionSegment segment; segment.plan = source.plan;
    for (unsigned c = 0; c < header.fuel; ++c) segment.parameter_locals.push_back(next().a);
    for (std::size_t p = 0; p < source.sources.size(); ++p) {
      const auto& phase_header = next();
      phase_layout_require(phase_header.op == kPhaseRegionRecord && phase_header.a >= 0 && phase_header.b >= 0 &&
          cursor + phase_header.a <= code_count && value + phase_header.b <= constant_count, "whole-program region phase");
      RegionPhase phase;
      for (int i = 0; i < phase_header.a; ++i) {
        phase.program.code.push_back(instruction(code[cursor + i])); phase.program.instruction_fuel.push_back(code[cursor + i].fuel);
      }
      settle_fuel(phase.program.instruction_fuel, phase_header.fuel >> 16 & 1U);
      phase.program.consts.assign(constants + value, constants + value + phase_header.b);
      phase.program.n_locals = int(phase_header.fuel & 0xffffU);
      for (std::size_t b = 0; b < source.sources[p].size(); ++b) phase.bindings.push_back({source.sources[p][b], int(b)});
      cursor += phase_header.a; value += phase_header.b;
      switch (bounded_region_phase_kind(segment.plan, p)) {
        case RegionPhaseKind::BasePredicate: segment.base_predicate = std::move(phase); break;
        case RegionPhaseKind::BaseBody: segment.base_body = std::move(phase); break;
        case RegionPhaseKind::Preparation: segment.preparations.push_back(std::move(phase)); break;
        case RegionPhaseKind::Request: segment.request_expressions.push_back(std::move(phase)); break;
        case RegionPhaseKind::Combine: segment.combine = std::move(phase); break;
        case RegionPhaseKind::Boundary: segment.boundary = std::move(phase); break;
      }
    }
    const auto verified = verify_bounded_region_segment(segment, program.n_locals);
    phase_layout_require(bool(verified), "lowered bounded region segment");
    program.bounded_region_segments.push_back(std::move(segment));
  }
  phase_layout_require(cursor == code_count && value == constant_count, "whole-program region records");
}
// Mutable phase of a native layout slot inside an executable region segment.
inline RegionPhase& native_phase_at(BoundedRegionSegment& segment,std::size_t ordinal) {
 switch(bounded_region_phase_kind(segment.plan,ordinal)) {
  case RegionPhaseKind::BasePredicate:return segment.base_predicate;
  case RegionPhaseKind::BaseBody:return segment.base_body;
  case RegionPhaseKind::Combine:return segment.combine;
  case RegionPhaseKind::Boundary:return *segment.boundary;
  case RegionPhaseKind::Preparation:return segment.preparations.at(bounded_region_preparation_ordinal(segment.plan,ordinal));
  case RegionPhaseKind::Request:return segment.request_expressions.at(ordinal-2-segment.preparations.size());
 }
 throw std::logic_error("GPU genotype phase ordinal");
}
} // namespace gagp::evo::repro
