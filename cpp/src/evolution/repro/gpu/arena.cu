#include "internal.hpp"

#include <cuda_runtime.h>

#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <string>

namespace gagp::evo::repro {

bool initialize_gpu_repro_runtime(std::string* message_out) {
  int device_count = 0;
  const cudaError_t err = cudaGetDeviceCount(&device_count);
  if (err != cudaSuccess) {
    if (message_out != nullptr) {
      *message_out = "cuda runtime initialization failed: ";
      *message_out += cudaGetErrorString(err);
    }
    return false;
  }
  return true;
}

namespace {

bool set_error(cudaError_t code, const char* what, std::string* message_out) {
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

bool query_device(int dev, std::string* message_out) {
  return set_error(cudaSetDevice(dev), "cudaSetDevice", message_out);
}

bool parse_env_device_override(int* out_dev) {
  const char* raw = std::getenv("GAGP_CUDA_DEVICE");
  if (raw == nullptr || *raw == '\0') {
    return false;
  }
  char* end = nullptr;
  const long parsed = std::strtol(raw, &end, 10);
  if (end == raw || *end != '\0' || parsed < 0 || parsed > std::numeric_limits<int>::max()) {
    return false;
  }
  *out_dev = static_cast<int>(parsed);
  return true;
}

}  // namespace

bool gpu_repro_config_fits_capacity(const GpuReproConfig& need, const GpuReproConfig& have) {
  return have.population_size >= need.population_size &&
         have.pair_count >= need.pair_count &&
         have.candidates_per_program >= need.candidates_per_program &&
         have.donor_pool_size_per_type >= need.donor_pool_size_per_type &&
         have.compiled_donor_count >= need.compiled_donor_count &&
         have.compiled_occurrence_count >= need.compiled_occurrence_count &&
         have.constant_domain_count >= need.constant_domain_count &&
         have.constant_value_count >= need.constant_value_count &&
         have.constant_group_count >= need.constant_group_count &&
         have.constant_origin_count >= need.constant_origin_count &&
         have.constant_root_count >= need.constant_root_count &&
         have.constant_stream_count >= need.constant_stream_count &&
         have.max_nodes >= need.max_nodes &&
         have.max_donor_nodes >= need.max_donor_nodes &&
         have.max_names >= need.max_names &&
         have.max_consts >= need.max_consts;
}

bool select_gpu_repro_device(int* device_id, std::string* message_out) {
  int device_count = 0;
  const cudaError_t count_err = cudaGetDeviceCount(&device_count);
  if (count_err != cudaSuccess || device_count <= 0) {
    if (message_out != nullptr) {
      *message_out = "cuda device unavailable";
      if (count_err != cudaSuccess) {
        *message_out += " err=";
        *message_out += cudaGetErrorString(count_err);
      }
    }
    return false;
  }

  int override_dev = -1;
  if (parse_env_device_override(&override_dev)) {
    if (override_dev >= device_count) {
      if (message_out != nullptr) {
        *message_out = "cuda device override out of range";
      }
      return false;
    }
    if (!query_device(override_dev, message_out)) {
      return false;
    }
    *device_id = override_dev;
    return true;
  }

  int best_dev = -1;
  double best_used_ratio = 2.0;
  std::size_t best_used_bytes = std::numeric_limits<std::size_t>::max();
  for (int dev = 0; dev < device_count; ++dev) {
    if (cudaSetDevice(dev) != cudaSuccess) {
      continue;
    }
    std::size_t free_bytes = 0;
    std::size_t total_bytes = 0;
    if (cudaMemGetInfo(&free_bytes, &total_bytes) != cudaSuccess || total_bytes == 0) {
      continue;
    }
    const std::size_t used_bytes = total_bytes - free_bytes;
    const double used_ratio = static_cast<double>(used_bytes) / static_cast<double>(total_bytes);
    if (best_dev < 0 || used_ratio < best_used_ratio ||
        (std::fabs(used_ratio - best_used_ratio) <= 1e-12 && used_bytes < best_used_bytes)) {
      best_dev = dev;
      best_used_ratio = used_ratio;
      best_used_bytes = used_bytes;
    }
  }
  if (best_dev < 0) {
    if (message_out != nullptr) {
      *message_out = "cuda device unavailable err=no usable device found";
    }
    return false;
  }
  if (!query_device(best_dev, message_out)) {
    return false;
  }
  *device_id = best_dev;
  return true;
}

void destroy_gpu_repro_arena(GpuReproArena* arena) {
  if (arena == nullptr) {
    return;
  }
  if (arena->device_id >= 0) cudaSetDevice(arena->device_id);
  if (arena->d_program_nodes) cudaFree(arena->d_program_nodes);
  if (arena->d_metas) cudaFree(arena->d_metas);
  if (arena->d_candidates) cudaFree(arena->d_candidates);
  if (arena->d_occurrences) cudaFree(arena->d_occurrences);
  if (arena->d_donor_contracts) cudaFree(arena->d_donor_contracts);
  if (arena->d_constant_domains) cudaFree(arena->d_constant_domains);
  if (arena->d_constant_values) cudaFree(arena->d_constant_values);
  if (arena->d_constant_groups) cudaFree(arena->d_constant_groups);
  if (arena->d_constant_origins) cudaFree(arena->d_constant_origins);
  if (arena->d_constant_roots) cudaFree(arena->d_constant_roots);
  if (arena->d_constant_streams) cudaFree(arena->d_constant_streams);
  if (arena->d_parent_constant_streams) cudaFree(arena->d_parent_constant_streams);
  if (arena->d_donor_constant_streams) cudaFree(arena->d_donor_constant_streams);

  if (arena->d_program_name_ids) cudaFree(arena->d_program_name_ids);
  if (arena->d_program_consts) cudaFree(arena->d_program_consts);
  if (arena->d_donor_nodes) cudaFree(arena->d_donor_nodes);
  if (arena->d_donor_lens) cudaFree(arena->d_donor_lens);
  if (arena->d_donor_name_ids) cudaFree(arena->d_donor_name_ids);
  if (arena->d_donor_name_counts) cudaFree(arena->d_donor_name_counts);
  if (arena->d_donor_consts) cudaFree(arena->d_donor_consts);
  if (arena->d_donor_const_counts) cudaFree(arena->d_donor_const_counts);
  if (arena->d_fitness) cudaFree(arena->d_fitness);
  if (arena->d_selection_counters) cudaFree(arena->d_selection_counters);
  if (arena->d_parent_a) cudaFree(arena->d_parent_a);
  if (arena->d_parent_b) cudaFree(arena->d_parent_b);
  if (arena->d_cand_a) cudaFree(arena->d_cand_a);
  if (arena->d_cand_b) cudaFree(arena->d_cand_b);
  if (arena->d_child_nodes) cudaFree(arena->d_child_nodes);
  if (arena->d_child_used_len) cudaFree(arena->d_child_used_len);
  if (arena->d_child_name_ids) cudaFree(arena->d_child_name_ids);
  if (arena->d_child_name_counts) cudaFree(arena->d_child_name_counts);
  if (arena->d_child_consts) cudaFree(arena->d_child_consts);
  if (arena->d_child_const_counts) cudaFree(arena->d_child_const_counts);
  if (arena->d_child_meta) cudaFree(arena->d_child_meta);
  if (arena->d_child_splices) cudaFree(arena->d_child_splices);
  if (arena->d_child_node_offsets) cudaFree(arena->d_child_node_offsets);
  if (arena->d_child_name_offsets) cudaFree(arena->d_child_name_offsets);
  if (arena->d_child_const_offsets) cudaFree(arena->d_child_const_offsets);
  if (arena->d_live_child_nodes) cudaFree(arena->d_live_child_nodes);
  if (arena->d_live_child_name_ids) cudaFree(arena->d_live_child_name_ids);
  if (arena->d_live_child_consts) cudaFree(arena->d_live_child_consts);
  *arena = GpuReproArena{};
}

namespace {

bool allocate_gpu_arena(GpuReproArena* arena,
                              const GpuReproConfig& config,
                              std::string* message_out) {
  if (arena == nullptr) {
    if (message_out != nullptr) {
      *message_out = "gpu reproduction arena is null";
    }
    return false;
  }
  if (!query_device(arena->device_id, message_out)) {
    return false;
  }
  auto alloc = [&](auto** ptr, std::size_t bytes, const char* what) {
    if (bytes == 0) {
      *ptr = nullptr;
      return true;
    }
    return set_error(cudaMalloc(reinterpret_cast<void**>(ptr), bytes), what, message_out);
  };
  const std::size_t total_donor_count = static_cast<std::size_t>(config.compiled_donor_count);
  const std::size_t population_count = static_cast<std::size_t>(config.population_size);
  const std::size_t candidate_count = population_count *
      static_cast<std::size_t>(config.candidates_per_program);
  const std::size_t child_count = static_cast<std::size_t>(config.pair_count) * 2;
  if (!alloc(&arena->d_program_nodes,
             sizeof(PlainNode) * population_count * static_cast<std::size_t>(config.max_nodes),
             "cudaMalloc program_nodes") ||
      !alloc(&arena->d_metas,
             sizeof(PackedProgramMeta) * population_count,
             "cudaMalloc metas") ||
      !alloc(&arena->d_candidates,
             sizeof(CandidateRange) * candidate_count,
             "cudaMalloc candidates") ||
      !alloc(&arena->d_occurrences,
             sizeof(CandidateOccurrence) *
                 static_cast<std::size_t>(config.compiled_occurrence_count),
             "cudaMalloc occurrences") ||
      !alloc(&arena->d_donor_contracts, sizeof(DonorContract) * static_cast<std::size_t>(config.compiled_donor_count),
             "cudaMalloc donor_contracts") ||
      !alloc(&arena->d_constant_domains, sizeof(ConstantMutationDomain) * static_cast<std::size_t>(config.constant_domain_count),
             "cudaMalloc constant_domains") ||
      !alloc(&arena->d_constant_values, sizeof(Value) * static_cast<std::size_t>(config.constant_value_count),
             "cudaMalloc constant_values") ||
      !alloc(&arena->d_constant_groups, sizeof(ConstantMutationGroup) * static_cast<std::size_t>(config.constant_group_count),
             "cudaMalloc constant_groups") ||
      !alloc(&arena->d_constant_origins, sizeof(int) * static_cast<std::size_t>(config.constant_origin_count),
             "cudaMalloc constant_origins") ||
      !alloc(&arena->d_constant_roots, sizeof(int) * static_cast<std::size_t>(config.constant_root_count),
             "cudaMalloc constant_roots") ||
      !alloc(&arena->d_constant_streams, sizeof(ConstantMutationStream) * static_cast<std::size_t>(config.constant_stream_count),
             "cudaMalloc constant_streams") ||
      !alloc(&arena->d_parent_constant_streams,
             sizeof(int) * population_count,
             "cudaMalloc parent_constant_streams") ||
      !alloc(&arena->d_donor_constant_streams,
             sizeof(int) * static_cast<std::size_t>(config.compiled_donor_count),
             "cudaMalloc donor_constant_streams") ||
      !alloc(&arena->d_program_name_ids,
             sizeof(std::uint64_t) * population_count * static_cast<std::size_t>(config.max_names),
             "cudaMalloc program_name_ids") ||
      !alloc(&arena->d_program_consts,
             sizeof(Value) * population_count * static_cast<std::size_t>(config.max_consts),
             "cudaMalloc program_consts") ||
      !alloc(&arena->d_donor_nodes,
             sizeof(PlainNode) * (total_donor_count * static_cast<std::size_t>(config.max_donor_nodes)),
             "cudaMalloc donor_nodes") ||
      !alloc(&arena->d_donor_lens,
             sizeof(int) * total_donor_count,
             "cudaMalloc donor_lens") ||
      !alloc(&arena->d_donor_name_ids,
             sizeof(std::uint64_t) * (total_donor_count * static_cast<std::size_t>(config.max_names)),
             "cudaMalloc donor_name_ids") ||
      !alloc(&arena->d_donor_name_counts,
             sizeof(int) * total_donor_count,
             "cudaMalloc donor_name_counts") ||
      !alloc(&arena->d_donor_consts,
             sizeof(Value) * (total_donor_count * static_cast<std::size_t>(config.max_consts)),
             "cudaMalloc donor_consts") ||
      !alloc(&arena->d_donor_const_counts,
             sizeof(int) * total_donor_count,
             "cudaMalloc donor_const_counts") ||
      !alloc(&arena->d_fitness,
             sizeof(double) * population_count,
             "cudaMalloc fitness") ||
      !alloc(&arena->d_selection_counters,
             sizeof(PackedSelectionCounters) * static_cast<std::size_t>(config.pair_count),
             "cudaMalloc selection_counters") ||
      !alloc(&arena->d_parent_a,
             sizeof(int) * static_cast<std::size_t>(config.pair_count),
             "cudaMalloc parent_a") ||
      !alloc(&arena->d_parent_b,
             sizeof(int) * static_cast<std::size_t>(config.pair_count),
             "cudaMalloc parent_b") ||
      !alloc(&arena->d_cand_a,
             sizeof(int) * static_cast<std::size_t>(config.pair_count),
             "cudaMalloc cand_a") ||
      !alloc(&arena->d_cand_b,
             sizeof(int) * static_cast<std::size_t>(config.pair_count),
             "cudaMalloc cand_b") ||
      !alloc(&arena->d_child_nodes,
             sizeof(PlainNode) * child_count * static_cast<std::size_t>(config.max_nodes),
             "cudaMalloc child_nodes") ||
      !alloc(&arena->d_child_used_len,
             sizeof(int) * child_count,
             "cudaMalloc child_used_len") ||
      !alloc(&arena->d_child_name_ids,
             sizeof(std::uint64_t) * child_count * static_cast<std::size_t>(config.max_names),
             "cudaMalloc child_name_ids") ||
      !alloc(&arena->d_child_name_counts,
             sizeof(int) * child_count,
             "cudaMalloc child_name_counts") ||
      !alloc(&arena->d_child_consts,
             sizeof(Value) * child_count * static_cast<std::size_t>(config.max_consts),
             "cudaMalloc child_consts") ||
      !alloc(&arena->d_child_const_counts,
             sizeof(int) * child_count,
             "cudaMalloc child_const_counts") ||
      !alloc(&arena->d_child_meta,
             sizeof(PackedChildMeta) * child_count,
             "cudaMalloc child_meta") ||
      !alloc(&arena->d_child_splices,
             sizeof(PackedChildSplice) * child_count,
             "cudaMalloc child_splices") ||
      !alloc(&arena->d_child_node_offsets,
             sizeof(int) * static_cast<std::size_t>(config.pair_count * 2 + 1),
             "cudaMalloc child_node_offsets") ||
      !alloc(&arena->d_child_name_offsets,
             sizeof(int) * static_cast<std::size_t>(config.pair_count * 2 + 1),
             "cudaMalloc child_name_offsets") ||
      !alloc(&arena->d_child_const_offsets,
             sizeof(int) * static_cast<std::size_t>(config.pair_count * 2 + 1),
             "cudaMalloc child_const_offsets") ||
      !alloc(&arena->d_live_child_nodes,
             sizeof(PlainNode) * child_count * static_cast<std::size_t>(config.max_nodes),
             "cudaMalloc live_child_nodes") ||
      !alloc(&arena->d_live_child_name_ids,
             sizeof(std::uint64_t) * child_count * static_cast<std::size_t>(config.max_names),
             "cudaMalloc live_child_name_ids") ||
      !alloc(&arena->d_live_child_consts,
             sizeof(Value) * child_count * static_cast<std::size_t>(config.max_consts),
             "cudaMalloc live_child_consts")) {
    destroy_gpu_repro_arena(arena);
    return false;
  }
  arena->capacity = config;
  return true;
}

template <typename T>
bool alloc_host_pinned(T** ptr, std::size_t count, std::string* message_out, const char* what) {
  if (count == 0) {
    *ptr = nullptr;
    return true;
  }
  return set_error(cudaMallocHost(reinterpret_cast<void**>(ptr), sizeof(T) * count), what, message_out);
}

}  // namespace

bool ensure_gpu_repro_arena_capacity(GpuReproArena* arena,
                                     const GpuReproConfig& config,
                                     std::string* message_out) {
  if (!require_gpu_transport_config(config, message_out)) return false;
  if (arena == nullptr) {
    if (message_out != nullptr) {
      *message_out = "gpu reproduction arena is null";
    }
    return false;
  }
  if (arena->device_id < 0) {
    if (!select_gpu_repro_device(&arena->device_id, message_out)) {
      return false;
    }
  }
  if (gpu_repro_config_fits_capacity(config, arena->capacity)) {
    return true;
  }
  destroy_gpu_repro_arena(arena);
  arena->device_id = -1;
  if (!select_gpu_repro_device(&arena->device_id, message_out)) {
    return false;
  }
  return allocate_gpu_arena(arena, config, message_out);
}

void destroy_gpu_repro_host_staging(GpuReproHostStaging* staging) {
  if (staging == nullptr) {
    return;
  }
  if (staging->selection_counters) cudaFreeHost(staging->selection_counters);
  if (staging->parent_a) cudaFreeHost(staging->parent_a);
  if (staging->parent_b) cudaFreeHost(staging->parent_b);
  if (staging->cand_a) cudaFreeHost(staging->cand_a);
  if (staging->cand_b) cudaFreeHost(staging->cand_b);
  if (staging->child_used_len) cudaFreeHost(staging->child_used_len);
  if (staging->child_name_counts) cudaFreeHost(staging->child_name_counts);
  if (staging->child_const_counts) cudaFreeHost(staging->child_const_counts);
  if (staging->child_meta) cudaFreeHost(staging->child_meta);
  if (staging->child_splices) cudaFreeHost(staging->child_splices);
  if (staging->child_node_offsets) cudaFreeHost(staging->child_node_offsets);
  if (staging->child_name_offsets) cudaFreeHost(staging->child_name_offsets);
  if (staging->child_const_offsets) cudaFreeHost(staging->child_const_offsets);
  if (staging->child_nodes) cudaFreeHost(staging->child_nodes);
  if (staging->child_name_ids) cudaFreeHost(staging->child_name_ids);
  if (staging->child_consts) cudaFreeHost(staging->child_consts);
  *staging = GpuReproHostStaging{};
}

bool ensure_gpu_repro_host_staging_capacity(GpuReproHostStaging* staging,
                                            const GpuReproConfig& config,
                                            std::string* message_out) {
  if (!require_gpu_transport_config(config, message_out)) return false;
  if (staging == nullptr) {
    if (message_out != nullptr) {
      *message_out = "gpu reproduction host staging is null";
    }
    return false;
  }
  if (gpu_repro_config_fits_capacity(config, staging->capacity)) {
    return true;
  }
  destroy_gpu_repro_host_staging(staging);

  if (!alloc_host_pinned(&staging->selection_counters,
                         static_cast<std::size_t>(config.pair_count),
                         message_out, "cudaMallocHost selection_counters") ||
      !alloc_host_pinned(&staging->parent_a, static_cast<std::size_t>(config.pair_count), message_out,
                         "cudaMallocHost parent_a") ||
      !alloc_host_pinned(&staging->parent_b, static_cast<std::size_t>(config.pair_count), message_out,
                         "cudaMallocHost parent_b") ||
      !alloc_host_pinned(&staging->cand_a, static_cast<std::size_t>(config.pair_count), message_out,
                         "cudaMallocHost cand_a") ||
      !alloc_host_pinned(&staging->cand_b, static_cast<std::size_t>(config.pair_count), message_out,
                         "cudaMallocHost cand_b") ||
      !alloc_host_pinned(&staging->child_used_len, static_cast<std::size_t>(config.pair_count * 2), message_out,
                         "cudaMallocHost child_used_len") ||
      !alloc_host_pinned(&staging->child_name_counts, static_cast<std::size_t>(config.pair_count * 2), message_out,
                         "cudaMallocHost child_name_counts") ||
      !alloc_host_pinned(&staging->child_const_counts, static_cast<std::size_t>(config.pair_count * 2), message_out,
                         "cudaMallocHost child_const_counts") ||
      !alloc_host_pinned(&staging->child_meta, static_cast<std::size_t>(config.pair_count * 2), message_out,
                         "cudaMallocHost child_meta") ||
      !alloc_host_pinned(&staging->child_splices, static_cast<std::size_t>(config.pair_count * 2), message_out,
                         "cudaMallocHost child_splices") ||
      !alloc_host_pinned(&staging->child_node_offsets, static_cast<std::size_t>(config.pair_count * 2 + 1), message_out,
                         "cudaMallocHost child_node_offsets") ||
      !alloc_host_pinned(&staging->child_name_offsets, static_cast<std::size_t>(config.pair_count * 2 + 1), message_out,
                         "cudaMallocHost child_name_offsets") ||
      !alloc_host_pinned(&staging->child_const_offsets, static_cast<std::size_t>(config.pair_count * 2 + 1), message_out,
                         "cudaMallocHost child_const_offsets") ||
      !alloc_host_pinned(&staging->child_nodes,
                         static_cast<std::size_t>(config.pair_count * 2 * config.max_nodes), message_out,
                         "cudaMallocHost child_nodes") ||
      !alloc_host_pinned(&staging->child_name_ids,
                         static_cast<std::size_t>(config.pair_count * 2 * config.max_names), message_out,
                         "cudaMallocHost child_name_ids") ||
      !alloc_host_pinned(&staging->child_consts,
                         static_cast<std::size_t>(config.pair_count * 2 * config.max_consts), message_out,
                         "cudaMallocHost child_consts")) {
    destroy_gpu_repro_host_staging(staging);
    return false;
  }
  staging->capacity = config;
  return true;
}

}  // namespace gagp::evo::repro
