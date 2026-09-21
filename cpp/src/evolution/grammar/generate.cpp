#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/random.hpp"
#include "gagp/evolution/grammar/budget.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/core/semantic_fuel.hpp"
#include "gagp/evolution/grammar/values.hpp"
#include "gagp/evolution/grammar/membership.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace gagp::evo::grammar {
namespace {
class Generator {
 public:
  Generator(const CompiledGrammar& grammar, std::uint64_t seed, const GenerationRequest& request,
      const GenerationFrame* frame = nullptr)
      : grammar_(grammar), random_(seed), frame_(frame) {
    out_.derivation.request = request;
    out_.derivation.request_scope_mapping = validate_request(grammar, request);
    environment_.assign(grammar.nonterminals()[request.nonterminal].scope.size(), -1);
    out_.derivation.grammar_hash = grammar.content_hash(); out_.derivation.seed = seed;
    out_.derivation.search_limits = request.budget; out_.derivation.execution_limits = grammar.execution_limits();
    for (const auto& input : grammar.inputs()) out_.genome.ast.names.push_back(input.name);
    for (const auto& local : grammar.locals()) out_.genome.ast.names.push_back(local.name);
    if (frame_) {
      environment_ = frame_environment(grammar, request, *frame_);
      reserved_binder_ids_.insert(frame_->binder_ids.begin(), frame_->binder_ids.end());
    }
  }
  GeneratedDerivation run() {
    grammar_.require_executable(out_.derivation.request.nonterminal);
    const auto& entry = grammar_.nonterminals()[out_.derivation.request.nonterminal];
    const bool wrap = entry.category == NodeCategory::Expression;
    if (frame_ && wrap) prepare_frame_costs();
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
    if (frame_) {
      // A donor frame supplies explicit values for destination locals. Its witness
      // certifies that context, not ordinary grammar-input-only seed replay.
      out_.derivation = reconstruct_derivation_in_frame(grammar_, out_.genome,
          out_.derivation.request, *frame_);
      out_.genome.meta = build_genome_meta(out_.genome.ast);
      out_.genome.derivation = std::make_shared<const DerivationMetadata>(out_.derivation);
      return std::move(out_);
    }
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
    const auto lowered_instructions = bytecode_instruction_count(lowered);
    if (lowered_instructions > kGrammarMaxLoweredInstructions)
      throw std::invalid_argument("generated program exceeds 1048576 lowered instructions");
    out_.derivation.lowered_instructions =
        static_cast<std::uint32_t>(lowered_instructions);
    out_.genome.meta = build_genome_meta(out_.genome.ast);
    out_.genome.derivation = std::make_shared<const DerivationMetadata>(out_.derivation);
    return std::move(out_);
  }

