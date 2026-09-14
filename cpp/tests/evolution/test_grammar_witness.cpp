#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "gagp/cli/grammar_artifact.hpp"
#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/grammar/values.hpp"
#include "gagp/evolution/repro/pack.hpp"

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;

namespace {
void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

CompiledGrammar compile(const std::string& text) {
  return compile_grammar(parse_definition(text));
}

void rejects(const std::function<void()>& action, const char* message,
    const std::string& diagnostic = {}) {
  try {
    action();
  } catch (const std::invalid_argument& error) {
    if (!diagnostic.empty() && std::string(error.what()).find(diagnostic) == std::string::npos)
      throw std::runtime_error(std::string(message) + ": " + error.what());
    return;
  }
  throw std::runtime_error(message);
}

std::uint32_t production(const CompiledGrammar& grammar, const std::string& stable_id) {
  const auto found = std::find_if(grammar.productions().begin(), grammar.productions().end(),
      [&](const CompiledProduction& value) { return value.stable_id == stable_id; });
  if (found == grammar.productions().end()) throw std::runtime_error("missing fixture production " + stable_id);
  return found->id;
}

std::uint32_t nonterminal(const CompiledGrammar& grammar, const std::string& stable_id) {
  const auto found = std::find_if(grammar.nonterminals().begin(), grammar.nonterminals().end(),
      [&](const CompiledNonterminal& value) { return value.stable_id == stable_id; });
  if (found == grammar.nonterminals().end()) throw std::runtime_error("missing fixture nonterminal " + stable_id);
  return found->id;
}

bool same_request(const GenerationRequest& left, const GenerationRequest& right) {
  if (left.nonterminal != right.nonterminal || left.type != right.type ||
      left.budget.max_nodes != right.budget.max_nodes || left.budget.max_depth != right.budget.max_depth ||
      left.visible_environment.size() != right.visible_environment.size()) return false;
  for (std::size_t i = 0; i < left.visible_environment.size(); ++i) {
    if (left.visible_environment[i].name != right.visible_environment[i].name ||
        left.visible_environment[i].type != right.visible_environment[i].type) return false;
  }
  return true;
}

bool same_metadata(const DerivationMetadata& left, const DerivationMetadata& right) {
  if (left.seed_replayable != right.seed_replayable || !same_request(left.request, right.request) ||
      left.request_scope_mapping != right.request_scope_mapping || left.grammar_hash != right.grammar_hash ||
      left.semantic_version != right.semantic_version || left.generator_version != right.generator_version ||
      left.rng_version != right.rng_version || left.search_limits.max_nodes != right.search_limits.max_nodes ||
      left.search_limits.max_depth != right.search_limits.max_depth ||
      left.execution_limits.fuel != right.execution_limits.fuel || left.seed != right.seed ||
      left.logical_steps != right.logical_steps || left.derived_nodes != right.derived_nodes ||
      left.lowered_instructions != right.lowered_instructions || left.nodes.size() != right.nodes.size() ||
      left.choices.size() != right.choices.size() || left.templates.size() != right.templates.size() ||
      left.holes.size() != right.holes.size()) return false;
  for (std::size_t i = 0; i < left.nodes.size(); ++i) {
    const auto& a = left.nodes[i]; const auto& b = right.nodes[i];
    if (std::tie(a.expression, a.production, a.nonterminal, a.logical_instance, a.template_instance, a.slot, a.fixed, a.template_depth) !=
        std::tie(b.expression, b.production, b.nonterminal, b.logical_instance, b.template_instance, b.slot, b.fixed, b.template_depth)) return false;
  }
  for (std::size_t i = 0; i < left.choices.size(); ++i) {
    const auto& a = left.choices[i]; const auto& b = right.choices[i];
    if (std::tie(a.nonterminal, a.production, a.parent, a.ast_begin, a.ast_end, a.template_instance, a.slot, a.enclosing_template_depth) !=
        std::tie(b.nonterminal, b.production, b.parent, b.ast_begin, b.ast_end, b.template_instance, b.slot, b.enclosing_template_depth)) return false;
  }
  for (std::size_t i = 0; i < left.templates.size(); ++i) {
    const auto& a = left.templates[i]; const auto& b = right.templates[i];
    if (std::tie(a.template_id, a.parent) != std::tie(b.template_id, b.parent)) return false;
  }
  for (std::size_t i = 0; i < left.holes.size(); ++i) {
    const auto& a = left.holes[i]; const auto& b = right.holes[i];
    if (std::tie(a.template_instance, a.slot, a.ast_begin, a.ast_end) !=
        std::tie(b.template_instance, b.slot, b.ast_begin, b.ast_end)) return false;
  }
  return true;
}

ProgramGenome imported_add(std::int64_t left, std::int64_t right) {
  ProgramGenome genome;
  genome.ast.nodes = {{NodeKind::PROGRAM, 0, 0}, {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::RETURN, 0, 0}, {NodeKind::ADD, 0, 0}, {NodeKind::CONST, 0, 0},
      {NodeKind::CONST, 1, 0}, {NodeKind::BLOCK_NIL, 0, 0}};
  genome.ast.consts = {Value::from_int(left), Value::from_int(right)};
  genome.meta = build_genome_meta(genome.ast);
  return genome;
}

void test_deterministic_witness_and_backtracking() {
  const auto grammar = compile(R"({
    "format_version":"grammar-definition-v1","entry":{"nonterminal":"Expr","type":"Int"},
    "search_limits":{"max_nodes":12,"max_depth":7},"execution_limits":{"fuel":100},
    "nonterminals":[{"id":"Expr","type":"Int","scope":[],"alternatives":[
      {"id":"almost","weight":1,"expression":{"signature":"add(Int,Int)->Int","args":[
        {"constant":{"type":"Int","values":["1"]}},{"constant":{"type":"Int","values":["2"]}}]}},
      {"id":"match","weight":1,"expression":{"signature":"add(Int,Int)->Int","args":[
        {"constant":{"type":"Int","values":["1"]}},{"constant":{"type":"Int","values":["3"]}}]}}
    ]}]})");
  const auto genome = imported_add(1, 3);
  check(!genome.derivation, "imported fixture unexpectedly has provenance");
  const auto first = reconstruct_derivation(grammar, genome);
  const auto second = reconstruct_derivation(grammar, genome);
  check(same_metadata(first, second), "witness reconstruction is not deterministic");
  check(!first.seed_replayable && first.grammar_hash == grammar.content_hash() &&
        first.nodes.size() == genome.ast.nodes.size() && first.choices.size() == 1,
        "reconstructed witness lost identity or node alignment");
  const auto selected = production(grammar, "Expr/match");
  check(first.choices[0].nonterminal == grammar.entry() && first.choices[0].production == selected &&
        first.choices[0].parent == kNoGrammarId && first.choices[0].ast_begin == 3 &&
        first.choices[0].ast_end == 6, "failed first alternative leaked state into the selected choice");
  for (std::size_t i = 0; i < first.nodes.size(); ++i) {
    const auto& origin = first.nodes[i];
    if (i < 3 || i == 6) {
      check(origin.fixed && origin.expression == kNoGrammarId && origin.production == kNoGrammarId &&
            origin.nonterminal == kNoGrammarId, "expression envelope has a derived origin");
    } else {
      check(!origin.fixed && origin.expression < grammar.expressions().size() &&
            origin.production == selected && origin.nonterminal == grammar.entry() &&
            origin.logical_instance != kNoGrammarId, "derived node origin does not describe the selected production");
    }
  }
  check(first.derived_nodes == 3 && first.search_limits.max_nodes == 12 &&
        first.search_limits.max_depth == 7 && first.execution_limits.fuel == 100 &&
        first.lowered_instructions > 0, "reconstructed witness lost limits or accounting");
}

