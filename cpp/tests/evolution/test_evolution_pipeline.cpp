#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/evolution/case_set.hpp"
#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/grammar/request.hpp"
#include "gagp/evolution/lifecycle.hpp"
#include "gagp/evolution/population_init.hpp"
#include "gagp/evolution/selection.hpp"
#include "gagp/evolution/timing.hpp"
#include "gagp/runtime/payload/payload.hpp"

namespace {

std::shared_ptr<const gagp::evo::grammar::CompiledGrammar> timing_grammar() {
  using namespace gagp::evo::grammar;
  return std::make_shared<const CompiledGrammar>(compile_grammar(parse_definition(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Expr","type":"Int"},
    "inputs":[{"name":"x","type":"Int"}],
    "search_limits":{"max_nodes":16,"max_depth":6},
    "execution_limits":{"fuel":20000},
    "nonterminals":[{"id":"Expr","type":"Int","scope":[],"alternatives":[
      {"id":"input","weight":1,"expression":{"input":"x"}},
      {"id":"constant","weight":1,"expression":{"constant":{"type":"Int","values":["0","1"]}}},
      {"id":"add","weight":1,"expression":{"signature":"add(Int,Int)->Int","args":[{"ref":"Expr"},{"ref":"Expr"}]}}
    ]}]
  })")));
}

}  // namespace

int main() {
  using gagp::Value;
  using gagp::evo::EvalCase;
  using gagp::evo::RType;

  const std::vector<EvalCase> cases = {
      EvalCase{{{"z", Value::from_int(2)}, {"a", Value::from_float(1.5)}},
               Value::from_int(3)},
      EvalCase{{{"z", Value::from_float(4.0)}, {"a", Value::from_float(2.5)}},
               Value::from_int(4)},
  };
  const auto case_set = gagp::evo::prepare_case_set(cases);
  assert((case_set.input_names == std::vector<std::string>{"a", "z"}));
  assert(case_set.input_specs.size() == 2);
  assert(case_set.input_specs[0].type == RType::Float);
  assert(case_set.input_specs[1].type == RType::Any);
  assert(case_set.bindings.size() == 2 && case_set.bindings[0].size() == 2);
  assert(case_set.bindings[0][0].idx == 0 && case_set.bindings[0][1].idx == 1);
  assert(case_set.expected_values.size() == 2);
  assert(case_set.expected_return_type == RType::Int);

  const auto mixed_expected = gagp::evo::prepare_case_set(
      {EvalCase{{}, Value::from_int(1)}, EvalCase{{}, Value::from_bool(true)}});
  assert(mixed_expected.expected_return_type == RType::Invalid);

  // Ranking compares fitness rounded to a 40-bit mantissa.
  assert(gagp::evo::canonicalize_fitness_for_ranking(1.0) == 1.0);
  assert(gagp::evo::canonicalize_fitness_for_ranking(1.0 + 0x1p-50) == 1.0);
  assert(gagp::evo::canonicalize_fitness_for_ranking(-3.5) == -3.5);

  gagp::evo::EvolutionConfig config;
  config.population_size = 3;
  config.compiled_grammar = timing_grammar();
  config.fuel = static_cast<int>(config.compiled_grammar->execution_limits().fuel);
  const auto entry = gagp::evo::grammar::entry_request(*config.compiled_grammar);
  std::vector<gagp::evo::ProgramGenome> replay_population;
  for (std::uint64_t seed = 0; seed < 3; ++seed) {
    replay_population.push_back(gagp::evo::grammar::generate_derivation(
        *config.compiled_grammar, seed, entry).genome);
  }
  const auto replay_case_set = gagp::evo::prepare_case_set(
      {EvalCase{{{"x", Value::from_int(0)}}, Value::from_int(0)}});
  // Without an initial population, initialization generates grammar members.
  const auto fresh = gagp::evo::initialize_population(config, replay_case_set);
  assert(!fresh.replayed && fresh.population.size() == 3);
  for (const auto& member : fresh.population)
    gagp::evo::grammar::require_membership(*config.compiled_grammar, member);
  const auto replay = gagp::evo::initialize_population(
      config, replay_case_set, &replay_population);
  assert(replay.replayed && replay.population.size() == replay_population.size());
  assert(replay.population[1].meta.program_key == replay_population[1].meta.program_key);

  config.population_size += 1;
  try {
    (void)gagp::evo::initialize_population(
        config, replay_case_set, &replay_population);
    assert(false && "expected replay size mismatch");
  } catch (const std::invalid_argument& error) {
    assert(std::string(error.what()) ==
           "initial_population size must match population_size");
  }

  gagp::evo::EvaluationTiming eval_a;
  eval_a.gpu_eval_pack_ms = 3.0;
  eval_a.gpu_eval_upload_ms = 5.0;
  gagp::evo::EvaluationTiming eval_total;
  gagp::evo::accumulate_timing(&eval_total, eval_a);
  gagp::evo::accumulate_timing(&eval_total, eval_a);
  assert(eval_total.gpu_eval_pack_upload_ms() == 16.0);
  assert(eval_total.gpu_eval_kernel_ms == 0.0);

  gagp::evo::ReproductionTiming repro_a;
  repro_a.setup_ms = 1.0;
  repro_a.decode_ms = 4.0;
  repro_a.variation.crossover_attempts = 1;
  repro_a.variation.mutation_attempts = 2;
  repro_a.variation.contract_rejections = 3;
  repro_a.variation.fallback_children = 7;
  repro_a.variation.unchanged_children = 8;
  repro_a.variation.changed_children = 9;
  gagp::evo::ReproductionTiming repro_total;
  gagp::evo::accumulate_timing(&repro_total, repro_a);
  gagp::evo::accumulate_timing(&repro_total, repro_a);
  assert(repro_total.setup_ms == 2.0);
  assert(repro_total.decode_ms == 8.0);
  assert(repro_total.kernel_ms == 0.0);
  assert(repro_total.variation.crossover_attempts == 2);
  assert(repro_total.variation.mutation_attempts == 4);
  assert(repro_total.variation.contract_rejections == 6);
  assert(repro_total.variation.fallback_children == 14);
  assert(repro_total.variation.unchanged_children == 16);
  assert(repro_total.variation.changed_children == 18);

  gagp::payload::clear();
  const Value retained = gagp::payload::make_string_value("retained");
  const Value discarded = gagp::payload::make_string_value("discarded");
  const gagp::evo::PayloadLifetimeManager lifetime(
      {EvalCase{{{"input", retained}}, Value::from_int(0)}});
  lifetime.retain({}, {});
  std::string payload_text;
  assert(gagp::payload::lookup_string(retained, &payload_text));
  assert(payload_text == "retained");
  assert(!gagp::payload::lookup_string(discarded, &payload_text));
  gagp::payload::clear();

  gagp::evo::EvolutionConfig timing_config;
  timing_config.population_size = 4;
  timing_config.generations = 2;
  timing_config.seed = 17;
  timing_config.skip_final_eval = true;
  timing_config.compiled_grammar = timing_grammar();
  timing_config.fuel = static_cast<int>(timing_config.compiled_grammar->execution_limits().fuel);
  const std::vector<EvalCase> timing_cases = {
      EvalCase{{{"x", Value::from_int(0)}}, Value::from_int(0)},
      EvalCase{{{"x", Value::from_int(1)}}, Value::from_int(1)},
  };
  const auto timed = gagp::evo::evolve_population(timing_cases, timing_config);
  assert(timed.timing.generations.size() == 2);
  assert(timed.reproduction_profile.rfind("resident-cpu-", 0) == 0);
  long long crossover_attempts = 0;
  for (const auto& generation : timed.timing.generations) {
    crossover_attempts += generation.reproduction.variation.crossover_attempts;
    assert(generation.evaluation.gpu_eval_call_ms == 0.0);
    assert(generation.evaluation.gpu_eval_kernel_ms == 0.0);
  }
  assert(timed.timing.evaluation_totals.gpu_eval_call_ms == 0.0);
  assert(timed.timing.reproduction_totals.variation.crossover_attempts == crossover_attempts);
  timing_config.skip_final_eval=false;timing_config.generations=5;timing_config.elite_count=1;
  timing_config.fitness_policy={gagp::FitnessMetric::Absolute,gagp::FitnessProximity::Discounted};
  const auto elitist=gagp::evo::evolve_population(timing_cases,timing_config);
  for(std::size_t i=1;i<elitist.history_best_fitness.size();++i)
    assert(elitist.history_best_fitness[i]>=elitist.history_best_fitness[i-1]);
  assert(elitist.best.fitness>=elitist.history_best_fitness.back());
  // Configuration errors are rejected before device initialization, even in
  // CPU-only builds.
  auto rejects=[&](const gagp::evo::EvolutionConfig& cfg,const char* message,
      const std::vector<gagp::evo::ProgramGenome>* initial=nullptr) {
    bool rejected=false;
    try{(void)gagp::evo::evolve_population(timing_cases,cfg,initial);}
    catch(const std::invalid_argument& error){rejected=std::string(error.what()).find(message)!=std::string::npos;}
    assert(rejected);
  };
  auto bad_site=timing_config;bad_site.gpu_phase_site_depth=16;
  rejects(bad_site,"invalid GPU phase site depth");
  // Fuel may lower the grammar's execution fuel but never raise it.
  auto wrong_fuel=timing_config;++wrong_fuel.fuel;
  rejects(wrong_fuel,"fuel");
  // An imported program outside the grammar is rejected before evaluation.
  std::vector<gagp::evo::ProgramGenome> invalid_population;
  for (std::uint64_t seed = 0; invalid_population.size() < 4; ++seed) {
    auto genome = gagp::evo::grammar::generate_derivation(*timing_config.compiled_grammar, 400 + seed, entry).genome;
    if (!genome.ast.consts.empty() || invalid_population.size() < 3) invalid_population.push_back(std::move(genome));
  }
  invalid_population.back().ast.consts.front() = Value::from_int(999);
  invalid_population.back().meta = gagp::evo::build_genome_meta(invalid_population.back().ast);
  invalid_population.back().derivation.reset();
  rejects(timing_config,"",&invalid_population);
  timing_config.elite_count=timing_config.population_size;
  bool bad_elites=false;
  try{(void)gagp::evo::evolve_population(timing_cases,timing_config);}catch(const std::invalid_argument&){bad_elites=true;}
  assert(bad_elites);
  return 0;
}