 private:
  std::uint32_t contextual_minimum(std::uint32_t id, std::uint32_t depth,
      const HoleBudgetLookup& enclosing = {}) const {
    if (frame_costs_.empty()) return minimum_expression_nodes(grammar_, id, depth, enclosing);
    return minimum_expression_nodes(grammar_, id, depth, enclosing,
        [&](std::uint32_t nt, std::uint32_t d) { return frame_costs_.at(nt).at(d); },
        [&](const CompiledExpression& expression, std::uint32_t) -> std::optional<std::uint32_t> {
          if (expression.kind == ExpressionKind::Local && !available_locals_.at(expression.target))
            return kNoGrammarId;
          return std::nullopt;
        });
  }
  void prepare_frame_costs() {
    available_locals_.assign(grammar_.locals().size(), false);
    for (std::size_t i = 0; i < grammar_.locals().size(); ++i)
      available_locals_[i] = std::any_of(frame_->locals.begin(), frame_->locals.end(), [&](const auto& binding) {
        return binding.name == grammar_.locals()[i].name;
      });
    const auto depth_limit = out_.derivation.request.budget.max_depth - 3;
    frame_costs_.assign(grammar_.nonterminals().size(),
        std::vector<std::uint32_t>(depth_limit + 1, kNoGrammarId));
    // Local availability is immutable throughout an expression donor. Compute the
    // same least fixed point as compilation, excluding unavailable local leaves.
    // Structural Program donors retain their own normal assignment/dataflow rules.
    for (std::uint32_t depth = 0; depth <= depth_limit; ++depth) {
      bool changed;
      do {
        changed = false;
        for (const auto& nt : grammar_.nonterminals()) {
          if (nt.category != NodeCategory::Expression) continue;
          auto best = frame_costs_[nt.id][depth];
          for (auto production : nt.productions)
            best = std::min(best, contextual_minimum(grammar_.productions()[production].expression, depth));
          if (best < frame_costs_[nt.id][depth]) {
            frame_costs_[nt.id][depth] = best;
            changed = true;
          }
        }
      } while (changed);
    }
    if (frame_costs_[out_.derivation.request.nonterminal][depth_limit] >
        out_.derivation.request.budget.max_nodes - 4)
      throw std::invalid_argument("no grammar derivation fits the donor budget and available local frame");
  }
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
    std::vector<LexicalRegion> lexical_regions;
    std::vector<TraversalSpec> traversal_specs;
    std::vector<NodeFuelSpec> fuel_specs;
    std::vector<BoundedRegionSpec> bounded_region_specs;
    std::vector<int> environment;
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
  std::vector<int> project(const std::vector<std::uint32_t>& mapping) const {
    std::vector<int> projected;
    projected.reserve(mapping.size());
    for (auto position : mapping) {
      if (position >= environment_.size())
        throw std::logic_error("compiled lexical scope mapping exceeds the current environment");
      projected.push_back(environment_[position]);
    }
    return projected;
  }
  int fresh_binder() {
    while (reserved_binder_ids_.count(next_binder_id_)) ++next_binder_id_;
    if (next_binder_id_ == std::numeric_limits<int>::max())
      throw std::overflow_error("generated AST exhausted public lexical binder IDs");
    return next_binder_id_++;
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
    return contextual_minimum(id, depth,
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
    const auto saved_environment = environment_;
    environment_ = project(source.scope_mapping);
    expression(source.children[0], depth, budget, production, nt);
    environment_ = saved_environment;
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
    const auto saved_environment = environment_;
    environment_ = project(source.scope_mapping);
    if (slot.nodes.empty()) {
      const auto saved_slot = active_slot_;
      active_slot_ = {owner->instance, source.target};
      slot.begin = begin; slot.choice_begin = static_cast<std::uint32_t>(out_.derivation.choices.size());
      const auto hole_begin = out_.derivation.holes.size();
      const auto region_begin = out_.genome.ast.lexical_regions.size();
      const auto traversal_begin = out_.genome.ast.traversal_specs.size();
      const auto fuel_begin = out_.genome.ast.fuel_specs.size();
      const auto bounded_begin = out_.genome.ast.bounded_region_specs.size();
      slot.environment = environment_;
      expression(source.children.at(0), slot.depth, slot.reserved, production, nt);
      active_slot_ = saved_slot;
      slot.nodes.assign(out_.genome.ast.nodes.begin() + begin, out_.genome.ast.nodes.end());
      slot.origins.assign(out_.derivation.nodes.begin() + begin, out_.derivation.nodes.end());
      slot.choices.assign(out_.derivation.choices.begin() + slot.choice_begin, out_.derivation.choices.end());
      slot.holes.assign(out_.derivation.holes.begin() + hole_begin, out_.derivation.holes.end());
      slot.lexical_regions.assign(out_.genome.ast.lexical_regions.begin() + region_begin,
          out_.genome.ast.lexical_regions.end());
      slot.traversal_specs.assign(out_.genome.ast.traversal_specs.begin() + traversal_begin,
          out_.genome.ast.traversal_specs.end());
      slot.fuel_specs.assign(out_.genome.ast.fuel_specs.begin() + fuel_begin,
          out_.genome.ast.fuel_specs.end());
      slot.bounded_region_specs.assign(
          out_.genome.ast.bounded_region_specs.begin() + bounded_begin,
          out_.genome.ast.bounded_region_specs.end());
      slot.reserved = static_cast<std::uint32_t>(slot.nodes.size());
    } else {
      const auto shift = begin - slot.begin;
      const auto choice_begin = static_cast<std::uint32_t>(out_.derivation.choices.size());
      std::unordered_map<int, int> binder_renames;
      for (const auto& region : slot.lexical_regions)
        for (const auto& binding : region.bindings)
          binder_renames.emplace(binding.id, fresh_binder());
      for (const auto& spec : slot.bounded_region_specs)
        for (const auto& phase : spec.phases)
          for (const auto& binding : phase.bindings)
            binder_renames.emplace(binding.binder_id, fresh_binder());
      auto copied_nodes = slot.nodes;
      for (auto& node : copied_nodes) {
        if (node.kind != NodeKind::REGION_VAR) continue;
        const auto introduced = binder_renames.find(node.i0);
        if (introduced != binder_renames.end()) {
          node.i0 = introduced->second;
          continue;
        }
        const auto free = std::find(slot.environment.begin(), slot.environment.end(), node.i0);
        if (free == slot.environment.end())
          throw std::logic_error("template hole snapshot contains an untracked free lexical binding");
        const auto position = static_cast<std::size_t>(free - slot.environment.begin());
        if (position >= environment_.size() || environment_[position] < 0)
          throw std::invalid_argument("general binding runtime: external lexical binding requires a materialization frame");
        node.i0 = environment_[position];
      }
      out_.genome.ast.nodes.insert(out_.genome.ast.nodes.end(), copied_nodes.begin(), copied_nodes.end());
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
      for (auto region : slot.lexical_regions) {
        region.node_index += shift;
        for (auto& binding : region.bindings) binding.id = binder_renames.at(binding.id);
        out_.genome.ast.lexical_regions.push_back(std::move(region));
      }
      for (auto traversal : slot.traversal_specs) {
        traversal.node_index += shift;
        out_.genome.ast.traversal_specs.push_back(traversal);
      }
      for (auto fuel : slot.fuel_specs) {
        fuel.node_index += shift;
        out_.genome.ast.fuel_specs.push_back(std::move(fuel));
      }
      for (auto spec : slot.bounded_region_specs) {
        spec.node_index += shift;
        for (auto& capture : spec.parameters) {
          if (capture.kind != RegionCaptureKind::Lexical) continue;
          const auto introduced = binder_renames.find(capture.index);
          if (introduced != binder_renames.end()) {
            capture.index = introduced->second;
            continue;
          }
          const auto free = std::find(slot.environment.begin(),
                                      slot.environment.end(), capture.index);
          if (free == slot.environment.end())
            throw std::logic_error("template hole snapshot contains an untracked lexical capture");
          const auto position = static_cast<std::size_t>(free - slot.environment.begin());
          if (position >= environment_.size() || environment_[position] < 0)
            throw std::invalid_argument("general binding runtime: external lexical binding requires a materialization frame");
          capture.index = environment_[position];
        }
        for (auto& phase : spec.phases)
          for (auto& binding : phase.bindings)
            binding.binder_id = binder_renames.at(binding.binder_id);
        out_.genome.ast.bounded_region_specs.push_back(std::move(spec));
      }
    }
    environment_ = saved_environment;
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
    const auto saved_environment = environment_;
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
        const auto required = frame_costs_.empty() ? production.minimum_nodes_by_depth.at(depth) :
            contextual_minimum(production.expression, depth);
        if (required <= budget &&
            feasible_alias_exit(production.expression, depth, budget, aliases)) eligible.push_back(id);
      }
      const auto production = random_.production(grammar_, eligible);
      const auto selected = static_cast<std::uint32_t>(out_.derivation.choices.size());
      out_.derivation.choices.push_back({nt, production, current_choice_, static_cast<std::uint32_t>(out_.genome.ast.nodes.size()), 0});
      choices.push_back(selected); current_choice_ = selected;
      const auto root = grammar_.productions()[production].expression;
      const auto& node = grammar_.expressions()[root];
      if (node.kind == ExpressionKind::Reference) {
        environment_ = project(node.scope_mapping);
        nt = node.target;
        continue;
      }
      expression(root, depth, budget, production, nt);
      break;
    }
    for (auto choice : choices) out_.derivation.choices[choice].ast_end = static_cast<std::uint32_t>(out_.genome.ast.nodes.size());
    current_choice_ = saved_parent;
    alias_frames_.resize(saved_aliases);
    environment_ = saved_environment;
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
    if (source.kind == ExpressionKind::Reference) {
      const auto saved_environment = environment_;
      environment_ = project(source.scope_mapping);
      derive(source.target, depth, budget);
      environment_ = saved_environment;
      return;
    }
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
    std::optional<BoundedRegionSpec> bounded_region;
    if (source.kind == ExpressionKind::Constant) {
      node = {NodeKind::CONST, static_cast<int>(out_.genome.ast.consts.size()), 0};
      out_.genome.ast.consts.push_back(constant(grammar_.constants()[source.target]));
    } else if (source.kind == ExpressionKind::Input) node = {NodeKind::VAR, static_cast<int>(source.target), 0};
    else if (source.kind == ExpressionKind::Local)
      node = {NodeKind::VAR, static_cast<int>(grammar_.inputs().size() + source.target), 0};
    else if (source.kind == ExpressionKind::Bound) {
      if (source.target >= environment_.size())
        throw std::logic_error("compiled bound position exceeds the current lexical environment");
      if (environment_[source.target] < 0)
        throw std::invalid_argument("general binding runtime: external lexical binding requires a materialization frame");
      node = {NodeKind::REGION_VAR, environment_[source.target], 0};
    }
    else if (source.kind == ExpressionKind::Structured) {
      const auto& contract = grammar_.structured_contracts().at(source.target);
      if (contract.family != StructuredFamily::BoundedRegion || !contract.plan)
        throw std::invalid_argument("materialization requires an implemented native structured contract");
      if (source.children.size() != bounded_region_arity(*contract.plan))
        throw std::logic_error("compiled bounded region child layout is inconsistent");
      if (source.children.size() >
          static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::overflow_error("structured node arity exceeds the native index range");
      node = {NodeKind::BOUNDED_REGION,
              static_cast<int>(source.children.size()), 0};
      bounded_region = BoundedRegionSpec{};
      bounded_region->plan = *contract.plan;
      if (source.captures.size() != contract.plan->parameter_types.size())
        throw std::logic_error("compiled bounded region capture layout is inconsistent");
      bounded_region->parameters.reserve(source.captures.size());
      for (const CompiledRegionCapture& capture : source.captures) {
        switch (capture.kind) {
          case CompiledCaptureKind::Input:
            if (capture.target >= grammar_.inputs().size())
              throw std::logic_error("compiled region input capture is out of range");
            bounded_region->parameters.push_back(
                {RegionCaptureKind::Name, static_cast<int>(capture.target)});
            break;
          case CompiledCaptureKind::Local:
            if (capture.target >= grammar_.locals().size())
              throw std::logic_error("compiled region local capture is out of range");
            if (grammar_.inputs().size() + capture.target >
                static_cast<std::size_t>(std::numeric_limits<int>::max()))
              throw std::overflow_error("compiled region local capture exceeds the native name range");
            bounded_region->parameters.push_back({
                RegionCaptureKind::Name,
                static_cast<int>(grammar_.inputs().size() + capture.target)});
            break;
          case CompiledCaptureKind::Bound:
            if (capture.target >= environment_.size())
              throw std::logic_error("compiled region bound capture is out of range");
            if (environment_[capture.target] < 0)
              throw std::invalid_argument("general binding runtime: external lexical binding requires a materialization frame");
            bounded_region->parameters.push_back(
                {RegionCaptureKind::Lexical, environment_[capture.target]});
            break;
          default:
            throw std::logic_error("compiled region capture kind is invalid");
        }
      }
      if (source.phases.size() != source.regions.size())
        throw std::logic_error("compiled region phases and lexical regions are not aligned");
      bounded_region->phases.reserve(source.phases.size());
      for (std::size_t phase_index = 0; phase_index < source.phases.size();
           ++phase_index) {
        const CompiledRegionPhase& phase = source.phases[phase_index];
        const RegionSlot& region = source.regions[phase_index];
        if (phase.argument != region.argument ||
            phase.sources.size() != region.bindings.size())
          throw std::logic_error("compiled region phase binding layout is inconsistent");
        RegionAstPhase materialized;
        materialized.argument = phase.argument;
        for (std::size_t binding = 0; binding < phase.sources.size(); ++binding)
          materialized.bindings.push_back(
              {phase.sources[binding], fresh_binder()});
        bounded_region->phases.push_back(std::move(materialized));
      }
    }
    else if (source.kind == ExpressionKind::Primitive) {
      const auto& signature = PrimitiveCatalog::standard().at(source.target);
      node = {*signature.lowering_node, 0, 0};
    } else if (source.kind == ExpressionKind::Control) {
      const auto& signature = PrimitiveCatalog::standard().control_signatures()[source.target];
      node = {signature.lowering_node, signature.requires_name ? static_cast<int>(grammar_.inputs().size() + source.local) : 0, 0};
    } else throw std::invalid_argument("materialization requires an implemented native node contract");
    const auto node_index = out_.genome.ast.nodes.size();
    emit(node, origin);
    if (!source.fuel_charges.empty())
      out_.genome.ast.fuel_specs.push_back(
          NodeFuelSpec{node_index, source.fuel_charges});
    if (bounded_region) bounded_region->node_index = node_index;
    std::optional<LexicalRegion> lexical_region;
    if (source.kind == ExpressionKind::Primitive && !source.regions.empty()) {
      if (source.regions.size() != 1)
        throw std::logic_error("native region primitive must have exactly one lexical body");
      const auto& region = source.regions.front();
      lexical_region = LexicalRegion{node_index, static_cast<int>(region.argument), {}};
      for (const auto& binding : region.bindings)
        lexical_region->bindings.push_back({fresh_binder(), binding.type});
      out_.genome.ast.lexical_regions.push_back(*lexical_region);
      const auto& signature = PrimitiveCatalog::standard().at(source.target);
      if (signature.traversal_direction)
        out_.genome.ast.traversal_specs.push_back({node_index, *signature.traversal_direction});
    }
    std::uint32_t remaining = budget - 1;
    for (std::size_t i = 0; i < source.children.size(); ++i) {
      std::uint64_t reserved = 0;
      for (std::size_t j = i + 1; j < source.children.size(); ++j) reserved += minimum(source.children[j], depth - 1);
      const auto lower = minimum(source.children[i], depth - 1);
      if (reserved > remaining || lower > remaining - reserved) throw std::logic_error("compiled minimum cost is inconsistent");
      const auto allowance = static_cast<std::uint32_t>(random_.integer(lower, remaining - reserved));
      const auto begin = out_.genome.ast.nodes.size();
      const auto saved_environment = environment_;
      if (bounded_region) {
        const auto phase = std::find_if(
            bounded_region->phases.begin(), bounded_region->phases.end(),
            [&](const RegionAstPhase& candidate) {
              return candidate.argument == i;
            });
        if (phase != bounded_region->phases.end()) {
          environment_.clear();
          for (const RegionAstBinding& binding : phase->bindings)
            environment_.push_back(binding.binder_id);
        }
      } else if (lexical_region &&
                 static_cast<std::uint32_t>(i) == source.regions.front().argument)
        for (const auto& binding : lexical_region->bindings) environment_.push_back(binding.id);
      expression(source.children[i], depth - 1, allowance, production, nt);
      environment_ = saved_environment;
      remaining -= static_cast<std::uint32_t>(out_.genome.ast.nodes.size() - begin);
    }
    if (bounded_region)
      out_.genome.ast.bounded_region_specs.push_back(
          std::move(*bounded_region));
  }
  const CompiledGrammar& grammar_;
  GrammarRandom random_;
  const GenerationFrame* frame_ = nullptr;
  std::vector<bool> available_locals_;
  std::vector<std::vector<std::uint32_t>> frame_costs_;
  GeneratedDerivation out_;
  std::uint32_t current_choice_ = kNoGrammarId;
  std::vector<std::pair<std::uint32_t, std::size_t>> alias_frames_;
  std::vector<std::unique_ptr<TemplatePlan>> plans_;
  std::pair<std::uint32_t, std::uint32_t> active_slot_{kNoGrammarId, kNoGrammarId};
  std::vector<int> environment_;
  int next_binder_id_ = 0;
  std::unordered_set<int> reserved_binder_ids_;
};
}  // namespace

GeneratedDerivation generate_derivation(const CompiledGrammar& grammar, std::uint64_t seed) {
  return generate_derivation(grammar, seed, entry_request(grammar));
}

GeneratedDerivation generate_derivation(const CompiledGrammar& grammar, std::uint64_t seed,
    const GenerationRequest& request) {
  return Generator(grammar, seed, request).run();
}

GeneratedDerivation generate_derivation_in_frame(const CompiledGrammar& grammar, std::uint64_t seed,
    const GenerationRequest& request, const GenerationFrame& frame) {
  return Generator(grammar, seed, request, &frame).run();
}

}  // namespace gagp::evo::grammar