CompiledGrammar template_grammar() {
  return compile(R"({
    "format_version":"grammar-definition-v1","entry":{"nonterminal":"Main","type":"Int"},
    "search_limits":{"max_nodes":20,"max_depth":10},"execution_limits":{"fuel":1000},
    "templates":[
      {"id":"Double","type":"Int","scope":[],"holes":[{"id":"value","type":"Int","scope":[]}],
       "body":{"signature":"add(Int,Int)->Int","args":[{"hole":"value"},{"hole":"value"}]}},
      {"id":"Forward","type":"Int","scope":[],"holes":[{"id":"value","type":"Int","scope":[]}],
       "body":{"template":"Double","holes":{"value":{"hole":"value"}}}}
    ],
    "nonterminals":[
      {"id":"Value","type":"Int","scope":[],"alternatives":[
       {"id":"leaf","weight":1,"expression":{"constant":{"type":"Int","range":["0","3"]}}},
       {"id":"sum","weight":4,"expression":{"signature":"add(Int,Int)->Int","args":[
        {"ref":"Value"},{"ref":"Value"}]}}]},
      {"id":"Main","type":"Int","scope":[],"alternatives":[{"id":"forward","weight":1,
       "expression":{"template":"Forward","holes":{"value":{"ref":"Value"}}}}]}
    ]})");
}

