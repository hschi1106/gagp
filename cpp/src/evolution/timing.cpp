#include "gagp/evolution/timing.hpp"

namespace gagp::evo {

void accumulate_timing(EvaluationTiming* total, const EvaluationTiming& value) {
  total->program_cases += value.program_cases;
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
  total->gpu_eval_copyback_ms += value.gpu_eval_copyback_ms;
  total->gpu_eval_teardown_ms += value.gpu_eval_teardown_ms;
}

void accumulate_timing(ReproductionTiming* total, const ReproductionTiming& value) {
  total->selection_ms += value.selection_ms;
  total->crossover_ms += value.crossover_ms;
  total->mutation_ms += value.mutation_ms;
  total->prepare_inputs_ms += value.prepare_inputs_ms;
  total->setup_ms += value.setup_ms;
  total->preprocess_ms += value.preprocess_ms;
  total->pack_ms += value.pack_ms;
  total->upload_ms += value.upload_ms;
  total->kernel_ms += value.kernel_ms;
  total->copyback_ms += value.copyback_ms;
  total->decode_ms += value.decode_ms;
  total->teardown_ms += value.teardown_ms;
  total->selection_kernel_ms += value.selection_kernel_ms;
  total->variation_kernel_ms += value.variation_kernel_ms;
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
