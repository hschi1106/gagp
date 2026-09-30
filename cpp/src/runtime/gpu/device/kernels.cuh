#pragma once

#include <cmath>
#include "execute_bytecode_device.cuh"

namespace gagp::gpu_detail {

__device__ inline double d_canonicalize_fitness_accumulator(double value) {
  if (!isfinite(value) || value == 0.0) {
    return value == 0.0 ? 0.0 : value;
  }
  int exponent = 0;
  const double mantissa = frexp(value, &exponent);
  constexpr int kMantissaBits = 48;
  const long long quantized_mantissa = llround(ldexp(mantissa, kMantissaBits));
  return ldexp(static_cast<double>(quantized_mantissa), exponent - kMantissaBits);
}

template <DPayloadFlavor Flavor, bool EnableRegions = false>
__global__ __launch_bounds__(1024) void evaluate_fitness_programs_impl(
    int program_count,
    const Value* all_consts, const DInstr* all_code, const DProgramMeta* metas,
    const Value* shared_case_local_vals, const unsigned char* shared_case_local_set,
    const Value* shared_answer,
    const DStringPayloadEntry* string_payload_entries, int string_payload_entry_count,
    const char* string_payload_bytes,
    const DListPayloadEntry* list_payload_entries, int list_payload_entry_count,
    const Value* list_payload_values,
    const DInstr* phase_code,
    const Value* phase_consts,
    int fuel, double penalty, double* fitness_out,
    const DRegionSegment* region_segments = nullptr, int region_segment_count = 0,
    const DRegionPhase* region_phases = nullptr, int region_phase_count = 0,
    const DRegionPhaseBinding* region_bindings = nullptr, int region_binding_count = 0,
    DRegionWorkspace workspace = {}, unsigned int* case_counts = nullptr, const int* case_order = nullptr) {
  const int tid = static_cast<int>(threadIdx.x);
  if constexpr (EnableRegions) {
    const std::size_t block_base = static_cast<std::size_t>(blockIdx.x) * blockDim.x;
    workspace.slot_stride = blockDim.x;
    if (workspace.frames) workspace.frames += block_base * workspace.frame_capacity + tid;
    if (workspace.memo_keys)
      workspace.memo_keys += block_base * workspace.memo_capacity * DMAX_REGION_STATES + tid;
    if (workspace.memo_values) workspace.memo_values += block_base * workspace.memo_capacity + tid;
  }

  // Each block owns its workspace slices throughout the launch. Grid-stride
  // program traversal bounds scratch allocation independently of population size.
  for (std::size_t prog_idx = blockIdx.x;
       prog_idx < static_cast<std::size_t>(program_count); prog_idx += gridDim.x) {

  const DProgramMeta meta = metas[prog_idx];
  const DPayloadTables payload_tables{
      string_payload_entries,
      string_payload_entry_count,
      string_payload_bytes,
      list_payload_entries,
      list_payload_entry_count,
      list_payload_values,
  };
  const DExecutionTables execution_tables{
      phase_code,
      phase_consts,
      region_segments, region_segment_count,
      region_phases, region_phase_count,
      region_bindings, region_binding_count,
  };

  extern __shared__ DInstr shared_code[];
  if (meta.is_valid && meta.code_len > 0) {
    for (int i = tid; i < meta.code_len; i += static_cast<int>(blockDim.x)) {
      shared_code[i] = all_code[meta.code_offset + i];
    }
  }
  __syncthreads();

  extern __shared__ unsigned char shared_bytes[];
  const std::size_t code_bytes = sizeof(DInstr) * static_cast<std::size_t>(meta.code_len);
  const std::size_t partial_offset =
      (code_bytes + alignof(double) - 1u) & ~static_cast<std::size_t>(alignof(double) - 1u);
  double* partial_scores = reinterpret_cast<double*>(shared_bytes + partial_offset);

  double local_score = 0.0;
  const int chunk_start = (meta.case_count * tid) / static_cast<int>(blockDim.x);
  const int chunk_end = (meta.case_count * (tid + 1)) / static_cast<int>(blockDim.x);
  for (int local_case = chunk_start; local_case < chunk_end; ++local_case) {
    int input_case = local_case;
    if constexpr (Flavor == DPayloadFlavor::IntListViews)
      if (case_order) input_case = case_order[local_case];
    const DResult result = d_execute_bytecode_impl<Flavor, EnableRegions>(
        meta, shared_code, all_consts, shared_case_local_vals, shared_case_local_set,
        payload_tables, execution_tables, input_case, fuel, workspace);
    unsigned int* counts = case_counts ? case_counts + prog_idx * 5 : nullptr;
    if (counts) atomicAdd(counts, 1u);
    if (result.is_error) {
      if (counts) {
        atomicAdd(counts + 1, 1u);
        if (result.err_code == ErrCode::Timeout) atomicAdd(counts + 2, 1u);
      }
      local_score = d_canonicalize_fitness_accumulator(local_score - fabs(penalty));
      continue;
    }

    if (counts && result.value.tag == ValueTag::FallbackToken) atomicAdd(counts + 3, 1u);
    double case_score = 0.0;
    if (vm_semantics::fitness_score_for_values(result.value, shared_answer[input_case], penalty, case_score)) {
      local_score = d_canonicalize_fitness_accumulator(local_score + case_score);
    } else if (counts) atomicAdd(counts + 4, 1u);
  }

  partial_scores[tid] = local_score;
  __syncthreads();

  const int lane = tid & 31;
  const int warp_id = tid >> 5;
  if (lane == 0) {
    const int warp_base = warp_id << 5;
    const int warp_end =
        ((warp_base + 32) < static_cast<int>(blockDim.x)) ? (warp_base + 32) : static_cast<int>(blockDim.x);
    double warp_score = 0.0;
    for (int i = warp_base; i < warp_end; ++i) {
      warp_score = d_canonicalize_fitness_accumulator(warp_score + partial_scores[i]);
    }
    // Store the warp reduction in its own warp's first slot. Writing to
    // partial_scores[warp_id] races with warp 0 reading partial_scores[1..31].
    partial_scores[warp_base] = warp_score;
  }
  __syncthreads();

  if (tid == 0) {
    double total_score = 0.0;
    const int warp_count = (static_cast<int>(blockDim.x) + 31) >> 5;
    for (int warp = 0; warp < warp_count; ++warp) {
      total_score = d_canonicalize_fitness_accumulator(total_score + partial_scores[warp << 5]);
    }
    fitness_out[prog_idx] = total_score;
  }
  if constexpr (!EnableRegions) break;
  // Thread 0 must finish consuming the reduction before shared code is reused.
  __syncthreads();
  }
}

}  // namespace gagp::gpu_detail
