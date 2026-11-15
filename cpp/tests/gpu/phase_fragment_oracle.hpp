#pragma once
// Whole-program membership oracle for phase fragments generated or varied on
// the GPU. The oracle grammar is a copy of the grammar with widened budgets
// and one appended entry nonterminal whose only production binds X's formal
// scope with nested lets around a reference to X; existing nonterminal and
// expression IDs are unchanged. A fragment of X is spliced into a host program
// of that entry at the X site, its scope references bound to the site's
// native binders, and the whole program is checked with the production
// membership and lowering APIs. Callers check fragment capacity and depth.
#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/cli/json.hpp"
#include "gagp/evolution/ast_program.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/grammar/catalog.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"

namespace gagp::evo::test {

// A fragment in preorder. Constant nodes index `consts`; scope references
// (role Bound) hold their position in the nonterminal's formal scope; fuel
// specs index fragment nodes.
struct PhaseFragment {
  enum class Role : unsigned char { Plain, Constant, Bound };
  std::vector<AstNode> nodes;
  std::vector<Role> roles;
  std::vector<Value> consts;
  std::vector<NodeFuelSpec> fuel;
  void push(AstNode node, Role role, const std::vector<FuelCharge>& charges) {
    if (!charges.empty()) fuel.push_back({nodes.size(), charges});
    nodes.push_back(node);
    roles.push_back(role);
  }
};

// The fragment closed as a standalone program: scope references become
// synthetic inputs, for comparing against an independent CPU lowering.
struct ProjectedFragment {
  ProgramGenome genome;
  std::vector<InputSpec> inputs;  // grammar inputs, then the scope inputs
  std::vector<std::string> scope_names;
};

class PhaseFragmentOracle {
 public:
  PhaseFragmentOracle(const grammar::CompiledGrammar& grammar, std::uint32_t root)
      : grammar_(grammar), root_(root), oracle_(oracle_grammar(grammar, root)) {
    const auto& nt = grammar.nonterminals().at(root);
    auto generated = grammar::generate_derivation(*oracle_, 0);
    generated.genome.derivation.reset();
    request_ = grammar::entry_request(*oracle_);
    std::vector<std::vector<int>> environments;
    VerifiedAst verified;
    const auto witness =
        grammar::reconstruct_derivation(*oracle_, generated.genome, request_, &verified, &environments);
    bool found = false;
    for (std::size_t i = 0; i < witness.choices.size() && !found; ++i) {
      const auto& choice = witness.choices[i];
      if (choice.nonterminal != root || choice.template_instance != grammar::kNoGrammarId) continue;
      begin_ = choice.ast_begin;
      end_ = choice.ast_end;
      binders_ = environments.at(i);
      found = true;
    }
    host_ = std::move(generated.genome);
    if (!found || binders_.size() != nt.scope.size())
      throw std::runtime_error("oracle host does not bind the formal scope of " + nt.stable_id);
  }

  ProgramGenome splice(const PhaseFragment& fragment) const {
    ProgramGenome genome;
    auto& ast = genome.ast;
    const auto& host = host_.ast;
    ast = host;
    ast.nodes.assign(host.nodes.begin(), host.nodes.begin() + begin_);
    const int constant_base = static_cast<int>(host.consts.size());
    for (std::size_t i = 0; i < fragment.nodes.size(); ++i) {
      auto node = fragment.nodes[i];
      if (fragment.roles[i] == PhaseFragment::Role::Bound) node.i0 = binders_.at(static_cast<std::size_t>(node.i0));
      else if (fragment.roles[i] == PhaseFragment::Role::Constant) node.i0 += constant_base;
      ast.nodes.push_back(node);
    }
    ast.nodes.insert(ast.nodes.end(), host.nodes.begin() + end_, host.nodes.end());
    ast.consts.insert(ast.consts.end(), fragment.consts.begin(), fragment.consts.end());
    const std::ptrdiff_t shift = static_cast<std::ptrdiff_t>(fragment.nodes.size()) -
                                 static_cast<std::ptrdiff_t>(end_ - begin_);
    // Host metadata outside the replaced subtree is kept and shifted; the
    // fragment carries fuel profiles only.
    const auto relocate = [&](auto& entries) {
      auto kept = entries;
      kept.clear();
      for (auto entry : entries) {
        if (entry.node_index >= begin_ && entry.node_index < end_) continue;
        if (entry.node_index >= end_)
          entry.node_index = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(entry.node_index) + shift);
        kept.push_back(std::move(entry));
      }
      entries = std::move(kept);
    };
    relocate(ast.fuel_specs);
    relocate(ast.lexical_regions);
    relocate(ast.bounded_region_specs);
    relocate(ast.traversal_specs);
    for (const auto& spec : fragment.fuel) ast.fuel_specs.push_back({spec.node_index + begin_, spec.charges});
    std::sort(ast.fuel_specs.begin(), ast.fuel_specs.end(),
              [](const NodeFuelSpec& a, const NodeFuelSpec& b) { return a.node_index < b.node_index; });
    return genome;
  }

