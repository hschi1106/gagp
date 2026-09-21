#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/crossover.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/grammar/request.hpp"
#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/evolution/mutation.hpp"
#include "gagp/evolution/population_init.hpp"
#include "gagp/evolution/repro/backend.hpp"
#include "gagp/evolution/selection.hpp"

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;

namespace {

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void rejects_invalid_argument(const std::function<void()>& action,
    const char* message) {
  try {
    action();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error(message);
}

std::shared_ptr<const CompiledGrammar> compile_shared(const std::string& text) {
  return std::make_shared<const CompiledGrammar>(
      compile_grammar(parse_definition(text)));
}

std::shared_ptr<const CompiledGrammar> repeated_hole_grammar() {
  return compile_shared(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","type":"Int"},
    "search_limits":{"max_nodes":12,"max_depth":7},
    "execution_limits":{"fuel":100},
    "templates":[{"id":"Twice","type":"Int","scope":[],
      "holes":[{"id":"value","type":"Int","scope":[]}],
      "body":{"signature":"add(Int,Int)->Int","args":[
        {"hole":"value"},{"hole":"value"}]}}],
    "nonterminals":[
      {"id":"Value","type":"Int","scope":[],"alternatives":[
        {"id":"value","weight":1,"expression":{"constant":{
          "type":"Int","values":["1","2","3","4","5","6"]}}}]},
      {"id":"Main","type":"Int","scope":[],"alternatives":[
        {"id":"twice","weight":1,"expression":{
          "template":"Twice","holes":{"value":{"ref":"Value"}}}}]}
    ]
  })");
}

std::shared_ptr<const CompiledGrammar> local_grammar() {
  return compile_shared(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","category":"Program","type":"Int"},
    "inputs":[{"name":"input","type":"Int"}],
    "locals":[{"name":"x","type":"Int"}],
    "search_limits":{"max_nodes":8,"max_depth":7},
    "execution_limits":{"fuel":100},
    "nonterminals":[
      {"id":"Seed","type":"Int","scope":[],"alternatives":[
        {"id":"seed","weight":1,"expression":{"constant":{
          "type":"Int","values":["1","2","3"]}}}]},
      {"id":"LocalX","type":"Int","scope":[],"alternatives":[
        {"id":"x","weight":1,"expression":{"local":"x"}}]},
      {"id":"Main","category":"Program","type":"Int","scope":[],
       "alternatives":[{"id":"body","weight":1,"expression":{
        "control":"program(Block)->Program","type":"Int","args":[
          {"control":"block_cons(Statement,Block)->Block","type":"Int","args":[
            {"control":"assign(Int)->Statement","type":"Int","name":"x",
             "args":[{"ref":"Seed"}]},
            {"control":"block_cons(Statement,Block)->Block","type":"Int","args":[
              {"control":"return(Int)->Statement","type":"Int",
               "args":[{"ref":"LocalX"}]},
              {"control":"block_nil()->Block","type":"Int","args":[]}]}]}]}}]}
    ]
  })");
}

std::uint32_t nonterminal(const CompiledGrammar& grammar,
    const std::string& stable_id) {
  const auto found = std::find_if(grammar.nonterminals().begin(),
      grammar.nonterminals().end(), [&](const CompiledNonterminal& value) {
        return value.stable_id == stable_id;
      });
  if (found == grammar.nonterminals().end())
    throw std::runtime_error("missing fixture nonterminal " + stable_id);
  return found->id;
}

EvolutionConfig compiled_config(
    const std::shared_ptr<const CompiledGrammar>& grammar, int population_size) {
  EvolutionConfig cfg;
  cfg.population_size = population_size;
  cfg.generations = 1;
  cfg.selection_pressure = 2;
  cfg.mutation_rate = 0.75;
  cfg.mutation_subtree_prob = 0.6;
  cfg.seed = 991;
  cfg.fuel = static_cast<int>(grammar->execution_limits().fuel);
  cfg.compiled_grammar = grammar;
  cfg.generation_request = entry_request(*grammar);
  cfg.reproduction_backend = repro::ReproductionBackend::Cpu;
  return cfg;
}

