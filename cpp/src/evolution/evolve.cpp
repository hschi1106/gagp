#include "gagp/evolution/evolve.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>

#include "gagp/evolution/lifecycle.hpp"
#include "gagp/evolution/population_init.hpp"
#include "gagp/evolution/repro/stats.hpp"
#include "gagp/evolution/selection.hpp"
#include "repro/gpu/phase_population_cpu.hpp"
#ifdef GAGP_HAS_CUDA
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "repro/gpu/phase_population.hpp"
#endif

namespace gagp::evo {

namespace {
// Owner construction admits the grammar and every initial genome into the
// phase-gene genotype; a construct it cannot hold is reported to the caller.
template<class F> auto admit_resident(F&& f) -> decltype(f()) {
  try { return f(); }
  catch (const std::invalid_argument& e) {
    throw std::invalid_argument(std::string("resident engine cannot admit this grammar or initial population: ") +
        e.what() + " (see Resident admission in spec/search_config.md)");
  }
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

ReproductionTiming reproduction_timing_from_stats(
    const repro::ReproductionStats& stats) {
  ReproductionTiming timing;
  timing.variation = stats.variation;
  timing.setup_ms = stats.setup_ms;
  timing.upload_ms = stats.upload_ms;
  timing.kernel_ms = stats.kernel_ms;
  timing.copyback_ms = stats.copyback_ms;
  timing.decode_ms = stats.decode_ms;
  timing.teardown_ms = stats.teardown_ms;
  return timing;
}

// Raw per-member results of one phase-owner evaluation. Canonicalization,
// ranking and every later search decision are shared by both engines.
struct PhaseOwnerFitness {
  std::vector<double> fitness;
  std::vector<unsigned> wrong;
  std::vector<std::array<std::uint64_t,5>> counts;  // cases, errors, timeouts, fallbacks, unscored
};
// Shared generational loop of the phase population. `Owner` is the GPU
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
  const auto evaluate = [&](EvaluationTiming& timing) {
    auto fit = evaluate_owner(timing);
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
    accumulate_timing(&result.timing.evaluation_totals,timing);
    selection_fitness=fitness;
    if(!wrong.empty())for(std::size_t rank=0;rank<ranking.size();++rank)selection_fitness[ranking[rank]]=double(ranking.size()-rank);
  };
  for (int gen=0;gen<cfg.generations;++gen) {
    const auto start=std::chrono::steady_clock::now();GenerationTiming timing;
    evaluate(timing.evaluation);timing.eval_ms=elapsed(start);
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
    const auto start=std::chrono::steady_clock::now();EvaluationTiming timing;evaluate(timing);
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
// GPU evaluator and GPU operators, with a private persistent genotype owner.
// Cold import is charged once; all changed-child lowering/integration is charged
// before completing a generation. No host reproduction or hidden AST replay.
void evolve_gpu_phase_population(const EvolutionConfig& cfg, const CaseSet& cases,
    const std::vector<ProgramGenome>& population, FitnessSessionGpu& session,
    EvolutionResult& result, std::chrono::steady_clock::time_point search_start,
    const std::function<void()>& await_session) {
  const auto schedule=resolve_phase_schedule(cfg.phase_schedule.value_or(PhaseSchedulePolicy{}),int(population.size()));
  const bool steady=schedule.mode==PhaseScheduleMode::SteadyState;
  const auto begin = std::chrono::steady_clock::now();
  const auto elapsed = [](auto start) { return std::chrono::duration<double,std::milli>(
      std::chrono::steady_clock::now()-start).count(); };
  const auto variation = resolve_phase_variation(cfg);
  auto owner = admit_resident([&] { return std::make_unique<repro::NativePhasePopulation>(cfg.compiled_grammar,
      population,cases.input_names,cfg.gpu_phase_site_depth,variation,await_session); });
  result.timing.init_population_ms += elapsed(begin);
  result.reproduction_profile = owner->profile_name();
  result.gpu_stage_storage_reserved_bytes=owner->stage_storage_reserved_bytes();
  if(cfg.gpu_phase_site_parsimony)result.reproduction_profile+="-site-parsimony";
  const bool diagnostics=std::getenv("GAGP_GPU_DIAGNOSTICS") != nullptr;
  const auto evaluate_owner=[&](EvaluationTiming& timing) {
    auto fit = owner->evaluate(session,diagnostics);
    if (!fit.ok) throw std::runtime_error("GPU phase fitness failed: " + fit.err.message);
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
      if (!fit.ok) throw std::runtime_error("GPU phase batch fitness failed: " + fit.err.message);
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
}
#endif
// CPU reference engine: same import, operators, draws and lowering as the GPU
// owner; programs are evaluated by the CPU evaluator on the host only.
void evolve_cpu_phase_population(const EvolutionConfig& cfg, const CaseSet& cases,
    const std::vector<ProgramGenome>& population, EvolutionResult& result,
    std::chrono::steady_clock::time_point search_start) {
  const auto begin = std::chrono::steady_clock::now();
  const auto schedule=resolve_phase_schedule(cfg.phase_schedule.value_or(PhaseSchedulePolicy{}),int(population.size()));
  const auto variation = resolve_phase_variation(cfg);
  auto host = admit_resident([&] { return std::make_unique<repro::HostPhasePopulation>(cfg.compiled_grammar,
      population,cases.input_names,cfg.gpu_phase_site_depth,variation); });
  auto& owner = *host;
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

EvolutionResult evolve_population(const std::vector<EvalCase>& cases,
                                  const EvolutionConfig& cfg,
                                  const std::vector<ProgramGenome>* initial_population) {
  if(cfg.gpu_phase_site_depth<0 || cfg.gpu_phase_site_depth>15)
    throw std::invalid_argument("invalid GPU phase site depth");
  if(cfg.gpu_phase_site_parsimony && cfg.fitness_policy.metric==FitnessMetric::Legacy)
    throw std::invalid_argument("GPU phase site parsimony requires a task metric with wrong counts");
  if (cfg.phase_schedule && !cfg.phase_schedule->is_default()) {
    validate_phase_schedule_policy(*cfg.phase_schedule, cfg.population_size);
    if (cfg.skip_final_eval)
      throw std::invalid_argument("steady_state does not support skipped final evaluation");
  }
  validate_evolution_config(cfg);
  if (cases.empty()) {
    throw std::invalid_argument("cases must not be empty");
  }
  if (cfg.elite_count < 0 || cfg.elite_count >= cfg.population_size)
    throw std::invalid_argument("elite_count must be >= 0 and < population_size");
  if (cfg.generations <= 0) {
    throw std::invalid_argument("generations must be > 0");
  }
  const auto all_t0 = std::chrono::steady_clock::now();
  const CaseSet case_set = evolution_case_set(cases, cfg);
  const PayloadLifetimeManager initial_payload_lifetime(cases);

  EvolutionResult result;
  result.timing.generations.reserve(static_cast<std::size_t>(cfg.generations));

#ifdef GAGP_HAS_CUDA
  FitnessSessionGpu gpu_session;
  // Session setup is dominated by CUDA context creation and reads only the
  // cases. It runs beside host admission (population initialization, owner
  // import and verification) and the owner awaits it before its first CUDA
  // call. A failed setup is still reported before any admission error.
  std::future<FitnessSessionInitResult> gpu_init;
  const auto await_session = [&] {
    if (!gpu_init.valid()) return;
    const FitnessSessionInitResult init_result = gpu_init.get();
    if (!init_result.ok)
      throw std::runtime_error("gpu fitness session init failed: " + init_result.err.message);
    result.timing.gpu_eval_init_ms = init_result.timing.total_ms;
    if (!gpu_session.use_device())
      throw std::runtime_error("gpu fitness session init failed: cuda set device failure");
  };
#endif
  if (cfg.eval_engine == EvalEngine::GPU) {
#ifdef GAGP_HAS_CUDA
    gpu_init = std::async(std::launch::async, [&] {
      return gpu_session.init(case_set.bindings, case_set.expected_values, cfg.fuel,
                              cfg.gpu_blocksize, cfg.penalty, cfg.fitness_policy);
    });
#else
    throw std::runtime_error("gpu evaluation requested but CUDA is unavailable in this build");
#endif
  }

  try {
    const auto init_t0 = std::chrono::steady_clock::now();
    // A site-depth run without a saved population grows its own phase genes;
    // the grammar generator does not respect the invocation-depth cap.
    std::vector<ProgramGenome> phase_initial;
    if (!initial_population && cfg.gpu_phase_site_depth) {
      phase_initial = admit_resident([&] { return repro::generate_phase_initial_population(*cfg.compiled_grammar,
          grammar::entry_request(*cfg.compiled_grammar), case_set.input_names, cfg.population_size, cfg.seed,
          cfg.gpu_phase_site_depth); });
      initial_population = &phase_initial;
    }
    PopulationInitialization initialization =
        initialize_population(cfg, case_set, initial_population);
    std::vector<ProgramGenome> population = std::move(initialization.population);
    result.timing.init_population_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - init_t0).count();

    initial_payload_lifetime.retain(population, result.history_best);
    if (cfg.eval_engine == EvalEngine::GPU) {
#ifdef GAGP_HAS_CUDA
      evolve_gpu_phase_population(cfg, case_set, population, gpu_session, result, all_t0, await_session);
#endif
    } else {
      evolve_cpu_phase_population(cfg, case_set, population, result, all_t0);
    }
  } catch (...) {
#ifdef GAGP_HAS_CUDA
    await_session();
#endif
    throw;
  }
  initial_payload_lifetime.retain({},result.history_best,cfg.skip_final_eval ? nullptr : &result.best,
      cfg.retain_final_population ? &result.final_population : nullptr);
  result.timing.total_ms=std::chrono::duration<double,std::milli>(
      std::chrono::steady_clock::now()-all_t0).count();
  return result;
}

}  // namespace gagp::evo
