#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/grammar/values.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/node_descriptor.hpp"
#include "gagp/evolution/compiler.hpp"

#include <memory>

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace gagp::evo::grammar {
namespace {
using ProductionDecisions = std::map<std::pair<std::uint32_t, std::size_t>, std::uint32_t>;
class Matcher {
 public:
  Matcher(const CompiledGrammar& grammar, const AstProgram& ast, const GenerationRequest& request, bool record_decisions = false, bool capture_exact_scopes = false, const GenerationFrame* frame = nullptr)
      : grammar_(grammar), ast_(ast), request_(request), record_decisions_(record_decisions),
        capture_exact_scopes_(capture_exact_scopes), frame_(frame) {}
  void run() {
    (void)validate_request(grammar_, request_);
    grammar_.require_executable(request_.nonterminal);
    if (ast_.nodes.empty() || ast_.nodes.size() > request_.budget.max_nodes)
      fail("materialized node budget exceeded or empty AST");
    std::vector<int> pending{1};
    for (const auto& node : ast_.nodes) {
      while (!pending.empty() && pending.back() == 0) pending.pop_back();
      if (pending.empty()) fail("trailing prefix nodes");
      if (pending.size() > request_.budget.max_depth) fail("materialized depth budget exceeded");
      --pending.back();
      if (!is_known_node_kind(static_cast<int>(node.kind))) fail("unknown native node");
      const auto arity = node_descriptor(node.kind).prefix_arity;
      if (arity) pending.push_back(arity);
    }
    for (auto count : pending) if (count) fail("missing prefix children");
    std::vector<InputSpec> inputs;
    if (frame_) inputs = frame_inputs(grammar_, request_, *frame_);
    else for (const auto& input : grammar_.inputs()) inputs.push_back({input.name, input.type});
    VerifyOptions options;
    options.capture_exact_scopes = capture_exact_scopes_;
    const auto result = verify_ast(ast_, inputs, options);
    if (!result) fail("native verification: " + result.diagnostic.message);
    verified_ = result.verified;
    const auto& entry = grammar_.nonterminals()[request_.nonterminal];
    if (verified_.return_type != entry.type) fail("return type differs from grammar entry");
    // Decode every pool value, including unused values, to reject opaque payloads.
    for (const auto& value : ast_.consts) constants_.push_back(canonical_json(encode_constant(value)));
    std::size_t start = 0;
    if (entry.category == NodeCategory::Expression) {
      if (ast_.nodes.size() < 5 || ast_.nodes[0].kind != NodeKind::PROGRAM ||
          ast_.nodes[1].kind != NodeKind::BLOCK_CONS || ast_.nodes[2].kind != NodeKind::RETURN ||
          ast_.nodes.back().kind != NodeKind::BLOCK_NIL || verified_.subtree_end[3] != ast_.nodes.size() - 1)
        fail("expression entry requires its single-return envelope");
      start = 3;
    }
    if (!nonterminal(request_.nonterminal, start)) fail("AST cannot be derived from the grammar entry");
  }
  const VerifiedAst& verified() const { return verified_; }
  VerifiedAst take_verified() { return std::move(verified_); }
  const ProductionDecisions& decisions() const { return decisions_; }
 private:
  struct Instance {
    std::uint32_t template_id;
    std::map<std::uint32_t, std::size_t> holes;
  };
  [[noreturn]] static void fail(const std::string& reason) {
    throw std::invalid_argument("grammar membership: " + reason);
  }
  struct Frame {
    Matcher& owner;
    explicit Frame(Matcher& value) : owner(value) {
      if (++owner.steps_ > 1048576) fail("matching exceeded 1048576 steps; simplify alias alternatives");
      if (++owner.depth_ > 4096) fail("matching exceeded 4096 grammar frames; simplify alias nesting");
    }
    ~Frame() { --owner.depth_; }
  };
  bool same_subtree(std::size_t left, std::size_t right) const {
    const auto size = verified_.subtree_end[left] - left;
    if (verified_.subtree_end[right] - right != size) return false;
    for (std::size_t offset = 0; offset < size; ++offset) {
      const auto& a = ast_.nodes[left + offset];
      const auto& b = ast_.nodes[right + offset];
      if (a.kind != b.kind) return false;
      const auto& descriptor = node_descriptor(a.kind);
      const auto equal_index = [&](NodeIndexRole role, int x, int y) {
        if (role == NodeIndexRole::Name) return ast_.names[x] == ast_.names[y];
        if (role == NodeIndexRole::Constant) return constants_[x] == constants_[y];
        return x == y;
      };
      if (!equal_index(descriptor.i0_role, a.i0, b.i0) ||
          !equal_index(descriptor.i1_role, a.i1, b.i1)) return false;
    }
    return true;
  }
  bool nonterminal(std::uint32_t id, std::size_t index) {
    Frame frame(*this);
    const auto key = std::make_pair(id, index);
    // Nonterminal definitions cannot capture holes of a caller's template;
    // each production's holes are enclosed by its own compiled Template node.
    // Therefore success has no side effects on the caller's hole bindings.
    if (accepted_.count(key)) return true;
    if (!active_.insert(key).second) return false;
    bool matches = false;
    for (auto production : grammar_.nonterminals()[id].productions) {
      if (expression(grammar_.productions()[production].expression, index)) {
        if (record_decisions_) decisions_.emplace(key, production);
        matches = true; break;
      }
    }
    active_.erase(key);
    // Negative results can depend on the current zero-node alias ancestry.
    if (matches) accepted_.insert(key);
    return matches;
  }
  bool constant(std::uint32_t id, const AstNode& node) {
    if (node.kind != NodeKind::CONST) return false;
    const auto& domain = grammar_.constants()[id];
    const auto& value = ast_.consts[node.i0];
    if (domain.integer_range)
      return value.tag == ValueTag::Int && value.i >= domain.minimum && value.i <= domain.maximum;
    return grammar_.constant_encoding_allowed(id, constants_[node.i0]);
  }
  bool expression(std::uint32_t id, std::size_t index) {
    Frame frame(*this);
    const auto& source = grammar_.expressions()[id];
    if (source.kind == ExpressionKind::Reference) return nonterminal(source.target, index);
    if (source.kind == ExpressionKind::Template) {
      instances_.push_back({source.target, {}});
      const bool matches = expression(source.children.at(0), index);
      instances_.pop_back();
      return matches;
    }
    if (source.kind == ExpressionKind::Hole) {
      auto owner = instances_.size();
      while (owner && instances_[owner - 1].template_id != source.template_id) --owner;
      if (!owner) fail("compiled hole has no enclosing template instance");
      // Child evaluation can append instances and invalidate references.
      const auto found = instances_[owner - 1].holes.find(source.target);
      if (found != instances_[owner - 1].holes.end() && !same_subtree(found->second, index)) return false;
      if (!expression(source.children.at(0), index)) return false;
      instances_[owner - 1].holes.emplace(source.target, index);
      return true;
    }
    const auto& node = ast_.nodes[index];
    if (source.kind == ExpressionKind::Constant) return constant(source.target, node);
    if (source.kind == ExpressionKind::Input || source.kind == ExpressionKind::Local) {
      const auto& name = source.kind == ExpressionKind::Input ?
          grammar_.inputs()[source.target].name : grammar_.locals()[source.target].name;
      return node.kind == NodeKind::VAR && ast_.names[node.i0] == name &&
          verified_.expression_types[index] == source.type;
    }
    NodeKind expected;
    if (source.kind == ExpressionKind::Primitive) {
      const auto& signature = PrimitiveCatalog::standard().at(source.target);
      if (!signature.lowering_node) fail("primitive has no native membership contract");
      expected = *signature.lowering_node;
    } else if (source.kind == ExpressionKind::Control) {
      const auto& signature = PrimitiveCatalog::standard().control_signatures()[source.target];
      expected = signature.lowering_node;
      if (node.kind != expected) return false;
      if (signature.requires_name && ast_.names[node.i0] != grammar_.locals()[source.local].name) return false;
    } else fail("expression has no native membership contract");
    if (node.kind != expected) return false;
    if (source.category == NodeCategory::Expression && verified_.expression_types[index] != source.type) return false;
    auto child_index = index + 1;
    for (auto child : source.children) {
      if (!expression(child, child_index)) return false;
      child_index = verified_.subtree_end[child_index];
    }
    return child_index == verified_.subtree_end[index];
  }
  const CompiledGrammar& grammar_;
  const AstProgram& ast_;
  const GenerationRequest& request_;
  VerifiedAst verified_;
  std::vector<std::string> constants_;
  std::vector<Instance> instances_;
  std::set<std::pair<std::uint32_t, std::size_t>> active_, accepted_;
  ProductionDecisions decisions_;
  bool record_decisions_ = false;
  bool capture_exact_scopes_ = false;
  const GenerationFrame* frame_ = nullptr;
  std::uint32_t steps_ = 0, depth_ = 0;
};

// Record only successful production decisions during matching, then walk that
// witness separately. Failed alternatives never leave partial provenance rows.
class WitnessBuilder {
 public:
  WitnessBuilder(const CompiledGrammar& grammar, const ProgramGenome& genome,
      const GenerationRequest& request, const Matcher& matcher, const GenerationFrame* frame = nullptr)
      : grammar_(grammar), genome_(genome), verified_(matcher.verified()),
        decisions_(matcher.decisions()), frame_(frame) {
    out_.seed_replayable = false;
    out_.request = request;
    out_.request_scope_mapping = validate_request(grammar, request);
    out_.grammar_hash = grammar.content_hash();
    out_.search_limits = request.budget;
    out_.execution_limits = grammar.execution_limits();
    out_.nodes.resize(genome.ast.nodes.size());
  }
  DerivationMetadata run() {
    const auto nt = out_.request.nonterminal;
    const bool wrap = grammar_.nonterminals()[nt].category == NodeCategory::Expression;
    if (wrap) {
      for (const auto index : {std::size_t{0}, std::size_t{1}, std::size_t{2}, out_.nodes.size() - 1})
        out_.nodes[index].fixed = true;
    }
    derive(nt, wrap ? 3 : 0);
    out_.derived_nodes = static_cast<std::uint32_t>(genome_.ast.nodes.size() - (wrap ? 4 : 0));
    std::vector<std::string> inputs;
    if (frame_) {
      for (const auto& input : frame_inputs(grammar_, out_.request, *frame_)) inputs.push_back(input.name);
    } else for (const auto& input : grammar_.inputs()) inputs.push_back(input.name);
    const auto lowered = compile_for_eval(genome_, verified_, inputs);
    if (!lowered.asgp_dc_segments.empty() || !lowered.asgp_dp1d_segments.empty() ||
        !lowered.asgp_dp2d_segments.empty())
      throw std::invalid_argument("grammar witness cannot certify specialized bytecode segments");
    if (lowered.code.size() > kGrammarMaxLoweredInstructions)
      throw std::invalid_argument("grammar witness exceeds 1048576 lowered instructions");
    out_.lowered_instructions = static_cast<std::uint32_t>(lowered.code.size());
    return std::move(out_);
  }
 private:
  struct Slot {
    std::uint32_t begin = kNoGrammarId;
    std::uint32_t choice_begin = 0;
    std::uint32_t enclosing_depth = 0;
    std::vector<NodeOrigin> nodes;
    std::vector<DerivationChoice> choices;
    std::vector<HoleOccurrence> holes;
  };
  struct Instance {
    std::uint32_t template_id;
    std::uint32_t instance;
    std::map<std::uint32_t, Slot> slots;
  };
  void step() {
    if (++out_.logical_steps > 1048576)
      throw std::invalid_argument("grammar witness exceeds 1048576 logical steps");
  }
  struct Frame {
    WitnessBuilder& owner;
    explicit Frame(WitnessBuilder& value) : owner(value) {
      owner.step();
      if (++owner.depth_ > 4096)
        throw std::invalid_argument("grammar witness exceeds 4096 grammar frames; simplify alias nesting");
    }
    ~Frame() { --owner.depth_; }
  };
  Instance& owner(std::uint32_t template_id) {
    for (auto i = instances_.rbegin(); i != instances_.rend(); ++i)
      if ((*i)->template_id == template_id) return **i;
    throw std::logic_error("grammar witness lost its enclosing template instance");
  }
  void derive(std::uint32_t nt, std::size_t index) {
    Frame frame(*this);
    const auto production = decisions_.at({nt, index});
    const auto parent = current_choice_;
    current_choice_ = static_cast<std::uint32_t>(out_.choices.size());
    out_.choices.push_back({nt, production, parent, static_cast<std::uint32_t>(index),
        static_cast<std::uint32_t>(verified_.subtree_end[index]), active_slot_.first, active_slot_.second, static_cast<std::uint32_t>(instances_.size())});
    expression(grammar_.productions()[production].expression, index, production, nt);
    current_choice_ = parent;
  }
  void hole(const CompiledExpression& source, std::size_t index,
      std::uint32_t production, std::uint32_t nt) {
    auto& instance = owner(source.template_id);
    auto& slot = instance.slots[source.target];
    const auto begin = static_cast<std::uint32_t>(index);
    const auto end = static_cast<std::uint32_t>(verified_.subtree_end[index]);
    if (slot.begin == kNoGrammarId) {
      slot.begin = begin;
      slot.enclosing_depth = static_cast<std::uint32_t>(instances_.size());
      slot.choice_begin = static_cast<std::uint32_t>(out_.choices.size());
      const auto hole_begin = out_.holes.size();
      const auto saved_slot = active_slot_;
      active_slot_ = {instance.instance, source.target};
      expression(source.children.at(0), index, production, nt);
      active_slot_ = saved_slot;
      slot.nodes.assign(out_.nodes.begin() + begin, out_.nodes.begin() + end);
      slot.choices.assign(out_.choices.begin() + slot.choice_begin, out_.choices.end());
      slot.holes.assign(out_.holes.begin() + hole_begin, out_.holes.end());
    } else {
      if (end - begin != slot.nodes.size() || begin < slot.begin)
        throw std::logic_error("grammar witness has inconsistent repeated hole spans");
      const auto shifted_depth = [&](std::uint32_t depth) {
        const auto adjusted = static_cast<std::int64_t>(depth) +
            static_cast<std::int64_t>(instances_.size()) - slot.enclosing_depth;
        if (adjusted < 0 || adjusted > 256)
          throw std::invalid_argument("grammar witness repeated hole exceeds physical template nesting capacity");
        return static_cast<std::uint32_t>(adjusted);
      };
      std::copy(slot.nodes.begin(), slot.nodes.end(), out_.nodes.begin() + begin);
      for (auto index = begin; index < end; ++index)
        out_.nodes[index].template_depth = shifted_depth(out_.nodes[index].template_depth);
      const auto shift = begin - slot.begin;
      const auto choice_begin = static_cast<std::uint32_t>(out_.choices.size());
      for (auto choice : slot.choices) {
        choice.enclosing_template_depth = shifted_depth(choice.enclosing_template_depth);
        choice.parent = choice.parent >= slot.choice_begin &&
            choice.parent < slot.choice_begin + slot.choices.size() ?
            choice_begin + choice.parent - slot.choice_begin : current_choice_;
        choice.ast_begin += shift; choice.ast_end += shift;
        out_.choices.push_back(choice);
      }
      for (auto occurrence : slot.holes) {
        occurrence.ast_begin += shift; occurrence.ast_end += shift;
        out_.holes.push_back(occurrence);
      }
    }
    out_.holes.push_back({instance.instance, source.target, begin, end});
  }
  void expression(std::uint32_t id, std::size_t index,
      std::uint32_t production, std::uint32_t nt) {
    Frame frame(*this);
    const auto& source = grammar_.expressions()[id];
    if (source.kind == ExpressionKind::Reference) { derive(source.target, index); return; }
    if (source.kind == ExpressionKind::Template) {
      if (instances_.size() >= 256)
        throw std::invalid_argument("grammar witness exceeds 256 nested template instances");
      const auto instance = static_cast<std::uint32_t>(out_.templates.size());
      out_.templates.push_back({source.target, instances_.empty() ? kNoGrammarId : instances_.back()->instance});
      instances_.push_back(std::make_unique<Instance>(Instance{source.target, instance, {}}));
      expression(source.children.at(0), index, production, nt);
      instances_.pop_back();
      return;
    }
    if (source.kind == ExpressionKind::Hole) { hole(source, index, production, nt); return; }
    NodeOrigin origin{id, production, nt, out_.logical_steps, kNoGrammarId, kNoGrammarId, source.fixed};
    if (source.fixed) origin.template_instance = owner(source.template_id).instance;
    else { origin.template_instance = active_slot_.first; origin.slot = active_slot_.second; }
    origin.template_depth = static_cast<std::uint32_t>(instances_.size());
    out_.nodes[index] = origin;
    auto child_index = index + 1;
    for (auto child : source.children) {
      expression(child, child_index, production, nt);
      child_index = verified_.subtree_end[child_index];
    }
  }
  const CompiledGrammar& grammar_;
  const ProgramGenome& genome_;
  const VerifiedAst& verified_;
  const ProductionDecisions& decisions_;
  const GenerationFrame* frame_ = nullptr;
  DerivationMetadata out_;
  std::uint32_t depth_ = 0;
  std::uint32_t current_choice_ = kNoGrammarId;
  std::pair<std::uint32_t, std::uint32_t> active_slot_{kNoGrammarId, kNoGrammarId};
  std::vector<std::unique_ptr<Instance>> instances_;
};
}  // namespace

void require_membership(const CompiledGrammar& grammar, const ProgramGenome& genome) {
  require_membership(grammar, genome, entry_request(grammar));
}

void require_membership(const CompiledGrammar& grammar, const ProgramGenome& genome,
    const GenerationRequest& request) {
  Matcher(grammar, genome.ast, request).run();
}

DerivationMetadata reconstruct_derivation(const CompiledGrammar& grammar, const ProgramGenome& genome) {
  return reconstruct_derivation(grammar, genome, entry_request(grammar));
}

DerivationMetadata reconstruct_derivation(const CompiledGrammar& grammar, const ProgramGenome& genome,
    const GenerationRequest& request) {
  return reconstruct_derivation(grammar, genome, request, nullptr);
}

DerivationMetadata reconstruct_derivation(const CompiledGrammar& grammar, const ProgramGenome& genome,
    const GenerationRequest& request, VerifiedAst* verified) {
  Matcher matcher(grammar, genome.ast, request, true, verified != nullptr);
  matcher.run();
  auto witness = WitnessBuilder(grammar, genome, request, matcher).run();
  if (verified) *verified = matcher.take_verified();
  return witness;
}

void require_membership_in_frame(const CompiledGrammar& grammar, const ProgramGenome& genome,
    const GenerationRequest& request, const GenerationFrame& frame) {
  Matcher(grammar, genome.ast, request, false, false, &frame).run();
}

DerivationMetadata reconstruct_derivation_in_frame(const CompiledGrammar& grammar, const ProgramGenome& genome,
    const GenerationRequest& request, const GenerationFrame& frame, VerifiedAst* verified) {
  Matcher matcher(grammar, genome.ast, request, true, verified != nullptr, &frame);
  matcher.run();
  auto witness = WitnessBuilder(grammar, genome, request, matcher, &frame).run();
  if (verified) *verified = matcher.take_verified();
  return witness;
}

}  // namespace gagp::evo::grammar
