#pragma once

#include <cstdint>
#include <memory>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include "gagp/core/value.hpp"
#include "gagp/evolution/repro/constant_types.hpp"
#include "gagp/evolution/ast_program.hpp"

namespace gagp::evo::grammar { class CompiledGrammar; }

namespace gagp::evo::repro {

struct ConstantMutationTable;

enum class ReproductionContractMode : int { Legacy = 0, CompiledGrammar = 1 };
inline constexpr std::uint32_t kNoCompatibilityId = std::numeric_limits<std::uint32_t>::max();

struct CandidateOccurrence {
  int start = 0;
  int stop = 0;
  int binder_offset = 0;
  int binder_count = 0;
};
struct DonorContract {
  std::uint32_t compatibility_id = kNoCompatibilityId;
  int materialized_nodes = 0;
  int materialized_depth = 0;
  int template_nesting = 0;
  int binder_offset = 0;
  int binder_count = 0;
};

constexpr int kGpuReproMaxNames = 128;
constexpr int kGpuReproMaxConsts = 128;
constexpr int kGpuReproKernelMaxNodes = 512;
constexpr int kGpuReproDonorTypeCount = 9;

enum class CandidateTag {
  Expr = 0,
  Program = 1,
};

struct CandidateRange {
  int start = -1;
  int stop = -1;
  int tag = static_cast<int>(CandidateTag::Expr);
  int aux = static_cast<int>(RType::Invalid);
  std::uint64_t scope_signature = 0;
  std::uint64_t binder_signature = 0;
  int scheme_kind = 0;
  int phase_name = 0;
  std::uint64_t visible_env_signature = 0;
  int dp_dependency_arity = -1;
  std::uint32_t compatibility_id = kNoCompatibilityId;
  int occurrence_offset = 0;
  int occurrence_count = 0;
  int replacement_max_nodes = 0;
  int replacement_max_depth = 0;
  int remaining_template_nesting = 0;
  int materialized_nodes = 0;
  int materialized_depth = 0;
  int template_nesting = 0;
  int donor_offset = 0;
  int donor_count = 0;
};

struct PlainNode {
  int kind = 0;
  int i0 = 0;
  int i1 = 0;
};

struct PlainLinearRecBinders {
  int node_index = 0;
  int elem_name = 0;
  int accum_name = 0;
  int index_name = 0;
};

struct PlainAsgpDcBinders {
  int node_index = 0;
  int solve_xs_name = 0;
  int solve_n_name = 0;
  int solve_lo_name = 0;
  int divide_n_name = 0;
  int combine_left_name = 0;
  int combine_right_name = 0;
};

struct PlainAsgpDp1dSpec {
  int node_index = 0;
  int lo = 0;
  int hi = 0;
  int base_state = 0;
  int boundary_const = 0;
  int dep_kind = 0;
  int dep_offsets[3] = {0, 0, 0};
  int dep_offset_count = 0;
  int solve_state_name = 0;
  int transition_state_name = 0;
  int transition_dep_names[3] = {0, 0, 0};
  int transition_dep_count = 0;
};

struct PlainAsgpDp2dSpec {
  int node_index = 0;
  int i_lo = 0;
  int i_hi = 0;
  int j_lo = 0;
  int j_hi = 0;
  int base_i = 0;
  int base_j = 0;
  int boundary_const = 0;
  int dep_kind = 0;
  int solve_i_name = 0;
  int solve_j_name = 0;
  int transition_i_name = 0;
  int transition_j_name = 0;
  int transition_dep_names[4] = {0, 0, 0, 0};
  int transition_dep_count = 0;
};

struct PackedProgramMeta {
  int used_len = 0;
  int name_count = 0;
  int const_count = 0;
  int linear_rec_count = 0;
  int asgp_dc_count = 0;
  int asgp_dp1d_count = 0;
  int asgp_dp2d_count = 0;
  int candidate_count = 0;
};

struct DonorProgram {
  AstProgram ast;
  RType type = RType::Invalid;
};

struct PreprocessOutput {
  ReproductionContractMode contract_mode = ReproductionContractMode::Legacy;
  int prepared_max_nodes = 0;
  int prepared_max_depth = 0;
  std::shared_ptr<const grammar::CompiledGrammar> compiled_grammar;
  std::shared_ptr<const ConstantMutationTable> constant_mutation;
  std::vector<int> parent_constant_streams;
  std::vector<int> donor_constant_streams;
  std::vector<std::string> compatibility_keys;
  std::vector<CandidateOccurrence> occurrences;
  std::vector<int> occurrence_binder_ids;
  std::vector<DonorContract> donor_contracts;
  std::vector<int> donor_binder_ids;
  std::vector<std::string> population_identities;
  std::vector<std::string> donor_identities;
  std::vector<std::vector<std::size_t>> subtree_ends;
  std::vector<std::vector<CandidateRange>> candidates;
  std::vector<DonorProgram> donor_pool;
};

enum class CompiledVariationPass : int { Crossover = 0, Mutation = 1 };

struct GpuReproConfig {
  CompiledVariationPass compiled_pass = CompiledVariationPass::Crossover;
  ReproductionContractMode contract_mode = ReproductionContractMode::Legacy;
  int donor_pool_size_per_site = 4;
  int compiled_donor_count = 0;
  int compiled_occurrence_count = 0;
  int constant_domain_count = 0;
  int constant_value_count = 0;
  int constant_group_count = 0;
  int constant_origin_count = 0;
  int constant_stream_count = 0;
  int constant_root_count = 0;
  int population_size = 0;
  int pair_count = 0;
  int candidates_per_program = 16;
  int donor_pool_size_per_type = 64;
  int max_nodes = 80;
  int max_donor_nodes = 24;
  int max_names = kGpuReproMaxNames;
  int max_consts = kGpuReproMaxConsts;
  int max_linear_rec_binders = 1;
  int max_asgp_dc_binders = 1;
  int max_asgp_dp1d_specs = 1;
  int max_asgp_dp2d_specs = 1;
  int tournament_k = 3;
  int max_expr_depth = 0;
  int max_for_k = 0;
  double mutation_ratio = 0.5;
  double mutation_subtree_ratio = 0.8;
  std::uint64_t seed = 0;
};

struct CompiledSpliceSources {
  // Immutable snapshots retain all sidecars and original table indices until
  // copyback. They are shared when prepared inputs move between overlap stages.
  std::vector<AstProgram> parents;
  std::vector<AstProgram> donors;
};

struct PackedHostData {
  GpuReproConfig config;
  std::shared_ptr<const CompiledSpliceSources> compiled_sources;
  std::shared_ptr<const grammar::CompiledGrammar> compiled_grammar;
  std::shared_ptr<const ConstantMutationTable> constant_mutation;
  std::vector<int> parent_constant_streams;
  std::vector<int> donor_constant_streams;
  std::vector<std::string> compatibility_keys;
  std::vector<CandidateOccurrence> occurrences;
  std::vector<int> occurrence_binder_ids;
  std::vector<DonorContract> donor_contracts;
  std::vector<int> donor_binder_ids;
  std::vector<PlainNode> program_nodes;
  std::vector<PackedProgramMeta> metas;
  std::vector<CandidateRange> candidates;
  std::vector<std::uint64_t> program_name_ids;
  std::vector<Value> program_consts;
  std::vector<PlainLinearRecBinders> program_linear_rec_binders;
  std::vector<PlainAsgpDcBinders> program_asgp_dc_binders;
  std::vector<PlainAsgpDp1dSpec> program_asgp_dp1d_specs;
  std::vector<PlainAsgpDp2dSpec> program_asgp_dp2d_specs;
  std::vector<PlainNode> donor_nodes;
  std::vector<int> donor_lens;
  std::vector<std::uint64_t> donor_name_ids;
  std::vector<int> donor_name_counts;
  std::vector<Value> donor_consts;
  std::vector<int> donor_const_counts;
  std::vector<PlainLinearRecBinders> donor_linear_rec_binders;
  std::vector<int> donor_linear_rec_counts;
  std::vector<PlainAsgpDcBinders> donor_asgp_dc_binders;
  std::vector<int> donor_asgp_dc_counts;
  std::vector<PlainAsgpDp1dSpec> donor_asgp_dp1d_specs;
  std::vector<int> donor_asgp_dp1d_counts;
  std::vector<PlainAsgpDp2dSpec> donor_asgp_dp2d_specs;
  std::vector<int> donor_asgp_dp2d_counts;
  std::unordered_map<std::uint64_t, std::string> name_lookup;
};

struct PackedSelectionCounters {
  std::uint64_t contract_rejections = 0;
  std::uint64_t budget_rejections = 0;
};

struct GpuReproSelectionPlan {
  std::vector<int> parent_a;
  std::vector<int> parent_b;
  std::vector<int> cand_a;
  std::vector<int> cand_b;
};

struct PackedChildMeta {
  int node_count = 0;
  int max_depth = 0;
  unsigned char uses_builtins = 0;
  unsigned char valid = 0;
};

enum class SpliceSourceKind : int { None = 0, Parent = 1, CompiledDonor = 2 };

// Written by device variation and consumed by host metadata reconstruction.
// A rejected splice keeps applied=0 and identifies its unchanged base parent.
enum class CompiledMutationOutcome : int {
  None = 0, Constant = 1, Subtree = 2, NoSite = 3,
  Capacity = 4, Invalid = 5, NoDonor = 6,
};

struct PackedChildSplice {
  CompiledMutationOutcome mutation_outcome = CompiledMutationOutcome::None;
  int applied = 0;
  int base_parent = -1;
  int destination_candidate = -1;
  SpliceSourceKind source_kind = SpliceSourceKind::None;
  int source_index = -1;
  int source_candidate = -1;
  int source_begin = 0;
  int source_end = 0;
  int occurrence_count = 0;
};

struct GpuReproChildData {
  GpuReproConfig config;
  std::vector<PackedSelectionCounters> selection_counters;
  GpuReproSelectionPlan selection;
  std::vector<PlainNode> child_nodes;
  std::vector<int> child_used_len;
  std::vector<std::uint64_t> child_name_ids;
  std::vector<int> child_name_counts;
  std::vector<Value> child_consts;
  std::vector<int> child_const_counts;
  std::vector<PackedChildMeta> child_meta;
  std::vector<PackedChildSplice> child_splices;
};

struct GpuReproChildView {
  GpuReproConfig config;
  const PackedSelectionCounters* selection_counters = nullptr;
  const int* parent_a = nullptr;
  const int* parent_b = nullptr;
  const int* cand_a = nullptr;
  const int* cand_b = nullptr;
  const PlainNode* child_nodes = nullptr;
  const int* child_node_offsets = nullptr;
  const std::uint64_t* child_name_ids = nullptr;
  const int* child_name_offsets = nullptr;
  const Value* child_consts = nullptr;
  const int* child_const_offsets = nullptr;
  const int* child_used_len = nullptr;
  const int* child_name_counts = nullptr;
  const int* child_const_counts = nullptr;
  const PackedChildMeta* child_meta = nullptr;
  const PackedChildSplice* child_splices = nullptr;
};

}  // namespace gagp::evo::repro
