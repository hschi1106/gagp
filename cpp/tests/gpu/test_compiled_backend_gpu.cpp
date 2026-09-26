#include "../../src/evolution/repro/compiled_decode.hpp"
#include "../../src/runtime/payload/staging.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/grammar/values.hpp"
#include "gagp/evolution/lifecycle.hpp"
#include "gagp/evolution/population_init.hpp"
#include "../fixtures/mixed_population.hpp"
#include "../fixtures/closed_crossover.hpp"
#include "../fixtures/bounded_capture.hpp"
#include "gagp/evolution/repro/backend.hpp"
#include "gagp/evolution/repro/gpu.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "gagp/runtime/payload/payload.hpp"
#include "../../src/evolution/repro/constant_prep.hpp"

namespace {

using gagp::ExecResult;
using gagp::ValueTag;
using gagp::evo::EvolutionConfig;
using gagp::evo::ProgramGenome;
using gagp::evo::ScoredGenome;
using gagp::evo::ast_cache_key;
using gagp::evo::grammar::CompiledGrammar;
using gagp::evo::grammar::GenerationRequest;
using gagp::evo::grammar::VariationCounters;
using gagp::evo::repro::GpuReproPreparedData;
using gagp::evo::repro::ReproductionBackend;
using gagp::evo::repro::ReproductionResult;
using gagp::evo::repro::ReproductionStats;

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

void expect_invalid(const std::function<void()>& action,
                    const std::string& description) {
  try {
    action();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error(description);
}

std::shared_ptr<const CompiledGrammar> repeated_capture_grammar() {
  return std::make_shared<const CompiledGrammar>(
      gagp::evo::grammar::compile_grammar(
          gagp::evo::grammar::parse_definition(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","type":"Int"},
    "search_limits":{"max_nodes":20,"max_depth":10},
    "execution_limits":{"fuel":100},
    "templates":[{"id":"Sibling","type":"Int","scope":[],
      "holes":[{"id":"value","type":"Int","scope":[
        {"name":"x","type":"Int"}]}],
      "body":{"signature":"add(Int,Int)->Int","args":[
        {"signature":"let(Int,Int)->Int","args":[
          {"constant":{"type":"Int","values":["1"]}},{"hole":"value"}],
         "bind":{"1":["x"]}},
        {"signature":"let(Int,Int)->Int","args":[
          {"constant":{"type":"Int","values":["2"]}},{"hole":"value"}],
         "bind":{"1":["x"]}}]}}],
    "nonterminals":[
      {"id":"Main","type":"Int","scope":[],"alternatives":[
        {"id":"main","weight":1,"expression":{"template":"Sibling","holes":{
          "value":{"ref":"Scoped"}}}}]},
      {"id":"Scoped","type":"Int","scope":[{"name":"x","type":"Int"}],
       "alternatives":[{"id":"bound_or_offset","weight":1,
         "expression":{"signature":"add(Int,Int)->Int","args":[
           {"bound":"x"},{"constant":{"type":"Int","values":["0","5"]}}]}}]}
    ]
  })")));
}

std::shared_ptr<const CompiledGrammar> payload_domain_grammar() {
  return std::make_shared<const CompiledGrammar>(
      gagp::evo::grammar::compile_grammar(
          gagp::evo::grammar::parse_definition(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","type":"Int"},
    "search_limits":{"max_nodes":5,"max_depth":4},
    "execution_limits":{"fuel":100},
    "nonterminals":[
      {"id":"Main","type":"Int","scope":[],"alternatives":[
        {"id":"main","weight":1,"expression":{
          "constant":{"type":"Int","values":["7"]}}}]},
      {"id":"UnusedString","type":"String","scope":[],"alternatives":[
        {"id":"payload","weight":1,"expression":{
          "constant":{"type":"String","values":["resource-only-payload"]}}}]},
      {"id":"UnusedInts","type":"IntList","scope":[],"alternatives":[
        {"id":"payload","weight":1,"expression":{
          "constant":{"type":"IntList","values":[["4","-9"]]}}}]},
      {"id":"UnusedFloats","type":"FloatList","scope":[],"alternatives":[
        {"id":"payload","weight":1,"expression":{
          "constant":{"type":"FloatList","values":[[1.25,-2.5]]}}}]},
      {"id":"UnusedStrings","type":"StringList","scope":[],"alternatives":[
        {"id":"payload","weight":1,"expression":{
          "constant":{"type":"StringList","values":[["nested-a","nested-b"]]}}}]}
    ]
  })")));
}

std::int64_t execute_int(const ProgramGenome& genome) {
  const auto verified = gagp::evo::verify_ast(genome.ast, {});
  require(verified.ok, "compiled backend child failed native AST verification: " +
                           verified.diagnostic.message);
  const ExecResult result = gagp::execute_bytecode_cpu(
      gagp::evo::compile_for_eval(genome, verified.verified), {}, 100);
  require(!result.is_error && result.value.tag == ValueTag::Int,
          "compiled backend child did not execute to Int with fuel 100");
  return result.value.i;
}

void require_certified_population(const CompiledGrammar& grammar,
                                  const GenerationRequest& request,
                                  const std::vector<ProgramGenome>& population,
                                  std::size_t expected_size) {
  require(population.size() == expected_size,
          "compiled backend changed the logical population size");
  for (const auto& child : population) {
    require(child.derivation != nullptr,
            "compiled backend returned a child without derivation certification");
    gagp::evo::grammar::require_membership(grammar, child, request);
    const std::int64_t value = execute_int(child);
    require(value == 3 || value == 13,
            "scoped repeated-hole child changed the grammar's executable result set");
  }
}

bool same_counters(const VariationCounters& left,
                   const VariationCounters& right) {
  return left.crossover_attempts == right.crossover_attempts &&
         left.mutation_attempts == right.mutation_attempts &&
         left.contract_rejections == right.contract_rejections &&
         left.budget_rejections == right.budget_rejections &&
         left.generation_rejections == right.generation_rejections &&
         left.acceptance_rejections == right.acceptance_rejections &&
         left.fallback_children == right.fallback_children &&
         left.unchanged_children == right.unchanged_children &&
         left.changed_children == right.changed_children;
}

bool same_population(const std::vector<ProgramGenome>& left,
                     const std::vector<ProgramGenome>& right) {
  if (left.size() != right.size()) return false;
  for (std::size_t i = 0; i < left.size(); ++i) {
    if (ast_cache_key(left[i].ast) != ast_cache_key(right[i].ast)) return false;
  }
  return true;
}

void require_kernel_accounting(const ReproductionStats& stats) {
  const double phases = stats.selection_kernel_ms + stats.variation_kernel_ms;
  const double tolerance = std::max(1e-6, std::abs(phases) * 1e-6);
  require(stats.selection_kernel_ms >= 0.0 &&
              stats.variation_kernel_ms >= 0.0 && stats.kernel_ms >= 0.0 &&
              std::abs(stats.kernel_ms - phases) <= tolerance,
          "GPU reproduction kernel timing did not equal selection plus variation");
}

EvolutionConfig compiled_config(
    const std::shared_ptr<const CompiledGrammar>& grammar, int population_size,
    double mutation_rate, double subtree_probability,
    bool explicit_request) {
  EvolutionConfig config;
  config.population_size = population_size;
  config.generations = 3;
  config.mutation_rate = mutation_rate;
  config.mutation_subtree_prob = subtree_probability;
  config.reproduction_backend = ReproductionBackend::Gpu;
  config.repro_overlap = false;
  config.selection_pressure = population_size;
  config.seed = UINT64_C(0x6b92d1715d3a820f);
  config.fuel = 100;
  config.compiled_grammar = grammar;
  if (explicit_request) {
    config.generation_request = gagp::evo::grammar::entry_request(*grammar);
  }
  return config;
}

std::vector<ProgramGenome> source_population(const CompiledGrammar& grammar,
                                             int population_size,
                                             std::int64_t wanted = -1) {
  std::vector<ProgramGenome> candidates;
  for (std::uint64_t seed = 0; seed < 4096 && candidates.size() < 2; ++seed) {
    ProgramGenome genome =
        gagp::evo::grammar::generate_derivation(grammar, seed).genome;
    const std::int64_t value = execute_int(genome);
    if (wanted >= 0) {
      if (value == wanted) candidates.push_back(std::move(genome));
    } else if (candidates.empty() ||
               execute_int(candidates.front()) != value) {
      candidates.push_back(std::move(genome));
    }
  }
  require(!candidates.empty() && (wanted >= 0 || candidates.size() == 2),
          "could not generate the grammar's 3/13 source witnesses");
  std::vector<ProgramGenome> population;
  population.reserve(static_cast<std::size_t>(population_size));
  for (int i = 0; i < population_size; ++i) {
    population.push_back(candidates[static_cast<std::size_t>(i) % candidates.size()]);
  }
  return population;
}

std::vector<ScoredGenome> score_manually(
    const std::vector<ProgramGenome>& population,
    std::int64_t preferred_value = -1) {
  std::vector<ScoredGenome> scored;
  scored.reserve(population.size());
  for (std::size_t i = 0; i < population.size(); ++i) {
    const bool preferred = preferred_value >= 0 &&
                           execute_int(population[i]) == preferred_value;
    scored.push_back(ScoredGenome{population[i],
        preferred ? 1000.0 : static_cast<double>(population.size() - i)});
  }
  return scored;
}

void require_attempts(const ReproductionResult& result, int population_size,
                      bool mutation_enabled) {
  require(result.stats.variation.crossover_attempts ==
              static_cast<std::uint64_t>((population_size + 1) / 2),
          "compiled backend recorded the wrong crossover attempt count");
  require(result.stats.variation.mutation_attempts ==
              static_cast<std::uint64_t>(mutation_enabled ? population_size : 0),
          "compiled backend recorded the wrong mutation attempt count");
  require_kernel_accounting(result.stats);
}

void test_prepared_replay_and_rejections() {
  const auto grammar = repeated_capture_grammar();
  EvolutionConfig config = compiled_config(grammar, 3, 1.0, 0.0, true);
  const GenerationRequest request = *config.generation_request;
  const auto population = source_population(*grammar, config.population_size, 3);
  const auto scored = score_manually(population, 3);

  const auto resources =
      gagp::evo::repro::make_gpu_repro_run_resources(config);

  ReproductionStats prep_stats;
  const GpuReproPreparedData prepared =
      gagp::evo::repro::prepare_gpu_repro_backend_inputs(
          population, config, UINT64_C(0x938fb27d8eab41c3), &prep_stats,
          resources);
  const GpuReproPreparedData separately_prepared =
      gagp::evo::repro::prepare_gpu_repro_backend_inputs(
          population, config, UINT64_C(0x192407bfd6aee50b), nullptr,
          resources);
  require(prepared.run_resources == resources &&
              separately_prepared.run_resources == resources,
          "compiled preparation did not retain the supplied run resources");
  require(prepared.packed.constant_mutation->grammar_domains ==
              separately_prepared.packed.constant_mutation->grammar_domains,
          "separate preparations rebuilt the immutable grammar domains");
  require(prepared.compiled_context != nullptr,
          "compiled preparation did not retain its variation context");
  require(prepared.config.population_size == 3 && prepared.config.pair_count == 2 &&
              prepared.config.tournament_k == config.selection_pressure,
          "compiled preparation lost odd population or selection-pressure settings");
  require(same_counters(prepared.preparation_counters,
                        prepared.compiled_context->counters()),
          "compiled preparation counter snapshot differed before execution");

  ReproductionStats first_stats = prep_stats;
  ReproductionStats second_stats = prep_stats;
  const ReproductionResult first =
      gagp::evo::repro::run_gpu_repro_backend_prepared(
          scored, config, prepared, &first_stats);
  const ReproductionResult second =
      gagp::evo::repro::run_gpu_repro_backend_prepared(
          scored, config, prepared, &second_stats);
  require(same_population(first.next_population, second.next_population),
          "the same prepared compiled call did not replay deterministically");
  require(same_counters(first.stats.variation, second.stats.variation),
          "prepared replay accumulated or changed variation counters");
  require_attempts(first, config.population_size, true);
  require_attempts(second, config.population_size, true);
  auto unit_budget_config = config;
  unit_budget_config.offspring_resource_budget = gagp::evo::grammar::ProjectedBudget{
      grammar->search_limits().max_nodes, grammar->search_limits().max_depth};
  const auto unit_prepared = gagp::evo::repro::prepare_gpu_repro_backend_inputs(
      population, unit_budget_config, UINT64_C(0x938fb27d8eab41c3), nullptr);
  for (const auto& candidate : unit_prepared.packed.candidates)
    if (candidate.start >= 0)
      require(candidate.has_projected_allowance,
              "certified projected allowance was lost in GPU candidate packing");
  const auto unit_result = gagp::evo::repro::run_gpu_repro_backend_prepared(
      scored, unit_budget_config, unit_prepared);
  require(same_population(first.next_population, unit_result.next_population) &&
              same_counters(first.stats.variation, unit_result.stats.variation),
          "unit projected budgets changed physical-budget reproduction");
  require_certified_population(*grammar, request, first.next_population, 3);
  bool saw_unchanged_group = false;
  bool saw_changed_group = false;
  for (const auto& child : first.next_population) {
    const auto value = execute_int(child);
    saw_unchanged_group = saw_unchanged_group || value == 3;
    saw_changed_group = saw_changed_group || value == 13;
  }
  require(saw_unchanged_group && saw_changed_group,
          "constant mutation did not sample independent child groups");

  auto changed_scored = scored;
  changed_scored.front().genome.ast.consts.front() = gagp::Value::from_int(999);
  expect_invalid(
      [&] {
        (void)gagp::evo::repro::run_gpu_repro_backend_prepared(
            changed_scored, config, prepared);
      },
      "prepared backend accepted a changed source genome");

  EvolutionConfig changed_rate = config;
  changed_rate.mutation_rate = 0.0;
  expect_invalid(
      [&] {
        (void)gagp::evo::repro::run_gpu_repro_backend_prepared(
            scored, changed_rate, prepared);
      },
      "prepared backend accepted a changed mutation rate");
  EvolutionConfig changed_mix = config;
  changed_mix.mutation_subtree_prob = 1.0;
  expect_invalid(
      [&] {
        (void)gagp::evo::repro::run_gpu_repro_backend_prepared(
            scored, changed_mix, prepared);
      },
      "prepared backend accepted a changed mutation operator mix");
  EvolutionConfig changed_selection = config;
  changed_selection.selection_pressure = 1;
  expect_invalid(
      [&] {
        (void)gagp::evo::repro::run_gpu_repro_backend_prepared(
            scored, changed_selection, prepared);
      },
      "prepared backend accepted changed selection pressure");
  EvolutionConfig changed_request = config;
  changed_request.generation_request->budget.max_nodes -= 1;
  expect_invalid(
      [&] {
        (void)gagp::evo::repro::run_gpu_repro_backend_prepared(
            scored, changed_request, prepared);
      },
      "prepared backend accepted a changed generation request");
  expect_invalid(
      [&] {
        (void)gagp::evo::repro::prepare_gpu_repro_backend_inputs(
            population, changed_request, 1, nullptr, resources);
      },
      "run resources accepted a changed generation request");
  EvolutionConfig changed_grammar = config;
  changed_grammar.compiled_grammar = repeated_capture_grammar();
  expect_invalid(
      [&] {
        (void)gagp::evo::repro::run_gpu_repro_backend_prepared(
            scored, changed_grammar, prepared);
      },
      "prepared backend accepted a changed compiled grammar owner");
  expect_invalid(
      [&] {
        (void)gagp::evo::repro::prepare_gpu_repro_backend_inputs(
            population, changed_grammar, 1, nullptr, resources);
      },
      "run resources accepted a changed compiled grammar owner");
}

void test_immediate_run_matches_public_replay() {
  const auto grammar = repeated_capture_grammar();
  auto config = compiled_config(grammar, 128, 1.0, 0.5, true);
  const auto population = source_population(*grammar, config.population_size);
  const auto scored = score_manually(population);
  std::mt19937_64 random(719), reference_random(719);
  const auto prepared = gagp::evo::repro::prepare_gpu_repro_backend_inputs(
      population, config, reference_random());
  const auto reference = gagp::evo::repro::run_gpu_repro_backend_prepared(scored, config, prepared);
  const auto result = gagp::evo::repro::run_gpu_repro_backend(scored, config, random);
  require(same_population(result.next_population, reference.next_population) &&
              same_counters(result.stats.variation, reference.stats.variation),
          "immediate run changed population or counters compared with public replay");
  require(random() == reference_random(), "immediate run changed host RNG consumption");
  auto invalid = scored;
  invalid.front().genome.ast.nodes.front().kind = static_cast<gagp::evo::NodeKind>(-1);
  expect_invalid([&] { (void)gagp::evo::repro::run_gpu_repro_backend(invalid, config, random); },
                 "immediate run accepted an invalid imported AST");
  auto wrong_fuel = config;
  ++wrong_fuel.fuel;
  expect_invalid([&] { (void)gagp::evo::repro::run_gpu_repro_backend(scored, wrong_fuel, random); },
                 "immediate run accepted mismatched execution fuel");
}

void test_prepared_parent_certificates() {
  const auto grammar = repeated_capture_grammar();
  auto config = compiled_config(grammar, 128, 0.0, 0.0, true);
  auto population = source_population(*grammar, config.population_size);
  for (auto& member : population) member.meta.program_key = "untrusted";
  const auto scored = score_manually(population);
  auto prepared = gagp::evo::repro::prepare_gpu_repro_backend_inputs(population, config, 91);
  require(prepared.parent_certificates != nullptr, "missing prepared parent certificates");
  const auto result = gagp::evo::repro::run_gpu_repro_backend_prepared(scored, config, prepared);
  auto plain = prepared;
  plain.parent_certificates.reset();
  const auto reference = gagp::evo::repro::run_gpu_repro_backend_prepared(scored, config, plain);
  require(same_population(result.next_population, reference.next_population) &&
              same_counters(result.stats.variation, reference.stats.variation),
          "parent certificates changed offspring or counters");
  for (const auto& member : result.next_population)
    require(member.meta.program_key == ast_cache_key(member.ast) && member.derivation,
            "parent certificates reused untrusted metadata");
  // Each independently invalid boundary must decline the continuation. Poison
  // saved metadata so accidental reuse is visible even for an identical AST.
  const auto certificates = prepared.parent_certificates;
  for (int boundary = 0; boundary < 3; ++boundary) {
    auto invalidated = std::make_shared<gagp::evo::repro::PreparedParentCertificates>(
        *certificates);
    if (boundary == 0) {
      for (auto& row : invalidated->analyses)
        row.reads = std::make_shared<gagp::payload::StagedPayloads>();
    } else if (boundary == 1) {
      invalidated->sources = std::make_shared<const gagp::evo::repro::CompiledSpliceSources>(
          *certificates->sources);
    } else {
      invalidated->context = std::make_shared<gagp::evo::grammar::VariationContext>(
          grammar, certificates->context->requests());
    }
    for (auto& meta : invalidated->metadata) meta.program_key = "invalidated";
    prepared.parent_certificates = invalidated;
    const auto fallback = gagp::evo::repro::run_gpu_repro_backend_prepared(scored, config, prepared);
    require(same_population(fallback.next_population, reference.next_population) &&
                same_counters(fallback.stats.variation, reference.stats.variation),
            "invalid parent certificate changed fallback behavior");
    for (const auto& member : fallback.next_population)
      require(member.meta.program_key == ast_cache_key(member.ast),
              "invalid parent certificate reused saved metadata");
  }
}

void test_run_resources_retain_grammar_payloads() {
  gagp::payload::clear();
  const auto grammar = payload_domain_grammar();
  EvolutionConfig config = compiled_config(grammar, 2, 1.0, 0.0, true);
  const auto population = source_population(*grammar, config.population_size, 7);
  const auto resources =
      gagp::evo::repro::make_gpu_repro_run_resources(config);
  const GpuReproPreparedData prepared =
      gagp::evo::repro::prepare_gpu_repro_backend_inputs(
          population, config, 29, nullptr, resources);

  const auto domains = prepared.packed.constant_mutation->grammar_domains;
  require(domains != nullptr,
          "prepared constant table did not retain grammar domains");
  const auto string_payload = std::find_if(
      domains->values.begin(), domains->values.end(),
      [](const gagp::Value& value) { return value.tag == ValueTag::String; });
  const auto int_list_payload = std::find_if(
      domains->values.begin(), domains->values.end(),
      [](const gagp::Value& value) { return value.tag == ValueTag::IntList; });
  const auto float_list_payload = std::find_if(
      domains->values.begin(), domains->values.end(),
      [](const gagp::Value& value) { return value.tag == ValueTag::FloatList; });
  const auto string_list_payload = std::find_if(
      domains->values.begin(), domains->values.end(),
      [](const gagp::Value& value) { return value.tag == ValueTag::StringList; });
  require(string_payload != domains->values.end() &&
              int_list_payload != domains->values.end() &&
              float_list_payload != domains->values.end() &&
              string_list_payload != domains->values.end(),
          "resource fixture did not materialize every grammar-only payload domain");

  const gagp::Value unrooted =
      gagp::payload::make_string_value("unrooted-sweep-control");
  gagp::evo::PayloadLifetimeManager lifetime({}, resources);
  lifetime.retain(population, {});
  std::string exact;
  require(!gagp::payload::lookup_string(unrooted, &exact),
          "payload lifetime test did not sweep its unrooted control");
  require(gagp::payload::lookup_string(*string_payload, &exact) &&
              exact == "resource-only-payload",
          "payload sweep dropped a grammar-only constant mutation value");
  std::vector<gagp::Value> ints;
  require(gagp::payload::lookup_list(*int_list_payload, &ints) &&
              ints.size() == 2 && ints[0].tag == ValueTag::Int &&
              ints[0].i == 4 && ints[1].tag == ValueTag::Int && ints[1].i == -9,
          "payload sweep dropped a grammar-only IntList mutation value");
  std::vector<gagp::Value> floats;
  require(gagp::payload::lookup_list(*float_list_payload, &floats) &&
              floats.size() == 2 && floats[0].tag == ValueTag::Float &&
              floats[0].f == 1.25 && floats[1].tag == ValueTag::Float &&
              floats[1].f == -2.5,
          "payload sweep dropped a grammar-only FloatList mutation value");
  std::vector<gagp::Value> strings;
  require(gagp::payload::lookup_list(*string_list_payload, &strings) &&
              strings.size() == 2 && strings[0].tag == ValueTag::String &&
              strings[1].tag == ValueTag::String,
          "payload sweep dropped a grammar-only StringList mutation value");
  std::string nested_a;
  std::string nested_b;
  require(gagp::payload::lookup_string(strings[0], &nested_a) &&
              gagp::payload::lookup_string(strings[1], &nested_b) &&
              nested_a == "nested-a" && nested_b == "nested-b",
          "payload sweep dropped nested StringList string roots");
  gagp::payload::clear();
}

void test_source_identity_normalizes_unused_tables() {
  const auto grammar = repeated_capture_grammar();
  EvolutionConfig config = compiled_config(grammar, 128, 1.0, 1.0, true);
  auto population = source_population(*grammar, config.population_size, 3);
  const auto compact_population = population;
  for (auto& genome : population) {
    genome.ast.names.push_back("unused_name");
    genome.ast.consts.push_back(gagp::Value::from_int(999));
  }
  const auto scored = score_manually(population, 3);
  std::mt19937_64 rng(17);
  const ReproductionResult result =
      gagp::evo::repro::run_gpu_repro_backend(scored, config, rng);
  require(result.next_population.size() == population.size(),
          "GPU reproduction rejected semantically irrelevant unused tables");
  std::mt19937_64 compact_rng(17);
  const auto compact_result = gagp::evo::repro::run_gpu_repro_backend(
      score_manually(compact_population, 3), config, compact_rng);
  require(same_population(result.next_population, compact_result.next_population) &&
              same_counters(result.stats.variation, compact_result.stats.variation),
          "warming compacted parents changed reproduction or compatibility ordering");
  population.front().ast.consts.push_back(
      gagp::Value::from_string_hash_len(918273645, 9));
  expect_invalid([&] {
    (void)gagp::evo::repro::prepare_gpu_repro_backend_inputs(population, config, 17);
  }, "compaction bypassed validation of an unused opaque payload");
}

void test_closed_cross_scope_crossover() {
  const auto grammar = std::make_shared<const CompiledGrammar>(
      gagp::evo::grammar::compile_grammar(gagp::evo::grammar::parse_definition(kClosedCrossoverGrammar)));
  const auto config = compiled_config(grammar, 16, 0.0, 0.0, true);
  const auto population = source_population(*grammar, 16, 3);
  const auto scored = score_manually(population, 3);
  std::mt19937_64 rng(17);
  const auto result = gagp::evo::repro::run_gpu_repro_backend(scored, config, rng);
  bool crossed = false;
  for (const auto& child : result.next_population) {
    const auto value = execute_int(child);
    require(value >= 2 && value <= 4, "closed GPU crossover produced an invalid value");
    crossed |= value != 3;
  }
  require(crossed, "GPU did not exchange payloads across different lexical arities");
  require(result.stats.kernel_ms > 0 && result.stats.variation.fallback_children == 0 &&
      result.stats.variation.acceptance_rejections == 0,
      "closed GPU crossover fell back or failed child admission");
}

void exercise_three_generations(int population_size, bool explicit_request,
                                double mutation_rate,
                                double subtree_probability) {
  const auto grammar = repeated_capture_grammar();
  EvolutionConfig config = compiled_config(grammar, population_size,
                                            mutation_rate,
                                            subtree_probability,
                                            explicit_request);
  if (!explicit_request) {
    std::mt19937_64 rejected_rng(config.seed);
    expect_invalid([&] {
      (void)gagp::evo::repro::run_gpu_repro_backend(
          score_manually(source_population(*grammar, population_size)),
          config, rejected_rng);
    }, "compiled backend accepted a missing generation request");
    config.generation_request = gagp::evo::grammar::entry_request(*grammar);
  }
  const GenerationRequest request = config.generation_request.value_or(
      gagp::evo::grammar::entry_request(*grammar));
  std::vector<ProgramGenome> population =
      source_population(*grammar, population_size);
  std::mt19937_64 rng(config.seed);
  bool saw_constant_change = false;
  for (int generation = 0; generation < 3; ++generation) {
    const std::int64_t selected_value = execute_int(population.front());
    const ReproductionResult result = gagp::evo::repro::run_gpu_repro_backend(
        score_manually(population), config, rng);
    require_attempts(result, population_size, mutation_rate == 1.0);
    require_certified_population(*grammar, request, result.next_population,
                                 static_cast<std::size_t>(population_size));
    if (mutation_rate == 1.0 && subtree_probability == 0.0) {
      for (const auto& child : result.next_population) {
        saw_constant_change = saw_constant_change ||
                              execute_int(child) != selected_value;
      }
    }
    population = result.next_population;
  }
  require(mutation_rate != 1.0 || subtree_probability != 0.0 ||
              saw_constant_change,
          "constant-only mode never changed a repeated logical constant group");
}

std::vector<gagp::BytecodeProgram> compile_population(
    const std::vector<ProgramGenome>& population) {
  std::vector<gagp::BytecodeProgram> programs;
  programs.reserve(population.size());
  for (const auto& genome : population) {
    const auto verified = gagp::evo::verify_ast(genome.ast, {});
    require(verified.ok,
            "overlap source failed native AST verification: " +
                verified.diagnostic.message);
    programs.push_back(gagp::evo::compile_for_eval(genome, verified.verified));
  }
  return programs;
}

void test_compiled_overlap_matches_direct_preparation() {
  const auto grammar = repeated_capture_grammar();
  EvolutionConfig config = compiled_config(grammar, 5, 1.0, 1.0, true);
  config.eval_engine = gagp::evo::EvalEngine::GPU;
  config.repro_overlap = true;
  require(gagp::evo::gpu_reproduction_overlap_enabled(config),
          "compiled GPU evaluation and reproduction did not enable overlap");

  gagp::FitnessSessionGpu eval_session;
  const gagp::FitnessSessionInitResult init = eval_session.init(
      std::vector<gagp::CaseBindings>(1),
      {gagp::Value::from_int(13)}, config.fuel, config.gpu_blocksize,
      config.penalty);
  require(init.ok, "overlap GPU evaluation session init failed: " +
                       init.err.message);

  const GenerationRequest request = *config.generation_request;
  std::vector<ProgramGenome> population =
      source_population(*grammar, config.population_size);
  const auto resources =
      gagp::evo::repro::make_gpu_repro_run_resources(config);
  std::shared_ptr<const gagp::evo::repro::ConstantMutationDomains>
      grammar_domains;
  std::mt19937_64 seed_rng(config.seed);
  for (int generation = 0; generation < config.generations; ++generation) {
    const std::uint64_t seed = seed_rng();
    const std::vector<gagp::BytecodeProgram> programs =
        compile_population(population);

    std::future<gagp::evo::OverlapPrepared> overlap_future;
    {
      std::vector<ProgramGenome> worker_population = population;
      EvolutionConfig worker_config = config;
      overlap_future = gagp::evo::start_gpu_reproduction_overlap(
          worker_population, worker_config, seed, resources);
      worker_population.clear();
      worker_config.compiled_grammar.reset();
      worker_config.generation_request.reset();
    }

    const gagp::FitnessEvalResult evaluation =
        eval_session.eval_programs(programs);
    require(evaluation.ok, "concurrent overlap GPU evaluation failed: " +
                               evaluation.err.message);
    require(evaluation.fitness.size() == population.size(),
            "overlap GPU evaluation changed fitness order or size");

    gagp::evo::OverlapPrepared overlap_prepared = overlap_future.get();
    require(overlap_prepared.prepared.run_resources == resources,
            "overlap preparation did not retain the reused run resources");
    const auto overlap_domains =
        overlap_prepared.prepared.packed.constant_mutation->grammar_domains;
    require(overlap_domains != nullptr,
            "overlap preparation lost its immutable grammar domains");
    if (grammar_domains == nullptr) grammar_domains = overlap_domains;
    require(overlap_domains == grammar_domains,
            "repeated overlap preparation rebuilt its immutable grammar domains");
    std::promise<gagp::evo::OverlapPrepared> completed_overlap;
    overlap_future = completed_overlap.get_future();
    completed_overlap.set_value(std::move(overlap_prepared));

    const ReproductionResult overlapped =
        gagp::evo::finish_gpu_reproduction_overlap(
            &overlap_future, population, evaluation.fitness, config);

    ReproductionStats direct_stats;
    const GpuReproPreparedData direct_prepared =
        gagp::evo::repro::prepare_gpu_repro_backend_inputs(
            population, config, seed, &direct_stats, resources);
    require(direct_prepared.run_resources == resources,
            "repeated overlap generation lost its run-resource owner");
    const auto generation_domains =
        direct_prepared.packed.constant_mutation->grammar_domains;
    require(generation_domains != nullptr,
            "repeated overlap generation lost its immutable grammar domains");
    require(generation_domains == grammar_domains,
            "repeated overlap generation rebuilt its immutable grammar domains");
    const std::vector<gagp::evo::ScoredGenomeRef> ranked =
        gagp::evo::rank_population_refs(population, evaluation.fitness, false);
    const ReproductionResult direct =
        gagp::evo::repro::run_gpu_repro_backend_prepared(
            ranked, config, direct_prepared, &direct_stats);

    require(same_population(overlapped.next_population,
                            direct.next_population),
            "overlap changed direct prepared child order or contents");
    require(same_counters(overlapped.stats.variation,
                          direct.stats.variation),
            "overlap changed direct prepared variation counters");
    require_attempts(overlapped, config.population_size, true);
    require_attempts(direct, config.population_size, true);
    require_certified_population(*grammar, request,
                                 overlapped.next_population,
                                 static_cast<std::size_t>(config.population_size));
    require_certified_population(*grammar, request, direct.next_population,
                                 static_cast<std::size_t>(config.population_size));
    population = overlapped.next_population;
  }
}

void test_evolution_run_resources_all_modes() {
  const auto grammar = repeated_capture_grammar();
  const auto population = source_population(*grammar, 5);
  gagp::evo::EvalCase one_case;
  one_case.expected = gagp::Value::from_int(13);
  for (int mode = 0; mode < 5; ++mode) {
    EvolutionConfig config = compiled_config(grammar, 5, 1.0, 0.5, true);
    config.eval_engine = (mode == 0 || mode == 4)
        ? gagp::evo::EvalEngine::CPU : gagp::evo::EvalEngine::GPU;
    config.reproduction_backend = mode <= 1
        ? ReproductionBackend::Cpu : ReproductionBackend::Gpu;
    config.repro_overlap = mode == 3;
    const auto result = gagp::evo::evolve_population({one_case}, config, &population);
    require(result.history_best.size() == static_cast<std::size_t>(config.generations) &&
                result.final_population.size() == population.size(),
            "compiled evolution did not complete all generations and final evaluation");
    std::vector<ProgramGenome> final;
    for (const auto& scored : result.final_population) final.push_back(scored.genome);
    require_certified_population(*grammar, *config.generation_request, final, population.size());
  }
}

void test_mixed_roots_across_all_modes() {
  using namespace gagp::evo;
  const std::vector<EvalCase> cases{{{}, gagp::Value::from_int(0)}};
  const auto case_set = prepare_case_set(cases);
  // Four CPU reproduction settings with both evaluation engines, plus GPU
  // reproduction with CPU/GPU evaluation and GPU evaluation with overlap.
  for (int mode = 0; mode < 11; ++mode) {
    auto cfg = gagp::test::mixed_population_config();
    if (mode < 8) {
      cfg.eval_engine = mode % 2 ? EvalEngine::GPU : EvalEngine::CPU;
      cfg.cpu_repro_ablation = static_cast<repro::CpuReproAblation>(mode / 2);
    } else {
      cfg.reproduction_backend = ReproductionBackend::Gpu;
      cfg.eval_engine = mode == 8 ? EvalEngine::CPU : EvalEngine::GPU;
      cfg.repro_overlap = mode == 10;
    }
    const auto population = initialize_population(cfg, case_set).population;
    const auto result = evolve_population(cases, cfg, &population);
    require(result.history_best.size() == 3 && result.final_population.size() == 16,
            "mixed public flow lost generations or population members");
    require(result.history_mean_fitness.front() < 0 && result.history_mean_fitness[1] == 0,
            "mixed public flow partitioned selection by root type");
    for (const auto& member : result.final_population) {
      require(member.genome.derivation->request.type == RType::Int && member.fitness == 0,
              "mixed public flow did not use population-wide fitness ranking");
      grammar::require_membership(*cfg.compiled_grammar, member.genome, *cfg.generation_request);
    }
    if (cfg.reproduction_backend == ReproductionBackend::Gpu)
      require(result.timing.reproduction_totals.kernel_ms > 0,
              "mixed GPU flow did not execute reproduction kernels");
  }

  auto mixed_cfg = gagp::test::mixed_population_config();
  mixed_cfg.selection_pressure = 1;
  auto mixed_population = initialize_population(mixed_cfg, case_set).population;
  std::vector<EvalCase> mixed_cases;
  for (std::size_t i = 0; i < 8; ++i)
    mixed_cases.push_back({{}, mixed_population[i].ast.consts.front()});
  const auto cpu = evolve_population(mixed_cases, mixed_cfg, &mixed_population);
  mixed_cfg.eval_engine = EvalEngine::GPU;
  mixed_population = initialize_population(mixed_cfg, prepare_case_set(mixed_cases)).population;
  const auto gpu = evolve_population(mixed_cases, mixed_cfg, &mixed_population);
  require(cpu.history_best_fitness == gpu.history_best_fitness &&
          cpu.history_mean_fitness == gpu.history_mean_fitness,
          "mixed expected-type cases diverged between CPU and GPU fitness");

  auto cfg = gagp::test::mixed_population_config();
  cfg.reproduction_backend = ReproductionBackend::Gpu;
  cfg.selection_pressure = 1;
  cfg.mutation_subtree_prob = 1;
  const auto population = initialize_population(cfg, case_set).population;
  std::vector<ScoredGenome> scored;
  for (const auto& genome : population) scored.push_back({genome, 0});
  auto resources = repro::make_gpu_repro_run_resources(cfg);
  ReproductionStats stats;
  const auto prepared = repro::prepare_gpu_repro_backend_inputs(population, cfg, 91, &stats, resources);
  const auto result = repro::run_gpu_repro_backend_prepared(scored, cfg, prepared, &stats);
  std::vector<int> types(8);
  grammar::VariationContext context(cfg.compiled_grammar, population_requests(cfg));
  for (const auto& genome : result.next_population)
    ++types.at(static_cast<std::size_t>(context.analyze(genome)->verified.return_type));
  require(std::all_of(types.begin(), types.end(), [](int count) { return count == 2; }),
          "mixed GPU mutation or copyback lost an exact payload/root type");
  auto changed = cfg;
  changed.additional_generation_requests.pop_back();
  expect_invalid([&] { (void)repro::run_gpu_repro_backend_prepared(scored, changed, prepared, nullptr); },
                 "prepared GPU state ignored a changed additional root set");
  expect_invalid([&] { (void)repro::prepare_gpu_repro_backend_inputs(population, changed, 91, nullptr, resources); },
                 "GPU run resources ignored a changed additional root set");
}

void test_sequence_constant_proposals_and_overlap() {
  using namespace gagp;
  using namespace gagp::evo;
  using namespace gagp::evo::grammar;
  const std::vector<std::pair<std::string, std::string>> domains{
      {"String", R"({"type":"Char","values":["a","b","\ud83d\ude00"]})"},
      {"IntList", R"({"type":"Int","range":["-20","20"]})"},
      {"FloatList", R"({"type":"Float","values":[-1.25,0,2.5]})"},
      {"FloatList", R"({"type":"Float","range":[-5,5]})"},
      {"FloatList", R"({"type":"Float","range":[-8,8],"quantization_scale":1000})"},
      {"FloatList", R"({"type":"Float","range":[-100,100],"sample_from":{"type":"Float","range":[-8,8],"quantization_scale":1000}})"},
      {"StringList", R"({"type":"String","sequence":{"length":[0,4],"element":{"type":"Char","values":["x","y"]}}})"}};
  for (const auto& entry : domains) {
    const auto grammar = std::make_shared<const CompiledGrammar>(compile_grammar(parse_definition(
        R"({"format_version":"grammar-definition-v2","entry":{"nonterminal":"Value","type":")" + entry.first +
        R"("},"search_limits":{"max_nodes":5,"max_depth":4},"execution_limits":{"fuel":100},"nonterminals":[{"id":"Value","type":")" + entry.first +
        R"(","scope":[],"alternatives":[{"id":"value","weight":1,"expression":{"constant":{"type":")" + entry.first +
        R"(","sequence":{"length":[0,5],"element":)" + entry.second + "}}}}]}]}")));
    auto config = compiled_config(grammar, 8, 1.0, 0.0, true);
    const auto make_population = [&] {
      std::vector<ProgramGenome> members;
      for (int i = 0; i < config.population_size; ++i)
        members.push_back(generate_derivation(*grammar, 43 + i).genome);
      return members;
    };
    auto population = make_population();
    // Identical parents make selection and crossover unable to explain a
    // changed child: this check must exercise device constant mutation.
    std::fill(population.begin(), population.end(), population.front());
    const auto resources = repro::make_gpu_repro_run_resources(config);
    const auto first = repro::prepare_gpu_repro_backend_inputs(population, config, 101, nullptr, resources);
    const auto second = repro::prepare_gpu_repro_backend_inputs(population, config, 102, nullptr, resources);
    const auto first_domains = first.packed.constant_mutation->grammar_domains;
    const auto second_domains = second.packed.constant_mutation->grammar_domains;
    require(first_domains != second_domains && first_domains->base_domains == second_domains->base_domains,
            "sequence proposals overwrote another preparation or failed to share grammar data");
    PayloadLifetimeManager lifetime({}, resources);
    lifetime.retain(population, {});
    for (const auto& prepared : {first_domains, second_domains})
      for (const auto& value : prepared->values)
        require(constant_domain_contains(grammar->constants().front(), value),
                "payload sweep dropped a live sequence proposal or nested payload");
    std::vector<ScoredGenome> scored;
    for (const auto& genome : population) scored.push_back({genome, 0});
    auto forged = first;
    auto forged_constants = std::make_shared<repro::ConstantMutationTable>(*first.packed.constant_mutation);
    forged_constants->grammar_domains = std::make_shared<repro::ConstantMutationDomains>(*first_domains);
    forged.packed.constant_mutation = forged_constants;
    expect_invalid([&] { (void)repro::run_gpu_repro_backend_prepared(scored, config, forged, nullptr); },
                   "GPU preparation accepted an unregistered sequence proposal table");
    ReproductionStats stats;
    const auto result = repro::run_gpu_repro_backend_prepared(scored, config, first, &stats);
    require(result.stats.kernel_ms > 0 && result.next_population.size() == population.size(),
            "sequence constant mutation did not run on GPU");
    require(result.stats.variation.mutation_attempts == population.size() &&
            result.stats.variation.fallback_children == 0 &&
            result.stats.variation.acceptance_rejections == 0,
            "GPU sequence mutation used a rejection or fallback path");
    bool changed = false;
    for (std::size_t i = 0; i < result.next_population.size(); ++i) {
      require_membership(*grammar, result.next_population[i]);
      changed |= ast_cache_key(result.next_population[i].ast) != ast_cache_key(population[i].ast);
    }
    require(changed, "GPU sequence mutation never changed a constant");
    const std::vector<EvalCase> cases{{{}, population.front().ast.consts.front()}};
    for (bool subtree : {false, true}) {
      config.mutation_subtree_prob = subtree ? 1.0 : 0.0;
      config.eval_engine = EvalEngine::GPU;
      config.repro_overlap = false;
      population = make_population();
      const auto direct = evolve_population(cases, config, &population);
      config.repro_overlap = true;
      population = make_population();
      const auto overlap = evolve_population(cases, config, &population);
      require(direct.history_best_fitness == overlap.history_best_fitness &&
              direct.history_mean_fitness == overlap.history_mean_fitness,
              "sequence proposal overlap changed replayed evolution");
      require(overlap.timing.reproduction_totals.kernel_ms > 0,
              "sequence overlap did not execute GPU reproduction");
      for (const auto& member : overlap.final_population) require_membership(*grammar, member.genome);
    }
  }
}

void test_float_range_on_gpu(bool quantized, bool separated = false, bool additive = false, bool grid = false) {
  using namespace gagp;
  using namespace gagp::evo;
  using namespace gagp::evo::grammar;
  std::string definition = R"({
    "format_version":"grammar-definition-v2","entry":{"nonterminal":"F","type":"Float"},
    "search_limits":{"max_nodes":5,"max_depth":4},"execution_limits":{"fuel":100},
    "nonterminals":[{"id":"F","type":"Float","scope":[],"alternatives":[
      {"id":"number","weight":1,"expression":{"constant":{"type":"Float","range":[-5,5]}}}]}]
  })";
  if (quantized) definition.insert(definition.find("\"range\""), "\"quantization_scale\":1000,");
  if (separated) {
    auto source = cli_detail::JsonParser(definition).parse();
    auto& domain = source.object_v.at("nonterminals").array_v[0].object_v.at("alternatives").array_v[0]
        .object_v.at("expression").object_v.at("constant");
    auto support = cli_detail::JsonParser(R"({"type":"Float","range":[-100,100]})").parse();
    support.object_v["sample_from"] = domain;
    if (additive) support.object_v["mutation"] = cli_detail::JsonParser(grid ?
        R"({"kind":"add","range":[-1,1],"gpu_grid_steps":65535})" : R"({"kind":"add","range":[-1,1]})").parse();
    domain = std::move(support);
    definition = canonical_json(source);
  }
  const auto grammar = std::make_shared<const CompiledGrammar>(compile_grammar(parse_definition(definition)));
  auto config = compiled_config(grammar, 8, 1.0, 0.0, true);
  auto population = std::vector<ProgramGenome>(8, generate_derivation(*grammar, 9).genome);
  if (separated) for (auto& member : population) {
    member.ast.consts.at(member.ast.nodes.at(3).i0) = Value::from_float(42.125);
    member.derivation.reset();
    require_membership(*grammar,member);
  }
  const auto prepared = repro::prepare_gpu_repro_backend_inputs(population, config, 101, nullptr);
  require(prepared.packed.constant_mutation->grammar_domains->values.empty(),
          "Float interval used host proposal values");
  const auto result = repro::run_gpu_repro_backend_prepared(score_manually(population), config, prepared, nullptr);
  require(result.stats.kernel_ms > 0 && result.stats.variation.fallback_children == 0 &&
          result.stats.variation.acceptance_rejections == 0 && result.stats.variation.changed_children > 0,
          "Float GPU constant mutation failed or used fallback");
  for (const auto& child : result.next_population) {
    require_membership(*grammar, child);
    if (separated && !additive) require(constant_domain_contains(*grammar->constants().front().sampling,
        child.ast.consts.at(child.ast.nodes.at(3).i0)), "GPU resampling ignored sample_from");
    if (additive) {
      const auto value = child.ast.consts.at(child.ast.nodes.at(3).i0).f;
      require(value >= 41.125 && value <= 43.125,"GPU additive mutation resampled from construction domain");
    }
  }
  config.eval_engine = EvalEngine::GPU;
  const std::vector<EvalCase> cases{{{}, Value::from_float(0)}};
  auto direct_population = population;
  const auto direct = evolve_population(cases, config, &direct_population);
  config.repro_overlap = true;
  auto overlap_population = population;
  const auto overlap = evolve_population(cases, config, &overlap_population);
  require(direct.history_best_fitness == overlap.history_best_fitness &&
          direct.history_mean_fitness == overlap.history_mean_fitness &&
          overlap.timing.reproduction_totals.kernel_ms > 0,
          "Float interval overlap changed evolution");
}

void test_mutable_template_constant_on_gpu() {
  using namespace gagp;
  using namespace gagp::evo;
  using namespace gagp::evo::grammar;
  const auto grammar = std::make_shared<const CompiledGrammar>(compile_grammar(parse_definition(R"({
    "format_version":"grammar-definition-v2","entry":{"nonterminal":"Main","type":"Int"},
    "search_limits":{"max_nodes":7,"max_depth":5},"execution_limits":{"fuel":100},
    "templates":[{"id":"FixedAdd","type":"Int","scope":[],"holes":[],
      "body":{"signature":"add(Int,Int)->Int","args":[
        {"constant":{"type":"Int","values":["99"]}},
        {"constant":{"type":"Int","range":["0","100"]},"mutable":true}]}}],
    "nonterminals":[{"id":"Main","type":"Int","scope":[],"variation":false,"alternatives":[
      {"id":"body","weight":1,"expression":{"template":"FixedAdd","holes":{}}}]}]
  })")));
  auto config = compiled_config(grammar, 16, 1.0, 0.0, true);
  const auto population = std::vector<ProgramGenome>(16, generate_derivation(*grammar, 3).genome);
  const auto prepared = repro::prepare_gpu_repro_backend_inputs(population, config, 101, nullptr);
  const auto result = repro::run_gpu_repro_backend_prepared(score_manually(population), config, prepared, nullptr);
  require(result.stats.kernel_ms > 0 && result.stats.variation.changed_children > 0 &&
          result.stats.variation.acceptance_rejections == 0,
          "explicit mutable template constant did not evolve on GPU");
  for (const auto& child : result.next_population) {
    require_membership(*grammar, child);
    require(child.ast.nodes[3].kind == NodeKind::ADD &&
            child.ast.consts.at(child.ast.nodes[4].i0).i == 99,
            "GPU constant mutation changed the fixed template skeleton");
  }
  config.eval_engine = EvalEngine::GPU;
  const std::vector<EvalCase> cases{{{}, Value::from_int(150)}};
  auto direct_population = population;
  const auto direct = evolve_population(cases, config, &direct_population);
  config.repro_overlap = true;
  auto overlap_population = population;
  const auto overlap = evolve_population(cases, config, &overlap_population);
  require(direct.history_mean_fitness == overlap.history_mean_fitness &&
          overlap.timing.reproduction_totals.kernel_ms > 0,
          "mutable template constant overlap changed fitness or omitted GPU execution");
  for (std::size_t i = 0; i < direct.final_population.size(); ++i)
    require(ast_cache_key(direct.final_population[i].genome.ast) ==
                ast_cache_key(overlap.final_population[i].genome.ast),
            "mutable template constant direct/overlap replay differs");
}

void test_constant_policies_on_gpu() {
  using namespace gagp;
  using namespace gagp::evo;
  using namespace gagp::evo::grammar;
  for (const auto policy : {"keep","flip","keep_sequence"}) {
    const bool sequence = std::string(policy) == "keep_sequence";
    std::string definition = std::string(R"({
      "format_version":"grammar-definition-v2","entry":{"nonterminal":"B","type":"Bool"},
      "search_limits":{"max_nodes":5,"max_depth":4},"execution_limits":{"fuel":100},
      "nonterminals":[{"id":"B","type":"Bool","scope":[],"alternatives":[
        {"id":"bool","weight":1,"expression":{"constant":{"type":"Bool","values":[false,true],"mutation":")") +
        (sequence ? "keep" : policy) + R"("}}}]}]})";
    if (sequence) {
      auto source = cli_detail::JsonParser(definition).parse();
      source.object_v.at("entry").object_v.at("type").string_v = "StringList";
      auto& nonterminal = source.object_v.at("nonterminals").array_v[0];
      nonterminal.object_v.at("type").string_v = "StringList";
      nonterminal.object_v.at("alternatives").array_v[0].object_v.at("expression").object_v.at("constant") =
          cli_detail::JsonParser(R"({"type":"StringList","mutation":"keep","sequence":{"length":[1,4],
            "element":{"type":"String","sequence":{"length":[1,4],"element":{"type":"Char","values":["a","b"]}}}}})").parse();
      definition = canonical_json(source);
    }
    const auto grammar = std::make_shared<const CompiledGrammar>(compile_grammar(parse_definition(definition)));
    auto config = compiled_config(grammar,8,1.0,0.0,true);
    const auto population = std::vector<ProgramGenome>(8,generate_derivation(*grammar,9).genome);
    const auto before = population.front().ast.consts.at(population.front().ast.nodes.at(3).i0);
    const bool flip = std::string(policy) == "flip";
    const auto prepared = repro::prepare_gpu_repro_backend_inputs(population,config,101,nullptr);
    if (sequence) require(prepared.packed.constant_mutation->grammar_domains->values.empty() &&
        !prepared.packed.constant_mutation->grammar_domains->has_sequence_domains,
        "keep allocated unused sequence proposals");
    const auto result = repro::run_gpu_repro_backend_prepared(score_manually(population),config,prepared,nullptr);
    require(result.stats.kernel_ms > 0 && result.stats.variation.fallback_children == 0 &&
            result.stats.variation.acceptance_rejections == 0 && result.stats.variation.mutation_attempts == 8,
            "keep/flip GPU mutation rejected, skipped or used fallback");
    for (const auto& child : result.next_population) {
      require_membership(*grammar,child);
      const auto value = child.ast.consts.at(child.ast.nodes.at(3).i0);
      require(flip ? value.b != before.b : canonical_json(encode_constant(value)) == canonical_json(encode_constant(before)),
              "GPU constant policy resampled instead of applying keep/flip");
    }
    config.eval_engine = EvalEngine::GPU;
    const std::vector<EvalCase> cases{{{},before}};
    auto direct_population = population;
    const auto direct = evolve_population(cases,config,&direct_population);
    config.repro_overlap = true;
    auto overlap_population = population;
    const auto overlap = evolve_population(cases,config,&overlap_population);
    require(direct.history_best_fitness == overlap.history_best_fitness &&
            direct.history_mean_fitness == overlap.history_mean_fitness &&
            overlap.timing.reproduction_totals.kernel_ms > 0,
            "keep/flip overlap changed evolution or omitted the GPU kernel");
  }
}