std::vector<ProgramGenome> generated_population(const CompiledGrammar& grammar,
    int size, std::uint64_t seed, const GenerationRequest* request = nullptr) {
  std::vector<ProgramGenome> population;
  population.reserve(static_cast<std::size_t>(size));
  for (int i = 0; i < size; ++i) {
    const std::uint64_t one_seed = seed + static_cast<std::uint64_t>(i);
    population.push_back(request
        ? generate_derivation(grammar, one_seed, *request).genome
        : generate_derivation(grammar, one_seed).genome);
  }
  return population;
}

AstNode& first_constant_node(ProgramGenome& genome) {
  const auto found = std::find_if(genome.ast.nodes.begin(), genome.ast.nodes.end(),
      [](const AstNode& node) { return node.kind == NodeKind::CONST; });
  if (found == genome.ast.nodes.end())
    throw std::runtime_error("fixture genome has no constant node");
  return *found;
}

std::vector<ScoredGenome> score_owned(
    const std::vector<ProgramGenome>& population) {
  std::vector<ScoredGenome> scored;
  scored.reserve(population.size());
  for (std::size_t i = 0; i < population.size(); ++i)
    scored.push_back(ScoredGenome{population[i], static_cast<double>(i)});
  return scored;
}

std::vector<ScoredGenomeRef> score_refs(
    const std::vector<ScoredGenome>& scored) {
  std::vector<ScoredGenomeRef> refs;
  refs.reserve(scored.size());
  for (const auto& one : scored)
    refs.push_back(ScoredGenomeRef{&one.genome, one.fitness});
  return refs;
}

void check_same_population(const std::vector<ProgramGenome>& actual,
    const std::vector<ProgramGenome>& expected, const char* message) {
  check(actual.size() == expected.size(), message);
  for (std::size_t i = 0; i < actual.size(); ++i)
    check(actual[i].meta.program_key == expected[i].meta.program_key, message);
}

void check_same_counters(const VariationCounters& actual,
    const VariationCounters& expected, const char* message) {
  check(actual.crossover_attempts == expected.crossover_attempts &&
        actual.mutation_attempts == expected.mutation_attempts &&
        actual.contract_rejections == expected.contract_rejections &&
        actual.budget_rejections == expected.budget_rejections &&
        actual.generation_rejections == expected.generation_rejections &&
        actual.acceptance_rejections == expected.acceptance_rejections &&
        actual.fallback_children == expected.fallback_children &&
        actual.unchanged_children == expected.unchanged_children &&
        actual.changed_children == expected.changed_children,
      message);
}

void require_population_membership(const CompiledGrammar& grammar,
    const std::vector<ProgramGenome>& population,
    const GenerationRequest* request = nullptr) {
  for (const auto& genome : population) {
    if (request) require_membership(grammar, genome, *request);
    else require_membership(grammar, genome);
  }
}

void test_backend_replay_and_owned_ref_parity() {
  const auto grammar = repeated_hole_grammar();
  const auto population = generated_population(*grammar, 6, 100);
  const auto scored = score_owned(population);
  const auto refs = score_refs(scored);
  auto cfg = compiled_config(grammar, 6);

  std::mt19937_64 owned_rng_a(4321);
  std::mt19937_64 owned_rng_b(4321);
  const auto owned_a = repro::run_reproduction_backend(scored, cfg, owned_rng_a);
  const auto owned_b = repro::run_reproduction_backend(scored, cfg, owned_rng_b);
  check_same_population(owned_a.next_population, owned_b.next_population,
      "compiled CPU backend is not deterministic for owned scored genomes");
  check_same_counters(owned_a.stats.variation, owned_b.stats.variation,
      "deterministic owned backend replay changed variation counters");

  std::mt19937_64 ref_rng(4321);
  const auto referenced = repro::run_reproduction_backend(refs, cfg, ref_rng);
  check_same_population(referenced.next_population, owned_a.next_population,
      "owned and referenced compiled CPU backend overloads diverged");
  check_same_counters(referenced.stats.variation, owned_a.stats.variation,
      "owned and referenced backend overloads changed variation counters");
  require_population_membership(*grammar, referenced.next_population);
}

