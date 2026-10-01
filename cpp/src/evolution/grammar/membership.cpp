#include "owned_population.hpp"
#include "evaluation_identity.hpp"
#include "variation_internal.hpp"
#include "gagp/evolution/grammar/derivation_resources.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/grammar/values.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/bounded_region.hpp"
#include "gagp/evolution/node_descriptor.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/core/semantic_fuel.hpp"
#include "../region_plan_equal.hpp"

#include <memory>
#include "../../runtime/payload/staging.hpp"
#include <cstdlib>
#include "gagp/evolution/grammar/cache.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include <stdexcept>
#include <utility>

namespace gagp::evo::grammar {
// Definition intentionally private to this translation unit. The witness copy
// has no certificate pointer, avoiding a cycle or reliance on public provenance.
class DerivationCertificate {
 public:
  std::string identity;
  DerivationMetadata witness;
  VerifiedAst verified;
  std::shared_ptr<const BytecodeProgram> executable;
  std::vector<std::vector<int>> environments;
};
namespace {
bool certificates_enabled() { return std::getenv("GAGP_NO_DERIVATION_CERTIFICATES") == nullptr; }
bool same_certificate_request(const GenerationRequest& a, const GenerationRequest& b) {
  if (a.nonterminal != b.nonterminal || a.type != b.type || a.stage != b.stage ||
      a.budget.max_nodes != b.budget.max_nodes || a.budget.max_depth != b.budget.max_depth ||
      a.visible_environment.size() != b.visible_environment.size()) return false;
  for (std::size_t i = 0; i < a.visible_environment.size(); ++i)
    if (a.visible_environment[i].name != b.visible_environment[i].name ||
        a.visible_environment[i].type != b.visible_environment[i].type) return false;
  return true;
}
std::string certificate_identity(const CompiledGrammar& grammar, const ProgramGenome& genome) {
  std::vector<std::string> inputs;
  for (const auto& input : grammar.inputs()) inputs.push_back(input.name);
  return runtime_cache_identity(genome, inputs, grammar.execution_limits().fuel);
}

using MatchKey = std::tuple<std::uint32_t, std::size_t, std::vector<int>>;
using ProductionDecisions = std::map<MatchKey, std::uint32_t>;

// Scope maps are positions in compiled environments, not public AST binder IDs.
struct LexicalScope {
  std::vector<int>& environment;
  std::vector<int> saved;
  explicit LexicalScope(std::vector<int>& env) : environment(env), saved(env) {}
  LexicalScope(std::vector<int>& env, const std::vector<std::uint32_t>& mapping)
      : environment(env), saved(env) {
    environment.clear();
    for (auto slot : mapping) environment.push_back(saved.at(slot));
  }
  ~LexicalScope() { environment = std::move(saved); }
};

const LexicalRegion& region_at(const AstProgram& ast, std::size_t index) {
  for (const auto& region : ast.lexical_regions)
    if (region.node_index == index) return region;
  throw std::logic_error("verified AST is missing lexical region metadata");
}
const BoundedRegionSpec& bounded_region_at(const AstProgram& ast, std::size_t index) {
  const auto* spec = lookup_bounded_region_spec(ast, index);
  if (spec) return *spec;
  throw std::logic_error("verified AST is missing bounded region metadata");
}
bool same_slot(const RegionValueSlot& left, const RegionValueSlot& right) {
  return left.bank == right.bank && left.slot == right.slot;
}
TraversalDirection direction_at(const AstProgram& ast, std::size_t index) {
  for (const auto& traversal : ast.traversal_specs)
    if (traversal.node_index == index) return traversal.direction;
  throw std::logic_error("verified AST is missing traversal metadata");
}
void extend_region_scope(std::vector<int>& environment, const CompiledExpression& source,
                         const AstProgram& ast, std::size_t index, std::size_t argument) {
  for (const auto& region : source.regions) {
    if (region.argument != argument) continue;
    if (ast.nodes[index].kind == NodeKind::BOUNDED_REGION) {
      const auto& spec = bounded_region_at(ast, index);
      const auto phase = std::find_if(spec.phases.begin(), spec.phases.end(),
          [&](const RegionAstPhase& item) { return item.argument == argument; });
      if (phase == spec.phases.end())
        throw std::logic_error("verified AST is missing bounded phase metadata");
      environment.clear();
      for (const auto& binding : phase->bindings)
        environment.push_back(binding.binder_id);
      continue;
    }
    const auto& native = region_at(ast, index);
    for (const auto& binding : native.bindings) environment.push_back(binding.id);
  }
}

class Matcher {
 public:
  Matcher(const CompiledGrammar& grammar, const AstProgram& ast, const GenerationRequest& request, bool capture_exact_scopes = false, const GenerationFrame* frame = nullptr,
      const std::vector<GenerationRequest>* population_requests = nullptr)
      : grammar_(grammar), ast_(ast), request_(request),
        capture_exact_scopes_(capture_exact_scopes), frame_(frame), population_requests_(population_requests) {}
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
      const auto arity = node_prefix_arity(node);
      if (arity) pending.push_back(arity);
    }
    for (auto count : pending) if (count) fail("missing prefix children");
    VerifyOptions options;
    options.capture_exact_scopes = capture_exact_scopes_;
    AstVerifyResult result;
    if (frame_) {
      const auto projected = project_frame(grammar_, request_, *frame_, ast_);
      result = verify_ast(projected.ast, projected.inputs, options);
    } else {
      std::vector<InputSpec> inputs;
      for (const auto& input : grammar_.inputs()) inputs.push_back({input.name, input.type});
      result = verify_ast(ast_, inputs, options);
    }
    if (!result) fail("native verification: " + result.diagnostic.message);
    verified_ = std::move(result.verified);
    if (population_requests_) {
      const auto selected = std::find_if(population_requests_->begin(), population_requests_->end(),
          [&](const GenerationRequest& candidate) { return candidate.type == verified_.return_type; });
      if (selected == population_requests_->end())
        fail("mixed population member has no admitted root result type");
      request_ = *selected;
    }
    fuel_specs_.assign(ast_.nodes.size(), nullptr);
    for (const NodeFuelSpec& spec : ast_.fuel_specs)
      fuel_specs_.at(spec.node_index) = &spec;
    const auto& entry = grammar_.nonterminals()[request_.nonterminal];
    if (verified_.return_type != entry.type) fail("return type differs from grammar entry");
    // Decode every pool value, including unused values, to reject opaque payloads.
    for (const auto& value : ast_.consts) constants_.push_back(canonical_constant_encoding(value));
    std::size_t start = 0;
    if (entry.category == NodeCategory::Expression) {
      if (ast_.nodes.size() < 5 || ast_.nodes[0].kind != NodeKind::PROGRAM ||
          ast_.nodes[1].kind != NodeKind::BLOCK_CONS || ast_.nodes[2].kind != NodeKind::RETURN ||
          ast_.nodes.back().kind != NodeKind::BLOCK_NIL || verified_.subtree_end[3] != ast_.nodes.size() - 1)
        fail("expression entry requires its single-return envelope");
      start = 3;
    }
    lexical_environment_ = frame_ ? frame_environment(grammar_, request_, *frame_) :
        std::vector<int>(entry.scope.size(), -1);
    if (!nonterminal(request_.nonterminal, start)) fail("AST cannot be derived from the grammar entry");
  }
  const GenerationRequest& request() const { return request_; }
  const VerifiedAst& verified() const { return verified_; }
  VerifiedAst take_verified() { return std::move(verified_); }
  const ProductionDecisions& decisions() const { return decisions_; }
 private:
  struct HoleMatch { std::size_t index; std::vector<int> environment; };
  struct Instance {
    std::uint32_t template_id;
    std::map<std::uint32_t, HoleMatch> holes;
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
  bool same_subtree(std::size_t left, std::size_t right,
                    const std::vector<int>& left_environment,
                    const std::vector<int>& right_environment) const {
    const auto size = verified_.subtree_end[left] - left;
    if (verified_.subtree_end[right] - right != size) return false;
    if (left_environment.size() != right_environment.size()) return false;
    std::map<int, int> ids;
    for (std::size_t i = 0; i < left_environment.size(); ++i)
      if (left_environment[i] >= 0) ids.emplace(left_environment[i], right_environment[i]);
    for (std::size_t offset = 0; offset < size; ++offset) {
      const auto& a = ast_.nodes[left + offset];
      const auto& b = ast_.nodes[right + offset];
      if (a.kind != b.kind) return false;
      if (!same_fuel_profile(left + offset, right + offset)) return false;
      if (node_descriptor(a.kind).metadata == NodeMetadataKind::LexicalRegion) {
        const auto& x = region_at(ast_, left + offset);
        const auto& y = region_at(ast_, right + offset);
        if (x.body_argument != y.body_argument || x.bindings.size() != y.bindings.size()) return false;
        for (std::size_t i = 0; i < x.bindings.size(); ++i) {
          if (x.bindings[i].type != y.bindings[i].type) return false;
          if (!ids.emplace(x.bindings[i].id, y.bindings[i].id).second) return false;
        }
        if (a.kind != NodeKind::LET_REGION &&
            direction_at(ast_, left + offset) != direction_at(ast_, right + offset)) return false;
      } else if (node_descriptor(a.kind).metadata == NodeMetadataKind::BoundedRegion) {
        const auto& x = bounded_region_at(ast_, left + offset);
        const auto& y = bounded_region_at(ast_, right + offset);
        if (!same_region_plan(x.plan, y.plan) ||
            x.parameters.size() != y.parameters.size() ||
            x.phases.size() != y.phases.size()) return false;
        for (std::size_t i = 0; i < x.parameters.size(); ++i) {
          const auto& first = x.parameters[i];
          const auto& second = y.parameters[i];
          if (first.kind != second.kind) return false;
          if (first.kind == RegionCaptureKind::Name) {
            if (ast_.names.at(first.index) != ast_.names.at(second.index)) return false;
          } else {
            const auto found = ids.find(first.index);
            if (found == ids.end() ? first.index != second.index : found->second != second.index)
              return false;
          }
        }
        for (std::size_t phase = 0; phase < x.phases.size(); ++phase) {
          const auto& first = x.phases[phase];
          const auto& second = y.phases[phase];
          if (first.argument != second.argument ||
              first.bindings.size() != second.bindings.size()) return false;
          for (std::size_t binding = 0; binding < first.bindings.size(); ++binding) {
            if (!same_slot(first.bindings[binding].source,
                           second.bindings[binding].source) ||
                !ids.emplace(first.bindings[binding].binder_id,
                             second.bindings[binding].binder_id).second) return false;
          }
        }
      }
    }
    for (std::size_t offset = 0; offset < size; ++offset) {
      const auto& a = ast_.nodes[left + offset];
      const auto& b = ast_.nodes[right + offset];
      if (a.kind != b.kind) return false;
      const auto& descriptor = node_descriptor(a.kind);
      const auto equal_index = [&](NodeIndexRole role, int x, int y) {
        if (role == NodeIndexRole::Name) return ast_.names[x] == ast_.names[y];
        if (role == NodeIndexRole::Constant) return constants_[x] == constants_[y];
        if (role == NodeIndexRole::BinderId) {
          const auto found = ids.find(x);
          return found == ids.end() ? x == y : found->second == y;
        }
        return x == y;
      };
      if (!equal_index(descriptor.i0_role, a.i0, b.i0) ||
          !equal_index(descriptor.i1_role, a.i1, b.i1)) return false;
    }
    return true;
  }
  static bool same_charges(const std::vector<FuelCharge>& left,
                           const std::vector<FuelCharge>& right) {
    // Compilation and native verification reject duplicate events. Profiles are
    // small, and input AST event order is deliberately not significant.
    if (left.size() != right.size()) return false;
    for (const auto& charge : left) {
      const auto match = std::find_if(right.begin(), right.end(), [&](const auto& other) {
        return charge.event == other.event && charge.cost == other.cost;
      });
      if (match == right.end()) return false;
    }
    return true;
  }
  bool same_fuel_profile(std::size_t left, std::size_t right) const {
    const NodeFuelSpec* first = fuel_specs_.at(left);
    const NodeFuelSpec* second = fuel_specs_.at(right);
    if (!first || !second) return first == second;
    return same_charges(first->charges, second->charges);
  }
  bool matches_fuel_profile(const CompiledExpression& source,
                            std::size_t index) const {
    const NodeFuelSpec* actual = fuel_specs_.at(index);
    if (source.fuel_charges.empty()) return actual == nullptr;
    return actual && same_charges(source.fuel_charges, actual->charges);
  }
  bool nonterminal(std::uint32_t id, std::size_t index) {
    Frame frame(*this);
    auto key = std::make_tuple(id, index, lexical_environment_);
    // Nonterminal definitions cannot capture holes of a caller's template;
    // each production's holes are enclosed by its own compiled Template node.
    // Therefore success has no side effects on the caller's hole bindings.
    // One table stores both active recursion (the sentinel) and successful
    // first-production decisions. Failed matches are erased because negative
    // results can depend on the current zero-node alias ancestry.
    const auto found = decisions_.lower_bound(key);
    if (found != decisions_.end() && found->first == key)
      return found->second != kNoGrammarId;
    const auto inserted = decisions_.emplace_hint(found, std::move(key), kNoGrammarId);
    bool matches = false;
    for (auto production : grammar_.nonterminals()[id].productions) {
      if (expression(grammar_.productions()[production].expression, index)) {
        inserted->second = production;
        matches = true; break;
      }
    }
    if (!matches) decisions_.erase(inserted);
    return matches;
  }
  bool constant(std::uint32_t id, const AstNode& node) {
    if (node.kind != NodeKind::CONST) return false;
    const auto& domain = grammar_.constants()[id];
    const auto& value = ast_.consts[node.i0];
    if (domain.integer_range)
      return value.tag == ValueTag::Int && value.i >= domain.minimum && value.i <= domain.maximum;
    if (domain.float_range || domain.elements) return constant_domain_contains(domain, value);
    return grammar_.constant_encoding_allowed(id, constants_[node.i0]);
  }
  bool bounded(const CompiledExpression& source, std::size_t index) const {
    const auto& node = ast_.nodes[index];
    if (node.kind != NodeKind::BOUNDED_REGION ||
        source.target >= grammar_.structured_contracts().size()) return false;
    const auto& contract = grammar_.structured_contracts()[source.target];
    if (contract.family != StructuredFamily::BoundedRegion || !contract.plan) return false;
    const auto& spec = bounded_region_at(ast_, index);
    if (node.i0 != static_cast<int>(source.children.size()) ||
        node.i0 != static_cast<int>(bounded_region_arity(*contract.plan)) ||
        !same_region_plan(spec.plan, *contract.plan) ||
        spec.parameters.size() != source.captures.size() ||
        spec.phases.size() != source.phases.size()) return false;
    for (std::size_t i = 0; i < source.captures.size(); ++i) {
      const auto& expected = source.captures[i];
      const auto& actual = spec.parameters[i];
      if (expected.kind == CompiledCaptureKind::Bound) {
        if (actual.kind != RegionCaptureKind::Lexical ||
            expected.target >= lexical_environment_.size() ||
            lexical_environment_[expected.target] < 0 ||
            actual.index != lexical_environment_[expected.target]) return false;
      } else {
        if (actual.kind != RegionCaptureKind::Name) return false;
        const auto& name = expected.kind == CompiledCaptureKind::Input ?
            grammar_.inputs().at(expected.target).name :
            grammar_.locals().at(expected.target).name;
        if (ast_.names.at(actual.index) != name) return false;
      }
    }
    for (std::size_t i = 0; i < source.phases.size(); ++i) {
      const auto& expected = source.phases[i];
      const auto& actual = spec.phases[i];
      if (actual.argument != expected.argument ||
          actual.bindings.size() != expected.sources.size()) return false;
      for (std::size_t j = 0; j < expected.sources.size(); ++j)
        if (!same_slot(actual.bindings[j].source, expected.sources[j])) return false;
    }
    return true;
  }
  bool expression(std::uint32_t id, std::size_t index) {
    Frame frame(*this);
    const auto& source = grammar_.expressions()[id];
    if (source.kind == ExpressionKind::Reference) {
      LexicalScope scope(lexical_environment_, source.scope_mapping);
      return nonterminal(source.target, index);
    }
    if (source.kind == ExpressionKind::Template) {
      LexicalScope scope(lexical_environment_, source.scope_mapping);
      instances_.push_back({source.target, {}});
      const bool matches = expression(source.children.at(0), index);
      instances_.pop_back();
      return matches;
    }
    if (source.kind == ExpressionKind::Hole) {
      LexicalScope scope(lexical_environment_, source.scope_mapping);
      auto owner = instances_.size();
      while (owner && instances_[owner - 1].template_id != source.template_id) --owner;
      if (!owner) fail("compiled hole has no enclosing template instance");
      // Child evaluation can append instances and invalidate references.
      const auto found = instances_[owner - 1].holes.find(source.target);
      if (found != instances_[owner - 1].holes.end() && !same_subtree(found->second.index, index, found->second.environment, lexical_environment_)) return false;
      if (!expression(source.children.at(0), index)) return false;
      instances_[owner - 1].holes.emplace(source.target, HoleMatch{index, lexical_environment_});
      return true;
    }
    const auto& node = ast_.nodes[index];
    if ((source.kind == ExpressionKind::Constant && node.kind != NodeKind::CONST) ||
        (source.kind == ExpressionKind::Bound && node.kind != NodeKind::REGION_VAR) ||
        ((source.kind == ExpressionKind::Input || source.kind == ExpressionKind::Local) &&
         node.kind != NodeKind::VAR)) return false;
    if (!matches_fuel_profile(source, index)) return false;
    if (source.kind == ExpressionKind::Constant) return constant(source.target, node);
    if (source.kind == ExpressionKind::Bound) {
      const int expected = lexical_environment_.at(source.target);
      return expected >= 0 && node.kind == NodeKind::REGION_VAR && node.i0 == expected &&
          verified_.expression_types[index] == source.type;
    }
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
      if (node.kind != expected) return false;
      if (signature.traversal_direction &&
          direction_at(ast_, index) != *signature.traversal_direction) return false;
    } else if (source.kind == ExpressionKind::Control) {
      const auto& signature = PrimitiveCatalog::standard().control_signatures()[source.target];
      expected = signature.lowering_node;
      if (node.kind != expected) return false;
      if (signature.requires_name && ast_.names[node.i0] != (source.target_input ? grammar_.inputs() : grammar_.locals())[source.local].name) return false;
    } else if (source.kind == ExpressionKind::Structured) {
      if (!bounded(source, index)) return false;
      expected = NodeKind::BOUNDED_REGION;
    } else fail("expression has no native membership contract");
    if (node.kind != expected) return false;
    if (source.category == NodeCategory::Expression && verified_.expression_types[index] != source.type) return false;
    auto child_index = index + 1;
    for (std::size_t argument = 0; argument < source.children.size(); ++argument) {
      LexicalScope scope(lexical_environment_);
      extend_region_scope(lexical_environment_, source, ast_, index, argument);
      if (!expression(source.children[argument], child_index)) return false;
      child_index = verified_.subtree_end[child_index];
    }
    return child_index == verified_.subtree_end[index];
  }
  const CompiledGrammar& grammar_;
  const AstProgram& ast_;
  GenerationRequest request_;
  VerifiedAst verified_;
  std::vector<std::string> constants_;
  std::vector<Instance> instances_;
  std::vector<int> lexical_environment_;
  std::vector<const NodeFuelSpec*> fuel_specs_;
  ProductionDecisions decisions_;
  bool capture_exact_scopes_ = false;
  const GenerationFrame* frame_ = nullptr;
  const std::vector<GenerationRequest>* population_requests_ = nullptr;
  std::uint32_t steps_ = 0, depth_ = 0;
};

