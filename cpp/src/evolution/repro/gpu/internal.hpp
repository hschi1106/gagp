#pragma once

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "gagp/evolution/repro/stats.hpp"
#include "gagp/evolution/repro/types.hpp"

namespace gagp::evo::repro {

// Validate dimensions and bounded storage for compiled operator passes.
inline bool require_gpu_transport_config(const GpuReproConfig& c, std::string* message_out) {
  const auto count = [](int n) { return n >= 0 && n <= 1048576; };
  if (c.population_size <= 0 || c.population_size > 65536 ||
      c.pair_count != (c.population_size + 1) / 2 ||
      c.candidates_per_program <= 0 || c.candidates_per_program > 65536 ||
      c.max_nodes <= 0 || c.max_nodes > kGpuReproKernelMaxNodes ||
      c.max_donor_nodes <= 0 || c.max_donor_nodes > kGpuReproKernelMaxNodes ||
      c.max_names <= 0 || c.max_names > kGpuReproMaxNames ||
      c.max_consts <= 0 || c.max_consts > kGpuReproMaxConsts ||
      c.max_expr_depth <= 0 || c.max_expr_depth > c.max_nodes ||
      c.tournament_k <= 0 || c.tournament_k > c.population_size ||
      !std::isfinite(c.mutation_ratio) || c.mutation_ratio < 0 || c.mutation_ratio > 1 ||
      !std::isfinite(c.mutation_subtree_ratio) || c.mutation_subtree_ratio < 0 || c.mutation_subtree_ratio > 1 ||
      !count(c.compiled_donor_count) || !count(c.compiled_occurrence_count) ||
      !count(c.constant_domain_count) || !count(c.constant_value_count) ||
      !count(c.constant_group_count) || !count(c.constant_origin_count) ||
      !count(c.constant_stream_count) || !count(c.constant_root_count) ||
      (c.compiled_pass != CompiledVariationPass::Crossover &&
       c.compiled_pass != CompiledVariationPass::Mutation)) {
    if (message_out) *message_out = "gpu reproduction unsupported: invalid compiled transport configuration";
    return false;
  }
  const std::uint64_t parents = c.population_size, children = static_cast<std::uint64_t>(c.pair_count) * 2;
  const std::uint64_t donors = c.compiled_donor_count;
  const std::uint64_t bytes = parents * (sizeof(PackedProgramMeta) +
      static_cast<std::uint64_t>(c.candidates_per_program) * sizeof(CandidateRange)) +
      (parents + children * 2) * static_cast<std::uint64_t>(c.max_nodes) * sizeof(PlainNode) +
      donors * static_cast<std::uint64_t>(c.max_donor_nodes) * sizeof(PlainNode) +
      (parents + donors + children * 2) *
          (static_cast<std::uint64_t>(c.max_names) * sizeof(std::uint64_t) +
           static_cast<std::uint64_t>(c.max_consts) * sizeof(Value)) +
      static_cast<std::uint64_t>(c.compiled_occurrence_count) * sizeof(CandidateOccurrence) +
      donors * (sizeof(DonorContract) + sizeof(int) * 4) +
      static_cast<std::uint64_t>(c.constant_domain_count) * sizeof(ConstantMutationDomain) +
      static_cast<std::uint64_t>(c.constant_value_count) * sizeof(Value) +
      static_cast<std::uint64_t>(c.constant_group_count) * sizeof(ConstantMutationGroup) +
      (static_cast<std::uint64_t>(c.constant_origin_count) + c.constant_root_count) * sizeof(int) +
      static_cast<std::uint64_t>(c.constant_stream_count) * sizeof(ConstantMutationStream) +
      static_cast<std::uint64_t>(c.pair_count) * sizeof(PackedSelectionCounters) +
      parents * (sizeof(double) + sizeof(int)) +
      children * (sizeof(PackedChildMeta) + sizeof(PackedChildSplice) + sizeof(int) * 8) + sizeof(int) * 3;
  if (bytes > 512ULL * 1024 * 1024) {
    if (message_out) *message_out = "gpu reproduction unsupported: compiled arena exceeds 512 MiB";
    return false;
  }
  return true;
}

struct ConstantMutationDomains;

struct GpuReproArena {
  void* storage = nullptr;
  // Strong ownership prevents a reused address from matching stale device data.
  std::shared_ptr<const ConstantMutationDomains> uploaded_domains;
  int device_id = -1;
  GpuReproConfig capacity;
  PlainNode* d_program_nodes = nullptr;
  PackedProgramMeta* d_metas = nullptr;
  CandidateRange* d_candidates = nullptr;
  CandidateOccurrence* d_occurrences = nullptr;
  DonorContract* d_donor_contracts = nullptr;
  ConstantMutationDomain* d_constant_domains = nullptr;
  Value* d_constant_values = nullptr;
  ConstantMutationGroup* d_constant_groups = nullptr;
  int* d_constant_origins = nullptr;
  int* d_constant_roots = nullptr;
  ConstantMutationStream* d_constant_streams = nullptr;
  int* d_parent_constant_streams = nullptr;
  int* d_donor_constant_streams = nullptr;
  std::uint64_t* d_program_name_ids = nullptr;
  Value* d_program_consts = nullptr;
  PlainNode* d_donor_nodes = nullptr;
  int* d_donor_lens = nullptr;
  std::uint64_t* d_donor_name_ids = nullptr;
  int* d_donor_name_counts = nullptr;
  Value* d_donor_consts = nullptr;
  int* d_donor_const_counts = nullptr;
  double* d_fitness = nullptr;
  PackedSelectionCounters* d_selection_counters = nullptr;
  int* d_parent_a = nullptr;
  int* d_parent_b = nullptr;
  int* d_cand_a = nullptr;
  int* d_cand_b = nullptr;
  PlainNode* d_child_nodes = nullptr;
  int* d_child_used_len = nullptr;
  std::uint64_t* d_child_name_ids = nullptr;
  int* d_child_name_counts = nullptr;
  Value* d_child_consts = nullptr;
  int* d_child_const_counts = nullptr;
  PackedChildMeta* d_child_meta = nullptr;
  PackedChildSplice* d_child_splices = nullptr;
  int* d_child_node_offsets = nullptr;
  int* d_child_name_offsets = nullptr;
  int* d_child_const_offsets = nullptr;
  PlainNode* d_live_child_nodes = nullptr;
  std::uint64_t* d_live_child_name_ids = nullptr;
  Value* d_live_child_consts = nullptr;
};

struct GpuReproHostStaging {
  GpuReproConfig capacity;
  PackedSelectionCounters* selection_counters = nullptr;
  int* parent_a = nullptr;
  int* parent_b = nullptr;
  int* cand_a = nullptr;
  int* cand_b = nullptr;
  int* child_used_len = nullptr;
  int* child_name_counts = nullptr;
  int* child_const_counts = nullptr;
  PackedChildMeta* child_meta = nullptr;
  PackedChildSplice* child_splices = nullptr;
  int* child_node_offsets = nullptr;
  int* child_name_offsets = nullptr;
  int* child_const_offsets = nullptr;
  PlainNode* child_nodes = nullptr;
  std::uint64_t* child_name_ids = nullptr;
  Value* child_consts = nullptr;
};

bool gpu_repro_config_fits_capacity(const GpuReproConfig& need, const GpuReproConfig& have);
bool initialize_gpu_repro_runtime(std::string* message_out);
bool select_gpu_repro_device(int* device_id, std::string* message_out);
void destroy_gpu_repro_arena(GpuReproArena* arena);
bool ensure_gpu_repro_arena_capacity(GpuReproArena* arena,
                                     const GpuReproConfig& config,
                                     std::string* message_out);
void destroy_gpu_repro_host_staging(GpuReproHostStaging* staging);
bool ensure_gpu_repro_host_staging_capacity(GpuReproHostStaging* staging,
                                            const GpuReproConfig& config,
                                            std::string* message_out);
bool upload_gpu_repro_inputs(const PackedHostData& packed,
                             GpuReproArena* arena,
                             ReproductionStats* stats,
                             std::string* message_out);
bool launch_gpu_repro_kernels(GpuReproArena* arena,
                              const GpuReproConfig& config,
                              const std::vector<double>& fitness,
                              ReproductionStats* stats,
                              std::string* message_out);
bool copyback_gpu_repro_children(const GpuReproArena& arena,
                                 const GpuReproConfig& config,
                                 GpuReproHostStaging* staging,
                                 GpuReproChildView* out,
                                 ReproductionStats* stats,
                                 std::string* message_out);

}  // namespace gagp::evo::repro
