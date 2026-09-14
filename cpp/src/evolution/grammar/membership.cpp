#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/grammar/values.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/node_descriptor.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace gagp::evo::grammar {
namespace {
class Matcher {
 public:
  Matcher(const CompiledGrammar& grammar, const AstProgram& ast, const GenerationRequest& request)
      : grammar_(grammar), ast_(ast), request_(request) {}
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
    for (const auto& input : grammar_.inputs()) inputs.push_back({input.name, input.type});
    const auto result = verify_ast(ast_, inputs);
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
      if (expression(grammar_.productions()[production].expression, index)) { matches = true; break; }
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
  std::uint32_t steps_ = 0, depth_ = 0;
};
}  // namespace

void require_membership(const CompiledGrammar& grammar, const ProgramGenome& genome) {
  require_membership(grammar, genome, entry_request(grammar));
}

void require_membership(const CompiledGrammar& grammar, const ProgramGenome& genome,
    const GenerationRequest& request) {
  Matcher(grammar, genome.ast, request).run();
}

}  // namespace gagp::evo::grammar
