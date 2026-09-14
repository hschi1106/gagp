#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/random.hpp"
#include "gagp/evolution/grammar/budget.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/grammar/values.hpp"
#include "gagp/evolution/grammar/membership.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>

namespace gagp::evo::grammar {
namespace {
class Generator {
 public:
  Generator(const CompiledGrammar& grammar, std::uint64_t seed, const GenerationRequest& request)
      : grammar_(grammar), random_(seed) {
    out_.derivation.request = request;
    out_.derivation.request_scope_mapping = validate_request(grammar, request);
    out_.derivation.grammar_hash = grammar.content_hash(); out_.derivation.seed = seed;
    out_.derivation.search_limits = request.budget; out_.derivation.execution_limits = grammar.execution_limits();
    for (const auto& input : grammar.inputs()) out_.genome.ast.names.push_back(input.name);
    for (const auto& local : grammar.locals()) out_.genome.ast.names.push_back(local.name);
  }
  GeneratedDerivation run() {
    grammar_.require_executable(out_.derivation.request.nonterminal);
    const auto& entry = grammar_.nonterminals()[out_.derivation.request.nonterminal];
    const bool wrap = entry.category == NodeCategory::Expression;
    auto depth = out_.derivation.request.budget.max_depth;
    auto budget = out_.derivation.request.budget.max_nodes;
    if (wrap) {
      if (depth <= 3 || budget <= 4)
        throw std::invalid_argument("expression entry needs four envelope nodes and three prefix levels in addition to its derivation");
      depth -= 3; budget -= 4;
    }
    if (wrap) {
      emit({NodeKind::PROGRAM, 0, 0}, {}); emit({NodeKind::BLOCK_CONS, 0, 0}, {});
      emit({NodeKind::RETURN, 0, 0}, {});
    }
    const auto start = out_.genome.ast.nodes.size();
    derive(out_.derivation.request.nonterminal, depth, budget);
    out_.derivation.derived_nodes = static_cast<std::uint32_t>(out_.genome.ast.nodes.size() - start);
    if (wrap) emit({NodeKind::BLOCK_NIL, 0, 0}, {});
    std::vector<InputSpec> inputs;
    for (const auto& input : grammar_.inputs()) inputs.push_back({input.name, input.type});
    const auto verified = verify_ast(out_.genome.ast, inputs);
    if (!verified) throw std::invalid_argument("generated derivation failed native verification: " + verified.diagnostic.message);
    if (verified.verified.return_type != entry.type) throw std::logic_error("generated return type violates grammar entry");
    require_membership(grammar_, out_.genome, out_.derivation.request);
    std::vector<std::string> input_names;
    for (const auto& input : inputs) input_names.push_back(input.name);
    const auto lowered = compile_for_eval(out_.genome, verified.verified, input_names);
    if (!lowered.asgp_dc_segments.empty() || !lowered.asgp_dp1d_segments.empty() || !lowered.asgp_dp2d_segments.empty())
      throw std::logic_error("typed grammar unexpectedly lowered to specialized segments");
    if (lowered.code.size() > kGrammarMaxLoweredInstructions)
      throw std::invalid_argument("generated program exceeds 1048576 lowered instructions");
    out_.derivation.lowered_instructions = static_cast<std::uint32_t>(lowered.code.size());
    out_.genome.meta = build_genome_meta(out_.genome.ast);
    out_.genome.derivation = std::make_shared<const DerivationMetadata>(out_.derivation);
    return std::move(out_);
  }

