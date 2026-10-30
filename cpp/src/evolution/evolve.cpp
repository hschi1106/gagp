#include "gagp/core/host_threads.hpp"
#include "batch_workers.hpp"
#include "fitness_verification.hpp"
#include <cmath>
#include "repro/owned_overlap.hpp"
#include "grammar/evaluation_identity.hpp"
#include "gagp/evolution/evolve.hpp"

#include <chrono>
#include <iostream>
#include <iomanip>
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
#include "repro/gpu/phase_population_cpu.hpp"
#include <array>
#include <numeric>
#ifdef GAGP_HAS_CUDA
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "repro/gpu/phase_population.hpp"
#include "../runtime/gpu/cuda_allocation_scope.hpp"
#include <numeric>
#endif

namespace gagp::evo {

namespace {
enum class PhaseEngine { None, Gpu, Cpu };
bool legacy_phase_flag_enabled() {
  const char* value = std::getenv("GAGP_GPU_PHASE_POPULATION");
  return value && std::string(value) != "0";
}
// Explicit requests need matching engines. The legacy switch only ever
// selected the GPU owner (presence enables it, as before).
PhaseEngine phase_engine(const EvolutionConfig& cfg) {
  const bool gpu = cfg.eval_engine == EvalEngine::GPU && cfg.reproduction_backend == repro::ReproductionBackend::Gpu;
  const bool cpu = cfg.eval_engine == EvalEngine::CPU && cfg.reproduction_backend == repro::ReproductionBackend::Cpu;
  if (cfg.phase_population) {
    if (!*cfg.phase_population) return PhaseEngine::None;
    if (gpu) return PhaseEngine::Gpu;
    if (cpu) return PhaseEngine::Cpu;
    throw std::invalid_argument("phase population requires matching CPU/CPU or GPU/GPU evaluation/reproduction engines");
  }
  return gpu && std::getenv("GAGP_GPU_PHASE_POPULATION") ? PhaseEngine::Gpu : PhaseEngine::None;
}
// Phase-only policies require an enabled owner (explicit, or the legacy GPU switch).
bool phase_policy_owner_enabled(const EvolutionConfig& cfg) {
  if (cfg.phase_population) return phase_engine(cfg) != PhaseEngine::None;
  return phase_engine(cfg) == PhaseEngine::Gpu && legacy_phase_flag_enabled();
}
void trace_search_generation(const EvolutionResult& result) {
  if(!std::getenv("GAGP_SEARCH_TRACE")) return;
  const auto& t=result.timing.generations.back();
  std::cerr<<std::setprecision(12)<<"generation="<<result.timing.generations.size()-1
    <<" best="<<result.history_best_fitness.back()<<" mean="<<result.history_mean_fitness.back()
    <<" generation_ms="<<t.total_ms<<" eval_ms="<<t.eval_ms<<" repro_ms="<<t.repro_ms
    <<" elapsed_ms="<<t.elapsed_search_ms<<'\n';
}


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
  std::vector<unsigned> wrong;
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
  auto ranked=rank_population_refs(population,evaluation.fitness,sort_output,evaluation.wrong);
  if (raw_fitness_out != nullptr) {
    *raw_fitness_out = evaluation.fitness;
    if(!evaluation.wrong.empty()) {
      const auto ordered=sort_output?ranked:rank_population_refs(population,evaluation.fitness,true,evaluation.wrong);
      for(std::size_t rank=0;rank<ordered.size();++rank)
        raw_fitness_out->at(ordered[rank].genome-population.data())=double(ordered.size()-rank);
    }
  }
  return ranked;
}

std::vector<ScoredGenomeRef> score_population_cpu_refs(
    const std::vector<ProgramGenome>& population,
    const std::vector<std::string>& input_names,
    const std::vector<CaseBindings>& shared_cases,
    const std::vector<Value>& shared_answer,
    int fuel,
    double penalty,
    int reduction_lanes, FitnessPolicy policy,
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
      compiled.programs, shared_cases, shared_answer, fuel, penalty, reduction_lanes, policy, policy.metric==FitnessMetric::Legacy?nullptr:&evaluation.wrong);
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
  evaluation.wrong = std::move(fit.wrong);
  evaluation.timing.execution_profile=fit.execution_profile;
  evaluation.timing.physical_program_cases=fit.physical_program_cases;
  evaluation.timing.shape_result_programs=fit.shape_result_programs;
  evaluation.timing.shape_result_groups=fit.shape_result_groups;
  evaluation.timing.simple_phase_count=fit.simple_phase_count;
  evaluation.timing.physical_blocksize=fit.physical_blocksize;
  evaluation.timing.fitness_reused_programs=fit.fitness_reused_programs;
  evaluation.timing.fitness_reused_program_cases=fit.fitness_reused_program_cases;
  evaluation.timing.concat_cache_stats=fit.concat_cache_stats;
  evaluation.timing.payload_workspace_retained_bytes=fit.payload_workspace_retained_bytes;
  evaluation.timing.payload_workspace_pool_peak_bytes=fit.payload_workspace_pool_peak_bytes;
  evaluation.timing.payload_workspace_pool_allocations=fit.payload_workspace_pool_allocations;
  evaluation.timing.payload_replay_programs=fit.payload_replay_programs;
  evaluation.timing.payload_replay_cases=fit.payload_replay_cases;
  evaluation.timing.payload_replay_launches=fit.payload_replay_launches;

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
  evaluation.timing.gpu_eval_initial_kernel_wall_ms = fit.timing.initial_kernel_wall_ms;
  evaluation.timing.gpu_eval_initial_kernel_event_ms = fit.timing.initial_kernel_event_ms;
  evaluation.timing.gpu_eval_replay_kernel_event_ms = fit.timing.replay_kernel_event_ms;
  evaluation.timing.gpu_eval_copyback_ms = fit.timing.copyback_ms;
  evaluation.timing.gpu_eval_teardown_ms = fit.timing.teardown_ms;
  return record_and_rank_evaluation(
      population, std::move(evaluation), result, generation_timing,
      fitness_sum_out, raw_fitness_out, sort_output);
}
#endif

