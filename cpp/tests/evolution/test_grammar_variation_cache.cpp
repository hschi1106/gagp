#include <algorithm>
#include <atomic>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/cache.hpp"
#include "gagp/evolution/grammar/variation_cache.hpp"
#include "gagp/runtime/payload/payload.hpp"
#include "../fixtures/mixed_population.hpp"
#include "../../src/runtime/payload/staging.hpp"
#include "../../src/evolution/batch_workers.hpp"

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

void test_batch_worker_barrier() {
  detail::BatchWorkers team(4);
  for (unsigned batch = 0; batch < 8; ++batch) {
    std::atomic<unsigned> completed{0};
    std::atomic<unsigned> started{0};
    bool failed = false;
    try {
      team.run([&] {
        payload::StagedPayloads reads;
        check(!payload::StagedPayloads::has_active_scope(),
              "worker retained a previous payload scope");
        payload::StagedPayloads::Scope scope(reads);
        const auto index = started.fetch_add(1);
        if (batch % 2 && index == 0) throw std::invalid_argument("batch failure");
        completed.fetch_add(1);
      });
    } catch (const std::invalid_argument&) { failed = true; }
    check(failed == bool(batch % 2), "worker exception was lost or crossed batches");
    check(started == 4 && completed == (batch % 2 ? 3 : 4),
          "batch returned before every worker finished");
  }
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

  std::string first_identity, repeated_identity;
  const auto identity_request = entry_request(*grammar);
  const auto first = cache.analyze(genome, identity_request, &first_identity);
  const auto repeated = cache.analyze_member(genome, {identity_request}, &repeated_identity);
  check(first_identity == runtime_cache_identity(genome, {}, 100) &&
        repeated_identity == first_identity, "lookup identity differs on miss/hit");
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
  // An outer generation transaction is thread-local. Warming must neither
  // dispatch readers that cannot see it nor publish its payloads prematurely.
  const auto token = Value::from_string_hash_len(17823641, 5);
  payload_genome.ast.consts[0] = token;
  VariationAnalysisCache staged_cache(string_grammar);
  payload::StagedPayloads transaction;
  check(!payload::StagedPayloads::has_active_scope(), "payload scope leaked into cache test");
  {
    payload::StagedPayloads::Scope scope(transaction);
    payload::register_string(token, "alive");
    const std::vector<ProgramGenome> population(40, payload_genome);
    const std::vector<GenerationRequest> requests{entry_request(*string_grammar)};
    staged_cache.warm_candidates(population, requests, 4);
    check(staged_cache.counters().hits == 0 && staged_cache.counters().misses == 0,
          "deferred warming committed an enclosing transaction's analysis");
    staged_cache.warm_population(population, requests, 4);
    check(staged_cache.analyze(payload_genome)->verified.return_type == RType::String,
          "population warming lost an enclosing transaction's payload view");
  }
  std::string absent;
  check(!payload::lookup_string(token, &absent), "warming committed an outer staged payload");
  rejects([&] { (void)staged_cache.analyze(payload_genome); },
          "aborted outer transaction left a usable stale payload analysis");
}

void test_population_root_reconstruction() {
  const auto config = gagp::test::mixed_population_config();
  const auto& grammar = *config.compiled_grammar;
  std::vector<GenerationRequest> requests{*config.generation_request};
  requests.insert(requests.end(), config.additional_generation_requests.begin(),
      config.additional_generation_requests.end());
  VariationAnalysisCache cache(config.compiled_grammar);
  for (const auto& request : requests) {
    const auto genome = generate_derivation(grammar, 7, request).genome;
    const auto explicit_root = analyze_variation(grammar, genome, request);
    const auto selected = cache.analyze_member(genome, requests);
    check(selected->witness.request.nonterminal == request.nonterminal &&
          selected->witness.lowered_instructions == explicit_root.witness.lowered_instructions &&
          selected->verified.expression_types == explicit_root.verified.expression_types &&
          selected->verified.expression_scope_ids == explicit_root.verified.expression_scope_ids &&
          selected->sites.size() == explicit_root.sites.size(),
          "population root selection changed the explicit-root analysis");
    for (std::size_t i = 0; i < selected->sites.size(); ++i)
      check(selected->sites[i].compatibility_key == explicit_root.sites[i].compatibility_key,
            "population root selection changed a variation contract");
  }
  auto invalid = generate_derivation(grammar, 7, requests.front()).genome;
  invalid.ast.consts[0] = Value::from_int(999);
  rejects([&] { (void)cache.analyze_member(invalid, requests); },
          "matching result type bypassed root grammar membership");
  auto missing = requests;
  missing.erase(missing.begin());
  rejects([&] { (void)cache.analyze_member(invalid, missing); },
          "unlisted root result type was admitted");
  invalid.ast.nodes.pop_back();
  rejects([&] { (void)cache.analyze_member(invalid, requests); },
          "population root selection skipped native verification");
}

