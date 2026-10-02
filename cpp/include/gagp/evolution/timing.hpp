#pragma once

#include <vector>
#include <string>

#include "gagp/evolution/grammar/variation_stats.hpp"

namespace gagp::evo {

struct EvaluationTiming {
  std::string execution_profile;
  // Only populated by opt-in diagnostic runs; not timing estimates.
  std::uint64_t program_cases = 0, eval_errors = 0, eval_timeouts = 0,
                eval_fallbacks = 0, eval_unscored = 0;
  double cpu_compile_ms = 0.0;
  double gpu_compile_ms = 0.0;
  double gpu_eval_call_ms = 0.0;
  double gpu_eval_pack_ms = 0.0;
  double gpu_eval_launch_prep_ms = 0.0;
  double gpu_eval_upload_ms = 0.0;
  double gpu_eval_kernel_ms = 0.0;
  double gpu_eval_copyback_ms = 0.0;
  double gpu_eval_teardown_ms = 0.0;

  double gpu_eval_pack_upload_ms() const {
    return gpu_eval_pack_ms + gpu_eval_upload_ms;
  }
};

struct ReproductionTiming {
  std::uint64_t gpu_donor_generated = 0;
  std::uint64_t gpu_donor_fallback = 0;
  std::uint64_t gpu_donor_device_bytes = 0;
  double gpu_donor_setup_ms = 0;

  double selection_ms = 0.0;
  double crossover_ms = 0.0;
  double mutation_ms = 0.0;
  double prepare_inputs_ms = 0.0;
  double setup_ms = 0.0;
  double preprocess_ms = 0.0;
  double pack_ms = 0.0;
  double upload_ms = 0.0;
  double kernel_ms = 0.0;
  double copyback_ms = 0.0;
  double decode_ms = 0.0;
  double teardown_ms = 0.0;
  double selection_kernel_ms = 0.0;
  double variation_kernel_ms = 0.0;
  grammar::VariationCounters variation;
};

struct GenerationTiming {
  // Actual completion timestamp relative to search start, including initialization.
  double elapsed_search_ms = 0.0;
  double eval_ms = 0.0;
  double repro_ms = 0.0;
  double total_ms = 0.0;
  EvaluationTiming evaluation;
  ReproductionTiming reproduction;
};

struct EvolutionTiming {
  double init_population_ms = 0.0;
  double gpu_eval_init_ms = 0.0;
  double final_eval_ms = 0.0;
  double total_ms = 0.0;
  EvaluationTiming evaluation_totals;
  ReproductionTiming reproduction_totals;
  std::vector<GenerationTiming> generations;
};

void accumulate_timing(EvaluationTiming* total, const EvaluationTiming& value);
void accumulate_timing(ReproductionTiming* total, const ReproductionTiming& value);

}  // namespace gagp::evo
