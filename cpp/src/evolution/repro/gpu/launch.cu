#include "internal.hpp"

#include <cuda_runtime.h>

#include <chrono>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#include "device/selection_kernels.cuh"
#include "device/compiled_variation.cuh"
#include "device/compiled_mutation.cuh"
#include "../constant_prep.hpp"

namespace gagp::evo::repro {

namespace {

double ms_between(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}

bool ensure_cuda(cudaError_t code, const char* what, std::string* message_out) {
  if (code == cudaSuccess) {
    return true;
  }
  if (message_out != nullptr) {
    std::ostringstream oss;
    oss << what << ": " << cudaGetErrorString(code);
    *message_out = oss.str();
  }
  return false;
}

}  // namespace

bool upload_gpu_repro_inputs(const PackedHostData& packed,
                             GpuReproArena* arena,
                             ReproductionStats* stats,
                             std::string* message_out) {
  if (!require_gpu_transport_config(packed.config, message_out)) return false;
  if (arena == nullptr || !gpu_repro_config_fits_capacity(packed.config, arena->capacity)) {
    if (message_out) *message_out = "gpu reproduction upload arena capacity mismatch";
    return false;
  }
  {
    const auto& c = packed.config;
    const std::size_t parents = static_cast<std::size_t>(c.population_size);
    const std::size_t donors = static_cast<std::size_t>(c.compiled_donor_count);
    const auto bad = [&]() {
      if (message_out) *message_out = "gpu reproduction compiled upload table shape mismatch";
      return false;
    };
    if (!packed.compiled_grammar || !packed.compiled_sources || !packed.constant_mutation ||
        packed.metas.size() != parents ||
        packed.program_nodes.size() != parents * c.max_nodes ||
        packed.program_name_ids.size() != parents * c.max_names ||
        packed.program_consts.size() != parents * c.max_consts ||
        packed.candidates.size() != parents * c.candidates_per_program ||
        packed.occurrences.size() != static_cast<std::size_t>(c.compiled_occurrence_count) ||
        packed.donor_nodes.size() != donors * c.max_donor_nodes ||
        packed.donor_name_ids.size() != donors * c.max_names ||
        packed.donor_consts.size() != donors * c.max_consts ||
        packed.donor_lens.size() != donors || packed.donor_name_counts.size() != donors ||
        packed.donor_const_counts.size() != donors || packed.donor_contracts.size() != donors ||
        packed.parent_constant_streams.size() != parents || packed.donor_constant_streams.size() != donors)
      return bad();
    const auto& constants = *packed.constant_mutation;
    if (!constants.grammar_domains ||
        constants.grammar_domains->grammar_owner != packed.compiled_grammar ||
        constants.grammar_domains->domains.size() != static_cast<std::size_t>(c.constant_domain_count) ||
        constants.grammar_domains->values.size() != static_cast<std::size_t>(c.constant_value_count) ||
        constants.groups.size() != static_cast<std::size_t>(c.constant_group_count) ||
        constants.node_group_origins.size() != static_cast<std::size_t>(c.constant_origin_count) ||
        constants.metadata_roots.size() != static_cast<std::size_t>(c.constant_root_count) ||
        constants.streams.size() != static_cast<std::size_t>(c.constant_stream_count))
      return bad();
    for (const auto& meta : packed.metas)
      if (meta.used_len <= 0 || meta.used_len > c.max_nodes ||
          meta.name_count < 0 || meta.name_count > c.max_names ||
          meta.const_count < 0 || meta.const_count > c.max_consts ||
          meta.candidate_count < 0 || meta.candidate_count > c.candidates_per_program ||
          meta.candidate_count > c.max_nodes)
        return bad();
    for (std::size_t i = 0; i < donors; ++i)
      if (packed.donor_lens[i] <= 0 || packed.donor_lens[i] > c.max_donor_nodes ||
          packed.donor_name_counts[i] < 0 || packed.donor_name_counts[i] > c.max_names ||
          packed.donor_const_counts[i] < 0 || packed.donor_const_counts[i] > c.max_consts)
        return bad();
  }
  const auto t0 = std::chrono::steady_clock::now();
  if (!ensure_cuda(cudaSetDevice(arena->device_id), "cudaSetDevice", message_out)) {
    return false;
  }
  const auto copy_to_device = [&](void* destination, const void* source,
                                  std::size_t bytes, const char* what) {
    return bytes == 0 || ensure_cuda(
        cudaMemcpy(destination, source, bytes, cudaMemcpyHostToDevice), what,
        message_out);
  };
  if (!copy_to_device(arena->d_program_nodes, packed.program_nodes.data(),
                      sizeof(PlainNode) * packed.program_nodes.size(), "cudaMemcpy program_nodes") ||
      !copy_to_device(arena->d_metas, packed.metas.data(),
                      sizeof(PackedProgramMeta) * packed.metas.size(), "cudaMemcpy metas") ||
      !copy_to_device(arena->d_candidates, packed.candidates.data(),
                      sizeof(CandidateRange) * packed.candidates.size(), "cudaMemcpy candidates") ||
      !copy_to_device(arena->d_occurrences, packed.occurrences.data(),
                      sizeof(CandidateOccurrence) * packed.occurrences.size(), "cudaMemcpy occurrences") ||
      !copy_to_device(arena->d_program_name_ids, packed.program_name_ids.data(),
                      sizeof(std::uint64_t) * packed.program_name_ids.size(), "cudaMemcpy program_name_ids") ||
      !copy_to_device(arena->d_program_consts, packed.program_consts.data(),
                      sizeof(Value) * packed.program_consts.size(), "cudaMemcpy program_consts") ||
      !copy_to_device(arena->d_donor_nodes, packed.donor_nodes.data(),
                      sizeof(PlainNode) * packed.donor_nodes.size(), "cudaMemcpy donor_nodes") ||
      !copy_to_device(arena->d_donor_lens, packed.donor_lens.data(),
                      sizeof(int) * packed.donor_lens.size(), "cudaMemcpy donor_lens") ||
      !copy_to_device(arena->d_donor_name_ids, packed.donor_name_ids.data(),
                      sizeof(std::uint64_t) * packed.donor_name_ids.size(), "cudaMemcpy donor_name_ids") ||
      !copy_to_device(arena->d_donor_name_counts, packed.donor_name_counts.data(),
                      sizeof(int) * packed.donor_name_counts.size(), "cudaMemcpy donor_name_counts") ||
      !copy_to_device(arena->d_donor_consts, packed.donor_consts.data(),
                      sizeof(Value) * packed.donor_consts.size(), "cudaMemcpy donor_consts") ||
      !copy_to_device(arena->d_donor_const_counts, packed.donor_const_counts.data(),
                      sizeof(int) * packed.donor_const_counts.size(), "cudaMemcpy donor_const_counts") ||
      !ensure_cuda(cudaMemset(arena->d_child_splices, 0,
                              sizeof(PackedChildSplice) *
                                  static_cast<std::size_t>(packed.config.pair_count) * 2),
                   "cudaMemset child_splices", message_out) ||
      !ensure_cuda(cudaDeviceSynchronize(), "cudaDeviceSynchronize upload", message_out)) {
    return false;
  }
  if (packed.constant_mutation) {
    const auto& constants = *packed.constant_mutation;
    if (arena->uploaded_domains != constants.grammar_domains) {
      // Validate finite values once per immutable owner/arena epoch, never per child.
      for (const auto& domain : constants.grammar_domains->domains) {
        ValueTag tag = ValueTag::Invalid;
        bool valid = constant_mutation_detail::expected_tag(domain.type, &tag);
        using Policy = grammar::ConstantMutationPolicy;
        if (domain.mutation == Policy::Keep && valid) continue;
        valid = valid && (domain.mutation == Policy::Resample ||
            domain.mutation == Policy::Add || (domain.mutation == Policy::Flip && tag == ValueTag::Bool));
        if (domain.mutation == Policy::Add) {
          valid = valid && ((domain.integer_range == 1 &&
              domain.delta.integer_minimum <= domain.delta.integer_maximum && domain.delta.gpu_grid_steps == 0) ||
              (domain.float_range == 1 && domain.float_quantization_scale == 0 &&
               std::isfinite(domain.delta.float_minimum) && std::isfinite(domain.delta.float_maximum) &&
               domain.delta.float_minimum <= domain.delta.float_maximum));
        }
        if ((domain.float_range != 0 && domain.float_range != 1) ||
            (domain.integer_range && domain.float_range)) {
          valid = false;
        } else if (domain.float_range == 1) {
          valid = valid && tag == ValueTag::Float && std::isfinite(domain.float_minimum) &&
              std::isfinite(domain.float_maximum) && domain.float_minimum <= domain.float_maximum &&
              grammar::valid_float_quantization(domain.float_minimum, domain.float_maximum,
                                                domain.float_quantization_scale);
        } else if (domain.integer_range == 1) {
          valid = valid && tag == ValueTag::Int && domain.minimum <= domain.maximum;
        } else if (domain.integer_range == 0) {
          valid = valid && domain.value_offset >= 0 && domain.value_count > 0 &&
              static_cast<std::size_t>(domain.value_offset) <= constants.grammar_domains->values.size() &&
              static_cast<std::size_t>(domain.value_count) <=
                  constants.grammar_domains->values.size() - domain.value_offset;
          bool yes = false, no = false;
          if (valid) for (int i = 0; i < domain.value_count; ++i) {
            const auto& value = constants.grammar_domains->values[domain.value_offset + i];
            valid = valid && value.tag == tag;
            if (tag == ValueTag::Bool) { if (value.b) yes = true; else no = true; }
          }
          if (domain.mutation == Policy::Flip) valid = valid && yes && no;
        } else valid = false;
        if (!valid) {
          if (message_out) *message_out = "GPU constant domain is invalid";
          return false;
        }
      }
      // On a partial upload failure the old stamp cannot describe these buffers.
      arena->uploaded_domains.reset();
      if (!copy_to_device(arena->d_constant_domains, constants.grammar_domains->domains.data(),
                          sizeof(ConstantMutationDomain) * constants.grammar_domains->domains.size(), "cudaMemcpy constant domains") ||
          !copy_to_device(arena->d_constant_values, constants.grammar_domains->values.data(),
                          sizeof(Value) * constants.grammar_domains->values.size(), "cudaMemcpy constant values"))
        return false;
      arena->uploaded_domains = constants.grammar_domains;
    }
    if (!copy_to_device(arena->d_constant_groups, constants.groups.data(),
                        sizeof(ConstantMutationGroup) * constants.groups.size(), "cudaMemcpy constant groups") ||
        !copy_to_device(arena->d_constant_origins, constants.node_group_origins.data(),
                        sizeof(int) * constants.node_group_origins.size(), "cudaMemcpy constant origins") ||
        !copy_to_device(arena->d_constant_roots, constants.metadata_roots.data(),
                        sizeof(int) * constants.metadata_roots.size(), "cudaMemcpy constant roots") ||
        !copy_to_device(arena->d_constant_streams, constants.streams.data(),
                        sizeof(ConstantMutationStream) * constants.streams.size(), "cudaMemcpy constant streams") ||
        !copy_to_device(arena->d_parent_constant_streams, packed.parent_constant_streams.data(),
                        sizeof(int) * packed.parent_constant_streams.size(), "cudaMemcpy parent constant streams") ||
        !copy_to_device(arena->d_donor_constant_streams, packed.donor_constant_streams.data(),
                        sizeof(int) * packed.donor_constant_streams.size(), "cudaMemcpy donor constant streams") ||
        !copy_to_device(arena->d_donor_contracts, packed.donor_contracts.data(),
                        sizeof(DonorContract) * packed.donor_contracts.size(), "cudaMemcpy donor contracts"))
      return false;
  }
  const auto t1 = std::chrono::steady_clock::now();
  if (stats != nullptr) {
    stats->upload_ms += ms_between(t0, t1);
  }
  return true;
}

bool launch_gpu_repro_kernels(GpuReproArena* arena,
                              const GpuReproConfig& config,
                              const std::vector<double>& fitness,
                              ReproductionStats* stats,
                              std::string* message_out) {
  if (!require_gpu_transport_config(config, message_out)) return false;
  if (arena == nullptr || !gpu_repro_config_fits_capacity(config, arena->capacity) ||
      fitness.size() != static_cast<std::size_t>(config.population_size)) {
    if (message_out) *message_out = "gpu reproduction launch input or capacity mismatch";
    return false;
  }
  const auto upload_t0 = std::chrono::steady_clock::now();
  if (!ensure_cuda(cudaSetDevice(arena->device_id), "cudaSetDevice", message_out)) {
    return false;
  }
  const bool mutation_pass = config.compiled_pass == CompiledVariationPass::Mutation;
  if (!mutation_pass && !ensure_cuda(cudaMemcpy(arena->d_fitness, fitness.data(),
                              sizeof(double) * fitness.size(), cudaMemcpyHostToDevice),
                   "cudaMemcpy fitness", message_out)) {
    return false;
  }
  const auto upload_t1 = std::chrono::steady_clock::now();
  if (stats != nullptr) {
    stats->upload_ms += ms_between(upload_t0, upload_t1);
  }

  const int select_threads = 256;
  const int select_blocks = (config.pair_count + select_threads - 1) / select_threads;
  const auto select_t0 = std::chrono::steady_clock::now();
  if (!mutation_pass) {
    tournament_select_kernel<<<select_blocks, select_threads>>>(
        arena->d_fitness, arena->d_candidates, config.population_size, config.pair_count,
        config.candidates_per_program, config.tournament_k, config.seed, arena->d_parent_a,
        arena->d_parent_b, arena->d_cand_a, arena->d_cand_b, arena->d_metas,
        arena->d_selection_counters);
    if (!ensure_cuda(cudaGetLastError(), "tournament_select_kernel", message_out) ||
        !ensure_cuda(cudaDeviceSynchronize(), "cudaDeviceSynchronize selection", message_out)) {
      return false;
    }
  }
  const auto select_t1 = std::chrono::steady_clock::now();

  const auto variation_t0 = std::chrono::steady_clock::now();
  {
    CompiledVariationPointers pointers;
    pointers.program_nodes = arena->d_program_nodes;
    pointers.metas = arena->d_metas;
    pointers.candidates = arena->d_candidates;
    pointers.occurrences = arena->d_occurrences;
    pointers.program_name_ids = arena->d_program_name_ids;
    pointers.program_consts = arena->d_program_consts;
    pointers.parent_a = arena->d_parent_a; pointers.parent_b = arena->d_parent_b;
    pointers.cand_a = arena->d_cand_a; pointers.cand_b = arena->d_cand_b;
    pointers.child_nodes = arena->d_child_nodes;
    pointers.child_used_len = arena->d_child_used_len;
    pointers.child_name_ids = arena->d_child_name_ids;
    pointers.child_name_counts = arena->d_child_name_counts;
    pointers.child_consts = arena->d_child_consts;
    pointers.child_const_counts = arena->d_child_const_counts;
    pointers.child_meta = arena->d_child_meta;
    pointers.child_splices = arena->d_child_splices;
    if (mutation_pass) {
      CompiledMutationPointers mutation;
      mutation.common = pointers;
      mutation.donor_nodes = arena->d_donor_nodes;
      mutation.donor_lens = arena->d_donor_lens;
      mutation.donor_name_ids = arena->d_donor_name_ids;
      mutation.donor_name_counts = arena->d_donor_name_counts;
      mutation.donor_consts = arena->d_donor_consts;
      mutation.donor_const_counts = arena->d_donor_const_counts;
      mutation.donor_contracts = arena->d_donor_contracts;
      mutation.constant_domains = arena->d_constant_domains;
      mutation.constant_values = arena->d_constant_values;
      mutation.constant_groups = arena->d_constant_groups;
      mutation.domains_validated = arena->uploaded_domains != nullptr;
      mutation.constant_origins = arena->d_constant_origins;
      mutation.constant_streams = arena->d_constant_streams;
      mutation.parent_constant_streams = arena->d_parent_constant_streams;
      mutation.metadata_roots = arena->d_constant_roots;
      compiled_mutation_kernel<<<config.pair_count, 32,
          2 * config.max_nodes * sizeof(AtomicSpliceOrigin)>>>(config, mutation);
    } else {
      compiled_crossover_kernel<<<config.pair_count, 32,
          2 * config.max_nodes * sizeof(AtomicSpliceOrigin)>>>(config, pointers);
    }
  }
  if (!ensure_cuda(cudaGetLastError(), "variation_kernel", message_out) ||
      !ensure_cuda(cudaDeviceSynchronize(), "cudaDeviceSynchronize variation", message_out)) {
    return false;
  }
  const auto variation_t1 = std::chrono::steady_clock::now();
  if (stats != nullptr) {
    const double selection_ms = mutation_pass ? 0.0 : ms_between(select_t0, select_t1);
    stats->selection_kernel_ms += selection_ms;
    stats->variation_kernel_ms += ms_between(variation_t0, variation_t1);
    stats->kernel_ms += selection_ms +
                        ms_between(variation_t0, variation_t1);
  }
  return true;
}

}  // namespace gagp::evo::repro
