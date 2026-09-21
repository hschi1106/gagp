#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/variation_cache.hpp"
#include "gagp/runtime/payload/payload.hpp"

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;

namespace {

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void rejects(const std::function<void()>& action, const char* message) {
  try {
    action();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error(message);
}

std::shared_ptr<const CompiledGrammar> fixture(int root_weight = 1) {
  return std::make_shared<const CompiledGrammar>(compile_grammar(parse_definition(
      R"({"format_version":"grammar-definition-v2",
      "entry":{"nonterminal":"Root","type":"Int"},
      "search_limits":{"max_nodes":9,"max_depth":6},
      "execution_limits":{"fuel":100},
      "nonterminals":[
        {"id":"Other","type":"Int","scope":[],"alternatives":[
          {"id":"value","weight":1,"expression":{"constant":{"type":"Int","values":["7","8"]}}}
        ]},
        {"id":"Root","type":"Int","scope":[],"alternatives":[
          {"id":"value","weight":)" + std::to_string(root_weight) +
      R"(,"expression":{"constant":{"type":"Int","values":["7","8"]}}}
        ]}
      ]})")));
}

std::uint32_t nonterminal(const CompiledGrammar& grammar, const std::string& name) {
  const auto found = std::find_if(grammar.nonterminals().begin(), grammar.nonterminals().end(),
      [&](const auto& value) { return value.stable_id == name; });
  if (found == grammar.nonterminals().end()) throw std::runtime_error("missing fixture nonterminal");
  return found->id;
}

ProgramGenome genome_for(const CompiledGrammar& grammar, std::int64_t value) {
  auto genome = generate_derivation(grammar, 3).genome;
  check(genome.ast.consts.size() == 1, "fixture did not generate one constant");
  genome.ast.consts[0] = Value::from_int(value);
  return genome;
}

void test_identity_and_requests() {
  auto grammar = fixture();
  VariationAnalysisCache cache(grammar);
  auto genome = genome_for(*grammar, 7);

  const auto first = cache.analyze(genome);
  const auto repeated = cache.analyze(genome);
  check(first == repeated, "identical cache input did not return the same analysis");
  check(cache.counters().hits == 1 && cache.counters().misses == 1,
        "basic hit/miss counters are incorrect");

  auto stale = std::make_shared<DerivationMetadata>();
  stale->grammar_hash = "stale attached derivation";
  genome.derivation = stale;
  check(cache.analyze(genome) == first, "stale derivation metadata changed the cache identity");

  auto changed_value = genome;
  changed_value.ast.consts[0] = Value::from_int(8);
  check(cache.analyze(changed_value) != first, "decoded domain value was omitted from the key");

  auto changed_ast = genome;
  changed_ast.ast.consts.push_back(Value::from_int(7));
  const auto constant = std::find_if(changed_ast.ast.nodes.begin(), changed_ast.ast.nodes.end(),
      [](const auto& node) { return node.kind == NodeKind::CONST; });
  check(constant != changed_ast.ast.nodes.end(), "fixture has no constant node");
  constant->i0 = 1;
  check(cache.analyze(changed_ast) != first, "AST or constant-pool structure was omitted from the key");

  GenerationRequest request = entry_request(*grammar);
  request.budget.max_nodes = 8;
  check(cache.analyze(genome, request) != first, "node budget was omitted from the key");
  request = entry_request(*grammar);
  request.budget.max_depth = 5;
  check(cache.analyze(genome, request) != first, "depth budget was omitted from the key");

  GenerationRequest environment = entry_request(*grammar);
  environment.visible_environment = {{"x", RType::Int}, {"label", RType::String}};
  const auto ordered = cache.analyze(genome, environment);
  auto changed_environment_type = environment;
  changed_environment_type.visible_environment[0].type = RType::Float;
  check(cache.analyze(genome, changed_environment_type) != ordered,
        "visible-environment type was omitted from the key");
  std::swap(environment.visible_environment[0], environment.visible_environment[1]);
  check(cache.analyze(genome, environment) != ordered,
        "visible-environment order was omitted from the key");

  GenerationRequest other = entry_request(*grammar);
  other.nonterminal = nonterminal(*grammar, "Other");
  check(cache.analyze(genome, other) != first, "requested nonterminal was omitted from the key");

  const auto before = cache.counters();
  auto invalid = entry_request(*grammar);
  invalid.budget.max_nodes = 0;
  rejects([&] { (void)cache.analyze(genome, invalid); }, "invalid request was accepted");
  check(cache.counters().hits == before.hits && cache.counters().misses == before.misses &&
        cache.counters().evictions == before.evictions,
        "invalid request affected cache lookup accounting");
}

void test_fifo_and_lifetimes() {
  auto grammar = fixture();
  auto first_genome = genome_for(*grammar, 7);
  VariationAnalysisCache cache(grammar, 2);
  grammar.reset();
  auto second_genome = first_genome;
  second_genome.ast.consts[0] = Value::from_int(8);
  auto third_genome = first_genome;
  third_genome.ast.consts.push_back(Value::from_int(7));

  const auto first = cache.analyze(first_genome);
  (void)cache.analyze(second_genome);
  check(cache.analyze(first_genome) == first, "cache hit unexpectedly changed FIFO order entry");
  (void)cache.analyze(third_genome);
  const auto reloaded = cache.analyze(first_genome);
  check(reloaded != first, "oldest insertion was not evicted");
  check(cache.counters().hits == 1 && cache.counters().misses == 4 &&
        cache.counters().evictions == 2, "FIFO counters are incorrect");

  rejects([&] { VariationAnalysisCache invalid(nullptr); }, "null grammar was accepted");
  rejects([&] { VariationAnalysisCache invalid(fixture(), 0); }, "zero capacity was accepted");
}

void test_grammar_and_payload_boundaries() {
  VariationAnalysisCache first(fixture(1));
  VariationAnalysisCache second(fixture(9));
  auto genome = genome_for(*fixture(), 7);
  const auto left = first.analyze(genome);
  const auto right = second.analyze(genome);
  check(left != right, "separate grammar caches reused an analysis object");
  check(!left->sites.empty() && left->sites[0].compatibility_key != right->sites[0].compatibility_key,
        "grammar content hash was omitted from compatibility identity");

  const auto string_grammar = std::make_shared<const CompiledGrammar>(compile_grammar(parse_definition(
      R"({"format_version":"grammar-definition-v2",
      "entry":{"nonterminal":"Root","type":"String"},
      "search_limits":{"max_nodes":5,"max_depth":4},
      "execution_limits":{"fuel":100},
      "nonterminals":[{"id":"Root","type":"String","scope":[],"alternatives":[
        {"id":"value","weight":1,"expression":{"constant":{"type":"String","values":["alive"]}}}
      ]}]})")));
  VariationAnalysisCache payload_cache(string_grammar);
  auto payload_genome = generate_derivation(*string_grammar, 1).genome;
  (void)payload_cache.analyze(payload_genome);
  payload::clear();
  rejects([&] { (void)payload_cache.analyze(payload_genome); },
          "expired payload token returned a stale cache hit");
}

}  // namespace

int main() {
  try {
    test_identity_and_requests();
    test_fifo_and_lifetimes();
    test_grammar_and_payload_boundaries();
    std::cout << "grammar variation cache: exact identity, FIFO, and lifetimes passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
