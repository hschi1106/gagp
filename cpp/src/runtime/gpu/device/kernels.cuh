#pragma once

#include <cmath>
#include "gagp/core/fitness_policy.hpp"
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

// Validation is per block/program, alongside normal evaluation preparation.
// Public metadata never grants this shortcut: the normal host verifier and
// typed-view proof run first, then this narrower dependency check must pass.
__device__ inline bool d_measure_cache_eligible(const DRegionPhase& phase,
    const DExecutionTables& tables) {
  const auto& p = phase.program;
  if (!p.typed_view || p.n_locals < 0 || p.n_locals > 8 ||
      p.verified_stack_bound > MAX_STACK || p.code_len <= 0 || p.code_len > 4096 ||
      p.const_len < 0 || phase.binding_count < 0 || phase.binding_offset < 0 ||
      phase.binding_count > tables.region_binding_count - phase.binding_offset)
    return false;
  for (int i = 0; i < phase.binding_count; ++i) {
    const auto& b = tables.region_bindings[phase.binding_offset + i];
    if (b.bank != RegionSlotBank::Measure || b.slot != 0 || b.local < 0 || b.local >= 8)
      return false;
  }
  for (int i = 0; i < p.const_len; ++i) {
    const auto tag = tables.phase_consts[p.const_offset + i].tag;
    if (tag != ValueTag::Int && tag != ValueTag::Bool) return false;
  }
  for (int i = 0; i < p.code_len; ++i) {
    const auto ins = tables.phase_code[p.code_offset + i];
    switch (ins.op) {
      case OP_PUSH_CONST: case OP_LOAD: case OP_STORE: case OP_NEG: case OP_NOT:
      case OP_ADD: case OP_SUB: case OP_MUL: case OP_MOD: case OP_EQ: case OP_NE:
      case OP_LT: case OP_LE: case OP_GT: case OP_GE: case OP_CHECK_INT: case OP_RETURN:
        break;
      case OP_JMP: case OP_JMP_IF_TRUE: case OP_JMP_IF_FALSE:
        if (ins.a <= i || ins.a > p.code_len) return false;
        break;
      case OP_CALL_BUILTIN: {
        const auto b = static_cast<BuiltinId>(ins.a);
        if (b != BuiltinId::IDiv0 && b != BuiltinId::IMod0 && b != BuiltinId::Min &&
            b != BuiltinId::Max && b != BuiltinId::Clip) return false;
        break;
      }
      default: return false;
    }
  }
  return true;
}

