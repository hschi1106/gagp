#pragma once

#include <vector>

#include "gagp/cli/json.hpp"
#include "gagp/evolution/case_set.hpp"
#include "gagp/evolution/genome.hpp"

namespace gagp::migration::bench {

struct FrozenReproductionConfig {
  int population_size = 0;
  int pair_count = 0;
  int candidates_per_program = 0;
  int donor_pool_size_per_type = 0;
  int max_nodes = 0;
  int max_donor_nodes = 0;
  int max_names = 0;
  int max_consts = 0;
  int max_linear_rec_binders = 0;
  int max_asgp_dc_binders = 0;
  int max_asgp_dp1d_specs = 0;
  int max_asgp_dp2d_specs = 0;
  int tournament_k = 0;
  int max_expr_depth = 0;
  int max_for_k = 0;
  double mutation_ratio = 0.0;
  double mutation_subtree_ratio = 0.0;
  std::uint64_t seed = 0;
};

struct FrozenReproduction {
  std::vector<evo::ProgramGenome> parents;
  std::vector<double> fitness;
  FrozenReproductionConfig config;
};

// Decode the frozen Goal 01 benchmark format and lower every release-1 member
// through the transition-only adapter. This format is deliberately not public.
std::vector<evo::ProgramGenome> decode_final_candidate_population(
    const cli_detail::JsonValue& raw,
    const std::vector<evo::InputSpec>& inputs, std::uint32_t minimum_dc_frames = 0,
    bool normalize_storage = false);

// Decode the source-less Goal 01 bytecode population used only by the frozen
// bounded-fallback runtime row. Specialized release-1 segments remain rejected;
// this boundary accepts ordinary verified bytecode with opaque payload tokens.
std::vector<BytecodeProgram> decode_final_candidate_bytecode_population(
    const cli_detail::JsonValue& raw);

// Decode the index-addressed cases paired with the source-less bytecode row.
// They intentionally bypass the named current fitness-case schema.
evo::CaseSet decode_final_candidate_bytecode_cases(
    const cli_detail::JsonValue& raw);

// Restores the parent population and validates every identity-bearing field in
// a Goal 01 reproduction capture. Retired candidate and donor tables are
// validated but deliberately not fed to the current compiled-grammar backend.
FrozenReproduction decode_frozen_reproduction(
    const cli_detail::JsonValue& raw,
    const std::vector<evo::InputSpec>& inputs, std::uint32_t minimum_dc_frames = 0,
    bool normalize_storage = false);

}  // namespace gagp::migration::bench
