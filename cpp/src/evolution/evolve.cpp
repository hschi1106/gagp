#include "gagp/core/host_threads.hpp"
#include "batch_workers.hpp"
#include "repro/owned_overlap.hpp"
#include "grammar/evaluation_identity.hpp"
#include "gagp/evolution/evolve.hpp"

#include <chrono>
#include <cstdlib>
#include <atomic>
#include <algorithm>
#include <future>
#include <thread>
#include <optional>
#include "../runtime/payload/staging.hpp"
#include <stdexcept>
#include <unordered_map>

#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/grammar/cache.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/lifecycle.hpp"
#include "gagp/evolution/population_init.hpp"
#include "gagp/evolution/repro/backend.hpp"
#include "gagp/evolution/repro/gpu.hpp"
#include "gagp/evolution/selection.hpp"
#include "gagp/runtime/cpu/fitness_cpu.hpp"
#ifdef GAGP_HAS_CUDA
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "repro/gpu/phase_population.hpp"
#include <numeric>
#endif

namespace gagp::evo {

namespace {

CaseSet evolution_case_set(const std::vector<EvalCase>& cases, const EvolutionConfig& cfg) {
  auto result = prepare_case_set(cases);
  validate_grammar_case_set(cfg, result);
  for (const auto& one : cases)
    if (one.inputs.size() != cfg.compiled_grammar->inputs().size())
      throw std::invalid_argument("every fitness case must supply every compiled grammar input");
  return result;
}

struct CompileCache {
  std::unordered_map<std::string, BytecodeProgram> by_program;
};

struct CompiledPopulation {
  std::vector<BytecodeProgram> programs;
  double compile_ms = 0.0;
};

struct PopulationEvaluation {
  std::vector<double> fitness;
  EvaluationTiming timing;
};

ReproductionTiming reproduction_timing_from_stats(
    const repro::ReproductionStats& stats) {
  ReproductionTiming timing;
  timing.variation = stats.variation;
  timing.selection_ms = stats.selection_ms;
  timing.crossover_ms = stats.crossover_ms;
  timing.mutation_ms = stats.mutation_ms;
  timing.prepare_inputs_ms = stats.prepare_inputs_ms;
  timing.setup_ms = stats.setup_ms;
  timing.preprocess_ms = stats.preprocess_ms;
  timing.gpu_donor_generated = stats.gpu_donor_generated;
  timing.gpu_donor_fallback = stats.gpu_donor_fallback;
  timing.gpu_donor_setup_ms = stats.gpu_donor_setup_ms;
  timing.gpu_donor_device_bytes = stats.gpu_donor_device_bytes;
  timing.pack_ms = stats.pack_ms;
  timing.upload_ms = stats.upload_ms;
  timing.kernel_ms = stats.kernel_ms;
  timing.copyback_ms = stats.copyback_ms;
  timing.decode_ms = stats.decode_ms;
  timing.teardown_ms = stats.teardown_ms;
  timing.selection_kernel_ms = stats.selection_kernel_ms;
  timing.variation_kernel_ms = stats.variation_kernel_ms;
  return timing;
}

CompiledPopulation compile_population(const std::vector<ProgramGenome>& population,
                                      const std::vector<std::string>& input_names,
                                      CompileCache* compile_cache, int fuel, bool parallel_allowed = false) {
  CompiledPopulation out;
  out.programs.reserve(population.size());
  CompileCache local_cache;
  CompileCache* cache = (compile_cache != nullptr) ? compile_cache : &local_cache;
  constexpr std::size_t batch_size = 128;
  const auto workers = parallel_allowed ? gagp::host_thread_limit() : 1u;
  const bool parallel = population.size() >= 32 && workers > 1 &&
      !payload::StagedPayloads::has_active_scope();
  struct PreparedCompile {
    grammar::detail::EvaluationIdentity identity;
    std::optional<BytecodeProgram> bytecode;
    std::unique_ptr<payload::StagedPayloads> reads;
    std::exception_ptr error;
    bool key_ready = false;
    std::chrono::steady_clock::time_point compile_begin{}, compile_end{};
  };
  const bool single_identity = std::getenv("GAGP_SINGLE_EVAL_IDENTITY") != nullptr;
  const auto identity_for = [&](const ProgramGenome& genome) {
    if (single_identity) return grammar::detail::evaluation_identity(
        genome, input_names, static_cast<std::uint32_t>(fuel));
    return grammar::detail::EvaluationIdentity{genome.derivation ? grammar::runtime_cache_identity(
        genome, input_names, static_cast<std::uint32_t>(fuel)) : genome.meta.program_key, nullptr};
  };
  const auto compile_one = [&](const ProgramGenome& genome,
                               const grammar::detail::EvaluationIdentity& identity) {
    const auto admitted = single_identity ? identity.executable : grammar::admitted_bytecode_for_eval(
        genome, input_names, static_cast<std::uint32_t>(fuel));
    if (admitted) return *admitted;
    return compile_for_eval(genome, input_names);
  };
  std::unique_ptr<detail::BatchWorkers> team;
  if (parallel) team = std::make_unique<detail::BatchWorkers>(workers);
  for (std::size_t begin = 0; begin < population.size(); begin += batch_size) {
    const auto count = std::min(batch_size, population.size() - begin);
    std::vector<PreparedCompile> prepared(parallel ? count : 0);
    bool valid = parallel;
    if (parallel) {
      std::atomic<std::size_t> next{0};
      team->run([&] {
          for (;;) {
            const auto i = next.fetch_add(1, std::memory_order_relaxed);
            if (i >= count) break;
            auto& row = prepared[i];
            row.reads = std::make_unique<payload::StagedPayloads>();
            try {
              payload::StagedPayloads::Scope scope(*row.reads);
              const auto& genome = population[begin + i];
              row.identity = identity_for(genome);
              row.key_ready = true;
              if (cache->by_program.find(row.identity.key) != cache->by_program.end()) continue;
              row.compile_begin = std::chrono::steady_clock::now();
              row.bytecode = compile_one(genome, row.identity);
              row.compile_end = std::chrono::steady_clock::now();
            } catch (...) { row.error = std::current_exception(); }
          }
      });
      std::vector<payload::StagedPayloads*> reads;
      for (const auto& row : prepared) reads.push_back(row.reads.get());
      valid = payload::StagedPayloads::commit_all(reads);
    }
    std::optional<std::chrono::steady_clock::time_point> compile_begin, compile_end;
    // Publish in source order. Duplicate keys keep the first compiled result;
    // errors and payload conflicts retain the ordinary sequential behavior.
    for (std::size_t i = 0; i < count; ++i) {
      const auto& genome = population[begin + i];
      if (valid && prepared[i].error && !prepared[i].key_ready)
        std::rethrow_exception(prepared[i].error);
      auto identity = valid ? std::move(prepared[i].identity) : identity_for(genome);
      auto& key = identity.key;
      const auto found = cache->by_program.find(key);
      if (found != cache->by_program.end()) {
        out.programs.push_back(found->second);
        continue;
      }
      BytecodeProgram bc;
      if (valid) {
        if (prepared[i].error) std::rethrow_exception(prepared[i].error);
        bc = std::move(*prepared[i].bytecode);
        compile_begin = compile_begin ? std::min(*compile_begin, prepared[i].compile_begin)
                                      : prepared[i].compile_begin;
        compile_end = compile_end ? std::max(*compile_end, prepared[i].compile_end)
                                  : prepared[i].compile_end;
      } else {
        const auto t0 = std::chrono::steady_clock::now();
        bc = compile_one(genome, identity);
        out.compile_ms += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
      }
      cache->by_program.emplace(std::move(key), bc);
      out.programs.push_back(std::move(bc));
    }
    if (compile_begin)
      out.compile_ms += std::chrono::duration<double, std::milli>(*compile_end - *compile_begin).count();
  }
  return out;
}

std::vector<ScoredGenomeRef> record_and_rank_evaluation(
    const std::vector<ProgramGenome>& population,
    PopulationEvaluation evaluation,
    EvolutionResult* result,
    GenerationTiming* generation_timing,
    double* fitness_sum_out,
    std::vector<double>* raw_fitness_out,
    bool sort_output) {
  for (double& value : evaluation.fitness) {
    value = canonicalize_fitness_for_ranking(value);
  }
  if (evaluation.fitness.size() != population.size()) {
    throw std::runtime_error("fitness size mismatch");
  }

  accumulate_timing(&result->timing.evaluation_totals, evaluation.timing);
  if (generation_timing != nullptr) generation_timing->evaluation = evaluation.timing;

  if (fitness_sum_out != nullptr) {
    long double sum = 0.0L;
    for (double one : evaluation.fitness) sum += static_cast<long double>(one);
    *fitness_sum_out = static_cast<double>(sum);
  }
  if (raw_fitness_out != nullptr) *raw_fitness_out = evaluation.fitness;
  return rank_population_refs(population, evaluation.fitness, sort_output);
}

std::vector<ScoredGenomeRef> score_population_cpu_refs(
    const std::vector<ProgramGenome>& population,
    const std::vector<std::string>& input_names,
    const std::vector<CaseBindings>& shared_cases,
    const std::vector<Value>& shared_answer,
    int fuel,
    double penalty,
    int reduction_lanes,
    CompileCache* compile_cache,
    EvolutionResult* result,
    GenerationTiming* generation_timing,
    double* fitness_sum_out,
    std::vector<double>* raw_fitness_out,
    bool sort_output) {
  const CompiledPopulation compiled = compile_population(population, input_names, compile_cache, fuel);
  PopulationEvaluation evaluation;
  evaluation.timing.cpu_compile_ms = compiled.compile_ms;
  evaluation.fitness = eval_fitness_cpu(
      compiled.programs, shared_cases, shared_answer, fuel, penalty, reduction_lanes);
  return record_and_rank_evaluation(
      population, std::move(evaluation), result, generation_timing,
      fitness_sum_out, raw_fitness_out, sort_output);
}

#ifdef GAGP_HAS_CUDA
std::vector<ScoredGenomeRef> score_population_gpu_refs(
    const std::vector<ProgramGenome>& population,
    const std::vector<std::string>& input_names,
    FitnessSessionGpu* session,
    int fuel,
    CompileCache* compile_cache,
    EvolutionResult* result,
    GenerationTiming* generation_timing,
    double* fitness_sum_out,
    std::vector<double>* raw_fitness_out,
    bool sort_output, const std::function<void()>& while_gpu_runs = {}) {
  const CompiledPopulation compiled = compile_population(population, input_names, compile_cache, fuel, true);
  FitnessEvalResult fit = session->eval_programs(compiled.programs, std::getenv("GAGP_GPU_DIAGNOSTICS") != nullptr, while_gpu_runs);
  if (!fit.ok) {
    throw std::runtime_error("gpu fitness evaluation failed: " + fit.err.message);
  }
  PopulationEvaluation evaluation;
  evaluation.fitness = std::move(fit.fitness);
  for (const auto& counts : fit.case_counts) {
    evaluation.timing.program_cases += counts[0];
    evaluation.timing.eval_errors += counts[1];
    evaluation.timing.eval_timeouts += counts[2];
    evaluation.timing.eval_fallbacks += counts[3];
    evaluation.timing.eval_unscored += counts[4];
  }
  evaluation.timing.gpu_compile_ms = compiled.compile_ms;
  evaluation.timing.gpu_eval_call_ms = fit.timing.total_ms;
  evaluation.timing.gpu_eval_pack_ms = fit.timing.pack_ms;
  evaluation.timing.gpu_eval_launch_prep_ms = fit.timing.launch_prep_ms;
  evaluation.timing.gpu_eval_upload_ms = fit.timing.upload_ms;
  evaluation.timing.gpu_eval_kernel_ms = fit.timing.kernel_ms;
  evaluation.timing.gpu_eval_copyback_ms = fit.timing.copyback_ms;
  evaluation.timing.gpu_eval_teardown_ms = fit.timing.teardown_ms;
  return record_and_rank_evaluation(
      population, std::move(evaluation), result, generation_timing,
      fitness_sum_out, raw_fitness_out, sort_output);
}
// Native evaluator and GPU operators, with a private persistent genotype owner.
// Cold import is charged once; all changed-child lowering/integration is charged
// before completing a generation. No host reproduction or hidden AST replay.
bool evolve_gpu_phase_population(const EvolutionConfig& cfg, const CaseSet& cases,
    const std::vector<ProgramGenome>& population, FitnessSessionGpu& session,
    EvolutionResult& result) {
  if (!std::getenv("GAGP_GPU_PHASE_POPULATION") || cfg.eval_engine != EvalEngine::GPU ||
      cfg.reproduction_backend != repro::ReproductionBackend::Gpu) return false;
  const auto begin = std::chrono::steady_clock::now();
  const auto elapsed = [](auto start) { return std::chrono::duration<double,std::milli>(
      std::chrono::steady_clock::now()-start).count(); };
  std::unique_ptr<repro::NativePhasePopulation> owner;
  try {
    const auto entry = grammar::entry_request(*cfg.compiled_grammar);
    const auto request = cfg.generation_request.value_or(entry);
    if (!cfg.additional_generation_requests.empty() || cfg.offspring_resource_budget ||
        cfg.initial_resource_budget || cfg.mutation_subtree_prob != 1.0 ||
        request.nonterminal != entry.nonterminal || request.type != entry.type ||
        !request.visible_environment.empty() || request.budget.max_nodes != entry.budget.max_nodes ||
        request.budget.max_depth != entry.budget.max_depth || request.stage != entry.stage)
      throw std::invalid_argument("native GPU phase profile requires the exact entry request and default resource/variation policy");
    owner = std::make_unique<repro::NativePhasePopulation>(cfg.compiled_grammar,population,cases.input_names);
  } catch (const std::invalid_argument& error) {
    result.reproduction_fallback_reason = error.what();
    result.timing.init_population_ms += elapsed(begin);
    return false;
  }
  result.timing.init_population_ms += elapsed(begin);
  result.reproduction_profile = "native-gpu-phase-v1";
  std::mt19937_64 rng(cfg.seed);
  std::vector<double> fitness;
  std::vector<std::size_t> ranking(population.size());
  const auto evaluate = [&](EvaluationTiming& timing) {
    auto fit = session.eval_programs(owner->programs(),std::getenv("GAGP_GPU_DIAGNOSTICS") != nullptr);
    if (!fit.ok) throw std::runtime_error("native GPU phase fitness failed: " + fit.err.message);
    fitness = std::move(fit.fitness);
    if (fitness.size()!=population.size()) throw std::runtime_error("phase fitness size mismatch");
    for (auto& f:fitness) f=canonicalize_fitness_for_ranking(f);
    for (const auto& c:fit.case_counts) {
      timing.program_cases+=c[0];timing.eval_errors+=c[1];timing.eval_timeouts+=c[2];
      timing.eval_fallbacks+=c[3];timing.eval_unscored+=c[4];
    }
    timing.gpu_eval_call_ms=fit.timing.total_ms;timing.gpu_eval_pack_ms=fit.timing.pack_ms;
    timing.gpu_eval_launch_prep_ms=fit.timing.launch_prep_ms;timing.gpu_eval_upload_ms=fit.timing.upload_ms;
    timing.gpu_eval_kernel_ms=fit.timing.kernel_ms;timing.gpu_eval_copyback_ms=fit.timing.copyback_ms;
    timing.gpu_eval_teardown_ms=fit.timing.teardown_ms;
    accumulate_timing(&result.timing.evaluation_totals,timing);
    std::iota(ranking.begin(),ranking.end(),0);
    std::stable_sort(ranking.begin(),ranking.end(),[&](auto a,auto b){return fitness[a]>fitness[b];});
  };
  for (int gen=0;gen<cfg.generations;++gen) {
    const auto start=std::chrono::steady_clock::now();GenerationTiming timing;
    evaluate(timing.evaluation);timing.eval_ms=elapsed(start);
    const auto best=ranking.front();
    result.history_best.push_back({owner->export_member(best),fitness[best]});
    result.history_best_fitness.push_back(fitness[best]);
    long double sum=0;for(double f:fitness)sum+=f;
    result.history_mean_fitness.push_back(static_cast<double>(sum/fitness.size()));
    const auto repro_start=std::chrono::steady_clock::now();
    timing.reproduction=reproduction_timing_from_stats(owner->reproduce(fitness,
        std::clamp(cfg.selection_pressure,1,cfg.population_size),cfg.mutation_rate,rng()));
    timing.repro_ms=elapsed(repro_start);
    result.genotype_device_bytes=std::max<std::uint64_t>(result.genotype_device_bytes,owner->device_bytes());
    accumulate_timing(&result.timing.reproduction_totals,timing.reproduction);
    timing.total_ms=elapsed(start);result.timing.generations.push_back(timing);
  }
  if(cfg.skip_final_eval)result.final_eval_skipped=true;
  else {
    const auto start=std::chrono::steady_clock::now();EvaluationTiming timing;evaluate(timing);
    result.best={owner->export_member(ranking.front()),fitness[ranking.front()]};
    if(cfg.retain_final_population)for(auto i:ranking)
      result.final_population.push_back({owner->export_member(i),fitness[i]});
    result.timing.final_eval_ms=elapsed(start);
  }
  return true;
}

#endif

}  // namespace

std::string eval_engine_name(EvalEngine engine) {
  if (engine == EvalEngine::GPU) return "gpu";
  return "cpu";
}

std::vector<ScoredGenome> evaluate_population(const std::vector<ProgramGenome>& population,
                                              const std::vector<EvalCase>& cases,
                                              const EvolutionConfig& cfg) {
  // Evaluation is also the package-independent execution boundary for
  // materialized ASTs. Compiled-grammar membership belongs to evolution and
  // reproduction, not to execution of an already verified program.
  const CaseSet case_set = prepare_case_set(cases);
  EvolutionResult result;
  return materialize_scored_population(score_population_cpu_refs(
      population, case_set.input_names, case_set.bindings, case_set.expected_values,
      cfg.fuel, cfg.penalty, cfg.gpu_blocksize, nullptr, &result, nullptr,
      nullptr, nullptr, true));
}

EvolutionResult evolve_population(const std::vector<EvalCase>& cases,
                                  const EvolutionConfig& cfg,
                                  const std::vector<ProgramGenome>* initial_population) {
  repro::require_reproduction_mode_supported(cfg);
  if (cases.empty()) {
    throw std::invalid_argument("cases must not be empty");
  }
  if (cfg.population_size <= 0) {
    throw std::invalid_argument("population_size must be > 0");
  }
  if (cfg.generations <= 0) {
    throw std::invalid_argument("generations must be > 0");
  }
  if (cfg.reproduction_backend != repro::ReproductionBackend::Cpu &&
      cfg.cpu_repro_ablation != repro::CpuReproAblation::None) {
    throw std::invalid_argument("cpu_repro_ablation requires cpu reproduction backend");
  }
  const auto all_t0 = std::chrono::steady_clock::now();
  std::mt19937_64 rng(cfg.seed);
  const CaseSet case_set = evolution_case_set(cases, cfg);
  EvolutionConfig reproduction_cfg = cfg;
  reproduction_cfg.verification_inputs = case_set.input_specs;
  const auto gpu_repro_resources = cfg.reproduction_backend == repro::ReproductionBackend::Gpu
      ? repro::make_gpu_repro_run_resources(reproduction_cfg) : nullptr;
  const PayloadLifetimeManager payload_lifetime(cases, gpu_repro_resources);

  EvolutionResult result;
  result.timing.generations.reserve(static_cast<std::size_t>(cfg.generations));

#ifdef GAGP_HAS_CUDA
  FitnessSessionGpu gpu_session;
#endif
  if (cfg.eval_engine == EvalEngine::GPU) {
#ifdef GAGP_HAS_CUDA
    const FitnessSessionInitResult init_result =
        gpu_session.init(case_set.bindings, case_set.expected_values, cfg.fuel,
                         cfg.gpu_blocksize, cfg.penalty);
    if (!init_result.ok) {
      throw std::runtime_error("gpu fitness session init failed: " + init_result.err.message);
    }
    result.timing.gpu_eval_init_ms = init_result.timing.total_ms;
#else
    throw std::runtime_error("gpu evaluation requested but CUDA is unavailable in this build");
#endif
  }

  const auto init_t0 = std::chrono::steady_clock::now();
  PopulationInitialization initialization =
      initialize_population(cfg, case_set, initial_population);
  std::vector<ProgramGenome> population = std::move(initialization.population);
  const auto init_t1 = std::chrono::steady_clock::now();

  result.timing.init_population_ms =
      std::chrono::duration<double, std::milli>(init_t1 - init_t0).count();

  payload_lifetime.retain(population, result.history_best);
#ifdef GAGP_HAS_CUDA
  if (evolve_gpu_phase_population(cfg,case_set,population,gpu_session,result)) {
    payload_lifetime.retain({},result.history_best,cfg.skip_final_eval ? nullptr : &result.best,
        cfg.retain_final_population ? &result.final_population : nullptr);
    result.timing.total_ms=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-all_t0).count();
    return result;
  }
#endif