template <DPayloadFlavor Flavor, bool EnableRegions = false, bool DirectRoot = false>
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
    int fuel, double penalty, FitnessPolicy policy, double* fitness_out,
    const DRegionSegment* region_segments = nullptr, int region_segment_count = 0,
    const DRegionPhase* region_phases = nullptr, int region_phase_count = 0,
    const DRegionPhaseBinding* region_bindings = nullptr, int region_binding_count = 0,
    DRegionWorkspace workspace = {}, unsigned int* case_counts = nullptr, const int* case_order = nullptr, const int* program_indices = nullptr, int* next_program = nullptr) {
  const int tid = static_cast<int>(threadIdx.x);
  if constexpr (EnableRegions) {
    const std::size_t block_base = static_cast<std::size_t>(blockIdx.x) * blockDim.x;
    workspace.slot_stride = blockDim.x;
    if (workspace.frames) {
      const auto slot = block_base * workspace.frame_capacity + tid;
      if(workspace.coordinate_executor) {
        workspace.frames=reinterpret_cast<DRegionFrame*>(reinterpret_cast<DUnboxedCoordinateFrame*>(workspace.frames)+slot);
      } else if(workspace.unboxed_window_frames) {
        if (workspace.window_soa) {
          const auto words = workspace.two_state_frames ? sizeof(DUnboxedWindowFrame<2>)/8 : sizeof(DUnboxedWindowFrame<1>)/8;
          workspace.frames = reinterpret_cast<DRegionFrame*>(reinterpret_cast<std::int64_t*>(workspace.frames) +
              block_base * workspace.frame_capacity * words + tid);
        } else workspace.frames=reinterpret_cast<DRegionFrame*>(workspace.two_state_frames ?
            reinterpret_cast<void*>(reinterpret_cast<DUnboxedWindowFrame<2>*>(workspace.frames)+slot) :
            reinterpret_cast<void*>(reinterpret_cast<DUnboxedWindowFrame<1>*>(workspace.frames)+slot));
      } else if (workspace.compact_frames) {
        if (workspace.two_state_frames)
          workspace.frames = reinterpret_cast<DRegionFrame*>(
              reinterpret_cast<DCompactPairRegionFrame*>(workspace.frames) + slot);
        else workspace.frames = reinterpret_cast<DRegionFrame*>(
              reinterpret_cast<DCompactRegionFrame*>(workspace.frames) + slot);
      }
      else workspace.frames += slot;
    }
    if (workspace.memo_keys)
      workspace.memo_keys += block_base * workspace.memo_capacity * DMAX_REGION_STATES + tid;
    if (workspace.memo_values) workspace.memo_values += block_base * workspace.memo_capacity + tid;
  }

  // Each block owns its workspace slices throughout the launch. Grid-stride
  // program traversal bounds scratch allocation independently of population size.
  for (std::size_t logical_idx = blockIdx.x;
       logical_idx < static_cast<std::size_t>(program_count);) {
  const std::size_t prog_idx = program_indices ? program_indices[logical_idx] : logical_idx;

  const DProgramMeta meta = metas[prog_idx];
  const DPayloadTables payload_tables{
      string_payload_entries,
      string_payload_entry_count,
      string_payload_bytes,
      list_payload_entries,
      list_payload_entry_count,
      list_payload_values,
  };
  DExecutionTables execution_tables{
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

  if constexpr (EnableRegions && (Flavor == DPayloadFlavor::BoundIntListViews ||
                                  Flavor == DPayloadFlavor::IntListViews)) {
    if (workspace.measure_phase_cache) {
      auto* cached_measure = reinterpret_cast<DMeasurePhaseCacheEntry*>(partial_scores + blockDim.x);
      int& cached_phase = *reinterpret_cast<int*>(cached_measure + DMEASURE_CACHE_MAX + 1);
      if (tid == 0) {
        cached_phase = -1;
        if (meta.is_valid && meta.region_count == 1 && meta.region_offset >= 0 &&
            meta.region_offset < region_segment_count) {
          const auto& segment = region_segments[meta.region_offset];
          if (segment.progress == RegionProgressKind::SequenceWindows &&
              segment.sequence_state == 0 && segment.state_types[0] == ValueTag::IntList &&
              segment.preparation_count == 1 && segment.preparation_types[0] == ValueTag::Int) {
            const int index = segment.preparation_phases[0];
            if (index >= 0 && index < region_phase_count &&
                d_measure_cache_eligible(region_phases[index], execution_tables)) cached_phase = index;
          }
        }
      }
      __syncthreads();
      if (cached_phase >= 0) {
        for (int length = tid + 2; length <= DMEASURE_CACHE_MAX; length += blockDim.x) {
          auto& entry = cached_measure[length];
          entry.error = -2; entry.spent_fuel = 0; entry.value = 0;
          // Run the same phase dispatch as an ordinary invocation, including
          // its typed/generic choice, constant shortcuts and exact fuel/errors.
          // The admission above excludes every input except state[0]'s length.
          DCompactRegionFrame frame{};
          frame.state[0] = Value::from_int_list_hash_len(0, length);
          typename DPayloadFlavorTraits<Flavor>::State cache_payload_state{};
          int remaining = fuel;
          const auto result = d_region_phase<Flavor>(region_segments[meta.region_offset],
              cached_phase, ValueTag::Int, frame, nullptr, 0, payload_tables,
              cache_payload_state, execution_tables, remaining);
          // A timeout cannot certify the result or all instruction charges.
          if (fuel >= 0 && remaining >= 0 &&
              !(result.is_error && result.err_code == ErrCode::Timeout)) {
            entry.spent_fuel = fuel - remaining;
            entry.error = result.is_error ? static_cast<int>(result.err_code) : -1;
            if (!result.is_error) entry.value = result.value.i;
          }
        }
      }
      __syncthreads();
      if (cached_phase >= 0) {
        execution_tables.measure_cache = cached_measure;
        execution_tables.measure_cached_phase = cached_phase;
      }
    }
  }

  double local_score = 0.0;
  const int chunk_start = (meta.case_count * tid) / static_cast<int>(blockDim.x);
  const int chunk_end = (meta.case_count * (tid + 1)) / static_cast<int>(blockDim.x);
  const bool strided = (Flavor == DPayloadFlavor::IntListViews || Flavor == DPayloadFlavor::BoundIntListViews) && case_order;
  const int case_begin = strided ? tid : chunk_start;
  const int case_end = strided ? meta.case_count : chunk_end;
  const int case_step = strided ? static_cast<int>(blockDim.x) : 1;
  for (int local_case = case_begin; local_case < case_end; local_case += case_step) {
    int input_case = local_case;
    if constexpr ((Flavor == DPayloadFlavor::IntListViews || Flavor == DPayloadFlavor::BoundIntListViews))
      if (case_order) input_case = case_order[local_case];
    DResult result;
    double resident_score = 0;
    bool exact=false;
    if constexpr (DirectRoot) {
      result = d_execute_region_root(meta, shared_code, all_consts, shared_case_local_vals,
          shared_case_local_set, payload_tables, execution_tables, input_case, fuel, workspace);
      if(!result.is_error) {
        resident_score=fitness_case_score(result.value,shared_answer[input_case],penalty,policy);
        exact=fitness_case_score(result.value,shared_answer[input_case],penalty,{FitnessMetric::Exact})==0;
      }
    } else {
    result = d_execute_bytecode_impl<Flavor, EnableRegions>(
        meta, shared_code, all_consts, shared_case_local_vals, shared_case_local_set,
        payload_tables, execution_tables, input_case, fuel, workspace,
        shared_answer+input_case,penalty,policy,&resident_score,&exact);
    }
    unsigned int* counts = case_counts ? case_counts + prog_idx * 6 : nullptr;
    if (counts) { atomicAdd(counts, 1u); if(result.is_error || !exact)atomicAdd(counts+5,1u); }
    if (result.is_error) {
      if (counts) {
        atomicAdd(counts + 1, 1u);
        if (result.err_code == ErrCode::Timeout) atomicAdd(counts + 2, 1u);
      }
      local_score = d_canonicalize_fitness_accumulator(local_score - fitness_error_ceiling(policy, penalty));
      continue;
    }

    if (counts && result.value.tag == ValueTag::FallbackToken) atomicAdd(counts + 3, 1u);
    local_score = d_canonicalize_fitness_accumulator(local_score +
        resident_score);
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
  // Workspace remains owned by the physical block when it takes another row.
  if (next_program) {
    // Reduction is complete and every signed queue index is exact in double.
    // Reuse its existing slot so queue support adds no static shared capacity.
    if (tid == 0) partial_scores[0] = static_cast<double>(atomicAdd(next_program, 1));
    __syncthreads();
    logical_idx = static_cast<std::size_t>(static_cast<int>(partial_scores[0]));
    // All lanes must consume the broadcast before the next root-code upload
    // can overlap this program's old partial-score storage.
    __syncthreads();
  } else {
    logical_idx += gridDim.x;
    __syncthreads();
  }
  }
}

}  // namespace gagp::gpu_detail
