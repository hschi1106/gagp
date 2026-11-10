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
#include <fstream>
#include <queue>
#include <sstream>
#include <tuple>

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
#include <numeric>
#endif

namespace gagp::evo {

namespace {
enum class PhaseEngine { None, Gpu, Cpu };
// Phase population is requested by the search configuration and needs
// matching evaluation and reproduction engines.
PhaseEngine phase_engine(const EvolutionConfig& cfg) {
  if (!cfg.phase_population.value_or(false)) return PhaseEngine::None;
  const bool gpu = cfg.eval_engine == EvalEngine::GPU && cfg.reproduction_backend == repro::ReproductionBackend::Gpu;
  const bool cpu = cfg.eval_engine == EvalEngine::CPU && cfg.reproduction_backend == repro::ReproductionBackend::Cpu;
  if (gpu) return PhaseEngine::Gpu;
  if (cpu) return PhaseEngine::Cpu;
  throw std::invalid_argument("phase population requires matching CPU/CPU or GPU/GPU evaluation/reproduction engines");
}
// Phase-only policies require an enabled owner.
bool phase_policy_owner_enabled(const EvolutionConfig& cfg) { return phase_engine(cfg) != PhaseEngine::None; }
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
                                      CompileCache* compile_cache, int fuel) {
  CompiledPopulation out;
  out.programs.reserve(population.size());
  CompileCache local_cache;
  CompileCache* cache = (compile_cache != nullptr) ? compile_cache : &local_cache;
  // Publish in source order. Duplicate keys keep the first compiled result.
  for (const auto& genome : population) {
    auto identity = grammar::detail::evaluation_identity(genome, input_names, static_cast<std::uint32_t>(fuel));
    auto& key = identity.key;
    const auto found = cache->by_program.find(key);
    if (found != cache->by_program.end()) {
      out.programs.push_back(found->second);
      continue;
    }
    const auto t0 = std::chrono::steady_clock::now();
    BytecodeProgram bc = identity.executable ? *identity.executable : compile_for_eval(genome, input_names);
    out.compile_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    cache->by_program.emplace(std::move(key), bc);
    out.programs.push_back(std::move(bc));
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
  const CompiledPopulation compiled = compile_population(population, input_names, compile_cache, fuel);
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
    if(cfg.retain_final_population) {
      auto exported=owner.export_population();
      result.best={exported.at(ranking.front()),fitness[ranking.front()],wrong.empty()?-1:int(wrong[ranking.front()])};
      for(auto i:ranking)result.final_population.push_back({std::move(exported.at(i)),fitness[i],wrong.empty()?-1:int(wrong[i])});
    } else {
      result.best={owner.export_member(ranking.front()),fitness[ranking.front()],wrong.empty()?-1:int(wrong[ranking.front()])};
    }
    result.genotype_device_bytes=std::max<std::uint64_t>(result.genotype_device_bytes,owner.device_bytes());
    result.gpu_stage_storage_peak_bytes=owner.stage_storage_peak_bytes();
    result.timing.final_eval_ms=elapsed(start);
  }
}
// Synchronous batch steady state, shared by both phase owners
// (schedule.mode steady_state, spec/search_config.md). After the initial
// evaluation each round draws one seed, creates `batch` children from the
// whole population (tournaments on the generational ranking values), scores
// those without an identical current member, then places children in child
// order: each replaces the loser of an inverse tournament of
// `replacement_size` uniformly drawn members (redrawn when it is the best).
// Every population-many placed children record one generation-equivalent.
template<class Owner,class Evaluate,class EvaluateBatch>
void run_phase_population_steady(const EvolutionConfig& cfg,const CaseSet& cases,Owner& owner,Evaluate evaluate_owner,
    EvaluateBatch evaluate_batch,const PhaseSchedulePolicy& schedule,EvolutionResult& result,
    std::chrono::steady_clock::time_point search_start) {
  using Clock=std::chrono::steady_clock;
  const auto elapsed=[](auto start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();};
  const int P=int(owner.population_size()),B=schedule.batch;
  const int pressure=std::clamp(cfg.selection_pressure,1,P);
  std::mt19937_64 rng(cfg.seed);
  GenerationTiming epoch;auto epoch_start=Clock::now();
  const auto eval_start=Clock::now();
  auto fit=evaluate_owner(epoch.evaluation);
  if(fit.fitness.size()!=std::size_t(P))throw std::runtime_error("phase fitness size mismatch");
  std::vector<double> fitness=std::move(fit.fitness);
  for(auto& f:fitness)f=canonicalize_fitness_for_ranking(f);
  std::vector<unsigned> wrong=std::move(fit.wrong);const bool has_wrong=!wrong.empty();
  for(const auto& c:fit.counts) {
    epoch.evaluation.program_cases+=c[0];epoch.evaluation.eval_errors+=c[1];epoch.evaluation.eval_timeouts+=c[2];
    epoch.evaluation.eval_fallbacks+=c[3];epoch.evaluation.eval_unscored+=c[4];
  }
  std::vector<unsigned> nodes=has_wrong?owner.node_counts(cfg.gpu_phase_site_parsimony):std::vector<unsigned>(P,0);
  epoch.eval_ms+=elapsed(eval_start);
  owner.begin_steady(B);
  const auto better=[&](int a,int b){
    if(fitness[a]!=fitness[b])return fitness[a]>fitness[b];
    if(has_wrong){if(wrong[a]!=wrong[b])return wrong[a]<wrong[b];if(nodes[a]!=nodes[b])return nodes[a]<nodes[b];}
    return a<b;
  };
  std::vector<int> order(P);std::iota(order.begin(),order.end(),0);std::sort(order.begin(),order.end(),better);
  std::vector<double> selection=fitness;
  const auto ranks=[&]{if(has_wrong)for(int r=0;r<P;++r)selection[order[r]]=double(P-r);};
  ranks();
  // `better` is a strict total order, so within a round only the best member
  // is tracked (a victim is never the best) and `order` is re-sorted once.
  int best=order.front();
  const auto scored=[&](int member){return ScoredGenome{owner.export_member(member),fitness[member],has_wrong?int(wrong[member]):-1};};
  const auto record=[&]{
    const int best=order.front();
    result.history_best.push_back(scored(best));result.history_best_fitness.push_back(fitness[best]);
    long double sum=0;for(double f:fitness)sum+=f;
    result.history_mean_fitness.push_back(static_cast<double>(sum/P));
    epoch.total_ms=elapsed(epoch_start);epoch.elapsed_search_ms=elapsed(search_start);
    accumulate_timing(&result.timing.evaluation_totals,epoch.evaluation);
    accumulate_timing(&result.timing.reproduction_totals,epoch.reproduction);
    result.timing.generations.push_back(epoch);trace_search_generation(result);
    epoch=GenerationTiming{};epoch_start=Clock::now();
  };
  const auto victim=[&]{
    for(;;) {
      int worst=int(rng()%std::uint64_t(P));
      for(int i=1;i<schedule.replacement_size;++i){const int c=int(rng()%std::uint64_t(P));if(better(worst,c))worst=c;}
      if(worst!=best)return worst;
    }
  };
  const std::uint64_t rounds=std::uint64_t(P)*std::uint64_t(cfg.generations)/std::uint64_t(B);
  const std::uint64_t per_epoch=std::uint64_t(P/B);
  std::vector<int> placed(P,-1);std::vector<std::array<int,2>> placements;
  for(std::uint64_t round=0;round<rounds;++round) {
    if(round%per_epoch==0)record();
    const auto repro_start=Clock::now();repro::ReproductionStats stats;
    const auto reuse=owner.make_batch(selection,pressure,cfg.mutation_rate,rng(),&stats);
    epoch.repro_ms+=elapsed(repro_start);
    const auto batch_start=Clock::now();
    auto scores=evaluate_batch(reuse,epoch.evaluation);
    const auto child_nodes=has_wrong?owner.batch_nodes(cfg.gpu_phase_site_parsimony):std::vector<unsigned>(B,0);
    epoch.eval_ms+=elapsed(batch_start);
    const auto place_start=Clock::now();
    std::fill(placed.begin(),placed.end(),-1);
    // Results of all children first: a reused source may be replaced by an
    // earlier child of this batch before a later child is placed.
    std::vector<double> child_fitness(B);std::vector<unsigned> child_wrong(B,0),child_count(B,0);
    for(int b=0;b<B;++b) {
      const int source=reuse[b];
      if(source>=0)++epoch.evaluation.fitness_reused_programs;
      else {
        const auto& c=scores.counts.at(b);
        epoch.evaluation.program_cases+=c[0];epoch.evaluation.eval_errors+=c[1];epoch.evaluation.eval_timeouts+=c[2];
        epoch.evaluation.eval_fallbacks+=c[3];epoch.evaluation.eval_unscored+=c[4];
      }
      child_fitness[b]=source>=0?fitness[source]:canonicalize_fitness_for_ranking(scores.fitness.at(b));
      if(has_wrong){child_wrong[b]=source>=0?wrong[source]:scores.wrong.at(b);child_count[b]=source>=0?nodes[source]:child_nodes[b];}
    }
    for(int b=0;b<B;++b) {
      const int v=victim();
      fitness[v]=child_fitness[b];if(has_wrong)wrong[v]=child_wrong[b];nodes[v]=child_count[b];placed[v]=b;
      if(!has_wrong)selection[v]=fitness[v];
      if(better(v,best))best=v;
    }
    std::sort(order.begin(),order.end(),better);
    if(order.front()!=best)throw std::logic_error("steady-state best tracking");
    if(has_wrong)ranks();
    placements.clear();
    for(int m=0;m<P;++m)if(placed[m]>=0)placements.push_back({m,placed[m]});
    owner.commit_batch(placements);
    epoch.repro_ms+=elapsed(place_start);
    accumulate_timing(&epoch.reproduction,reproduction_timing_from_stats(stats));
  }
  if(result.history_best.size()<std::size_t(cfg.generations))record();
  else {  // the last generation-equivalent has no history row; keep its totals
    accumulate_timing(&result.timing.evaluation_totals,epoch.evaluation);
    accumulate_timing(&result.timing.reproduction_totals,epoch.reproduction);
  }
  const auto final_start=Clock::now();
  if(cfg.retain_final_population) {
    auto genomes=owner.export_population();
    if(genomes.size()!=std::size_t(P))throw std::runtime_error("steady-state final population size");
    for(int i:order)result.final_population.push_back(ScoredGenome{std::move(genomes[i]),fitness[i],has_wrong?int(wrong[i]):-1});
    result.best=result.final_population.front();
  } else result.best=scored(order.front());
  result.timing.final_eval_ms=elapsed(final_start);
}
// Rejects owner-construction policies that the native phase owners do not support.
void require_phase_entry_policy(const EvolutionConfig& cfg) {
  const auto entry = grammar::entry_request(*cfg.compiled_grammar);
  const auto request = cfg.generation_request.value_or(entry);
  if (!cfg.additional_generation_requests.empty() || cfg.mutation_subtree_prob != 1.0 ||
      request.nonterminal != entry.nonterminal || request.type != entry.type ||
      !request.visible_environment.empty() || request.budget.max_nodes != entry.budget.max_nodes ||
      request.budget.max_depth != entry.budget.max_depth)
    throw std::invalid_argument("native GPU phase profile requires the exact entry request and default resource/variation policy");
  if(!cfg.gpu_projected_phase_profile &&
      (cfg.initial_resource_budget || cfg.offspring_resource_budget))
    throw std::invalid_argument("projected budgets use generic native GPU reproduction unless gpu_projected_phase_profile is requested");
}
PhaseVariationPolicy resolve_phase_variation(const EvolutionConfig& cfg) {
  if (!cfg.phase_variation) return {};
  validate_phase_variation_policy(*cfg.phase_variation);
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
  void begin_steady(int b) { owner.begin_steady(b); }
  std::vector<int> make_batch(const std::vector<double>& f,int t,double m,std::uint64_t s,repro::ReproductionStats* r) { return owner.make_batch(f,t,m,s,r); }
  std::vector<unsigned> batch_nodes(bool sites) const { return owner.batch_nodes(sites); }
  void commit_batch(const std::vector<std::array<int,2>>& p) { owner.commit_batch(p); }
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
  void begin_steady(int b) { owner.begin_steady(b); }
  std::vector<int> make_batch(const std::vector<double>& f,int t,double m,std::uint64_t s,repro::ReproductionStats* r) { return owner.make_batch(f,t,m,s,r); }
  std::vector<unsigned> batch_nodes(bool sites) const { return owner.batch_nodes(sites); }
  void commit_batch(const std::vector<std::array<int,2>>& p) { owner.commit_batch(p); }
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
  const auto schedule=resolve_phase_schedule(cfg.phase_schedule.value_or(PhaseSchedulePolicy{}),int(population.size()));
  const bool steady=schedule.mode==PhaseScheduleMode::SteadyState;
  const auto begin = std::chrono::steady_clock::now();
  const auto elapsed = [](auto start) { return std::chrono::duration<double,std::milli>(
      std::chrono::steady_clock::now()-start).count(); };
  // The phase population is an explicit request: unsupported policies fail closed.
  require_phase_entry_policy(cfg);
  auto owner = std::make_unique<repro::NativePhasePopulation>(cfg.compiled_grammar,population,cases.input_names,
      cfg.offspring_resource_budget,cfg.gpu_phase_site_depth,resolve_phase_variation(cfg));
  result.timing.init_population_ms += elapsed(begin);
  result.reproduction_profile = owner->profile_name();
  result.gpu_stage_storage_reserved_bytes=owner->stage_storage_reserved_bytes();
  if(cfg.gpu_phase_site_parsimony)result.reproduction_profile+="-site-parsimony";
  const bool diagnostics=std::getenv("GAGP_GPU_DIAGNOSTICS") != nullptr;
  const auto evaluate_owner=[&](EvaluationTiming& timing) {
    auto fit = owner->evaluate(session,diagnostics);
    if (!fit.ok) throw std::runtime_error("native GPU phase fitness failed: " + fit.err.message);
    timing.execution_profile=fit.execution_profile;
    timing.physical_program_cases=fit.physical_program_cases;
    timing.shape_result_programs=fit.shape_result_programs;
    timing.shape_result_groups=fit.shape_result_groups;
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
  if(steady) {
    result.reproduction_profile+="-steady"+std::to_string(schedule.batch);
    const auto evaluate_batch=[&](const std::vector<int>& reuse,EvaluationTiming& timing) {
      auto fit=owner->evaluate_batch(session,reuse,unsigned(cases.bindings.size()),diagnostics);
      if (!fit.ok) throw std::runtime_error("native GPU phase batch fitness failed: " + fit.err.message);
      timing.execution_profile=fit.execution_profile;
      timing.physical_program_cases+=fit.physical_program_cases;
      timing.gpu_eval_call_ms+=fit.timing.total_ms;timing.gpu_eval_pack_ms+=fit.timing.pack_ms;
      timing.gpu_eval_upload_ms+=fit.timing.upload_ms;timing.gpu_eval_kernel_ms+=fit.timing.kernel_ms;
      timing.gpu_eval_copyback_ms+=fit.timing.copyback_ms;
      PhaseOwnerFitness out;out.fitness=std::move(fit.fitness);out.wrong=std::move(fit.wrong);
      for(const auto& c:fit.case_counts)out.counts.push_back({std::uint64_t(c[0]),std::uint64_t(c[1]),std::uint64_t(c[2]),std::uint64_t(c[3]),std::uint64_t(c[4])});
      out.counts.resize(reuse.size(),{0,0,0,0,0});
      return out;
    };
    run_phase_population_steady(cfg,cases,adapter,evaluate_owner,evaluate_batch,schedule,result,search_start);
  }
  else run_phase_population(cfg,cases,adapter,evaluate_owner,result,search_start);
  owner.reset();
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
  const auto schedule=resolve_phase_schedule(cfg.phase_schedule.value_or(PhaseSchedulePolicy{}),int(population.size()));
  repro::HostPhasePopulation owner(cfg.compiled_grammar,population,cases.input_names,cfg.offspring_resource_budget,
      cfg.gpu_phase_site_depth,resolve_phase_variation(cfg));
  result.timing.init_population_ms += std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
  result.reproduction_profile = owner.profile_name();
  if(cfg.gpu_phase_site_parsimony)result.reproduction_profile+="-site-parsimony";
  if(schedule.mode==PhaseScheduleMode::SteadyState) {
    result.reproduction_profile+="-steady"+std::to_string(schedule.batch);
    const auto evaluate_owner=[&](EvaluationTiming& timing) {
      auto fit=owner.evaluate(cases.bindings,cases.expected_values,cfg.fuel,cfg.penalty,cfg.gpu_blocksize,cfg.fitness_policy);
      timing.execution_profile="cpu-phase-reference";
      PhaseOwnerFitness out;out.fitness=std::move(fit.fitness);out.wrong=std::move(fit.wrong);
      for(const auto& c:fit.case_counts)out.counts.push_back({c.cases,c.errors,c.timeouts,0,0});
      return out;
    };
    const auto evaluate_batch=[&](const std::vector<int>& reuse,EvaluationTiming& timing) {
      auto fit=owner.evaluate_batch(reuse,cases.bindings,cases.expected_values,cfg.fuel,cfg.penalty,cfg.gpu_blocksize,cfg.fitness_policy);
      timing.execution_profile="cpu-phase-reference";
      timing.physical_program_cases+=fit.evaluated_programs*cases.bindings.size();
      PhaseOwnerFitness out;out.fitness=std::move(fit.fitness);out.wrong=std::move(fit.wrong);
      for(const auto& c:fit.case_counts)out.counts.push_back({c.cases,c.errors,c.timeouts,0,0});
      return out;
    };
    CpuPhaseOwner adapter{owner,population.size()};
    run_phase_population_steady(cfg,cases,adapter,evaluate_owner,evaluate_batch,schedule,result,search_start);
    return;
  }
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
  if (cfg.phase_schedule && !cfg.phase_schedule->is_default()) {
    validate_phase_schedule_policy(*cfg.phase_schedule, cfg.population_size);
    // A non-default schedule is steady_state.
    if (phase_engine(cfg) == PhaseEngine::None)
      throw std::invalid_argument("steady_state requires the native phase population");
    if (cfg.cpu_fitness_verification.top_k || !cfg.checkpoint_generations.empty() || cfg.skip_final_eval)
      throw std::invalid_argument("steady_state supports neither verification, checkpoints nor skipped final evaluation");
  }
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
  // A site-depth phase run without a saved population grows its own phase
  // genes; native generation does not respect the invocation-depth cap.
  std::vector<ProgramGenome> phase_initial;
  if (!initial_population && cfg.gpu_phase_site_depth && phase_engine(cfg) != PhaseEngine::None) {
    require_phase_entry_policy(cfg);
    phase_initial = repro::generate_phase_initial_population(*cfg.compiled_grammar, *cfg.generation_request,
        case_set.input_names, cfg.population_size, cfg.seed, cfg.gpu_phase_site_depth);
    initial_population = &phase_initial;
  }
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
                                         overlap_gpu
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