void test_deferred_candidate_analysis() {
  const auto config = gagp::test::mixed_population_config();
  std::vector<GenerationRequest> requests{*config.generation_request};
  requests.insert(requests.end(), config.additional_generation_requests.begin(),
      config.additional_generation_requests.end());
  std::vector<ProgramGenome> candidates;
  for (std::uint64_t i = 0; i < 160; ++i)
    candidates.push_back(generate_derivation(*config.compiled_grammar, i,
        requests[i % requests.size()]).genome);
  candidates[17].ast.nodes.pop_back();
  for (const auto capacity : {std::size_t{3}, std::size_t{256}}) {
    VariationAnalysisCache ordered(config.compiled_grammar, capacity);
    VariationAnalysisCache deferred(config.compiled_grammar, capacity);
    deferred.warm_candidates(candidates, requests, 4);
    check(deferred.registry().keys().empty() && deferred.counters().hits == 0 &&
          deferred.counters().misses == 0 && deferred.counters().evictions == 0,
          "speculative candidates changed registry or accounting before admission");
    // Consume in a different order from speculation, across the batch boundary.
    for (std::size_t i = candidates.size(); i-- > 0;) {
      if (i == 17) {
        rejects([&] { ordered.analyze_member(candidates[i], requests); }, "oracle accepted malformed child");
        rejects([&] { deferred.analyze_member(candidates[i], requests); }, "speculation admitted malformed child");
      } else {
        const auto a = ordered.analyze_member(candidates[i], requests);
        const auto b = deferred.analyze_member(candidates[i], requests);
        check(a->sites.size() == b->sites.size(), "speculation changed candidate sites");
        for (std::size_t j = 0; j < a->sites.size(); ++j)
          check(a->sites[j].compatibility_id == b->sites[j].compatibility_id,
                "deferred compatibility registration changed IDs");
      }
      check(ordered.registry().keys() == deferred.registry().keys() &&
            ordered.counters().hits == deferred.counters().hits &&
            ordered.counters().misses == deferred.counters().misses &&
            ordered.counters().evictions == deferred.counters().evictions,
            "speculation changed ordered registry or cache accounting");
    }
  }
}
void test_parallel_population_analysis() {
  const auto config = gagp::test::mixed_population_config();
  std::vector<GenerationRequest> requests{*config.generation_request};
  requests.insert(requests.end(), config.additional_generation_requests.begin(),
      config.additional_generation_requests.end());
  std::vector<ProgramGenome> population;
  for (std::uint64_t i = 0; i < 160; ++i)
    population.push_back(generate_derivation(*config.compiled_grammar, i,
        requests[i % requests.size()]).genome);
  for (const auto capacity : {std::size_t{3}, std::size_t{256}}) {
    VariationAnalysisCache sequential(config.compiled_grammar, capacity);
    VariationAnalysisCache parallel(config.compiled_grammar, capacity);
    sequential.warm_population(population, requests, 1);
    parallel.warm_population(population, requests, 4);
    check(sequential.registry().keys() == parallel.registry().keys(),
          "parallel analysis reordered compatibility IDs");
    check(sequential.counters().hits == parallel.counters().hits &&
          sequential.counters().misses == parallel.counters().misses &&
          sequential.counters().evictions == parallel.counters().evictions,
          "parallel analysis changed ordered cache accounting");
    for (const auto& genome : population) {
      const auto a = sequential.analyze_member(genome, requests);
      const auto b = parallel.analyze_member(genome, requests);
      check(a->verified.expression_types == b->verified.expression_types &&
            a->witness.lowered_instructions == b->witness.lowered_instructions &&
            a->sites.size() == b->sites.size(), "parallel analysis changed verification");
      for (std::size_t i = 0; i < a->sites.size(); ++i)
        check(a->sites[i].compatibility_key == b->sites[i].compatibility_key &&
              a->sites[i].compatibility_id == b->sites[i].compatibility_id,
              "parallel analysis changed a site contract");
    }
  }
  VariationAnalysisCache invalid(config.compiled_grammar);
  population[17].ast.nodes.pop_back();
  rejects([&] { invalid.warm_population(population, requests, 4); },
          "parallel analysis swallowed malformed AST");
  rejects([&] { invalid.warm_population(population, requests, 0); },
          "parallel analysis accepted zero workers");
}

}  // namespace

int main() {
  try {
    test_batch_worker_barrier();
    test_identity_and_requests();
    test_population_root_reconstruction();
    test_parallel_population_analysis();
    test_deferred_candidate_analysis();
    test_fifo_and_lifetimes();
    test_grammar_and_payload_boundaries();
    std::cout << "grammar variation cache: exact identity, FIFO, and lifetimes passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