 private:
  struct Slot {
    std::uint32_t expression = kNoGrammarId;
    std::uint32_t depth = kNoGrammarId;
    std::uint32_t count = 0;
    std::uint32_t reserved = kNoGrammarId;
    std::uint32_t begin = 0;
    std::uint32_t choice_begin = 0;
    std::vector<AstNode> nodes;
    std::vector<NodeOrigin> origins;
    std::vector<DerivationChoice> choices;
    std::vector<HoleOccurrence> holes;
  };
  struct TemplatePlan {
    std::uint32_t template_id;
    std::uint32_t instance;
    std::vector<Slot> slots;
  };
  TemplatePlan* plan(std::uint32_t id) const {
    for (auto i = plans_.rbegin(); i != plans_.rend(); ++i)
      if ((*i)->template_id == id) return i->get();
    return nullptr;
  }
  void gather(std::uint32_t id, std::uint32_t depth, TemplatePlan& owner) {
    const auto& node = grammar_.expressions()[id];
    if (node.kind == ExpressionKind::Reference) return;
    if (node.kind == ExpressionKind::Hole && node.template_id == owner.template_id) {
      auto& slot = owner.slots.at(node.target);
      if (node.children.empty()) throw std::logic_error("unfilled materialized template hole");
      slot.expression = node.children[0]; slot.depth = std::min(slot.depth, depth); ++slot.count;
      return;
    }
    const bool wrapper = node.kind == ExpressionKind::Template || node.kind == ExpressionKind::Hole;
    if (!wrapper && !depth) throw std::invalid_argument("template skeleton exceeds depth budget");
    for (auto child : node.children) gather(child, wrapper ? depth : depth - 1, owner);
  }
  std::uint32_t minimum(std::uint32_t id, std::uint32_t depth) const {
    return minimum_expression_nodes(grammar_, id, depth,
        [&](std::uint32_t owner_id, std::uint32_t slot_id) -> std::optional<std::uint32_t> {
          const auto* owner = plan(owner_id);
          if (!owner || owner->slots.at(slot_id).reserved == kNoGrammarId) return std::nullopt;
          return owner->slots[slot_id].reserved;
        });
  }
  void instantiate(std::uint32_t id, std::uint32_t depth, std::uint32_t budget,
      std::uint32_t production, std::uint32_t nt) {
    const auto& source = grammar_.expressions()[id];
    if (plans_.size() >= 256) throw std::invalid_argument("template nesting exceeds 256 instances");
    auto owner = std::make_unique<TemplatePlan>();
    owner->template_id = source.target;
    owner->instance = static_cast<std::uint32_t>(out_.derivation.templates.size());
    out_.derivation.templates.push_back({source.target, plans_.empty() ? kNoGrammarId : plans_.back()->instance});
    owner->slots.resize(grammar_.templates()[source.target].holes.size());
    gather(source.children.at(0), depth, *owner);
    auto* current = owner.get(); plans_.push_back(std::move(owner));
    for (auto& slot : current->slots) {
      if (!slot.count) throw std::logic_error("compiled template lost a hole occurrence");
      slot.reserved = minimum(slot.expression, slot.depth);
    }
    for (auto& slot : current->slots) {
      const auto baseline = minimum(source.children[0], depth);
      if (baseline > budget) throw std::invalid_argument("shared template hole cannot fit its joint occurrence budgets");
      const auto maximum = slot.reserved + (budget - baseline) / slot.count;
      slot.reserved = static_cast<std::uint32_t>(random_.integer(slot.reserved, maximum));
    }
    expression(source.children[0], depth, budget, production, nt);
    plans_.pop_back();
  }
  void hole(std::uint32_t id, std::uint32_t depth, std::uint32_t budget,
      std::uint32_t production, std::uint32_t nt) {
    const auto& source = grammar_.expressions()[id];
    auto* owner = plan(source.template_id);
    if (!owner) throw std::logic_error("template hole has no active instance");
    auto& slot = owner->slots.at(source.target);
    if (slot.reserved > budget || slot.depth > depth) throw std::logic_error("template reservation exceeds occurrence budget");
    const auto begin = static_cast<std::uint32_t>(out_.genome.ast.nodes.size());
    if (slot.nodes.empty()) {
      const auto saved_slot = active_slot_;
      active_slot_ = {owner->instance, source.target};
      slot.begin = begin; slot.choice_begin = static_cast<std::uint32_t>(out_.derivation.choices.size());
      const auto hole_begin = out_.derivation.holes.size();
      expression(source.children.at(0), slot.depth, slot.reserved, production, nt);
      active_slot_ = saved_slot;
      slot.nodes.assign(out_.genome.ast.nodes.begin() + begin, out_.genome.ast.nodes.end());
      slot.origins.assign(out_.derivation.nodes.begin() + begin, out_.derivation.nodes.end());
      slot.choices.assign(out_.derivation.choices.begin() + slot.choice_begin, out_.derivation.choices.end());
      slot.holes.assign(out_.derivation.holes.begin() + hole_begin, out_.derivation.holes.end());
      slot.reserved = static_cast<std::uint32_t>(slot.nodes.size());
    } else {
      const auto shift = begin - slot.begin;
      const auto choice_begin = static_cast<std::uint32_t>(out_.derivation.choices.size());
      out_.genome.ast.nodes.insert(out_.genome.ast.nodes.end(), slot.nodes.begin(), slot.nodes.end());
      out_.derivation.nodes.insert(out_.derivation.nodes.end(), slot.origins.begin(), slot.origins.end());
      for (auto choice : slot.choices) {
        choice.parent = choice.parent >= slot.choice_begin && choice.parent < slot.choice_begin + slot.choices.size() ?
            choice_begin + choice.parent - slot.choice_begin : current_choice_;
        choice.ast_begin += shift; choice.ast_end += shift;
        out_.derivation.choices.push_back(choice);
      }
      for (auto occurrence : slot.holes) {
        occurrence.ast_begin += shift; occurrence.ast_end += shift;
        out_.derivation.holes.push_back(occurrence);
      }
    }
    out_.derivation.holes.push_back({owner->instance, source.target, begin, static_cast<std::uint32_t>(out_.genome.ast.nodes.size())});
  }
  void emit(AstNode node, NodeOrigin origin) {
    out_.genome.ast.nodes.push_back(node); out_.derivation.nodes.push_back(origin);
  }
  void step() {
    if (++out_.derivation.logical_steps > 1048576)
      throw std::invalid_argument("grammar derivation exceeded 1048576 logical steps; reduce alias recursion or raise terminal weights");
  }
  void derive(std::uint32_t nt, std::uint32_t depth, std::uint32_t budget) {
    const auto saved_parent = current_choice_;
    const auto saved_aliases = alias_frames_.size();
    std::vector<bool> aliases(grammar_.nonterminals().size(), false);
    for (const auto& frame : alias_frames_)
      if (frame.second == out_.genome.ast.nodes.size()) aliases[frame.first] = true;
    std::vector<std::uint32_t> choices;
    for (;;) {
      step(); aliases[nt] = true;
      alias_frames_.push_back({nt, out_.genome.ast.nodes.size()});
      std::vector<std::uint32_t> eligible;
      for (auto id : grammar_.nonterminals()[nt].productions) {
        const auto& production = grammar_.productions()[id];
        if (production.minimum_nodes_by_depth.at(depth) <= budget &&
            feasible_alias_exit(production.expression, depth, budget, aliases)) eligible.push_back(id);
      }
      const auto production = random_.production(grammar_, eligible);
      const auto selected = static_cast<std::uint32_t>(out_.derivation.choices.size());
      out_.derivation.choices.push_back({nt, production, current_choice_, static_cast<std::uint32_t>(out_.genome.ast.nodes.size()), 0});
      choices.push_back(selected); current_choice_ = selected;
      const auto root = grammar_.productions()[production].expression;
      const auto& node = grammar_.expressions()[root];
      if (node.kind == ExpressionKind::Reference) { nt = node.target; continue; }
      expression(root, depth, budget, production, nt);
      break;
    }
    for (auto choice : choices) out_.derivation.choices[choice].ast_end = static_cast<std::uint32_t>(out_.genome.ast.nodes.size());
    current_choice_ = saved_parent;
    alias_frames_.resize(saved_aliases);
  }
  bool feasible_alias_exit(std::uint32_t root, std::uint32_t depth, std::uint32_t budget,
      const std::vector<bool>& blocked) const {
    std::vector<std::uint32_t> pending{root};
    auto visited = blocked;
    while (!pending.empty()) {
      const auto id = pending.back(); pending.pop_back();
      const auto& node = grammar_.expressions()[id];
      if (node.kind == ExpressionKind::Template || node.kind == ExpressionKind::Hole) {
        pending.insert(pending.end(), node.children.begin(), node.children.end());
      } else if (node.kind != ExpressionKind::Reference) {
        if (minimum(id, depth) <= budget) return true;
      } else if (!visited[node.target]) {
        visited[node.target] = true;
        for (auto production : grammar_.nonterminals()[node.target].productions)
          pending.push_back(grammar_.productions()[production].expression);
      }
    }
    return false;
  }
  Value constant(const ConstantDomain& domain) {
    if (domain.integer_range) return Value::from_int(random_.integer(domain.minimum, domain.maximum));
    const auto& data = domain.values[static_cast<std::size_t>(random_.bounded(domain.values.size()))];
    return materialize_constant(domain.type, data);
  }
  void expression(std::uint32_t id, std::uint32_t depth, std::uint32_t budget,
      std::uint32_t production, std::uint32_t nt) {
    step();
    const auto& source = grammar_.expressions()[id];
    if (source.kind == ExpressionKind::Reference) { derive(source.target, depth, budget); return; }
    if (source.kind == ExpressionKind::Template) { instantiate(id, depth, budget, production, nt); return; }
    if (source.kind == ExpressionKind::Hole) { hole(id, depth, budget, production, nt); return; }
    if (!depth || !budget) throw std::logic_error("infeasible grammar child budget");
    NodeOrigin origin{id, production, nt, out_.derivation.logical_steps, kNoGrammarId, kNoGrammarId};
    origin.fixed = source.fixed;
    if (source.fixed) {
      const auto* owner = plan(source.template_id);
      if (owner) origin.template_instance = owner->instance;
    } else { origin.template_instance = active_slot_.first; origin.slot = active_slot_.second; }
    AstNode node;
    if (source.kind == ExpressionKind::Constant) {
      node = {NodeKind::CONST, static_cast<int>(out_.genome.ast.consts.size()), 0};
      out_.genome.ast.consts.push_back(constant(grammar_.constants()[source.target]));
    } else if (source.kind == ExpressionKind::Input) node = {NodeKind::VAR, static_cast<int>(source.target), 0};
    else if (source.kind == ExpressionKind::Local)
      node = {NodeKind::VAR, static_cast<int>(grammar_.inputs().size() + source.target), 0};
    else if (source.kind == ExpressionKind::Primitive) {
      const auto& signature = PrimitiveCatalog::standard().at(source.target);
      node = {*signature.lowering_node, 0, 0};
    } else if (source.kind == ExpressionKind::Control) {
      const auto& signature = PrimitiveCatalog::standard().control_signatures()[source.target];
      node = {signature.lowering_node, signature.requires_name ? static_cast<int>(grammar_.inputs().size() + source.local) : 0, 0};
    } else throw std::invalid_argument("materialization requires an implemented native node contract");
    emit(node, origin);
    std::uint32_t remaining = budget - 1;
    for (std::size_t i = 0; i < source.children.size(); ++i) {
      std::uint64_t reserved = 0;
      for (std::size_t j = i + 1; j < source.children.size(); ++j) reserved += minimum(source.children[j], depth - 1);
      const auto lower = minimum(source.children[i], depth - 1);
      if (reserved > remaining || lower > remaining - reserved) throw std::logic_error("compiled minimum cost is inconsistent");
      const auto allowance = static_cast<std::uint32_t>(random_.integer(lower, remaining - reserved));
      const auto begin = out_.genome.ast.nodes.size();
      expression(source.children[i], depth - 1, allowance, production, nt);
      remaining -= static_cast<std::uint32_t>(out_.genome.ast.nodes.size() - begin);
    }
  }
  const CompiledGrammar& grammar_;
  GrammarRandom random_;
  GeneratedDerivation out_;
  std::uint32_t current_choice_ = kNoGrammarId;
  std::vector<std::pair<std::uint32_t, std::size_t>> alias_frames_;
  std::vector<std::unique_ptr<TemplatePlan>> plans_;
  std::pair<std::uint32_t, std::uint32_t> active_slot_{kNoGrammarId, kNoGrammarId};
};
}  // namespace

GeneratedDerivation generate_derivation(const CompiledGrammar& grammar, std::uint64_t seed) {
  return generate_derivation(grammar, seed, entry_request(grammar));
}

GeneratedDerivation generate_derivation(const CompiledGrammar& grammar, std::uint64_t seed,
    const GenerationRequest& request) {
  return Generator(grammar, seed, request).run();
}

}  // namespace gagp::evo::grammar
