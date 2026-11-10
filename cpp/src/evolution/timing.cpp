#include "gagp/evolution/timing.hpp"
#include <algorithm>

namespace gagp::evo {

void accumulate_timing(EvaluationTiming* total, const EvaluationTiming& value) {
  total->program_cases += value.program_cases;
  total->physical_program_cases += value.physical_program_cases;
  total->physical_blocksize=std::max(total->physical_blocksize,value.physical_blocksize);
  total->fitness_reused_programs+=value.fitness_reused_programs;
  total->fitness_reused_program_cases+=value.fitness_reused_program_cases;
  total->shape_result_programs += value.shape_result_programs;
  total->shape_result_groups += value.shape_result_groups;
  for(std::size_t i=0;i<total->concat_cache_stats.size();++i)
    total->concat_cache_stats[i] += value.concat_cache_stats[i];
  total->payload_workspace_retained_bytes=std::max(total->payload_workspace_retained_bytes,value.payload_workspace_retained_bytes);
  total->payload_workspace_pool_peak_bytes=std::max(total->payload_workspace_pool_peak_bytes,value.payload_workspace_pool_peak_bytes);
  total->payload_workspace_pool_allocations+=value.payload_workspace_pool_allocations;
  total->payload_replay_programs += value.payload_replay_programs;
  total->payload_replay_cases += value.payload_replay_cases;
  total->payload_replay_launches += value.payload_replay_launches;

  total->eval_errors += value.eval_errors;
  total->eval_timeouts += value.eval_timeouts;
  total->eval_fallbacks += value.eval_fallbacks;
  total->eval_unscored += value.eval_unscored;
  total->cpu_compile_ms += value.cpu_compile_ms;
  total->gpu_compile_ms += value.gpu_compile_ms;
  total->gpu_eval_call_ms += value.gpu_eval_call_ms;
  total->gpu_eval_pack_ms += value.gpu_eval_pack_ms;
  total->gpu_eval_launch_prep_ms += value.gpu_eval_launch_prep_ms;
  total->gpu_eval_upload_ms += value.gpu_eval_upload_ms;
  total->gpu_eval_kernel_ms += value.gpu_eval_kernel_ms;
  total->gpu_eval_initial_kernel_wall_ms += value.gpu_eval_initial_kernel_wall_ms;
  total->gpu_eval_replay_kernel_event_ms += value.gpu_eval_replay_kernel_event_ms;
  total->gpu_eval_copyback_ms += value.gpu_eval_copyback_ms;
  total->gpu_eval_teardown_ms += value.gpu_eval_teardown_ms;
}

void accumulate_timing(ReproductionTiming* total, const ReproductionTiming& value) {
  total->selection_ms += value.selection_ms;
  total->crossover_ms += value.crossover_ms;
  total->mutation_ms += value.mutation_ms;
  total->setup_ms += value.setup_ms;
  total->preprocess_ms += value.preprocess_ms;
  total->upload_ms += value.upload_ms;
  total->kernel_ms += value.kernel_ms;
  total->copyback_ms += value.copyback_ms;
  total->decode_ms += value.decode_ms;
  total->teardown_ms += value.teardown_ms;
  total->variation.crossover_attempts += value.variation.crossover_attempts;
  total->variation.mutation_attempts += value.variation.mutation_attempts;
  total->variation.contract_rejections += value.variation.contract_rejections;
  total->variation.budget_rejections += value.variation.budget_rejections;
  total->variation.generation_rejections += value.variation.generation_rejections;
  total->variation.acceptance_rejections += value.variation.acceptance_rejections;
  total->variation.fallback_children += value.variation.fallback_children;
  total->variation.unchanged_children += value.variation.unchanged_children;
  total->variation.changed_children += value.variation.changed_children;
}

}  // namespace gagp::evo