  // Throws unless the spliced program is a grammar member whose witness derives
  // the fragment from the root nonterminal at the host site.
  void require_member(const PhaseFragment& fragment) const {
    const auto genome = splice(fragment);
    grammar::require_membership(*oracle_, genome, request_);
    const auto witness = grammar::reconstruct_derivation(*oracle_, genome, request_);
    if (witness.lowered_instructions == 0) throw std::runtime_error("independent lowering missing");
    const auto fragment_end = begin_ + fragment.nodes.size();
    const bool derived = std::any_of(witness.choices.begin(), witness.choices.end(), [&](const auto& choice) {
      return choice.nonterminal == root_ && choice.ast_begin == begin_ && choice.ast_end == fragment_end;
    });
    if (!derived) throw std::runtime_error("fragment is not derived from its root at the host site");
  }

  ProjectedFragment project(const PhaseFragment& fragment) const {
    ProjectedFragment out;
    auto& ast = out.genome.ast;
    for (const auto& input : grammar_.inputs()) out.inputs.push_back({input.name, input.type});
    const auto& scope = grammar_.nonterminals().at(root_).scope;
    for (std::size_t i = 0; i < scope.size(); ++i) {
      out.scope_names.push_back("__gagp_scope_" + std::to_string(i));
      ast.names.push_back(out.scope_names.back());
      out.inputs.push_back({out.scope_names.back(), scope[i].type});
    }
    ast.nodes = {{NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN}};
    for (std::size_t i = 0; i < fragment.nodes.size(); ++i) {
      auto node = fragment.nodes[i];
      if (fragment.roles[i] == PhaseFragment::Role::Bound) node.kind = NodeKind::VAR;
      ast.nodes.push_back(node);
    }
    ast.nodes.push_back({NodeKind::BLOCK_NIL});
    ast.consts = fragment.consts;
    for (const auto& spec : fragment.fuel) ast.fuel_specs.push_back({spec.node_index + 3, spec.charges});
    return out;
  }

 private:
  static cli_detail::JsonValue parse_json(const std::string& text) {
    return cli_detail::JsonParser(text, {true, 512}).parse();
  }
  static std::string constant_of(RType type) {
    switch (type) {
      case RType::Int: return R"({"type":"Int","values":["0"]})";
      case RType::Float: return R"({"type":"Float","values":[0]})";
      case RType::Bool: return R"({"type":"Bool","values":[true]})";
      case RType::Char: return R"({"type":"Char","values":["a"]})";
      case RType::String: return R"({"type":"String","values":[""]})";
      case RType::IntList: return R"({"type":"IntList","values":[[]]})";
      case RType::FloatList: return R"({"type":"FloatList","values":[[]]})";
      case RType::StringList: return R"({"type":"StringList","values":[[]]})";
      default: throw std::runtime_error("oracle scope type has no constant");
    }
  }
  static std::shared_ptr<const grammar::CompiledGrammar> oracle_grammar(
      const grammar::CompiledGrammar& grammar, std::uint32_t root) {
    const auto& nt = grammar.nonterminals().at(root);
    const std::string type(grammar::type_name(nt.type));
    std::string body = R"({"ref":")" + nt.stable_id + R"("})";
    for (std::size_t i = nt.scope.size(); i-- > 0;) {
      const std::string bound(grammar::type_name(nt.scope[i].type));
      body = R"({"signature":"let()" + bound + "," + type + ")->" + type + R"(","bind":{"1":[")" +
             nt.scope[i].name + R"("]},"args":[{"constant":)" + constant_of(nt.scope[i].type) + "}," + body + "]}";
    }
    auto document = parse_json(grammar.canonical_definition());
    auto& limits = document.object_v.at("search_limits").object_v;
    limits.at("max_nodes").number_v = std::min(65536.0, limits.at("max_nodes").number_v + 1024);
    limits.at("max_depth").number_v = std::min(256.0, limits.at("max_depth").number_v + 64);
    // Canonical definitions order nonterminals by ID; a run of 'z' longer than
    // every existing ID sorts last and keeps the existing numbering.
    std::size_t longest = 0;
    for (const auto& existing : grammar.nonterminals()) longest = std::max(longest, existing.stable_id.size());
    const std::string host(longest + 1, 'z');
    document.object_v.at("entry") = parse_json(R"({"nonterminal":")" + host + R"(","type":")" + type + R"("})");
    document.object_v.at("nonterminals").array_v.push_back(parse_json(
        R"({"id":")" + host + R"(","type":")" + type +
        R"(","scope":[],"alternatives":[{"id":"host","weight":1,"expression":)" + body + "}]}"));
    auto oracle = std::make_shared<const grammar::CompiledGrammar>(
        grammar::compile_grammar(grammar::parse_definition(grammar::canonical_json(document))));
    bool same = oracle->nonterminals().size() == grammar.nonterminals().size() + 1 &&
                oracle->expressions().size() > grammar.expressions().size();
    for (std::size_t i = 0; same && i < grammar.nonterminals().size(); ++i)
      same = oracle->nonterminals()[i].stable_id == grammar.nonterminals()[i].stable_id;
    for (std::size_t i = 0; same && i < grammar.expressions().size(); ++i)
      same = oracle->expressions()[i].kind == grammar.expressions()[i].kind;
    if (!same) throw std::runtime_error("oracle grammar renumbered the grammar");
    return oracle;
  }

  const grammar::CompiledGrammar& grammar_;
  std::uint32_t root_;
  std::shared_ptr<const grammar::CompiledGrammar> oracle_;
  ProgramGenome host_;
  std::size_t begin_ = 0, end_ = 0;
  std::vector<int> binders_;
  grammar::GenerationRequest request_;
};

}  // namespace gagp::evo::test