// Raw per-member results of one phase-owner evaluation. Canonicalization,
// ranking and every later search decision are shared by both engines.
struct PhaseOwnerFitness {
  std::vector<double> fitness;
  std::vector<unsigned> wrong;
  std::vector<std::array<std::uint64_t,5>> counts;  // cases, errors, timeouts, fallbacks, unscored
};
// Shared generational loop of the native phase population. `Owner` is the GPU
// owner or the CPU reference owner; `evaluate_owner` fills engine timing and
// returns raw results. Selection, verification, history and export are common.
template<class Owner,class Evaluate>
void run_phase_population(const EvolutionConfig& cfg,const CaseSet& cases,Owner& owner,Evaluate evaluate_owner,
    EvolutionResult& result,std::chrono::steady_clock::time_point search_start) {
  const auto elapsed = [](auto start) { return std::chrono::duration<double,std::milli>(
      std::chrono::steady_clock::now()-start).count(); };
  std::mt19937_64 rng(cfg.seed);
  std::vector<double> fitness, selection_fitness;
  std::vector<unsigned> wrong;
  std::vector<std::size_t> ranking(owner.population_size());
  const auto evaluate = [&](int generation, EvaluationTiming& timing) {
    auto fit = evaluate_owner(timing);
    std::vector<double> raw;
    if(cfg.cpu_fitness_verification.top_k) {
      const auto copy_start=std::chrono::steady_clock::now();
      raw=fit.fitness; // Keep raw scores for approximation diagnostics.
      timing.verification_ms+=elapsed(copy_start);
    }
    fitness = std::move(fit.fitness);
    wrong=std::move(fit.wrong);
    if (fitness.size()!=owner.population_size()) throw std::runtime_error("phase fitness size mismatch");
    for (auto& f:fitness) f=canonicalize_fitness_for_ranking(f);
    for (const auto& c:fit.counts) {
      timing.program_cases+=c[0];timing.eval_errors+=c[1];timing.eval_timeouts+=c[2];
      timing.eval_fallbacks+=c[3];timing.eval_unscored+=c[4];
    }
    std::iota(ranking.begin(),ranking.end(),0);
    const auto nodes=wrong.empty()?std::vector<unsigned>{}:owner.node_counts(cfg.gpu_phase_site_parsimony);
    const auto rank = [&] { std::stable_sort(ranking.begin(),ranking.end(),[&](auto a,auto b){
      if(fitness[a]!=fitness[b])return fitness[a]>fitness[b];
      if(!wrong.empty()) {
        if(wrong[a]!=wrong[b])return wrong[a]<wrong[b];
        if(nodes[a]!=nodes[b])return nodes[a]<nodes[b];
      }
      return a<b;
    }); };
    rank();
    const auto& verify = cfg.cpu_fitness_verification;
    if (verify.top_k && (generation == cfg.generations || generation % verify.interval == 0 ||
        (verify.near_zero_error >= 0 && fitness[ranking.front()] >= -verify.near_zero_error))) {
      const auto verification_start = std::chrono::steady_clock::now();
      const auto old_best = ranking.front();
      std::vector<std::size_t> indices(ranking.begin(), ranking.begin() + verify.top_k);
      std::vector<BytecodeProgram> programs;
      for (const auto i : indices)
        programs.push_back(compile_for_eval(owner.export_member(i), cases.input_names));
      std::vector<unsigned> reference_wrong;
      std::vector<CpuFitnessCaseCounts> counts;
      const auto reference = eval_fitness_cpu(programs, cases.bindings, cases.expected_values,
          cfg.fuel, cfg.penalty, cfg.gpu_blocksize, cfg.fitness_policy, &reference_wrong, &counts);
      detail::reconcile_reference_fitness(indices, reference, reference_wrong, fitness, wrong, timing, raw);
      timing.verification_programs = programs.size();
      for (const auto& c : counts) {
        timing.verification_cases += c.cases;
        timing.verification_errors += c.errors;
        timing.verification_timeouts += c.timeouts;
      }
      rank();
      timing.verification_best_changes = old_best != ranking.front();
      timing.verification_best_unverified = std::find(indices.begin(), indices.end(), ranking.front()) == indices.end();
      timing.verification_ms += elapsed(verification_start);
    }
    accumulate_timing(&result.timing.evaluation_totals,timing);
    if (std::binary_search(cfg.checkpoint_generations.begin(), cfg.checkpoint_generations.end(), generation)) {
      auto exported = owner.export_population();
      std::vector<ScoredGenome> snapshot;
      for (std::size_t i = 0; i < exported.size(); ++i)
        snapshot.push_back({std::move(exported[i]), fitness[i], wrong.empty() ? -1 : int(wrong[i])});
      cfg.evaluation_checkpoint(generation, snapshot);
    }
    selection_fitness=fitness;
    if(!wrong.empty())for(std::size_t rank=0;rank<ranking.size();++rank)selection_fitness[ranking[rank]]=double(ranking.size()-rank);
  };
  for (int gen=0;gen<cfg.generations;++gen) {
    const auto start=std::chrono::steady_clock::now();GenerationTiming timing;
    evaluate(gen,timing.evaluation);timing.eval_ms=elapsed(start);
    const auto best=ranking.front();
    result.history_best.push_back({owner.export_member(best),fitness[best],wrong.empty()?-1:int(wrong[best])});
    result.history_best_fitness.push_back(fitness[best]);
    long double sum=0;for(double f:fitness)sum+=f;
    result.history_mean_fitness.push_back(static_cast<double>(sum/fitness.size()));
    const auto repro_start=std::chrono::steady_clock::now();
    timing.reproduction=reproduction_timing_from_stats(owner.reproduce(selection_fitness,
        std::clamp(cfg.selection_pressure,1,cfg.population_size),cfg.mutation_rate,rng(),cfg.elite_count));
    timing.repro_ms=elapsed(repro_start);
    result.genotype_device_bytes=std::max<std::uint64_t>(result.genotype_device_bytes,owner.device_bytes());
    result.gpu_stage_storage_peak_bytes=owner.stage_storage_peak_bytes();
    accumulate_timing(&result.timing.reproduction_totals,timing.reproduction);
    const auto finish=std::chrono::steady_clock::now();
    timing.total_ms=std::chrono::duration<double,std::milli>(finish-start).count();
    timing.elapsed_search_ms=std::chrono::duration<double,std::milli>(finish-search_start).count();
    result.timing.generations.push_back(timing);
    trace_search_generation(result);
  }
  if(cfg.skip_final_eval)result.final_eval_skipped=true;
  else {
    const auto start=std::chrono::steady_clock::now();EvaluationTiming timing;evaluate(cfg.generations,timing);
    if(cfg.retain_final_population && std::getenv("GAGP_BATCH_PHASE_EXPORT")) {
      auto exported=owner.export_population();
      result.best={exported.at(ranking.front()),fitness[ranking.front()],wrong.empty()?-1:int(wrong[ranking.front()])};
      for(auto i:ranking)result.final_population.push_back({std::move(exported.at(i)),fitness[i],wrong.empty()?-1:int(wrong[i])});
    } else {
      result.best={owner.export_member(ranking.front()),fitness[ranking.front()],wrong.empty()?-1:int(wrong[ranking.front()])};
      if(cfg.retain_final_population)for(auto i:ranking)
        result.final_population.push_back({owner.export_member(i),fitness[i],wrong.empty()?-1:int(wrong[i])});
    }
    result.genotype_device_bytes=std::max<std::uint64_t>(result.genotype_device_bytes,owner.device_bytes());
    result.gpu_stage_storage_peak_bytes=owner.stage_storage_peak_bytes();
    result.timing.final_eval_ms=elapsed(start);
  }
}
// Rejects owner-construction policies that the native phase owners do not support.
void require_phase_entry_policy(const EvolutionConfig& cfg) {
  const auto entry = grammar::entry_request(*cfg.compiled_grammar);
  const auto request = cfg.generation_request.value_or(entry);
  if (!cfg.additional_generation_requests.empty() || cfg.mutation_subtree_prob != 1.0 ||
      request.nonterminal != entry.nonterminal || request.type != entry.type ||
      !request.visible_environment.empty() || request.budget.max_nodes != entry.budget.max_nodes ||
      request.budget.max_depth != entry.budget.max_depth || request.stage != entry.stage)
    throw std::invalid_argument("native GPU phase profile requires the exact entry request and default resource/variation policy");
  if(!cfg.gpu_projected_phase_profile &&
      (cfg.initial_resource_budget || cfg.offspring_resource_budget))
    throw std::invalid_argument("projected budgets use generic native GPU reproduction unless gpu_projected_phase_profile is requested");
}
PhaseVariationPolicy resolve_phase_variation(const EvolutionConfig& cfg) {
  if (!cfg.phase_variation) return phase_variation_policy_from_environment();
  validate_phase_variation_policy(*cfg.phase_variation);
  require_environment_agrees(*cfg.phase_variation);
  return *cfg.phase_variation;
}
struct CpuPhaseOwner {
  repro::HostPhasePopulation& owner;
  std::size_t population;
  std::size_t population_size() const { return population; }
  std::vector<unsigned> node_counts(bool sites) const { return owner.node_counts(sites); }
  ProgramGenome export_member(std::size_t i) const { return owner.export_member(i); }
  std::vector<ProgramGenome> export_population() const { return owner.export_population(); }
  repro::ReproductionStats reproduce(const std::vector<double>& f,int t,double m,std::uint64_t s,int e) { return owner.reproduce(f,t,m,s,e); }
  std::uint64_t device_bytes() const { return 0; }
  std::uint64_t stage_storage_peak_bytes() const { return 0; }
};
#ifdef GAGP_HAS_CUDA
// Adapter giving the GPU owner the uniform owner interface.
struct GpuPhaseOwner {
  repro::NativePhasePopulation& owner;
  std::size_t population;
  std::size_t population_size() const { return population; }
  std::vector<unsigned> node_counts(bool sites) const { return owner.node_counts(sites); }
  ProgramGenome export_member(std::size_t i) const { return owner.export_member(i); }
  std::vector<ProgramGenome> export_population() const { return owner.export_population(); }
  repro::ReproductionStats reproduce(const std::vector<double>& f,int t,double m,std::uint64_t s,int e) { return owner.reproduce(f,t,m,s,e); }
  std::uint64_t device_bytes() const { return owner.device_bytes(); }
  std::uint64_t stage_storage_peak_bytes() const { return owner.stage_storage_peak_bytes(); }
};
// Native evaluator and GPU operators, with a private persistent genotype owner.
// Cold import is charged once; all changed-child lowering/integration is charged
// before completing a generation. No host reproduction or hidden AST replay.
bool evolve_gpu_phase_population(const EvolutionConfig& cfg, const CaseSet& cases,
    const std::vector<ProgramGenome>& population, FitnessSessionGpu& session,
    EvolutionResult& result, std::chrono::steady_clock::time_point search_start) {
  if (phase_engine(cfg) != PhaseEngine::Gpu) return false;
  const auto begin = std::chrono::steady_clock::now();
  const auto elapsed = [](auto start) { return std::chrono::duration<double,std::milli>(
      std::chrono::steady_clock::now()-start).count(); };
  gpu_detail::CudaAllocationScope allocation_scope;
  std::unique_ptr<repro::NativePhasePopulation> owner;
  const auto policy = resolve_phase_variation(cfg);
  try {
    require_phase_entry_policy(cfg);
    owner = std::make_unique<repro::NativePhasePopulation>(cfg.compiled_grammar,population,cases.input_names,cfg.offspring_resource_budget,cfg.gpu_phase_site_depth,policy);
  } catch (const std::invalid_argument& error) {
    if(cfg.phase_population || cfg.gpu_projected_phase_profile || cfg.cpu_fitness_verification.top_k || cfg.gpu_phase_site_depth || cfg.gpu_phase_site_parsimony)throw; // Explicit profiles fail closed
    result.reproduction_fallback_reason = error.what();
    result.timing.init_population_ms += elapsed(begin);
    owner.reset();allocation_scope.finish();
    return false;
  }
  result.timing.init_population_ms += elapsed(begin);
  result.reproduction_profile = owner->profile_name();
  result.gpu_stage_storage_reserved_bytes=owner->stage_storage_reserved_bytes();
  if(allocation_scope.used())result.reproduction_profile+="-cuda-pool";
  if(cfg.gpu_phase_site_parsimony)result.reproduction_profile+="-site-parsimony";
  const bool diagnostics=std::getenv("GAGP_GPU_DIAGNOSTICS") != nullptr;
  const auto evaluate_owner=[&](EvaluationTiming& timing) {
    auto fit = owner->evaluate(session,diagnostics);
    if (!fit.ok) throw std::runtime_error("native GPU phase fitness failed: " + fit.err.message);
    timing.execution_profile=fit.execution_profile;
    timing.physical_program_cases=fit.physical_program_cases;
    timing.shape_result_programs=fit.shape_result_programs;
    timing.shape_result_groups=fit.shape_result_groups;
    timing.simple_phase_count=fit.simple_phase_count;
    timing.physical_blocksize=fit.physical_blocksize;
    timing.fitness_reused_programs=fit.fitness_reused_programs;
    timing.fitness_reused_program_cases=fit.fitness_reused_program_cases;
    timing.concat_cache_stats=fit.concat_cache_stats;
    timing.payload_workspace_retained_bytes=fit.payload_workspace_retained_bytes;
    timing.payload_workspace_pool_peak_bytes=fit.payload_workspace_pool_peak_bytes;
    timing.payload_workspace_pool_allocations=fit.payload_workspace_pool_allocations;
    timing.payload_replay_programs=fit.payload_replay_programs;
    timing.payload_replay_cases=fit.payload_replay_cases;
    timing.payload_replay_launches=fit.payload_replay_launches;
    timing.gpu_eval_call_ms=fit.timing.total_ms;timing.gpu_eval_pack_ms=fit.timing.pack_ms;
    timing.gpu_eval_launch_prep_ms=fit.timing.launch_prep_ms;timing.gpu_eval_upload_ms=fit.timing.upload_ms;
    timing.gpu_eval_initial_kernel_wall_ms=fit.timing.initial_kernel_wall_ms;
    timing.gpu_eval_initial_kernel_event_ms=fit.timing.initial_kernel_event_ms;
    timing.gpu_eval_replay_kernel_event_ms=fit.timing.replay_kernel_event_ms;
    timing.gpu_eval_kernel_ms=fit.timing.kernel_ms;timing.gpu_eval_copyback_ms=fit.timing.copyback_ms;
    timing.gpu_eval_teardown_ms=fit.timing.teardown_ms;
    PhaseOwnerFitness out;out.fitness=std::move(fit.fitness);out.wrong=std::move(fit.wrong);
    for(const auto& c:fit.case_counts)out.counts.push_back({std::uint64_t(c[0]),std::uint64_t(c[1]),std::uint64_t(c[2]),std::uint64_t(c[3]),std::uint64_t(c[4])});
    return out;
  };
  GpuPhaseOwner adapter{*owner,population.size()};
  run_phase_population(cfg,cases,adapter,evaluate_owner,result,search_start);
  owner.reset();allocation_scope.finish();
  return true;
}
#endif
// CPU reference engine: same import, operators, draws and lowering as the GPU
// owner; programs are evaluated by the CPU evaluator on the host only.
void evolve_cpu_phase_population(const EvolutionConfig& cfg, const CaseSet& cases,
    const std::vector<ProgramGenome>& population, EvolutionResult& result,
    std::chrono::steady_clock::time_point search_start) {
  const auto begin = std::chrono::steady_clock::now();
  require_phase_entry_policy(cfg);
  repro::HostPhasePopulation owner(cfg.compiled_grammar,population,cases.input_names,cfg.offspring_resource_budget,
      cfg.gpu_phase_site_depth,resolve_phase_variation(cfg));
  result.timing.init_population_ms += std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
  result.reproduction_profile = owner.profile_name();
  if(cfg.gpu_phase_site_parsimony)result.reproduction_profile+="-site-parsimony";
  const auto evaluate_owner=[&](EvaluationTiming& timing) {
    auto fit=owner.evaluate(cases.bindings,cases.expected_values,cfg.fuel,cfg.penalty,cfg.gpu_blocksize,cfg.fitness_policy);
    timing.execution_profile="cpu-phase-reference";
    timing.fitness_reused_programs=fit.reused_programs;
    timing.fitness_reused_program_cases=fit.reused_programs*cases.bindings.size();
    timing.physical_program_cases=fit.evaluated_programs*cases.bindings.size();
    PhaseOwnerFitness out;out.fitness=std::move(fit.fitness);out.wrong=std::move(fit.wrong);
    for(const auto& c:fit.case_counts)out.counts.push_back({c.cases,c.errors,c.timeouts,0,0});
    return out;
  };
  CpuPhaseOwner adapter{owner,population.size()};
  run_phase_population(cfg,cases,adapter,evaluate_owner,result,search_start);
}


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
      cfg.fuel, cfg.penalty, cfg.gpu_blocksize, cfg.fitness_policy, nullptr, &result, nullptr,
      nullptr, nullptr, true));
}