void test_manual_selection_crossover_mutation_replay() {
  const auto grammar = repeated_hole_grammar();
  const auto population = generated_population(*grammar, 6, 700);
  const auto scored = score_owned(population);
  auto cfg = compiled_config(grammar, 6);
  cfg.mutation_rate = 0.625;
  constexpr std::uint64_t kSeed = 87654;

  std::mt19937_64 backend_rng(kSeed);
  const auto actual = repro::run_reproduction_backend(scored, cfg, backend_rng);

  std::mt19937_64 manual_rng(kSeed);
  auto selected = tournament_selection_indices_without_replacement(
      scored, manual_rng, cfg.selection_pressure, cfg.population_size);
  std::shuffle(selected.begin(), selected.end(), manual_rng);
  std::uniform_real_distribution<double> probability(0.0, 1.0);
  std::uniform_int_distribution<std::uint64_t> operator_seed(0, 2000000000ULL);
  VariationContext context(grammar);
  std::vector<ProgramGenome> expected;
  expected.reserve(static_cast<std::size_t>(cfg.population_size));
  for (std::size_t i = 0; i + 1 < selected.size(); i += 2) {
    auto children = crossover(scored[selected[i]].genome,
        scored[selected[i + 1]].genome, operator_seed(manual_rng), context);
    if (probability(manual_rng) < cfg.mutation_rate)
      children.first = mutate(children.first, operator_seed(manual_rng),
          context, cfg.mutation_subtree_prob);
    expected.push_back(std::move(children.first));
    if (probability(manual_rng) < cfg.mutation_rate)
      children.second = mutate(children.second, operator_seed(manual_rng),
          context, cfg.mutation_subtree_prob);
    expected.push_back(std::move(children.second));
  }

  check_same_population(actual.next_population, expected,
      "backend changed tournament/shuffle/crossover-then-mutation seed order");
  check_same_counters(actual.stats.variation, context.counters(),
      "backend variation counters differ from independent operator replay");
}

void test_repeated_holes_and_local_children_remain_members() {
  for (const auto& grammar : {repeated_hole_grammar(), local_grammar()}) {
    const auto population = generated_population(*grammar, 6, 40);
    const auto scored = score_owned(population);
    auto cfg = compiled_config(grammar, 6);
    cfg.mutation_rate = 1.0;
    cfg.mutation_subtree_prob = 1.0;
    std::mt19937_64 rng(12345);
    const auto result = repro::run_reproduction_backend(scored, cfg, rng);
    check(result.next_population.size() == 6,
        "compiled reproduction returned the wrong population size");
    require_population_membership(*grammar, result.next_population);
  }
}

void test_all_parents_are_validated_before_selection() {
  const auto grammar = repeated_hole_grammar();
  auto population = generated_population(*grammar, 3, 80);
  auto& invalid = population.back();
  invalid.ast.consts.at(static_cast<std::size_t>(first_constant_node(invalid).i0)) =
      Value::from_int(999);
  invalid.meta = build_genome_meta(invalid.ast);
  invalid.derivation.reset();
  auto scored = score_owned(population);
  scored.back().fitness = -1000000.0;
  auto cfg = compiled_config(grammar, 2);
  cfg.selection_pressure = 2;
  std::mt19937_64 rng(11);
  rejects_invalid_argument(
      [&] { (void)repro::run_reproduction_backend(scored, cfg, rng); },
      "compiled backend accepted an invalid parent outside the selected set");

  const auto refs = score_refs(scored);
  std::mt19937_64 ref_rng(11);
  rejects_invalid_argument(
      [&] { (void)repro::run_reproduction_backend(refs, cfg, ref_rng); },
      "referenced compiled backend did not validate every scored parent");
}

