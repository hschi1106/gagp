#pragma once

#include <cstdint>

#include "compiled_splice.cuh"
#include "selection_kernels.cuh"

namespace gagp::evo::repro {

// Compact launch contract for the compiled-grammar crossover pass. All packed
// parent tables use the fixed strides in config; occurrence records use their
// packed global offsets.
struct CompiledVariationPointers {
  const DPlainNode* program_nodes = nullptr;
  const DPackedProgramMeta* metas = nullptr;
  const DCandidateRange* candidates = nullptr;
  const CandidateOccurrence* occurrences = nullptr;
  const std::uint64_t* program_name_ids = nullptr;
  const Value* program_consts = nullptr;
  const int* parent_a = nullptr;
  const int* parent_b = nullptr;
  const int* cand_a = nullptr;
  const int* cand_b = nullptr;

  DPlainNode* child_nodes = nullptr;
  int* child_used_len = nullptr;
  std::uint64_t* child_name_ids = nullptr;
  int* child_name_counts = nullptr;
  Value* child_consts = nullptr;
  int* child_const_counts = nullptr;
  PackedChildMeta* child_meta = nullptr;
  PackedChildSplice* child_splices = nullptr;
};

__device__ inline bool d_compiled_parent_is_copyable(
    int parent, const GpuReproConfig& config,
    const CompiledVariationPointers& pointers) {
  if (parent < 0 || parent >= config.population_size || pointers.metas == nullptr)
    return false;
  const DPackedProgramMeta meta = pointers.metas[parent];
  return meta.used_len >= 0 && meta.used_len <= config.max_nodes &&
         meta.name_count >= 0 && meta.name_count <= config.max_names &&
         meta.const_count >= 0 && meta.const_count <= config.max_consts &&
         meta.candidate_count >= 0 &&
         meta.candidate_count <= config.candidates_per_program;
}

__device__ inline void d_publish_compiled_child_meta(
    int child, int node_count, PackedChildMeta* child_meta) {
  PackedChildMeta meta;
  meta.node_count = node_count;
  // Native compiled-grammar analysis runs after crossover. The legacy prefix
  // parser cannot derive a trustworthy depth for region templates.
  meta.max_depth = 0;
  meta.uses_builtins = 0;
  meta.valid = 1;
  child_meta[child] = meta;
}

__device__ inline void d_copy_compiled_base_child(
    int child, int base_parent, const GpuReproConfig& config,
    const CompiledVariationPointers& pointers) {
  const DPackedProgramMeta meta = pointers.metas[base_parent];
  const std::uint64_t base_node_offset =
      static_cast<std::uint64_t>(base_parent) * config.max_nodes;
  const std::uint64_t child_node_offset =
      static_cast<std::uint64_t>(child) * config.max_nodes;
  for (int i = 0; i < meta.used_len; ++i)
    pointers.child_nodes[child_node_offset + i] =
        pointers.program_nodes[base_node_offset + i];

  const std::uint64_t base_name_offset =
      static_cast<std::uint64_t>(base_parent) * config.max_names;
  const std::uint64_t child_name_offset =
      static_cast<std::uint64_t>(child) * config.max_names;
  for (int i = 0; i < meta.name_count; ++i)
    pointers.child_name_ids[child_name_offset + i] =
        pointers.program_name_ids[base_name_offset + i];

  const std::uint64_t base_const_offset =
      static_cast<std::uint64_t>(base_parent) * config.max_consts;
  const std::uint64_t child_const_offset =
      static_cast<std::uint64_t>(child) * config.max_consts;
  for (int i = 0; i < meta.const_count; ++i)
    pointers.child_consts[child_const_offset + i] =
        pointers.program_consts[base_const_offset + i];

  pointers.child_used_len[child] = meta.used_len;
  pointers.child_name_counts[child] = meta.name_count;
  pointers.child_const_counts[child] = meta.const_count;
  d_publish_compiled_child_meta(child, meta.used_len, pointers.child_meta);
  PackedChildSplice splice;
  splice.base_parent = base_parent;
  pointers.child_splices[child] = splice;
}

__device__ inline bool d_compiled_occurrences_are_valid(
    const DCandidateRange& candidate, int parent_len,
    const GpuReproConfig& config, const CompiledVariationPointers& pointers) {
  if (pointers.occurrences == nullptr || config.compiled_occurrence_count < 0 ||
      candidate.materialized_nodes <= 0 || candidate.occurrence_offset < 0 ||
      candidate.occurrence_count <= 0 ||
      candidate.occurrence_offset > config.compiled_occurrence_count ||
      candidate.occurrence_count >
          config.compiled_occurrence_count - candidate.occurrence_offset)
    return false;
  int previous_stop = 0;
  for (int i = 0; i < candidate.occurrence_count; ++i) {
    const CandidateOccurrence occurrence =
        pointers.occurrences[candidate.occurrence_offset + i];
    if (occurrence.start < previous_stop || occurrence.stop <= occurrence.start ||
        occurrence.stop > parent_len ||
        occurrence.stop - occurrence.start != candidate.materialized_nodes)
      return false;
    if (i == 0 && (occurrence.start != candidate.start ||
                   occurrence.stop != candidate.stop))
      return false;
    previous_stop = occurrence.stop;
  }
  return true;
}

__device__ inline bool d_run_compiled_crossover_child(
    int child, int base_parent, int source_parent, int destination_index,
    int source_index, AtomicSpliceOrigin* origins,
    const GpuReproConfig& config, const CompiledVariationPointers& pointers) {
  if (!d_compiled_parent_is_copyable(base_parent, config, pointers) ||
      !d_compiled_parent_is_copyable(source_parent, config, pointers))
    return false;
  const DPackedProgramMeta base_meta = pointers.metas[base_parent];
  const DPackedProgramMeta source_meta = pointers.metas[source_parent];
  if (destination_index < 0 || destination_index >= base_meta.candidate_count ||
      source_index < 0 || source_index >= source_meta.candidate_count)
    return false;

  const DCandidateRange destination = pointers.candidates[
      static_cast<std::uint64_t>(base_parent) * config.candidates_per_program +
      destination_index];
  const DCandidateRange source = pointers.candidates[
      static_cast<std::uint64_t>(source_parent) * config.candidates_per_program +
      source_index];
  if (!d_candidate_keys_compatible(destination, source,
                                   ReproductionContractMode::CompiledGrammar) ||
      !d_compiled_occurrences_are_valid(destination, base_meta.used_len, config,
                                        pointers) ||
      !d_compiled_occurrences_are_valid(source, source_meta.used_len, config,
                                        pointers) ||
      source.materialized_nodes != source.stop - source.start)
    return false;

  const std::uint64_t base_node_offset =
      static_cast<std::uint64_t>(base_parent) * config.max_nodes;
  const std::uint64_t source_node_offset =
      static_cast<std::uint64_t>(source_parent) * config.max_nodes;
  const std::uint64_t base_name_offset =
      static_cast<std::uint64_t>(base_parent) * config.max_names;
  const std::uint64_t source_name_offset =
      static_cast<std::uint64_t>(source_parent) * config.max_names;
  const std::uint64_t base_const_offset =
      static_cast<std::uint64_t>(base_parent) * config.max_consts;
  const std::uint64_t source_const_offset =
      static_cast<std::uint64_t>(source_parent) * config.max_consts;
  const std::uint64_t child_node_offset =
      static_cast<std::uint64_t>(child) * config.max_nodes;
  const std::uint64_t child_name_offset =
      static_cast<std::uint64_t>(child) * config.max_names;
  const std::uint64_t child_const_offset =
      static_cast<std::uint64_t>(child) * config.max_consts;

  PackedChildSplice descriptor;
  descriptor.base_parent = base_parent;
  descriptor.destination_candidate = destination_index;
  descriptor.source_kind = SpliceSourceKind::Parent;
  descriptor.source_index = source_parent;
  descriptor.source_candidate = source_index;
  descriptor.source_begin = source.start;
  descriptor.source_end = source.stop;
  const bool prepared = d_prepare_compiled_splice(
      pointers.program_nodes + base_node_offset, base_meta.used_len,
      pointers.program_name_ids + base_name_offset, base_meta.name_count,
      pointers.program_consts + base_const_offset, base_meta.const_count,
      pointers.occurrences + destination.occurrence_offset,
      destination.occurrence_count,
      pointers.program_nodes + source_node_offset, source_meta.used_len,
      source.start, source.stop,
      pointers.program_name_ids + source_name_offset, source_meta.name_count,
      pointers.program_consts + source_const_offset, source_meta.const_count,
      pointers.child_nodes + child_node_offset, origins, config.max_nodes,
      pointers.child_name_ids + child_name_offset, config.max_names,
      pointers.child_consts + child_const_offset, config.max_consts,
      pointers.child_used_len + child, pointers.child_name_counts + child,
      pointers.child_const_counts + child, descriptor,
      pointers.child_splices + child);
  if (prepared)
    d_publish_compiled_child_meta(child, pointers.child_used_len[child],
                                  pointers.child_meta);
  return prepared;
}

// One block owns one selected pair. Threads zero and one are independent serial
// control lanes, one per child; other lanes are intentionally idle.
__global__ void compiled_crossover_kernel(
    GpuReproConfig config, CompiledVariationPointers pointers) {
  const int pair = static_cast<int>(blockIdx.x);
  const int lane = static_cast<int>(threadIdx.x);
  if (pair >= config.pair_count || lane >= 2) return;
  if (config.contract_mode != ReproductionContractMode::CompiledGrammar ||
      config.max_nodes <= 0 || config.max_nodes > kGpuReproKernelMaxNodes ||
      config.max_names < 0 || config.max_consts < 0 ||
      config.candidates_per_program <= 0 ||
      pointers.program_nodes == nullptr || pointers.candidates == nullptr ||
      pointers.program_name_ids == nullptr || pointers.program_consts == nullptr ||
      pointers.parent_a == nullptr || pointers.parent_b == nullptr ||
      pointers.cand_a == nullptr || pointers.cand_b == nullptr ||
      pointers.child_nodes == nullptr || pointers.child_used_len == nullptr ||
      pointers.child_name_ids == nullptr || pointers.child_name_counts == nullptr ||
      pointers.child_consts == nullptr || pointers.child_const_counts == nullptr ||
      pointers.child_meta == nullptr || pointers.child_splices == nullptr)
    return;

  const int child = pair * 2 + lane;
  const int base_parent = lane == 0 ? pointers.parent_a[pair] : pointers.parent_b[pair];
  const int source_parent = lane == 0 ? pointers.parent_b[pair] : pointers.parent_a[pair];
  const int destination_index = lane == 0 ? pointers.cand_a[pair] : pointers.cand_b[pair];
  const int source_index = lane == 0 ? pointers.cand_b[pair] : pointers.cand_a[pair];
  if (!d_compiled_parent_is_copyable(base_parent, config, pointers)) return;

  __shared__ AtomicSpliceOrigin
      origin_storage[2 * kGpuReproKernelMaxNodes];
  if (!d_run_compiled_crossover_child(
          child, base_parent, source_parent, destination_index, source_index,
          origin_storage + lane * kGpuReproKernelMaxNodes, config, pointers)) {
    // d_prepare_compiled_splice is transactional only for published lengths;
    // overwrite every externally visible table to restore the exact base.
    d_copy_compiled_base_child(child, base_parent, config, pointers);
  }
}

}  // namespace gagp::evo::repro
