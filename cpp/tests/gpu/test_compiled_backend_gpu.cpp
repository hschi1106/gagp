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
#include "gagp/evolution/lifecycle.hpp"
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
    "format_version":"grammar-definition-v1",
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
    "format_version":"grammar-definition-v1",
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

void exercise_three_generations(int population_size, bool explicit_request,
                                double mutation_rate,
                                double subtree_probability) {
  const auto grammar = repeated_capture_grammar();
  EvolutionConfig config = compiled_config(grammar, population_size,
                                            mutation_rate,
                                            subtree_probability,
                                            explicit_request);
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

void test_public_modes_and_overlap() {
  exercise_three_generations(3, false, 0.0, 0.0);
  exercise_three_generations(3, true, 1.0, 0.0);
  exercise_three_generations(4, true, 1.0, 1.0);
  test_compiled_overlap_matches_direct_preparation();
  test_evolution_run_resources_all_modes();
}

}  // namespace

int main() {
  try {
    test_prepared_replay_and_rejections();
    test_run_resources_retain_grammar_payloads();
    test_public_modes_and_overlap();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "compiled public GPU backend: prepared replay, run-resource "
               "reuse and payload retention, guards, modes, overlap parity, "
               "and repeated-generation execution passed\n";
  return 0;
}