EvolutionResult evolve_population(const std::vector<EvalCase>& cases,
                                  const EvolutionConfig& cfg,
                                  const std::vector<ProgramGenome>* initial_population) {
  if(cfg.gpu_phase_site_depth<0 || cfg.gpu_phase_site_depth>15 ||
      (cfg.gpu_phase_site_depth && (cfg.initial_resource_budget || cfg.offspring_resource_budget || cfg.gpu_projected_phase_profile)))
    throw std::invalid_argument("invalid GPU phase site depth or conflicting projected budget");
  if(cfg.gpu_phase_site_depth || cfg.gpu_phase_site_parsimony) {
    if(!phase_policy_owner_enabled(cfg))
      throw std::invalid_argument("GPU phase site policy requires resident GPU evaluation/reproduction or the explicit CPU phase reference");
    if(cfg.gpu_phase_site_parsimony && cfg.fitness_policy.metric==FitnessMetric::Legacy)
      throw std::invalid_argument("GPU phase site parsimony requires a task metric with wrong counts");
  }
  const auto& verify = cfg.cpu_fitness_verification;
  if (verify.top_k < 0 || verify.top_k > cfg.population_size || verify.interval < 1 ||
      !std::isfinite(verify.near_zero_error) || verify.near_zero_error < -1)
    throw std::invalid_argument("invalid CPU fitness verification budget/interval/threshold");
  if (verify.top_k) {
    if (!phase_policy_owner_enabled(cfg))
      throw std::invalid_argument("CPU fitness verification requires resident GPU evaluation/reproduction or the explicit CPU phase reference");
    if (cfg.fitness_policy.metric == FitnessMetric::Legacy)
      throw std::invalid_argument("CPU fitness verification requires a task metric with wrong counts");
  }
  if (!std::is_sorted(cfg.checkpoint_generations.begin(), cfg.checkpoint_generations.end()) ||
      std::adjacent_find(cfg.checkpoint_generations.begin(), cfg.checkpoint_generations.end()) != cfg.checkpoint_generations.end() ||
      (!cfg.checkpoint_generations.empty() && (!cfg.evaluation_checkpoint || cfg.checkpoint_generations.front() < 0 ||
        cfg.checkpoint_generations.back() > cfg.generations ||
        (cfg.skip_final_eval && cfg.checkpoint_generations.back() == cfg.generations))))
    throw std::invalid_argument("invalid evaluation checkpoints");
  if(cfg.gpu_projected_phase_profile) {
    if(!phase_policy_owner_enabled(cfg))
      throw std::invalid_argument("gpu_projected_phase_profile requires GPU evaluation/reproduction and GAGP_GPU_PHASE_POPULATION enabled, or an explicit phase population");
    if(!cfg.compiled_grammar || !cfg.initial_resource_budget || !cfg.offspring_resource_budget ||
        cfg.initial_resource_budget->max_nodes!=cfg.offspring_resource_budget->max_nodes ||
        cfg.initial_resource_budget->max_depth!=cfg.offspring_resource_budget->max_depth)
      throw std::invalid_argument("gpu_projected_phase_profile requires a compiled grammar and matching initial/offspring budgets");
    if(cfg.offspring_resource_budget->max_nodes!=cfg.compiled_grammar->search_limits().max_nodes ||
        cfg.offspring_resource_budget->max_depth==0 || cfg.offspring_resource_budget->max_depth>15)
      throw std::invalid_argument("gpu_projected_phase_profile requires the grammar node cap and projected depth in [1,15]");
  }
  if (cfg.phase_variation && phase_engine(cfg) == PhaseEngine::None)
    throw std::invalid_argument("phase variation policy requires the native phase population");
  repro::require_reproduction_mode_supported(cfg);
  if (cases.empty()) {
    throw std::invalid_argument("cases must not be empty");
  }
  if (cfg.elite_count < 0 || cfg.elite_count >= cfg.population_size)
    throw std::invalid_argument("elite_count must be >= 0 and < population_size");
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
  // The phase owner has independent whole-program and per-phase capacities.
  // Keep ordinary generic preflight early; defer it when phase admission is requested.
  const bool phase_requested=phase_engine(cfg)!=PhaseEngine::None;
  auto gpu_repro_resources = cfg.reproduction_backend==repro::ReproductionBackend::Gpu && !phase_requested
      ? repro::make_gpu_repro_run_resources(reproduction_cfg) : nullptr;
  const PayloadLifetimeManager initial_payload_lifetime(cases,gpu_repro_resources);

  EvolutionResult result;
  result.timing.generations.reserve(static_cast<std::size_t>(cfg.generations));

#ifdef GAGP_HAS_CUDA
  FitnessSessionGpu gpu_session;
#endif
  if (cfg.eval_engine == EvalEngine::GPU) {
#ifdef GAGP_HAS_CUDA
    const FitnessSessionInitResult init_result =
        gpu_session.init(case_set.bindings, case_set.expected_values, cfg.fuel,
                         cfg.gpu_blocksize, cfg.penalty, cfg.fitness_policy);
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

  initial_payload_lifetime.retain(population, result.history_best);
#ifdef GAGP_HAS_CUDA
  if (evolve_gpu_phase_population(cfg,case_set,population,gpu_session,result,all_t0)) {
    initial_payload_lifetime.retain({},result.history_best,cfg.skip_final_eval ? nullptr : &result.best,
        cfg.retain_final_population ? &result.final_population : nullptr);
    result.timing.total_ms=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-all_t0).count();
    return result;
  }
#endif
  if (phase_engine(cfg) == PhaseEngine::Cpu) {
    evolve_cpu_phase_population(cfg, case_set, population, result, all_t0);
    initial_payload_lifetime.retain({},result.history_best,cfg.skip_final_eval ? nullptr : &result.best,
        cfg.retain_final_population ? &result.final_population : nullptr);
    result.timing.total_ms=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-all_t0).count();
    return result;
  }

  const auto checkpoint = [&](int generation, const std::vector<ProgramGenome>& members,
                              const std::vector<ScoredGenomeRef>& scored) {
    if (!std::binary_search(cfg.checkpoint_generations.begin(), cfg.checkpoint_generations.end(), generation)) return;
    std::vector<ScoredGenome> snapshot(members.size());
    for (const auto& one : scored)
      snapshot.at(one.genome - members.data()) = materialize_scored_genome(one);
    cfg.evaluation_checkpoint(generation, snapshot);
  };
  if(cfg.reproduction_backend==repro::ReproductionBackend::Gpu && !gpu_repro_resources)
    gpu_repro_resources=repro::make_gpu_repro_run_resources(reproduction_cfg);
  const PayloadLifetimeManager payload_lifetime(cases,gpu_repro_resources);
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
          cfg.fuel, cfg.penalty, cfg.gpu_blocksize, cfg.fitness_policy,
          nullptr, &result, &generation_timing, &fitness_sum,
          gpu_repro_resources ? &raw_fitness : nullptr, true);
    }
    checkpoint(gen, evaluated_population, scored);
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

    for (int i = 0; i < cfg.elite_count; ++i)
      reproduction.next_population.at(i) = *scored.at(i).genome;
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
    generation_timing.elapsed_search_ms=std::chrono::duration<double,std::milli>(gen_t1-all_t0).count();
    generation_timing.total_ms =
        std::chrono::duration<double, std::milli>(gen_t1 - gen_t0).count();
    result.timing.generations.push_back(generation_timing);
    trace_search_generation(result);
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
      checkpoint(cfg.generations, population, final_scored);
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
          case_set.expected_values, cfg.fuel, cfg.penalty, cfg.gpu_blocksize, cfg.fitness_policy,
          nullptr, &result, nullptr, nullptr, nullptr, true);
      checkpoint(cfg.generations, population, final_scored);
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