void test_templates_domains_and_provenance_independence() {
  const auto grammar = template_grammar();
  ProgramGenome genome;
  bool saw_recursive_shared_subtree = false;
  for (std::uint64_t seed = 0; seed < 64; ++seed) {
    auto candidate = generate_derivation(grammar, seed).genome;
    candidate.derivation.reset();
    const auto first = reconstruct_derivation(grammar, candidate);
    const auto second = reconstruct_derivation(grammar, candidate);
    check(same_metadata(first, second), "recursive template witness changed across repeated reconstruction");
    for (std::size_t choice_index = 0; choice_index < first.choices.size(); ++choice_index) {
      const auto& choice = first.choices[choice_index];
      check(choice.ast_begin < choice.ast_end && choice.ast_end <= candidate.ast.nodes.size(),
            "recursive witness choice has an invalid span");
      if (choice.parent != kNoGrammarId) {
        check(choice.parent < choice_index, "recursive witness choice parent points forward");
        const auto& parent = first.choices[choice.parent];
        check(parent.ast_begin <= choice.ast_begin && parent.ast_end >= choice.ast_end,
              "recursive witness choice escaped its parent span");
      }
    }
    for (std::size_t i = 0; i < first.holes.size(); ++i) {
      const auto& a = first.holes[i];
      for (std::size_t j = i + 1; j < first.holes.size(); ++j) {
        const auto& b = first.holes[j];
        if (a.template_instance != b.template_instance || a.slot != b.slot ||
            a.ast_end - a.ast_begin != b.ast_end - b.ast_begin) continue;
        if (a.ast_end - a.ast_begin > 1) saw_recursive_shared_subtree = true;
        for (std::uint32_t offset = 0; offset < a.ast_end - a.ast_begin; ++offset)
          check(first.nodes[a.ast_begin + offset].logical_instance ==
                first.nodes[b.ast_begin + offset].logical_instance,
                "recursive shared subtree lost its logical trace");
      }
    }
    if (genome.ast.nodes.empty() && std::all_of(first.holes.begin(), first.holes.end(),
        [](const HoleOccurrence& hole) { return hole.ast_end - hole.ast_begin == 1; })) genome = std::move(candidate);
  }
  check(saw_recursive_shared_subtree, "64 template seeds did not exercise a recursive shared subtree");
  check(!genome.ast.nodes.empty(), "64 template seeds did not provide a leaf rejection fixture");
  const auto witness = reconstruct_derivation(grammar, genome);
  check(!witness.seed_replayable && witness.templates.size() == 2 && witness.holes.size() == 4,
        "forwarded template witness lost instances or hole occurrences");
  bool saw_fixed_skeleton = false;
  bool saw_copied_occurrences = false;
  for (const auto& origin : witness.nodes)
    saw_fixed_skeleton = saw_fixed_skeleton || (origin.fixed && origin.template_instance != kNoGrammarId);
  for (std::size_t i = 0; i < witness.holes.size(); ++i) {
    const auto& a = witness.holes[i];
    check(a.ast_begin < a.ast_end && a.ast_end <= witness.nodes.size(), "template hole has an invalid AST span");
    for (std::size_t j = i + 1; j < witness.holes.size(); ++j) {
      const auto& b = witness.holes[j];
      if (a.template_instance != b.template_instance || a.slot != b.slot ||
          a.ast_end - a.ast_begin != b.ast_end - b.ast_begin) continue;
      saw_copied_occurrences = true;
      for (std::uint32_t offset = 0; offset < a.ast_end - a.ast_begin; ++offset) {
        const auto& x = witness.nodes[a.ast_begin + offset];
        const auto& y = witness.nodes[b.ast_begin + offset];
        check(x.logical_instance == y.logical_instance && x.template_instance == y.template_instance &&
              x.slot == y.slot && x.template_instance != kNoGrammarId && x.slot != kNoGrammarId,
              "repeated hole copies lost logical, template, or slot identity");
      }
    }
  }
  check(saw_fixed_skeleton, "template skeleton nodes were not marked fixed");
  check(saw_copied_occurrences, "fixture did not expose a repeated forwarded hole");

  auto witnessed = genome;
  witnessed.derivation = std::make_shared<const DerivationMetadata>(witness);
  const auto clone = witnessed;
  check(clone.derivation == witnessed.derivation && !clone.derivation->seed_replayable,
        "clone lost reconstructed immutable provenance or made it seed-replayable");
  const auto witnessed_compacted = repro::compact_genome_tables(clone);
  check(witnessed_compacted.derivation == witnessed.derivation &&
        !witnessed_compacted.derivation->seed_replayable &&
        same_metadata(witness, reconstruct_derivation(grammar, witnessed_compacted)),
        "compaction lost reconstructed provenance or reconstructibility");

  auto bad_shared = genome;
  const auto repeated = std::find_if(witness.holes.begin(), witness.holes.end(), [&](const HoleOccurrence& a) {
    return std::any_of(witness.holes.begin(), witness.holes.end(), [&](const HoleOccurrence& b) {
      return &a != &b && a.template_instance == b.template_instance && a.slot == b.slot;
    });
  });
  check(repeated != witness.holes.end(), "missing repeated-hole rejection fixture");
  const auto changed_node = repeated->ast_begin;
  check(bad_shared.ast.nodes[changed_node].kind == NodeKind::CONST, "repeated-hole fixture is not a leaf");
  bad_shared.ast.consts.push_back(Value::from_int((bad_shared.ast.consts[bad_shared.ast.nodes[changed_node].i0].i + 1) % 4));
  bad_shared.ast.nodes[changed_node].i0 = static_cast<int>(bad_shared.ast.consts.size() - 1);
  rejects([&] { reconstruct_derivation(grammar, bad_shared); }, "unequal shared-hole copies were reconstructed");

  auto bad_domain = genome;
  bad_domain.ast.consts.push_back(Value::from_int(4));
  for (auto& node : bad_domain.ast.nodes)
    if (node.kind == NodeKind::CONST) node.i0 = static_cast<int>(bad_domain.ast.consts.size() - 1);
  rejects([&] { reconstruct_derivation(grammar, bad_domain); }, "out-of-domain constant was reconstructed");

  auto padded = genome;
  padded.ast.names.push_back("unused");
  padded.ast.consts.push_back(Value::from_int(999));
  auto stale = std::make_shared<DerivationMetadata>();
  stale->grammar_hash = "stale-provenance-must-be-ignored";
  stale->nodes.resize(1);
  padded.derivation = stale;
  const auto stale_witness = reconstruct_derivation(grammar, padded);
  padded.derivation.reset();
  check(same_metadata(stale_witness, reconstruct_derivation(grammar, padded)),
        "supplied stale provenance influenced reconstruction");
  const auto compacted = repro::compact_genome_tables(padded);
  check(same_metadata(witness, reconstruct_derivation(grammar, compacted)),
        "clone/table compaction changed the reconstructed witness");
}