// Record only successful production decisions during matching, then walk that
// witness separately. Failed alternatives never leave partial provenance rows.
class WitnessBuilder {
 public:
  WitnessBuilder(const CompiledGrammar& grammar, const ProgramGenome& genome,
      const GenerationRequest& request, const Matcher& matcher, const GenerationFrame* frame = nullptr,
      std::vector<std::vector<int>>* choice_lexical_environments = nullptr)
      : grammar_(grammar), genome_(genome), verified_(matcher.verified()),
        decisions_(matcher.decisions()), frame_(frame),
        choice_lexical_environments_out_(choice_lexical_environments) {
    out_.seed_replayable = false;
    out_.request = request;
    out_.request_scope_mapping = validate_request(grammar, request);
    out_.grammar_hash = grammar.content_hash();
    out_.search_limits = request.budget;
    out_.execution_limits = grammar.execution_limits();
    out_.nodes.resize(genome.ast.nodes.size());
  }
  DerivationMetadata run(bool validate_lowering = true, const ProjectedBudget* budget = nullptr,
      std::shared_ptr<const BytecodeProgram>* executable = nullptr) {
    if (choice_lexical_environments_out_) choice_lexical_environments_out_->clear();
    const auto nt = out_.request.nonterminal;
    const bool wrap = grammar_.nonterminals()[nt].category == NodeCategory::Expression;
    if (wrap) {
      for (const auto index : {std::size_t{0}, std::size_t{1}, std::size_t{2}, out_.nodes.size() - 1})
        out_.nodes[index].fixed = true;
    }
    lexical_environment_ = frame_ ? frame_environment(grammar_, out_.request, *frame_) :
        std::vector<int>(grammar_.nonterminals()[nt].scope.size(), -1);
    derive(nt, wrap ? 3 : 0);
    std::vector<ResourceCharge> charges(out_.nodes.size());
    for (std::size_t i = 0; i < charges.size(); ++i)
      if (out_.nodes[i].expression != kNoGrammarId)
        charges[i] = grammar_.expressions().at(out_.nodes[i].expression).resource_charge;
    out_.resources = std::make_shared<const ResourceProjection>(verified_.subtree_end, charges);
    out_.derived_nodes = static_cast<std::uint32_t>(genome_.ast.nodes.size() - (wrap ? 4 : 0));
    // Resource-only queries never admit a child for execution. Full witness
    // reconstruction still checks lowering at generation/acceptance boundaries.
    if (!validate_lowering || (budget && !budget->accepts(out_.resources->subtree())))
      return std::move(out_);
    auto lowered = [&] {
      if (frame_) {
        auto projected = project_frame(grammar_, out_.request, *frame_, genome_.ast);
        auto projected_genome = genome_;
        projected_genome.ast = std::move(projected.ast);
        std::vector<std::string> inputs;
        for (const auto& input : projected.inputs) inputs.push_back(input.name);
        return compile_for_eval(projected_genome, verified_, inputs);
      }
      std::vector<std::string> inputs;
      for (const auto& input : grammar_.inputs()) inputs.push_back(input.name);
      return compile_for_eval(genome_, verified_, inputs);
    }();
    const auto lowered_instructions = bytecode_instruction_count(lowered);
    if (lowered_instructions > kGrammarMaxLoweredInstructions)
      throw std::invalid_argument("grammar witness exceeds 1048576 lowered instructions");
    out_.lowered_instructions = static_cast<std::uint32_t>(lowered_instructions);
    if (executable) *executable = std::make_shared<const BytecodeProgram>(std::move(lowered));
    if (choice_lexical_environments_out_) {
      if (choice_lexical_environments_.size() != out_.choices.size())
        throw std::logic_error("grammar witness lexical sidecar lost choice alignment");
      *choice_lexical_environments_out_ = std::move(choice_lexical_environments_);
    }
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
    std::vector<std::vector<int>> choice_lexical_environments;
    std::vector<int> environment;
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
    const auto production = decisions_.at(std::make_tuple(nt, index, lexical_environment_));
    const auto parent = current_choice_;
    current_choice_ = static_cast<std::uint32_t>(out_.choices.size());
    out_.choices.push_back({nt, production, parent, static_cast<std::uint32_t>(index),
        static_cast<std::uint32_t>(verified_.subtree_end[index]), active_slot_.first, active_slot_.second, static_cast<std::uint32_t>(instances_.size())});
    if (choice_lexical_environments_out_)
      choice_lexical_environments_.push_back(lexical_environment_);
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
      slot.environment = lexical_environment_;
      slot.enclosing_depth = static_cast<std::uint32_t>(instances_.size());
      slot.choice_begin = static_cast<std::uint32_t>(out_.choices.size());
      const auto hole_begin = out_.holes.size();
      const auto choice_environment_begin = choice_lexical_environments_.size();
      const auto saved_slot = active_slot_;
      active_slot_ = {instance.instance, source.target};
      expression(source.children.at(0), index, production, nt);
      active_slot_ = saved_slot;
      slot.nodes.assign(out_.nodes.begin() + begin, out_.nodes.begin() + end);
      slot.choices.assign(out_.choices.begin() + slot.choice_begin, out_.choices.end());
      slot.holes.assign(out_.holes.begin() + hole_begin, out_.holes.end());
      if (choice_lexical_environments_out_)
        slot.choice_lexical_environments.assign(
            choice_lexical_environments_.begin() + choice_environment_begin,
            choice_lexical_environments_.end());
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
      std::map<int, int> alpha;
      if (choice_lexical_environments_out_) {
        if (slot.environment.size() != lexical_environment_.size())
          throw std::logic_error("grammar witness repeated hole changed its formal lexical arity");
        for (std::size_t i = 0; i < slot.environment.size(); ++i)
          if (slot.environment[i] >= 0)
            alpha.emplace(slot.environment[i], lexical_environment_[i]);
        for (std::size_t offset = 0; offset < end - begin; ++offset) {
          const auto metadata = node_descriptor(
              genome_.ast.nodes[slot.begin + offset].kind).metadata;
          if (metadata == NodeMetadataKind::LexicalRegion) {
            const auto& first = region_at(genome_.ast, slot.begin + offset);
            const auto& current = region_at(genome_.ast, begin + offset);
            if (first.bindings.size() != current.bindings.size())
              throw std::logic_error("grammar witness repeated hole changed lexical binding arity");
            for (std::size_t i = 0; i < first.bindings.size(); ++i)
              if (!alpha.emplace(first.bindings[i].id, current.bindings[i].id).second)
                throw std::logic_error("grammar witness repeated hole reused a lexical binding ID");
          } else if (metadata == NodeMetadataKind::BoundedRegion) {
            const auto& first = bounded_region_at(genome_.ast, slot.begin + offset);
            const auto& current = bounded_region_at(genome_.ast, begin + offset);
            if (first.phases.size() != current.phases.size())
              throw std::logic_error("grammar witness repeated hole changed bounded phase arity");
            for (std::size_t phase = 0; phase < first.phases.size(); ++phase) {
              if (first.phases[phase].bindings.size() !=
                  current.phases[phase].bindings.size())
                throw std::logic_error(
                    "grammar witness repeated hole changed bounded binding arity");
              for (std::size_t i = 0; i < first.phases[phase].bindings.size(); ++i)
                if (!alpha.emplace(first.phases[phase].bindings[i].binder_id,
                                   current.phases[phase].bindings[i].binder_id).second)
                  throw std::logic_error(
                      "grammar witness repeated hole reused a bounded binding ID");
            }
          }
        }
      }
      for (auto choice : slot.choices) {
        choice.enclosing_template_depth = shifted_depth(choice.enclosing_template_depth);
        choice.parent = choice.parent >= slot.choice_begin &&
            choice.parent < slot.choice_begin + slot.choices.size() ?
            choice_begin + choice.parent - slot.choice_begin : current_choice_;
        choice.ast_begin += shift; choice.ast_end += shift;
        out_.choices.push_back(choice);
      }
      if (choice_lexical_environments_out_) {
        for (auto environment : slot.choice_lexical_environments) {
          for (auto& id : environment) {
            if (id < 0) continue;
            const auto mapped = alpha.find(id);
            if (mapped == alpha.end())
              throw std::logic_error("grammar witness repeated hole lost a lexical binding mapping");
            id = mapped->second;
          }
          choice_lexical_environments_.push_back(std::move(environment));
        }
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
    if (source.kind == ExpressionKind::Reference) {
      LexicalScope scope(lexical_environment_, source.scope_mapping);
      derive(source.target, index); return;
    }
    if (source.kind == ExpressionKind::Template) {
      LexicalScope scope(lexical_environment_, source.scope_mapping);
      if (instances_.size() >= 256)
        throw std::invalid_argument("grammar witness exceeds 256 nested template instances");
      const auto instance = static_cast<std::uint32_t>(out_.templates.size());
      out_.templates.push_back({source.target, instances_.empty() ? kNoGrammarId : instances_.back()->instance});
      instances_.push_back(std::make_unique<Instance>(Instance{source.target, instance, {}}));
      expression(source.children.at(0), index, production, nt);
      instances_.pop_back();
      return;
    }
    if (source.kind == ExpressionKind::Hole) {
      LexicalScope scope(lexical_environment_, source.scope_mapping);
      hole(source, index, production, nt); return;
    }
    NodeOrigin origin{id, production, nt, out_.logical_steps, kNoGrammarId, kNoGrammarId, source.fixed};
    if (source.fixed) origin.template_instance = owner(source.template_id).instance;
    else { origin.template_instance = active_slot_.first; origin.slot = active_slot_.second; }
    origin.template_depth = static_cast<std::uint32_t>(instances_.size());
    out_.nodes[index] = origin;
    auto child_index = index + 1;
    for (std::size_t argument = 0; argument < source.children.size(); ++argument) {
      LexicalScope scope(lexical_environment_);
      extend_region_scope(lexical_environment_, source, genome_.ast, index, argument);
      expression(source.children[argument], child_index, production, nt);
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
  std::vector<int> lexical_environment_;
  std::vector<std::vector<int>> choice_lexical_environments_;
  std::vector<std::vector<int>>* choice_lexical_environments_out_ = nullptr;
};
}  // namespace

namespace variation_detail {
std::shared_ptr<const OwnedScalarPopulation> OwnedScalarPopulation::create(
    const std::vector<ProgramGenome>& input, VariationContext& context) {
  if (!certificates_enabled() || context.requests().size() != 1 || context.offspring_budget() ||
      payload::StagedPayloads::has_active_scope())
    throw std::invalid_argument("owned preparation requires scalar single-root certificate admission");
  auto owned = std::shared_ptr<OwnedScalarPopulation>(new OwnedScalarPopulation);
  owned->grammar_ = context.grammar_owner();
  owned->request_ = context.request();
  owned->genomes_.reserve(input.size());
  owned->proofs_.reserve(input.size());
  for (const auto& source : input) {
    for (const auto& value : source.ast.consts)
      if (value.tag != ValueTag::Int && value.tag != ValueTag::Float &&
          value.tag != ValueTag::Bool && value.tag != ValueTag::Char)
        throw std::invalid_argument("owned preparation does not admit registry constants");
    // Copies before admission; certify_execution validates before compaction and
    // refreshes the certificate if table indices change. Its result is private.
    auto admitted = certify_execution(source, context);
    if (!admitted.derivation || !admitted.derivation->certificate)
      throw std::logic_error("owned admission did not produce a certificate");
    owned->proofs_.push_back(admitted.derivation->certificate);
    owned->genomes_.push_back(std::move(admitted));
  }
  return owned;
}
std::shared_ptr<const OwnedScalarPopulation> OwnedScalarPopulation::adopt_decoded(
    std::vector<ProgramGenome> input, VariationContext& context) {
  // Only from_gpu_pass can call this, after decode's full ordered admission.
  // Moving its fresh vector leaves no external mutable aliases to AST storage.
  auto owned = std::shared_ptr<OwnedScalarPopulation>(new OwnedScalarPopulation);
  owned->grammar_ = context.grammar_owner();
  owned->request_ = context.request();
  owned->proofs_.reserve(input.size());
  for (const auto& genome : input) {
    if (!genome.derivation || !genome.derivation->certificate)
      throw std::logic_error("decoded scalar population lost admission certificate");
    for (const auto& value : genome.ast.consts)
      if (value.tag != ValueTag::Int && value.tag != ValueTag::Float &&
          value.tag != ValueTag::Bool && value.tag != ValueTag::Char)
        throw std::logic_error("decoded scalar population acquired registry constants");
    owned->proofs_.push_back(genome.derivation->certificate);
  }
  owned->genomes_ = std::move(input);
  return owned;
}
bool OwnedScalarPopulation::matches(const VariationContext& context) const {
  return grammar_ == context.grammar_owner() && context.requests().size() == 1 &&
      !context.offspring_budget() && same_certificate_request(request_, context.request());
}
const DerivationMetadata& OwnedScalarPopulation::witness(std::size_t i) const { return proofs_.at(i)->witness; }
const VerifiedAst& OwnedScalarPopulation::verified(std::size_t i) const { return proofs_.at(i)->verified; }
const std::vector<std::vector<int>>& OwnedScalarPopulation::environments(std::size_t i) const { return proofs_.at(i)->environments; }
const std::string& OwnedScalarPopulation::identity(std::size_t i) const { return proofs_.at(i)->identity; }
}  // namespace variation_detail

namespace {
bool may_reuse_executable(const ProgramGenome& genome) {
  if (!certificates_enabled() || !std::getenv("GAGP_REUSE_ADMISSION_COMPILE") ||
      !genome.derivation || !genome.derivation->certificate ||
      !genome.derivation->certificate->executable) return false;
  for (const auto& value : genome.ast.consts)
    if (value.tag != ValueTag::Int && value.tag != ValueTag::Float &&
        value.tag != ValueTag::Bool && value.tag != ValueTag::Char) return false;
  return true;
}
}  // namespace

detail::EvaluationIdentity detail::evaluation_identity(const ProgramGenome& genome,
    const std::vector<std::string>& input_names, std::uint32_t fuel) {
  EvaluationIdentity result;
  result.key = genome.derivation ? runtime_cache_identity(genome, input_names, fuel)
                                : genome.meta.program_key;
  if (may_reuse_executable(genome)) {
    const auto& proof = *genome.derivation->certificate;
    if (proof.identity == result.key) result.executable = proof.executable;
  }
  return result;
}

std::shared_ptr<const BytecodeProgram> admitted_bytecode_for_eval(
    const ProgramGenome& genome, const std::vector<std::string>& input_names,
    std::uint32_t fuel) {
  if (!may_reuse_executable(genome)) return nullptr;
  return detail::evaluation_identity(genome, input_names, fuel).executable;
}

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
  return reconstruct_derivation(grammar, genome, request, nullptr, nullptr);
}

DerivationMetadata reconstruct_derivation(const CompiledGrammar& grammar, const ProgramGenome& genome,
    const GenerationRequest& request, VerifiedAst* verified,
    std::vector<std::vector<int>>* choice_lexical_environments) {
  // A public caller without a read snapshot cannot bind mutable registry
  // contents to one consistent proof. Scalar-only ASTs have no such dependency.
  const bool registry_constants = std::any_of(genome.ast.consts.begin(), genome.ast.consts.end(), [](const auto& value) {
    return value.tag == ValueTag::String || value.tag == ValueTag::IntList ||
        value.tag == ValueTag::FloatList || value.tag == ValueTag::StringList;
  });
  const bool cache = certificates_enabled() &&
      (!registry_constants || payload::StagedPayloads::has_active_scope());
  if (cache && genome.derivation && genome.derivation->certificate) {
    const auto& proof = *genome.derivation->certificate;
    if (proof.witness.grammar_hash == grammar.content_hash() &&
        same_certificate_request(proof.witness.request, request) &&
        proof.identity == certificate_identity(grammar, genome)) {
      if (verified) *verified = proof.verified;
      if (choice_lexical_environments) *choice_lexical_environments = proof.environments;
      auto witness = proof.witness;
      witness.certificate = genome.derivation->certificate;
      return witness;
    }
  }
  Matcher matcher(grammar, genome.ast, request, cache || verified != nullptr);
  matcher.run();
  std::vector<std::vector<int>> environments;
  std::shared_ptr<const BytecodeProgram> executable;
  auto witness = WitnessBuilder(grammar, genome, request, matcher, nullptr,
      cache ? &environments : choice_lexical_environments).run(true, nullptr,
          cache && !registry_constants && std::getenv("GAGP_REUSE_ADMISSION_COMPILE")
              ? &executable : nullptr);
  if (cache) {
    auto proof = std::make_shared<DerivationCertificate>();
    proof->identity = certificate_identity(grammar, genome);
    proof->witness = witness;
    proof->verified = matcher.verified();
    proof->executable = std::move(executable);
    proof->environments = std::move(environments);
    if (choice_lexical_environments) *choice_lexical_environments = proof->environments;
    witness.certificate = std::move(proof);
  }
  if (verified) *verified = matcher.take_verified();
  return witness;
}

DerivationMetadata reconstruct_population_derivation(const CompiledGrammar& grammar,
    const ProgramGenome& genome, const std::vector<GenerationRequest>& requests,
    VerifiedAst* verified, std::vector<std::vector<int>>* choice_lexical_environments) {
  validate_population_requests(grammar, requests);
  if (requests.size() == 1)
    return reconstruct_derivation(grammar, genome, requests.front(), verified, choice_lexical_environments);
  Matcher matcher(grammar, genome.ast, requests.front(), verified != nullptr, nullptr, &requests);
  matcher.run();
  auto witness = WitnessBuilder(grammar, genome, matcher.request(), matcher, nullptr,
      choice_lexical_environments).run();
  if (verified) *verified = matcher.take_verified();
  return witness;
}

bool validate_budgeted_derivation(const CompiledGrammar& grammar, const ProgramGenome& genome,
    const GenerationRequest& request, const ProjectedBudget& budget) {
  Matcher matcher(grammar, genome.ast, request);
  matcher.run();
  const auto witness = WitnessBuilder(grammar, genome, request, matcher).run(true, &budget);
  return budget.accepts(witness.resources->subtree());
}

ResourceProjection project_derivation_resources(const CompiledGrammar& grammar,
    const ProgramGenome& genome, const GenerationRequest& request) {
  Matcher matcher(grammar, genome.ast, request);
  matcher.run();
  const auto witness = WitnessBuilder(grammar, genome, request, matcher).run(false);
  return *witness.resources;
}

ResourceProjection project_derivation_resources(const CompiledGrammar& grammar,
    const ProgramGenome& genome) {
  return project_derivation_resources(grammar, genome, entry_request(grammar));
}

ResourceProjection project_derivation_resources_in_frame(const CompiledGrammar& grammar,
    const ProgramGenome& genome, const GenerationRequest& request, const GenerationFrame& frame) {
  Matcher matcher(grammar, genome.ast, request, false, &frame);
  matcher.run();
  return *WitnessBuilder(grammar, genome, request, matcher, &frame).run(false).resources;
}

void require_membership_in_frame(const CompiledGrammar& grammar, const ProgramGenome& genome,
    const GenerationRequest& request, const GenerationFrame& frame) {
  Matcher(grammar, genome.ast, request, false, &frame).run();
}

DerivationMetadata reconstruct_derivation_in_frame(const CompiledGrammar& grammar, const ProgramGenome& genome,
    const GenerationRequest& request, const GenerationFrame& frame, VerifiedAst* verified,
    std::vector<std::vector<int>>* choice_lexical_environments) {
  Matcher matcher(grammar, genome.ast, request, verified != nullptr, &frame);
  matcher.run();
  auto witness = WitnessBuilder(grammar, genome, request, matcher, &frame,
      choice_lexical_environments).run();
  if (verified) *verified = matcher.take_verified();
  return witness;
}

}  // namespace gagp::evo::grammar