  for (int gen = 0; gen < cfg.generations; ++gen) {
    GenerationTiming generation_timing;
    const auto gen_t0 = std::chrono::steady_clock::now();
    const auto eval_t0 = std::chrono::steady_clock::now();
    std::vector<ScoredGenomeRef> scored;
    double fitness_sum = 0.0;
    std::vector<double> raw_fitness;
    const bool overlap_gpu = gpu_reproduction_overlap_enabled(cfg);
    repro::OwnedGpuReproOverlap overlap;
    if (overlap_gpu) {
      overlap = repro::start_owned_gpu_repro_overlap(
          std::move(population), reproduction_cfg, rng(), gpu_repro_resources);
    }
    const auto& evaluated_population = overlap_gpu ? *overlap.population : population;
    if (cfg.eval_engine == EvalEngine::GPU) {
#ifdef GAGP_HAS_CUDA
      scored = score_population_gpu_refs(evaluated_population, case_set.input_names, &gpu_session, cfg.fuel, nullptr,
                                         &result, &generation_timing, &fitness_sum,
                                         gpu_repro_resources ? &raw_fitness : nullptr, true,
                                         overlap_gpu && gagp::host_thread_limit() == 1
                                             ? std::function<void()>([&] { overlap.completion.wait(); })
                                             : std::function<void()>{});
#else
      throw std::runtime_error("gpu evaluation requested but CUDA is unavailable in this build");
#endif
    } else {
      scored = score_population_cpu_refs(
          evaluated_population, case_set.input_names, case_set.bindings, case_set.expected_values,
          cfg.fuel, cfg.penalty, cfg.gpu_blocksize,
          nullptr, &result, &generation_timing, &fitness_sum,
          gpu_repro_resources ? &raw_fitness : nullptr, true);
    }
    const auto eval_t1 = std::chrono::steady_clock::now();
    const ScoredGenomeRef& best = scored.front();
    result.history_best.push_back(materialize_scored_genome(best));
    result.history_best_fitness.push_back(best.fitness);

    const double mean = fitness_sum / static_cast<double>(scored.size());
    result.history_mean_fitness.push_back(mean);

    const auto repro_t0 = std::chrono::steady_clock::now();
    repro::ReproductionResult reproduction;
    if (overlap_gpu) {
      reproduction = overlap.completion.get()(raw_fitness);
    } else if (gpu_repro_resources) {
      // Preparation assigns candidate streams and tournament indices in source
      // population order. Match the overlap path, which starts before fitness is
      // available; the ranked view above is only for reporting the best member.
      const auto source_order = rank_population_refs(population, raw_fitness, false);
      reproduction = repro::run_gpu_repro_backend(source_order, reproduction_cfg, rng, gpu_repro_resources);
    } else {
      reproduction = repro::run_reproduction_backend(scored, reproduction_cfg, rng);
    }
    const auto repro_t1 = std::chrono::steady_clock::now();

    population = std::move(reproduction.next_population);
    overlap.population.reset();
    payload_lifetime.retain(population, result.history_best);
    const auto gen_t1 = std::chrono::steady_clock::now();

    generation_timing.reproduction = reproduction_timing_from_stats(reproduction.stats);
    accumulate_timing(&result.timing.reproduction_totals,
                      generation_timing.reproduction);
    generation_timing.eval_ms =
        std::chrono::duration<double, std::milli>(eval_t1 - eval_t0).count();
    generation_timing.repro_ms =
        std::chrono::duration<double, std::milli>(repro_t1 - repro_t0).count();
    generation_timing.total_ms =
        std::chrono::duration<double, std::milli>(gen_t1 - gen_t0).count();
    result.timing.generations.push_back(generation_timing);
    (void)gen;
  }

