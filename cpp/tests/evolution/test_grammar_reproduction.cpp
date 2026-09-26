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
#include "../fixtures/mixed_population.hpp"
#include "../../src/evolution/grammar/variation_internal.hpp"

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

std::shared_ptr<const CompiledGrammar> many_sites_grammar() {
  std::string holes, arguments;
  constexpr int count = 20;  // More than the GPU preparation's 16-site cap.
  for (int i = 0; i < count; ++i) {
    const auto id = "h" + std::to_string(i);
    if (i) { holes += ","; arguments += ","; }
    holes += "{\"id\":\"" + id + "\",\"type\":\"Int\",\"scope\":[]}";
    arguments += "\"" + id + "\":{\"ref\":\"Value\"}";
  }
  const std::function<std::string(int, int)> sum = [&](int begin, int end) {
    if (end - begin == 1)
      return std::string("{\"hole\":\"h") + std::to_string(begin) + "\"}";
    const int middle = (begin + end) / 2;
    return std::string("{\"signature\":\"add(Int,Int)->Int\",\"args\":[") +
        sum(begin, middle) + "," + sum(middle, end) + "]}";
  };
  return compile_shared(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","type":"Int"},
    "search_limits":{"max_nodes":64,"max_depth":12},
    "execution_limits":{"fuel":200},
    "templates":[{"id":"Many","type":"Int","scope":[],"holes":[)" +
    holes + "],\"body\":" + sum(0, count) + R"(}],
    "nonterminals":[
      {"id":"Value","type":"Int","scope":[],"alternatives":[
        {"id":"value","weight":1,"expression":{"constant":{
          "type":"Int","values":["1","2","3","4","5","6"]}}}]},
      {"id":"Main","type":"Int","scope":[],"alternatives":[
        {"id":"many","weight":1,"expression":{"template":"Many","holes":{)" +
    arguments + "}}}]}]}");
}

std::shared_ptr<const CompiledGrammar> captured_hole_grammar() {
  return compile_shared(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","type":"Int"},
    "search_limits":{"max_nodes":30,"max_depth":12},
    "execution_limits":{"fuel":100},
    "templates":[{"id":"Twice","type":"Int","scope":[],
      "holes":[{"id":"value","type":"Int","scope":[{"name":"x","type":"Int"}]}],
      "body":{"signature":"add(Int,Int)->Int","args":[
        {"signature":"let(Int,Int)->Int","args":[
          {"constant":{"type":"Int","values":["1"]}},{"hole":"value"}],"bind":{"1":["x"]}},
        {"signature":"let(Int,Int)->Int","args":[
          {"constant":{"type":"Int","values":["2"]}},{"hole":"value"}],"bind":{"1":["x"]}}]}}],
    "nonterminals":[
      {"id":"Main","type":"Int","scope":[],"alternatives":[
        {"id":"main","weight":1,"expression":{"template":"Twice","holes":{"value":{"ref":"Scoped"}}}}]},
      {"id":"Scoped","type":"Int","scope":[{"name":"x","type":"Int"}],"alternatives":[
        {"id":"bound","weight":1,"expression":{"bound":"x"}},
        {"id":"offset","weight":1,"expression":{"signature":"add(Int,Int)->Int","args":[
          {"bound":"x"},{"constant":{"type":"Int","values":["3","4","5"]}}]}}]}
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

void test_prepared_ablation(repro::CpuReproAblation ablation) {
  bool observed_distinct_candidates = false;
  for (const auto& grammar : {repeated_hole_grammar(), local_grammar(), many_sites_grammar(), captured_hole_grammar()}) {
    const auto population = generated_population(*grammar, 6, 40);
    const auto scored = score_owned(population);
    const auto refs = score_refs(scored);
    for (int size : {5, 6}) {
      for (double mutation : {0.0, 1.0}) {
        auto cfg = compiled_config(grammar, size);
        cfg.cpu_repro_ablation = ablation;
        cfg.mutation_rate = mutation;
        cfg.mutation_subtree_prob = 1.0;
        for (std::uint64_t seed : {11, 12345, 87654}) {
          std::mt19937_64 owned_rng(seed), ref_rng(seed), plain_rng(seed);
          const auto owned = repro::run_reproduction_backend(scored, cfg, owned_rng);
          const auto referenced = repro::run_reproduction_backend(refs, cfg, ref_rng);
          check_same_population(owned.next_population, referenced.next_population,
              "GPU candidate ablation owned/ref replay differs");
          check_same_counters(owned.stats.variation, referenced.stats.variation,
              "GPU candidate ablation replay counters differ");
          check(owned_rng == ref_rng, "candidate replay consumed different RNG streams");
          require_population_membership(*grammar, owned.next_population);
          const auto& counters = owned.stats.variation;
          check(owned.next_population.size() == static_cast<std::size_t>(size),
              "candidate ablation did not cap odd populations");
          check(counters.crossover_attempts == static_cast<std::uint64_t>((size + 1) / 2) &&
                counters.mutation_attempts == static_cast<std::uint64_t>(mutation * size),
              "candidate ablation changed crossover/mutation scheduling");
          check(counters.changed_children + counters.unchanged_children ==
                2 * counters.crossover_attempts + counters.mutation_attempts,
              "candidate ablation lost operator output classifications");
          check(counters.acceptance_rejections == 0,
              "candidate ablation broke repeated-hole or local membership");
          cfg.cpu_repro_ablation = repro::CpuReproAblation::None;
          const auto plain = repro::run_reproduction_backend(scored, cfg, plain_rng);
          cfg.cpu_repro_ablation = ablation;
          if (ablation == repro::CpuReproAblation::GpuCoupledDonor && mutation == 0)
            check_same_population(owned.next_population, plain.next_population,
                "coupled donor ablation changed ordinary CPU crossover");
          for (std::size_t i = 0; i < owned.next_population.size(); ++i)
            observed_distinct_candidates |= owned.next_population[i].meta.program_key !=
                plain.next_population[i].meta.program_key;
          check(owned_rng == plain_rng,
              "candidate preparation perturbed tournament/operator RNG scheduling");
        }
      }
    }
    auto cfg = compiled_config(grammar, 6);
    cfg.cpu_repro_ablation = ablation;
    cfg.reproduction_backend = repro::ReproductionBackend::Gpu;
    rejects_invalid_argument([&] { repro::require_reproduction_mode_supported(cfg); },
        "CPU candidate ablation was silently ignored by GPU reproduction");
  }
  check(observed_distinct_candidates,
      "candidate ablation silently used ordinary CPU candidate selection");

  // Candidate preparation must respect a non-entry request, not regenerate the
  // grammar entry or weaken the imported-parent membership check.
  const auto grammar = repeated_hole_grammar();
  auto cfg = compiled_config(grammar, 6);
  cfg.cpu_repro_ablation = ablation;
  cfg.generation_request->nonterminal = nonterminal(*grammar, "Value");
  const auto population = generated_population(*grammar, 6, 55, &*cfg.generation_request);
  auto scored = score_owned(population);
  std::mt19937_64 rng(27);
  const auto result = repro::run_reproduction_backend(scored, cfg, rng);
  require_population_membership(*grammar, result.next_population, &*cfg.generation_request);
  first_constant_node(scored.back().genome).i0 = 999;
  scored.back().fitness = -1000000;
  rejects_invalid_argument([&] { (void)repro::run_reproduction_backend(scored, cfg, rng); },
      "candidate ablation accepted an invalid unselected parent");
}

void test_coupled_mutation_keeps_crossover_site() {
  const auto grammar = many_sites_grammar();
  const auto population = generated_population(*grammar, 6, 40);
  const auto scored = score_owned(population);
  auto cfg = compiled_config(grammar, 6);
  cfg.cpu_repro_ablation = repro::CpuReproAblation::GpuCoupledDonor;
  cfg.mutation_rate = 1;
  cfg.mutation_subtree_prob = 1;
  int checked_nonroot_sites = 0;
  int changed = 0;
  for (std::uint64_t seed : {11, 27, 88, 125}) {
    std::mt19937_64 backend_rng(seed), replay_rng(seed);
    const auto result = repro::run_reproduction_backend(scored, cfg, backend_rng);
    auto selected = tournament_selection_indices_without_replacement(
        scored, replay_rng, cfg.selection_pressure, cfg.population_size);
    std::shuffle(selected.begin(), selected.end(), replay_rng);
    std::uniform_int_distribution<std::uint64_t> operator_seed(0, 2000000000ULL);
    std::uniform_real_distribution<double> probability(0, 1);
    VariationContext context(grammar);
    for (std::size_t i = 0; i < selected.size(); i += 2) {
      variation_detail::SelectedCrossoverSites sites;
      (void)variation_detail::crossover_with_sites(scored[selected[i]].genome,
          scored[selected[i + 1]].genome, operator_seed(replay_rng), context, &sites);
      check(sites.has_value(), "fixture has no coupled crossover site");
      for (std::size_t side = 0; side < 2; ++side) {
        (void)probability(replay_rng);
        (void)operator_seed(replay_rng);
        const auto& original = scored[selected[i + side]].genome;
        const auto& child = result.next_population[i + side];
        const auto& site = side ? sites->second : sites->first;
        check(child.ast.nodes.size() == original.ast.nodes.size(),
              "fixed-shape coupled fixture changed structure");
        if (site.materialized_nodes == 1) ++checked_nonroot_sites;
        changed += child.meta.program_key != original.meta.program_key;
        for (std::size_t node = 0; node < original.ast.nodes.size(); ++node) {
          const bool in_site = std::any_of(site.occurrences.begin(), site.occurrences.end(),
              [&](const VariationSpan& span) { return node >= span.begin && node < span.end; });
          if (in_site || original.ast.nodes[node].kind != NodeKind::CONST) continue;
          const auto& before = original.ast.consts.at(original.ast.nodes[node].i0);
          const auto& after = child.ast.consts.at(child.ast.nodes[node].i0);
          check(before.tag == after.tag && before.i == after.i,
                "coupled donor changed a constant outside the original crossover site");
        }
      }
    }
    check(backend_rng == replay_rng, "coupled mutation changed outer RNG consumption");
  }
  check(checked_nonroot_sites >= 4 && changed > 0,
        "coupling regression did not exercise changing proper subtrees");

  // The constant branch operates on the crossed child using ordinary constant
  // mutation, instead of replacing that child with an original-parent donor.
  cfg.mutation_subtree_prob = 0;
  std::mt19937_64 coupled_rng(19), plain_rng(19);
  const auto coupled = repro::run_reproduction_backend(scored, cfg, coupled_rng);
  cfg.cpu_repro_ablation = repro::CpuReproAblation::None;
  const auto plain = repro::run_reproduction_backend(scored, cfg, plain_rng);
  check_same_population(coupled.next_population, plain.next_population,
      "coupled constant branch differs from ordinary post-crossover mutation");
}

void test_mixed_population_initialization_and_evolution() {
  const std::vector<EvalCase> cases{{{}, Value::from_int(0)}};
  const auto case_set = prepare_case_set(cases);
  auto cfg = gagp::test::mixed_population_config();
  const auto initialized = initialize_population(cfg, case_set);
  check(initialized.population.size() == 16 && !initialized.replayed,
        "mixed initialization did not generate the requested population");
  std::vector<int> types(8);
  const auto requests = population_requests(cfg);
  for (std::size_t i = 0; i < initialized.population.size(); ++i) {
    const auto& genome = initialized.population[i];
    const auto& request = requests[i % 8];
    require_membership(*cfg.compiled_grammar, genome, request);
    ++types.at(static_cast<std::size_t>(genome.derivation->request.type));
  }
  check(std::all_of(types.begin(), types.end(), [](int count) { return count == 2; }),
        "mixed initialization omitted an exact runtime type");
  auto imported = initialized.population;
  for (auto& genome : imported) {
    auto forged = std::make_shared<DerivationMetadata>(*genome.derivation);
    forged->request = *cfg.generation_request;
    genome.derivation = forged;
  }
  const auto replayed = initialize_population(cfg, case_set, &imported);
  check(replayed.replayed, "mixed replay was regenerated");
  check_same_population(replayed.population, initialized.population,
      "mixed replay changed program identities");
  for (std::size_t i = 0; i < replayed.population.size(); ++i)
    check(replayed.population[i].derivation->request.type ==
          initialized.population[i].derivation->request.type,
          "mixed replay trusted forged root metadata");

  // Expected output may match any admitted root, while all input contracts
  // remain exact. The first root is not privileged by fitness validation.
  std::swap(*cfg.generation_request, cfg.additional_generation_requests.front());
  validate_grammar_case_set(cfg, case_set);
  std::vector<EvalCase> mixed_cases;
  for (std::size_t i = 0; i < 8; ++i)
    mixed_cases.push_back({{}, initialized.population[i].ast.consts.front()});
  const auto mixed_case_set = prepare_case_set(mixed_cases);
  check(mixed_case_set.expected_return_type == RType::Invalid,
        "fixture did not contain mixed expected types");
  validate_grammar_case_set(cfg, mixed_case_set);
  auto missing_root = cfg;
  missing_root.additional_generation_requests.pop_back();
  rejects_invalid_argument([&] { validate_grammar_case_set(missing_root, mixed_case_set); },
      "mixed cases accepted an expected type absent from the root set");
  auto invalid_expected = mixed_case_set;
  invalid_expected.expected_values.front() = Value::invalid();
  rejects_invalid_argument([&] { validate_grammar_case_set(cfg, invalid_expected); },
      "mixed cases accepted an invalid expected Value tag");
  auto invalid = cfg;
  invalid.additional_generation_requests.push_back(*invalid.generation_request);
  rejects_invalid_argument([&] { (void)initialize_population(invalid, case_set); },
      "mixed initialization accepted duplicate root types");

  for (const auto ablation : {repro::CpuReproAblation::None,
       repro::CpuReproAblation::GpuSelection, repro::CpuReproAblation::GpuCandidates,
       repro::CpuReproAblation::GpuCoupledDonor}) {
    cfg = gagp::test::mixed_population_config();
    cfg.cpu_repro_ablation = ablation;
    // Regenerate payloads after each evolution run's live-root sweep.
    const auto population = initialize_population(cfg, case_set).population;
    const auto result = evolve_population(cases, cfg, &population);
    check(result.history_best.size() == 3 && result.final_population.size() == 16,
          "mixed evolution did not complete generations and final evaluation");
    check(result.history_mean_fitness.front() < 0 && result.history_mean_fitness[1] == 0,
          "mixed evolution partitioned tournament selection by root type");
    for (const auto& scored : result.final_population) {
      check(scored.genome.derivation->request.type == RType::Int && scored.fitness == 0,
            "global tournament did not replace weaker root types");
      require_membership(*cfg.compiled_grammar, scored.genome, *cfg.generation_request);
    }
  }
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

void test_nonterminal_variation_policy() {
  const auto original = repeated_hole_grammar();
  auto document = cli_detail::JsonParser(original->canonical_definition()).parse();
  auto& rules = document.object_v.at("nonterminals").array_v;
  for (auto& rule : rules) {
    if (rule.object_v.at("id").string_v != "Main") continue;
    auto& policy = rule.object_v["variation"];
    policy.kind = cli_detail::JsonValue::Kind::Bool;
    policy.bool_v = false;
  }
  const auto restricted = compile_shared(canonical_json(document));
  check(original->content_hash() != restricted->content_hash(),
        "variation policy was omitted from grammar identity");
  const auto genome = generate_derivation(*original, 7).genome;
  VariationContext before(original), after(restricted);
  const auto original_sites = before.analyze(genome)->sites;
  const auto restricted_sites = after.analyze(genome)->sites;
  check(!restricted_sites.empty() && original_sites.size() == restricted_sites.size() + 1,
        "disabling the root also disabled descendant variation or left the root eligible");
  for (const auto& site : restricted_sites)
    check(site.nonterminal != restricted->entry(), "disabled nonterminal remained a variation site");
  const auto regenerated = generate_derivation(*restricted, 7).genome;
  check(ast_cache_key(genome.ast) == ast_cache_key(regenerated.ast),
        "variation policy changed construction or membership");
  for (auto& rule : rules)
    if (rule.object_v.at("id").string_v == "Main")
      rule.object_v.at("variation").kind = cli_detail::JsonValue::Kind::Number;
  rejects_invalid_argument([&] { (void)compile_shared(canonical_json(document)); },
                           "non-Boolean variation policy was accepted");
}

void test_certification_compaction_boundaries() {
  const auto grammar = compile_shared(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Value","type":"Int"},
    "search_limits":{"max_nodes":5,"max_depth":4},"execution_limits":{"fuel":100},
    "nonterminals":[{"id":"Value","type":"Int","scope":[],"alternatives":[
      {"id":"one","weight":1,"expression":{"constant":{"type":"Int","values":["1"]}}}
    ]}]
  })");
  VariationContext context(grammar);
  const auto original = generate_derivation(*grammar, 7).genome;
  const auto unchanged = variation_detail::certify(original, context);
  check(ast_cache_key(unchanged.ast) == ast_cache_key(original.ast) &&
        unchanged.derivation->lowered_instructions > 0,
        "unchanged compaction lost the certified executable witness");
  auto padded = original;
  padded.ast.names.push_back("unused");
  padded.ast.consts.insert(padded.ast.consts.begin(), Value::from_bool(false));
  ++padded.ast.nodes[3].i0;
  const auto compacted = variation_detail::certify(padded, context);
  check(ast_cache_key(compacted.ast) == ast_cache_key(original.ast),
        "changed compaction failed to remap the referenced constant");
  check(context.analyze(compacted)->verified.return_type == RType::Int,
        "changed compaction retained a stale type certificate");
  padded.ast.consts.push_back(Value::invalid());
  rejects_invalid_argument([&] { (void)variation_detail::certify(padded, context); },
      "compaction hid an invalid unused constant before validation");
}

int main() {
  try {
    test_certification_compaction_boundaries();
    test_nonterminal_variation_policy();
    test_backend_replay_and_owned_ref_parity();
    test_manual_selection_crossover_mutation_replay();
    test_repeated_holes_and_local_children_remain_members();
    test_all_parents_are_validated_before_selection();
    test_prepared_ablation(repro::CpuReproAblation::GpuCandidates);
    test_prepared_ablation(repro::CpuReproAblation::GpuCoupledDonor);
    test_coupled_mutation_keeps_crossover_site();
    test_mixed_population_initialization_and_evolution();
    test_compiled_initialization_and_evolve_validation();
    test_nonentry_request_is_used_end_to_end();
    test_odd_population_counts_discarded_crossover_child();
    std::cout << "grammar reproduction: backend replay, validation, integration, and counters passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
