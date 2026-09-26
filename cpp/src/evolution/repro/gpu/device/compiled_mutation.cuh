#pragma once

#include <cstdint>
#include "gagp/evolution/repro/mutation_schedule.hpp"

#include "compiled_variation.cuh"
#include "constant_mutation.cuh"

namespace gagp::evo::repro {

// Compact launch contract for the second compiled-grammar variation pass.
// `common` describes the analyzed crossover children as input programs and a
// separate output child table. Mutation uses the input index directly; it does
// not perform selection.
struct CompiledMutationPointers {
  CompiledVariationPointers common;

  const DPlainNode* donor_nodes = nullptr;
  const int* donor_lens = nullptr;
  const std::uint64_t* donor_name_ids = nullptr;
  const int* donor_name_counts = nullptr;
  const Value* donor_consts = nullptr;
  const int* donor_const_counts = nullptr;
  const DonorContract* donor_contracts = nullptr;

  const ConstantMutationDomain* constant_domains = nullptr;
  const Value* constant_values = nullptr;
  const ConstantMutationGroup* constant_groups = nullptr;
  const int* constant_origins = nullptr;
  const ConstantMutationStream* constant_streams = nullptr;
  const int* parent_constant_streams = nullptr;
  const int* metadata_roots = nullptr;
  bool domains_validated = false;
};

namespace compiled_mutation_detail {

__device__ inline double unit_random(DGrammarRandom* random) {
  return compiled_mutation_unit(random);
}

__device__ inline std::uint64_t child_seed(const GpuReproConfig& config, int child) {
  return compiled_mutation_seed(config.seed, child);
}

__device__ inline void set_outcome(int child, int parent,
                                   CompiledMutationOutcome outcome,
                                   PackedChildSplice* splices) {
  PackedChildSplice splice;
  splice.mutation_outcome = outcome;
  splice.base_parent = parent;
  splices[child] = splice;
}

__device__ inline bool valid_stream(
    int parent, const GpuReproConfig& config,
    const CompiledMutationPointers& pointers, ConstantMutationStream* stream) {
  if (pointers.parent_constant_streams == nullptr ||
      pointers.constant_streams == nullptr || stream == nullptr)
    return false;
  const int stream_index = pointers.parent_constant_streams[parent];
  if (stream_index < 0 || stream_index >= config.constant_stream_count)
    return false;
  *stream = pointers.constant_streams[stream_index];
  return stream->node_origin_offset >= 0 && stream->node_count >= 0 &&
         stream->node_origin_offset <= config.constant_origin_count &&
         stream->node_count <=
             config.constant_origin_count - stream->node_origin_offset &&
         stream->metadata_root_offset >= 0 &&
         stream->metadata_root_count >= 0 &&
         stream->metadata_root_count <= kGpuReproMaxConsts &&
         stream->metadata_root_offset <= config.constant_root_count &&
         stream->metadata_root_count <=
             config.constant_root_count - stream->metadata_root_offset;
}

__device__ inline CompiledMutationOutcome run_constant(
    int child, int parent, std::uint64_t seed, AtomicSpliceOrigin* origins,
    Value* constant_work, int* remapped_roots, const GpuReproConfig& config,
    const CompiledMutationPointers& pointers) {
  const DPackedProgramMeta meta = pointers.common.metas[parent];
  ConstantMutationStream stream;
  if (!valid_stream(parent, config, pointers, &stream) ||
      stream.node_count != meta.used_len ||
      (stream.node_count > 0 && pointers.constant_origins == nullptr) ||
      (stream.metadata_root_count > 0 && pointers.metadata_roots == nullptr))
    return CompiledMutationOutcome::Invalid;

  const std::uint64_t node_offset =
      static_cast<std::uint64_t>(child) * config.max_nodes;
  const std::uint64_t const_offset =
      static_cast<std::uint64_t>(child) * config.max_consts;
  for (int i = 0; i < meta.used_len; ++i) origins[i] = {i, -1};

  const DConstantMutationResult result = d_mutate_compiled_constants(
      pointers.common.child_nodes + node_offset, meta.used_len, origins,
      pointers.common.child_consts + const_offset,
      pointers.common.child_const_counts + child, config.max_consts,
      constant_work,
      {pointers.constant_domains, config.constant_domain_count,
       pointers.constant_values, config.constant_value_count,
       pointers.constant_groups, config.constant_group_count, pointers.domains_validated},
      {pointers.constant_origins + stream.node_origin_offset,
       stream.node_count},
      {},
      {stream.metadata_root_count == 0
           ? nullptr
           : pointers.metadata_roots + stream.metadata_root_offset,
       stream.metadata_root_count == 0 ? nullptr : remapped_roots,
       stream.metadata_root_count},
      seed);
  switch (result) {
    case DConstantMutationResult::Applied:
      return CompiledMutationOutcome::Constant;
    case DConstantMutationResult::NoGroups:
      return CompiledMutationOutcome::NoSite;
    case DConstantMutationResult::Capacity:
      return CompiledMutationOutcome::Capacity;
    default:
      return CompiledMutationOutcome::Invalid;
  }
}

__device__ inline bool valid_donor_contract(
    const DCandidateRange& destination, int donor,
    const GpuReproConfig& config, const CompiledMutationPointers& pointers,
    int* donor_len) {
  if (donor < 0 || donor >= config.compiled_donor_count ||
      pointers.donor_lens == nullptr || pointers.donor_contracts == nullptr ||
      donor_len == nullptr)
    return false;
  const int length = pointers.donor_lens[donor];
  const DonorContract contract = pointers.donor_contracts[donor];
  if (length <= 0 || length > config.max_donor_nodes ||
      contract.compatibility_id != destination.compatibility_id ||
      contract.materialized_nodes != length ||
      contract.materialized_depth <= 0 || contract.template_nesting < 0 ||
      destination.replacement_max_nodes <= 0 ||
      destination.replacement_max_depth <= 0 ||
      destination.remaining_template_nesting < 0 ||
      length > destination.replacement_max_nodes ||
      contract.materialized_depth > destination.replacement_max_depth ||
      contract.template_nesting > destination.remaining_template_nesting)
    return false;
  *donor_len = length;
  return true;
}

__device__ inline CompiledMutationOutcome run_subtree(
    int child, int parent, DGrammarRandom* random, AtomicSpliceOrigin* origins,
    const GpuReproConfig& config, const CompiledMutationPointers& pointers) {
  const DPackedProgramMeta meta = pointers.common.metas[parent];
  if (meta.candidate_count == 0) return CompiledMutationOutcome::NoSite;
  const int candidate_index = static_cast<int>(
      random->bounded(static_cast<std::uint64_t>(meta.candidate_count)));
  const std::uint64_t candidate_offset =
      static_cast<std::uint64_t>(parent) * config.candidates_per_program +
      candidate_index;
  const DCandidateRange destination =
      pointers.common.candidates[candidate_offset];
  if (!d_candidate_is_valid(destination) ||
      !d_compiled_occurrences_are_valid(destination, meta.used_len, config,
                                        pointers.common) ||
      destination.donor_offset < 0 || destination.donor_count < 0 ||
      destination.donor_offset > config.compiled_donor_count ||
      destination.donor_count >
          config.compiled_donor_count - destination.donor_offset)
    return CompiledMutationOutcome::Invalid;
  if (destination.donor_count == 0) return CompiledMutationOutcome::NoDonor;

  const int donor = destination.donor_offset + static_cast<int>(
      random->bounded(static_cast<std::uint64_t>(destination.donor_count)));
  int donor_len = 0;
  if (!valid_donor_contract(destination, donor, config, pointers, &donor_len))
    return CompiledMutationOutcome::Invalid;
  if (pointers.donor_nodes == nullptr || pointers.donor_name_ids == nullptr ||
      pointers.donor_name_counts == nullptr || pointers.donor_consts == nullptr ||
      pointers.donor_const_counts == nullptr)
    return CompiledMutationOutcome::Invalid;
  const int donor_name_count = pointers.donor_name_counts[donor];
  const int donor_const_count = pointers.donor_const_counts[donor];
  if (donor_name_count < 0 || donor_name_count > config.max_names ||
      donor_const_count < 0 || donor_const_count > config.max_consts)
    return CompiledMutationOutcome::Invalid;

  const long long removed =
      static_cast<long long>(destination.materialized_nodes) *
      destination.occurrence_count;
  const long long inserted =
      static_cast<long long>(donor_len) * destination.occurrence_count;
  const long long resulting =
      static_cast<long long>(meta.used_len) - removed + inserted;
  if (resulting < 0 || resulting > config.max_nodes)
    return CompiledMutationOutcome::Capacity;

  const std::uint64_t base_node_offset =
      static_cast<std::uint64_t>(parent) * config.max_nodes;
  const std::uint64_t base_name_offset =
      static_cast<std::uint64_t>(parent) * config.max_names;
  const std::uint64_t base_const_offset =
      static_cast<std::uint64_t>(parent) * config.max_consts;
  const std::uint64_t donor_node_offset =
      static_cast<std::uint64_t>(donor) * config.max_donor_nodes;
  const std::uint64_t donor_name_offset =
      static_cast<std::uint64_t>(donor) * config.max_names;
  const std::uint64_t donor_const_offset =
      static_cast<std::uint64_t>(donor) * config.max_consts;
  const std::uint64_t child_node_offset =
      static_cast<std::uint64_t>(child) * config.max_nodes;
  const std::uint64_t child_name_offset =
      static_cast<std::uint64_t>(child) * config.max_names;
  const std::uint64_t child_const_offset =
      static_cast<std::uint64_t>(child) * config.max_consts;

  PackedChildSplice descriptor;
  descriptor.mutation_outcome = CompiledMutationOutcome::Subtree;
  descriptor.base_parent = parent;
  descriptor.destination_candidate = candidate_index;
  descriptor.source_kind = SpliceSourceKind::CompiledDonor;
  descriptor.source_index = donor;
  descriptor.source_candidate = -1;
  descriptor.source_begin = 0;
  descriptor.source_end = donor_len;
  if (!d_prepare_compiled_splice(
          pointers.common.program_nodes + base_node_offset, meta.used_len,
          pointers.common.program_name_ids + base_name_offset, meta.name_count,
          pointers.common.program_consts + base_const_offset, meta.const_count,
          pointers.common.occurrences + destination.occurrence_offset,
          destination.occurrence_count,
          pointers.donor_nodes + donor_node_offset, donor_len, 0, donor_len,
          pointers.donor_name_ids + donor_name_offset, donor_name_count,
          pointers.donor_consts + donor_const_offset, donor_const_count,
          pointers.common.child_nodes + child_node_offset, origins,
          config.max_nodes,
          pointers.common.child_name_ids + child_name_offset, config.max_names,
          pointers.common.child_consts + child_const_offset, config.max_consts,
          pointers.common.child_used_len + child,
          pointers.common.child_name_counts + child,
          pointers.common.child_const_counts + child, descriptor,
          pointers.common.child_splices + child))
    return CompiledMutationOutcome::Capacity;
  d_publish_compiled_child_meta(child, pointers.common.child_used_len[child],
                                pointers.common.child_meta);
  return CompiledMutationOutcome::Subtree;
}

}  // namespace compiled_mutation_detail

// One block owns two sequential input indices. For an odd population the last
// lane copies the last real input into the padded output slot without mutation.
__global__ void compiled_mutation_kernel(
    GpuReproConfig config, CompiledMutationPointers pointers) {
  using namespace compiled_mutation_detail;
  const int pair = static_cast<int>(blockIdx.x);
  const int lane = static_cast<int>(threadIdx.x);
  if (pair >= config.pair_count || lane >= 2) return;
  if (config.compiled_pass != CompiledVariationPass::Mutation ||
      config.population_size <= 0 || config.max_nodes <= 0 ||
      config.max_nodes > kGpuReproKernelMaxNodes || config.max_names < 0 ||
      config.max_consts < 0 || config.max_consts > kGpuReproMaxConsts ||
      config.max_names > kGpuReproMaxNames || config.candidates_per_program <= 0 ||
      config.compiled_donor_count < 0 || config.compiled_occurrence_count < 0 ||
      config.constant_domain_count < 0 || config.constant_value_count < 0 ||
      config.constant_group_count < 0 || config.constant_origin_count < 0 ||
      config.constant_stream_count < 0 || config.constant_root_count < 0 ||
      config.mutation_ratio < 0.0 || config.mutation_ratio > 1.0 ||
      config.mutation_subtree_ratio < 0.0 ||
      config.mutation_subtree_ratio > 1.0 ||
      pointers.common.program_nodes == nullptr ||
      pointers.common.metas == nullptr || pointers.common.candidates == nullptr ||
      (config.compiled_occurrence_count > 0 && pointers.common.occurrences == nullptr) ||
      pointers.common.program_name_ids == nullptr ||
      pointers.common.program_consts == nullptr ||
      pointers.common.child_nodes == nullptr ||
      pointers.common.child_used_len == nullptr ||
      pointers.common.child_name_ids == nullptr ||
      pointers.common.child_name_counts == nullptr ||
      pointers.common.child_consts == nullptr ||
      pointers.common.child_const_counts == nullptr ||
      pointers.common.child_meta == nullptr ||
      pointers.common.child_splices == nullptr)
    return;

  const int child = pair * 2 + lane;
  const int parent = child < config.population_size
                         ? child
                         : config.population_size - 1;
  if (!d_compiled_parent_is_copyable(parent, config, pointers.common)) return;
  d_copy_compiled_base_child(child, parent, config, pointers.common);
  if (child >= config.population_size) return;

  DGrammarRandom random(child_seed(config, child));
  if (unit_random(&random) >= config.mutation_ratio) return;

  extern __shared__ AtomicSpliceOrigin origin_storage[];
  __shared__ __align__(16) unsigned char constant_work_storage[
      2 * kGpuReproMaxConsts * sizeof(Value)];
  __shared__ int remapped_roots[2 * kGpuReproMaxConsts];
  AtomicSpliceOrigin* origins =
      origin_storage + lane * config.max_nodes;
  Value* constant_work = reinterpret_cast<Value*>(constant_work_storage) +
                         lane * kGpuReproMaxConsts;

  CompiledMutationOutcome outcome = CompiledMutationOutcome::NoSite;
  const bool choose_subtree =
      unit_random(&random) < config.mutation_subtree_ratio;
  if (!choose_subtree) {
    outcome = run_constant(
        child, parent, random.next(), origins,
        constant_work,
        remapped_roots + lane * kGpuReproMaxConsts, config, pointers);
    if (outcome == CompiledMutationOutcome::Constant) {
      PackedChildSplice splice;
      splice.mutation_outcome = CompiledMutationOutcome::Constant;
      splice.base_parent = parent;
      pointers.common.child_splices[child] = splice;
      d_publish_compiled_child_meta(child,
                                    pointers.common.child_used_len[child],
                                    pointers.common.child_meta);
      return;
    }
    if (outcome != CompiledMutationOutcome::NoSite) {
      d_copy_compiled_base_child(child, parent, config, pointers.common);
      set_outcome(child, parent, outcome, pointers.common.child_splices);
      return;
    }
    // A constant branch with no eligible groups uses subtree mutation.
  }

  outcome = run_subtree(child, parent, &random, origins, config, pointers);
  if (outcome == CompiledMutationOutcome::Subtree) return;
  d_copy_compiled_base_child(child, parent, config, pointers.common);
  set_outcome(child, parent, outcome, pointers.common.child_splices);
}

}  // namespace gagp::evo::repro