  if (cfg.skip_final_eval) {
    result.final_eval_skipped = true;
    result.timing.final_eval_ms = 0.0;
    const std::vector<ProgramGenome> empty_population;
    payload_lifetime.retain(empty_population, result.history_best);
  } else {
    const auto final_eval_t0 = std::chrono::steady_clock::now();
    if (cfg.eval_engine == EvalEngine::GPU) {
#ifdef GAGP_HAS_CUDA
      const std::vector<ScoredGenomeRef> final_scored =
          score_population_gpu_refs(population, case_set.input_names, &gpu_session, cfg.fuel,
                                    nullptr, &result, nullptr, nullptr, nullptr, true);
      result.best = materialize_scored_genome(final_scored.front());
      result.final_population = cfg.retain_final_population
                                    ? materialize_scored_population(final_scored)
                                    : std::vector<ScoredGenome>{};
#else
      throw std::runtime_error("gpu evaluation requested but CUDA is unavailable in this build");
#endif
    } else {
      const std::vector<ScoredGenomeRef> final_scored = score_population_cpu_refs(
          population, case_set.input_names, case_set.bindings,
          case_set.expected_values, cfg.fuel, cfg.penalty, cfg.gpu_blocksize,
          nullptr, &result, nullptr, nullptr, nullptr, true);
      result.best = materialize_scored_genome(final_scored.front());
      result.final_population = cfg.retain_final_population
                                    ? materialize_scored_population(final_scored)
                                    : std::vector<ScoredGenome>{};
    }
    const auto final_eval_t1 = std::chrono::steady_clock::now();
    result.timing.final_eval_ms =
        std::chrono::duration<double, std::milli>(final_eval_t1 - final_eval_t0).count();
    const std::vector<ProgramGenome> empty_population;
    const std::vector<ProgramGenome>& retained_population = cfg.retain_final_population ? population : empty_population;
    payload_lifetime.retain(
        retained_population, result.history_best, &result.best,
        cfg.retain_final_population ? &result.final_population : nullptr);
  }
  result.timing.total_ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - all_t0).count();
  return result;
}

}  // namespace gagp::evo