void test_int_addition_on_gpu() {
  using namespace gagp;
  using namespace gagp::evo;
  using namespace gagp::evo::grammar;
  const auto grammar = std::make_shared<const CompiledGrammar>(compile_grammar(parse_definition(R"({
    "format_version":"grammar-definition-v2","entry":{"nonterminal":"I","type":"Int"},
    "search_limits":{"max_nodes":5,"max_depth":4},"execution_limits":{"fuel":100},
    "nonterminals":[{"id":"I","type":"Int","scope":[],"alternatives":[
    {"id":"integer","weight":1,"expression":{"constant":{"type":"Int","range":["-100","100"],
      "sample_from":{"type":"Int","values":["0"]},"mutation":{"kind":"add","range":["1","2"]}}}}]}]
  })")));
  auto config = compiled_config(grammar,8,1.0,0.0,true);
  auto population = std::vector<ProgramGenome>(8,generate_derivation(*grammar,9).genome);
  for (auto& member : population) {
    member.ast.consts.at(member.ast.nodes.at(3).i0) = Value::from_int(42);
    member.derivation.reset(); require_membership(*grammar,member);
  }
  const auto prepared = repro::prepare_gpu_repro_backend_inputs(population,config,101,nullptr);
  require(prepared.packed.constant_mutation->grammar_domains->values.empty(),"Int add built host proposals");
  const auto result = repro::run_gpu_repro_backend_prepared(score_manually(population),config,prepared,nullptr);
  require(result.stats.kernel_ms > 0 && result.stats.variation.changed_children == 8 &&
          result.stats.variation.fallback_children == 0 && result.stats.variation.acceptance_rejections == 0,
          "Int additive GPU reproduction rejected or skipped mutation");
  for (const auto& child : result.next_population) {
    require_membership(*grammar,child);
    const auto value = execute_int(child);
    require(value == 43 || value == 44,"GPU Int addition resampled or used membership range as delta");
  }
  config.eval_engine = EvalEngine::GPU;
  const std::vector<EvalCase> cases{{{},Value::from_int(50)}};
  auto direct_population = population;
  const auto direct = evolve_population(cases,config,&direct_population);
  config.repro_overlap = true;
  auto overlap_population = population;
  const auto overlap = evolve_population(cases,config,&overlap_population);
  require(direct.history_best_fitness == overlap.history_best_fitness &&
          direct.history_mean_fitness == overlap.history_mean_fitness &&
          overlap.timing.reproduction_totals.kernel_ms > 0,"Int addition overlap changed evolution");
}

void test_generation_stages_on_gpu() {
  using namespace gagp;
  using namespace gagp::evo;
  using namespace gagp::evo::grammar;
  const auto grammar = std::make_shared<const CompiledGrammar>(compile_grammar(parse_definition(R"({
    "format_version":"grammar-definition-v2","entry":{"nonterminal":"Value","type":"Int"},
    "search_limits":{"max_nodes":5,"max_depth":4},"execution_limits":{"fuel":100},
    "nonterminals":[{"id":"Value","type":"Int","scope":[],"alternatives":[
      {"id":"initial","weight":1,"generation_stages":["initial"],
       "expression":{"constant":{"type":"Int","values":["1"]}}},
      {"id":"mutation","weight":1,"generation_stages":["mutation"],
       "expression":{"constant":{"type":"Int","values":["2"]}}}]}]
  })")));
  auto config = compiled_config(grammar, 8, 1.0, 1.0, true);
  const auto population = std::vector<ProgramGenome>(8, generate_derivation(*grammar, 9).genome);
  const auto resources = repro::make_gpu_repro_run_resources(config);
  const auto prepared = repro::prepare_gpu_repro_backend_inputs(population, config, 101, nullptr, resources);
  auto wrong_stage = config;
  wrong_stage.generation_request->stage = GenerationStage::Mutation;
  expect_invalid([&] {
    (void)repro::prepare_gpu_repro_backend_inputs(population, wrong_stage, 101, nullptr, resources);
  }, "GPU run resources ignored request stage");
  const auto result = repro::run_gpu_repro_backend_prepared(score_manually(population), config, prepared, nullptr);
  require(result.stats.kernel_ms > 0 && result.stats.variation.fallback_children == 0 &&
          result.stats.variation.acceptance_rejections == 0, "staged GPU donor used fallback or rejection: kernel=" + std::to_string(result.stats.kernel_ms) +
          " fallback=" + std::to_string(result.stats.variation.fallback_children) +
          " rejected=" + std::to_string(result.stats.variation.acceptance_rejections) +
          " generation=" + std::to_string(result.stats.variation.generation_rejections) +
          " budget=" + std::to_string(result.stats.variation.budget_rejections) +
          " contract=" + std::to_string(result.stats.variation.contract_rejections) +
          " donors=" + std::to_string(prepared.config.compiled_donor_count));
  for (const auto& child : result.next_population)
    require(execute_int(child) == 2, "GPU subtree mutation sampled initial productions");
  config.eval_engine = EvalEngine::GPU;
  const std::vector<EvalCase> cases{{{}, Value::from_int(2)}};
  auto direct_population = population;
  const auto direct = evolve_population(cases, config, &direct_population);
  config.repro_overlap = true;
  auto overlap_population = population;
  const auto overlap = evolve_population(cases, config, &overlap_population);
  require(direct.history_best_fitness == overlap.history_best_fitness &&
          direct.history_mean_fitness == overlap.history_mean_fitness &&
          overlap.timing.reproduction_totals.kernel_ms > 0,
          "staged donor overlap changed evolution or omitted GPU reproduction");
  for (const auto& child : overlap.final_population)
    require(execute_int(child.genome) == 2, "overlap offspring escaped mutation stage");
}

void test_public_modes_and_overlap() {
  exercise_three_generations(3, false, 0.0, 0.0);
  exercise_three_generations(3, true, 1.0, 0.0);
  exercise_three_generations(4, true, 1.0, 1.0);
  test_compiled_overlap_matches_direct_preparation();
  test_evolution_run_resources_all_modes();
}

void test_expanded_representation_capacity() {
  using namespace gagp;
  using namespace gagp::evo;
  using namespace gagp::evo::grammar;
  const auto tree = [](const auto& self, int depth, bool input) -> std::string {
    const std::string charge = R"("resource_charge":{"nodes":0,"depth":0,"resets_depth":false})";
    if (!depth) return "{" + charge + (input ? R"(,"input":"x"})" : R"(,"constant":{"type":"Int","values":["1"]}})");
    const auto child = self(self, depth - 1, input);
    return "{" + charge + R"(,"signature":"add(Int,Int)->Int","args":[)" + child + "," + child + "]}";
  };
  const auto definition = [&](bool input) { return std::string(R"({"format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"E","type":"Int"},"inputs":[{"name":"x","type":"Int"}],
    "search_limits":{"max_nodes":600,"max_depth":20},"execution_limits":{"fuel":1000},
    "nonterminals":[{"id":"E","type":"Int","scope":[],"alternatives":[
      {"id":"expanded","weight":1,"expression":)") + tree(tree, 8, input) + "}]}]}"; };
  auto grammar = std::make_shared<const CompiledGrammar>(compile_grammar(parse_definition(definition(false))));
  auto config = compiled_config(grammar, 2, 1.0, 1.0, true);
  config.fuel = 1000;
  config.offspring_resource_budget = ProjectedBudget{4, 3};
  auto genome = generate_derivation(*grammar, 0).genome;
  require(genome.ast.consts.size() > repro::kGpuReproMaxConsts,
          "capacity fixture must exercise source table overflow rejection");
  expect_invalid([&] {
    (void)repro::prepare_gpu_repro_backend_inputs(std::vector<ProgramGenome>(2, genome), config, 42, nullptr);
  }, "oversized source constant table was packed into a truncated buffer");
  // Use inputs for the node-capacity case so parent and generated donor tables
  // fit independently of the separate constant-table limit.
  grammar = std::make_shared<const CompiledGrammar>(compile_grammar(parse_definition(definition(true))));
  config.compiled_grammar = grammar;
  config.generation_request = entry_request(*grammar);
  config.verification_inputs = {{"x", RType::Int}};
  genome = generate_derivation(*grammar, 0).genome;
  const std::vector<ProgramGenome> population(2, genome);
  require(population.front().ast.nodes.size() == 515,
          "capacity fixture must exceed the old 512-node kernel limit");
  const auto prepared = repro::prepare_gpu_repro_backend_inputs(population, config, 42, nullptr);
  const auto result = repro::run_gpu_repro_backend_prepared(score_manually(population), config, prepared);
  require(result.stats.kernel_ms > 0, "expanded representation bypassed GPU reproduction");
  require_attempts(result, 2, true);
  require(result.stats.variation.fallback_children == 0 &&
              result.stats.variation.acceptance_rejections == 0,
          "large GPU splice silently fell back to its parent");
  for (const auto& child : result.next_population) {
    require(child.ast.nodes.size() == 515 &&
                child.derivation->resources->subtree().nodes == 4,
            "expanded representation lost physical nodes or projected cost");
    const auto evaluated = execute_bytecode_cpu(compile_for_eval(child, verify_ast(child.ast, config.verification_inputs).verified, {"x"}), {{0, Value::from_int(1)}}, 1000);
    require(!evaluated.is_error && evaluated.value.tag == ValueTag::Int && evaluated.value.i == 256,
            "large GPU splice changed the program result");
  }
}

void test_bounded_external_capture_donors() {
  using namespace gagp;
  using namespace gagp::evo;
  using namespace gagp::evo::grammar;
  auto definition = cli_detail::JsonParser(gagp::test::bounded_capture_definition()).parse();
  for (auto& nt : definition.object_v.at("nonterminals").array_v)
    if (nt.object_v.at("id").string_v != "Recurrence")
      nt.object_v["variation"] = cli_detail::JsonParser("false").parse();
  const auto grammar = std::make_shared<const CompiledGrammar>(
      compile_grammar(parse_definition(canonical_json(definition))));
  auto config = compiled_config(grammar, 8, 1.0, 1.0, true);
  config.fuel = 1000;
  std::vector<ProgramGenome> population;
  for (std::uint64_t seed = 0; seed < 8; ++seed)
    population.push_back(generate_derivation(*grammar, seed).genome);
  VariationContext context(grammar);
  for (const auto& parent : population) {
    const auto analysis = context.analyze(parent);
    require(analysis->sites.size() == 1 && analysis->sites.front().occurrences.size() == 2,
            "bounded capture donor fixture must vary one repeated contextual recurrence");
  }
  const auto prepared = repro::prepare_gpu_repro_backend_inputs(population, config, 101, nullptr);
  const auto result = repro::run_gpu_repro_backend_prepared(
      score_manually(population), config, prepared, nullptr);
  require(result.stats.kernel_ms > 0 && result.stats.variation.mutation_attempts == 8 &&
          result.stats.variation.generation_rejections == 0 &&
          result.stats.variation.acceptance_rejections == 0 &&
          result.stats.variation.fallback_children == 0,
          "GPU reproduction rejected legal donors with external bounded captures");
  std::vector<BytecodeProgram> programs;
  std::vector<double> expected;
  for (const auto& child : result.next_population) {
    require_membership(*grammar, child);
    const auto verified = verify_ast(child.ast, {});
    require(verified.ok, "GPU bounded donor splice lost lexical validity");
    programs.push_back(compile_for_eval(child, verified.verified));
    const auto cpu = execute_bytecode_cpu(programs.back(), {}, 1000);
    require(!cpu.is_error && cpu.value.tag == ValueTag::Int &&
            (cpu.value.i == 9 || cpu.value.i == 29),
            "GPU bounded donor splice changed its repeated capture mapping");
    expected.push_back(cpu.value.i == 9 ? 0.0 : -20.0);
  }
  FitnessSessionGpu fitness;
  // Keep the numeric error (20) below the fitness clamp, and distinguish it
  // from the execution-error penalty. See spec/fitness.md.
  require(fitness.init({{}}, {Value::from_int(9)}, 1000, 1024, 1000.0).ok,
          "could not initialize GPU bounded donor fitness check");
  const auto evaluated = fitness.eval_programs(programs);
  if (!evaluated.ok || evaluated.fitness != expected) {
    std::cerr << "bounded capture GPU evaluation ok=" << evaluated.ok << " actual:";
    for (auto value : evaluated.fitness) std::cerr << ' ' << value;
    std::cerr << " expected:";
    for (auto value : expected) std::cerr << ' ' << value;
    std::cerr << '\n';
  }
  require(evaluated.ok && evaluated.fitness == expected,
          "captured bounded donor children changed CPU/GPU fitness");
  config.eval_engine = EvalEngine::GPU;
  config.generations = 2;
  config.penalty = 1000.0;
  const std::vector<EvalCase> cases{{{}, Value::from_int(0)}};
  auto direct_population = population;
  const auto direct = evolve_population(cases, config, &direct_population);
  config.repro_overlap = true;
  auto overlap_population = population;
  const auto overlap = evolve_population(cases, config, &overlap_population);
  require(direct.history_best_fitness.size() == 2 &&
          direct.history_best_fitness == overlap.history_best_fitness &&
          direct.history_mean_fitness == overlap.history_mean_fitness &&
          overlap.timing.reproduction_totals.kernel_ms > 0,
          "bounded external captures changed overlap evolution or bypassed GPU reproduction");
  for (auto mean : overlap.history_mean_fitness)
    require(mean >= -29.0 && mean <= -9.0,
            "bounded capture overlap introduced an execution-error penalty");
}

void test_projected_offspring_budget() {
  using namespace gagp;
  using namespace gagp::evo;
  using namespace gagp::evo::grammar;
  const auto grammar = std::make_shared<const CompiledGrammar>(compile_grammar(parse_definition(R"({
    "format_version":"grammar-definition-v2","entry":{"nonterminal":"E","type":"Int"},
    "search_limits":{"max_nodes":5,"max_depth":4},"execution_limits":{"fuel":100},
    "nonterminals":[{"id":"E","type":"Int","scope":[],"alternatives":[
      {"id":"a_expensive","weight":1,"expression":{"constant":{"type":"Int","values":["2"]},
        "resource_charge":{"nodes":20,"depth":1,"resets_depth":false}}},
      {"id":"b_cheap","weight":1,"expression":{"constant":{"type":"Int","range":["1","2"],
        "sample_from":{"type":"Int","values":["1"]},"mutation":{"kind":"add","range":["1","1"]}}}}]}]
  })")));
  auto config = compiled_config(grammar, 8, 1.0, 0.0, true);
  const auto population = source_population(*grammar, 8, 1);
  const auto scored = score_manually(population);
  const auto old_resources = repro::make_gpu_repro_run_resources(config);
  const auto old_prepared = repro::prepare_gpu_repro_backend_inputs(population, config, 42, nullptr);
  config.offspring_resource_budget = ProjectedBudget{5, 4};
  expect_invalid([&] { (void)repro::prepare_gpu_repro_backend_inputs(population, config, 42, nullptr, old_resources); },
                 "GPU run resources ignored changed projected budget");
  expect_invalid([&] { (void)repro::run_gpu_repro_backend_prepared(scored, config, old_prepared); },
                 "GPU prepared state ignored changed projected budget");
  std::uint64_t rejected = 0;
  for (unsigned seed = 0; seed < 4; ++seed) {
    const auto prepared = repro::prepare_gpu_repro_backend_inputs(population, config, seed, nullptr);
    for (const auto& candidate : prepared.packed.candidates)
      require(!candidate.has_projected_allowance,
              "ambiguous resource grammar received an unsafe device pruning rule");
    const auto result = repro::run_gpu_repro_backend_prepared(scored, config, prepared);
    rejected += result.stats.variation.budget_rejections;
    require(result.stats.kernel_ms > 0,"projected budget test bypassed GPU reproduction");
    for (const auto& child : result.next_population)
      require(execute_int(child) == 1 && child.derivation->resources->subtree().nodes == 5,
              "GPU copyback admitted projected-over-budget offspring");
  }
  require(rejected > 0,"GPU projected budget rejection was not exercised");
  config.eval_engine = EvalEngine::GPU;
  const std::vector<EvalCase> cases{{{}, Value::from_int(1)}};
  const auto direct = evolve_population(cases, config, &population);
  config.repro_overlap = true;
  const auto overlap = evolve_population(cases, config, &population);
  require(direct.history_best_fitness == overlap.history_best_fitness &&
              direct.history_mean_fitness == overlap.history_mean_fitness &&
              overlap.timing.reproduction_totals.kernel_ms > 0,
          "projected offspring budget changed overlap semantics or backend routing");
}

void test_overlap_preserves_unsorted_population_replay() {
  using namespace gagp;
  using namespace gagp::evo;
  using namespace gagp::evo::grammar;
  auto grammar = std::make_shared<const CompiledGrammar>(compile_grammar(parse_definition(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","type":"Int"},
    "search_limits":{"max_nodes":5,"max_depth":4},
    "execution_limits":{"fuel":100},
    "nonterminals":[{"id":"Main","type":"Int","scope":[],"alternatives":[
      {"id":"value","weight":1,"expression":{"constant":{"type":"Int","range":["0","31"]}}}
    ]}]
  })")));
  auto config = compiled_config(grammar, 16, 0.5, 0.5, true);
  config.eval_engine = EvalEngine::GPU;
  config.selection_pressure = 3;
  config.penalty = 100;
  std::vector<ProgramGenome> population;
  for (int i = 0; i < config.population_size; ++i) {
    auto member = generate_derivation(*grammar, 42 + i).genome;
    member.ast.consts.front() = Value::from_int((i * 7) % 16);
    member.meta = build_genome_meta(member.ast);
    population.push_back(std::move(member));
  }
  const std::vector<EvalCase> cases{{{}, Value::from_int(0)}};
  auto direct_population = population;
  const auto direct = evolve_population(cases, config, &direct_population);
  config.repro_overlap = true;
  auto overlap_population = population;
  const auto overlap = evolve_population(cases, config, &overlap_population);
  require(direct.history_best_fitness == overlap.history_best_fitness &&
              direct.history_mean_fitness == overlap.history_mean_fitness,
          "overlap reordered unequal-fitness parents before tournament selection");
  require(direct.final_population.size() == overlap.final_population.size(),
          "overlap changed final population size");
  for (std::size_t i = 0; i < direct.final_population.size(); ++i)
    require(ast_cache_key(direct.final_population[i].genome.ast) ==
                ast_cache_key(overlap.final_population[i].genome.ast),
            "overlap changed seeded final population replay");
}

}  // namespace

int main() {
  try {
    test_overlap_preserves_unsorted_population_replay();
    test_expanded_representation_capacity();
    test_prepared_replay_and_rejections();
    test_prepared_parent_certificates();
    test_immediate_run_matches_public_replay();
    test_projected_offspring_budget();
    test_bounded_external_capture_donors();
    test_run_resources_retain_grammar_payloads();
    test_source_identity_normalizes_unused_tables();
    test_closed_cross_scope_crossover();
    test_public_modes_and_overlap();
    test_mixed_roots_across_all_modes();
    test_sequence_constant_proposals_and_overlap();
    test_generation_stages_on_gpu();
    test_mutable_template_constant_on_gpu();
    test_constant_policies_on_gpu();
    test_int_addition_on_gpu();
    test_float_range_on_gpu(false);
    test_float_range_on_gpu(true);
    test_float_range_on_gpu(false,true);
    test_float_range_on_gpu(true,true);
    test_float_range_on_gpu(true,true,true,false);
    test_float_range_on_gpu(true,true,true,true);
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "compiled public GPU backend: prepared replay, run-resource "
               "reuse and payload retention, guards, modes, overlap parity, "
               "and repeated-generation execution passed\n";
  return 0;
}
