#pragma once

#include <string>
#include <vector>

#include "gagp/evolution/evolve.hpp"

namespace fixed_asgp {

// A matching fitness metric is only one part of search alignment. Keep named
// differences machine-readable; callers must not equate an aligned-fitness flag
// with equivalent initialization, variation, selection, or evaluation budgets.
struct SearchContractItem {
  std::string id;
  std::string source;
  std::string native;
  bool matched = false;
};
struct SearchContractAudit {
  std::string version = "asgp-native-search-contract-v1";
  std::string reproduction_profile;
  std::vector<SearchContractItem> items;
  bool fully_aligned() const {
    if(items.empty())return false;
    for(const auto& item:items)if(!item.matched)return false;
    return true;
  }
};

// task_metric_verified means the caller checked the task's metric, proximity,
// cap, summary weight/count/cap and expected-output encoding. Merely selecting
// a non-Legacy enum is insufficient. frozen_initial_population_verified means
// translated initialization was independently checked, not newly resampled.
inline SearchContractAudit audit_search_contract(
    const gagp::evo::EvolutionConfig& cfg, const std::string& reproduction_profile,
    bool task_metric_verified = false, bool frozen_initial_population_verified = false) {
  const bool phase_owner=reproduction_profile.rfind("native-gpu-phase-",0)==0;
  const bool gpu=cfg.reproduction_backend==gagp::evo::repro::ReproductionBackend::Gpu;
  SearchContractAudit audit;
  audit.reproduction_profile=reproduction_profile;
  audit.items={
    {"task_metric","task metric/proximity/ceiling/summary parameters",
      task_metric_verified?"verified task policy":"unverified task policy",
      task_metric_verified && cfg.fitness_policy.metric!=gagp::FitnessMetric::Legacy},
    {"elitism_count","1",std::to_string(cfg.elite_count),cfg.elite_count==1},
    {"initial_population","ramped depth 2..7, alternating full/grow",
      frozen_initial_population_verified?"verified translated frozen population":"native grammar initialization",
      frozen_initial_population_verified},
    {"crossover_gate","0.7 probability per child, one uniformly chosen phase",
      "one crossover attempt per child; no probability gate",false},
    {"crossover_sites","uniform phase, uniform destination node, compatible donor in same phase",
      phase_owner?"whole-genome grammar sites; bounded compatible-donor retries":
                  "grammar-admitted site pairs, scopes and nonterminals",false},
    {"mutation_gate","0.3 probability per child",std::to_string(cfg.mutation_rate),cfg.mutation_rate==0.3},
    {"mutation_scope","uniform phase then uniform node",
      "grammar-admitted sites; source phase is not sampled uniformly",false},
    {"mutation_donor_depth","grow donor within max_depth minus selected node level plus one",
      "compiled request and projected resource admission",false},
    {"phase_limits","source logical phase depth limit 7",
      "native grammar depth/node budgets and backend physical capacities",false},
    {"terminal_sampler","grow: terminal probability 0.5 when both categories fit; full: inner",
      "weighted feasible productions; no conditional terminal/inner gate",false},
    {"constant_sampler","uniform constant production, uniform range, uniform value",
      "translated ranges are separate weighted productions",false},
    {"ranking","error, wrong cases, source logical phase nodes",
      "40-bit canonical error, wrong cases, native AST nodes; deterministic final tie",false},
    {"tournament_sampling","independent draws with replacement, size 2",
      phase_owner?"independent device draws with replacement":
        gpu?"permuted population tournament groups":"shuffled population tournament groups",
      gpu && phase_owner && cfg.selection_pressure==2},
    {"offspring_budget","population minus elites children are generated",
      "population children generated; elites replace first ranked slots",false},
    {"execution_budget","source evaluator steps, recursion and arena limits",
      "native instruction fuel, region memo/frame and payload capacities",false},
    {"rng_replay","ASGP RNG stream and source operator draw order",
      "native CPU/CUDA RNG streams and operator draw order",false},
  };
  return audit;
}

} // namespace fixed_asgp