void test_custom_request_and_artifact_boundary() {
  const auto grammar = compile(R"({
    "format_version":"grammar-definition-v1","entry":{"nonterminal":"Entry","type":"Int"},
    "search_limits":{"max_nodes":12,"max_depth":7},"execution_limits":{"fuel":100},
    "nonterminals":[
      {"id":"Entry","type":"Int","scope":[],"alternatives":[{"id":"zero","weight":1,
       "expression":{"constant":{"type":"Int","values":["0"]}}}]},
      {"id":"Scoped","type":"Int","scope":[{"name":"x","type":"Int"},{"name":"label","type":"String"}],
       "alternatives":[{"id":"seven","weight":1,"expression":{"constant":{"type":"Int","values":["7"]}}}]}
    ]})");
  GenerationRequest request{nonterminal(grammar, "Scoped"), RType::Int,
      {{"extra", RType::Bool}, {"label", RType::String}, {"x", RType::Int}}, {5, 4}};
  auto genome = generate_derivation(grammar, 41, request).genome;
  genome.derivation.reset();
  const auto witness = reconstruct_derivation(grammar, genome, request);
  check(!witness.seed_replayable && same_request(witness.request, request) &&
        witness.request_scope_mapping == std::vector<std::uint32_t>({2, 1}) &&
        witness.search_limits.max_nodes == 5 && witness.search_limits.max_depth == 4,
        "custom request or scope mapping was not preserved by reconstruction");

  GeneratedDerivation reconstructed{genome, witness};
  rejects([&] { cli_detail::encode_generated_artifact(grammar, reconstructed); },
      "generated-artifact encoder accepted reconstructed provenance", "reconstructed");
}
}  // namespace

int main() {
  try {
    test_deterministic_witness_and_backtracking();
    test_templates_domains_and_provenance_independence();
    test_custom_request_and_artifact_boundary();
    std::cout << "grammar witness: deterministic reconstruction, template identity, requests, and artifact boundary passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