void test_compiled_initialization_and_evolve_validation() {
  const auto grammar = repeated_hole_grammar();
  auto cfg = compiled_config(grammar, 5);
  const std::vector<EvalCase> cases{{{}, Value::from_int(4)}};
  const auto case_set = prepare_case_set(cases);

  const auto initialized = initialize_population(cfg, case_set);
  check(!initialized.replayed && initialized.population.size() == 5,
      "compiled config did not initialize an absent population");
  require_population_membership(*grammar, initialized.population);

  auto run_cfg = cfg;
  run_cfg.population_size = 6;
  run_cfg.skip_final_eval = true;
  const auto evolved = evolve_population(cases, run_cfg);
  check(evolved.history_best.size() == 1 &&
        evolved.timing.generations.size() == 1,
      "compiled grammar was not wired through the evolution loop");
  check(evolved.timing.generations[0].reproduction.variation.crossover_attempts == 3,
      "generation timing did not receive compiled variation counters");
  check_same_counters(evolved.timing.reproduction_totals.variation,
      evolved.timing.generations[0].reproduction.variation,
      "evolution timing did not accumulate variation counters");

  auto invalid_population = generated_population(*grammar, 6, 400);
  first_constant_node(invalid_population[5]).i0 = 999;
  invalid_population[5].meta = build_genome_meta(invalid_population[5].ast);
  invalid_population[5].derivation.reset();
  rejects_invalid_argument(
      [&] { (void)evolve_population(cases, run_cfg, &invalid_population); },
      "invalid imported population reached evaluation before membership rejection");

  auto wrong_fuel = run_cfg;
  ++wrong_fuel.fuel;
  rejects_invalid_argument(
      [&] { (void)evolve_population(cases, wrong_fuel); },
      "compiled evolution accepted fuel different from grammar execution fuel");
}

void test_nonentry_request_is_used_end_to_end() {
  const auto grammar = repeated_hole_grammar();
  auto request = entry_request(*grammar);
  request.nonterminal = nonterminal(*grammar, "Value");
  request.type = RType::Int;
  request.budget = grammar->search_limits();
  (void)validate_request(*grammar, request);

  auto cfg = compiled_config(grammar, 6);
  cfg.generation_request = request;
  const std::vector<EvalCase> cases{{{}, Value::from_int(2)}};
  const auto case_set = prepare_case_set(cases);
  const auto initialized = initialize_population(cfg, case_set);
  require_population_membership(*grammar, initialized.population, &request);

  const auto scored = score_owned(initialized.population);
  std::mt19937_64 rng(7001);
  const auto reproduced = repro::run_reproduction_backend(scored, cfg, rng);
  require_population_membership(*grammar, reproduced.next_population, &request);

  cfg.skip_final_eval = true;
  const auto evolved = evolve_population(cases, cfg);
  check(evolved.history_best.size() == 1,
      "nonentry generation request did not run through evolve_population");
}

void test_odd_population_counts_discarded_crossover_child() {
  const auto grammar = repeated_hole_grammar();
  const auto population = generated_population(*grammar, 6, 900);
  const auto scored = score_owned(population);
  auto cfg = compiled_config(grammar, 5);
  cfg.mutation_rate = 1.0;
  std::mt19937_64 rng(987);
  const auto result = repro::run_reproduction_backend(scored, cfg, rng);
  const auto& counters = result.stats.variation;
  check(result.next_population.size() == 5,
      "odd compiled reproduction population size was not capped");
  check(counters.crossover_attempts == 3,
      "odd population did not perform the final crossover pair");
  check(counters.mutation_attempts == 5,
      "discarded odd crossover child was unexpectedly mutated");
  check(counters.changed_children + counters.unchanged_children == 11,
      "variation classifications omitted the discarded crossover output");
  check(counters.changed_children + counters.unchanged_children ==
        counters.crossover_attempts * 2 + counters.mutation_attempts,
      "compiled variation operator-output counts are not conserved");
}

}  // namespace

int main() {
  try {
    test_backend_replay_and_owned_ref_parity();
    test_manual_selection_crossover_mutation_replay();
    test_repeated_holes_and_local_children_remain_members();
    test_all_parents_are_validated_before_selection();
    test_compiled_initialization_and_evolve_validation();
    test_nonentry_request_is_used_end_to_end();
    test_odd_population_counts_discarded_crossover_child();
    std::cout << "grammar reproduction: backend replay, validation, integration, and counters passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
