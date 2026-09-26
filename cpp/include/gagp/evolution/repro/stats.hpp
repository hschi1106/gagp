#pragma once

#include "gagp/evolution/grammar/variation_stats.hpp"

namespace gagp::evo::repro {

struct ReproductionStats {
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
  // Pass timers partition their aggregate counterparts; donor is nested in preprocessing.
  double crossover_prepare_ms = 0.0;
  double crossover_preprocess_ms = 0.0;
  double crossover_donor_ms = 0.0;
  double crossover_pack_ms = 0.0;
  double crossover_decode_ms = 0.0;
  double mutation_prepare_ms = 0.0;
  double mutation_preprocess_ms = 0.0;
  double mutation_donor_ms = 0.0;
  double mutation_pack_ms = 0.0;
  double mutation_decode_ms = 0.0;
  double replay_validation_ms = 0.0;
  double overlap_wait_ms = 0.0;
  grammar::VariationCounters variation;
};

}  // namespace gagp::evo::repro
