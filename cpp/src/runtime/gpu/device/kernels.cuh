#pragma once

#include <cmath>
#include "gagp/core/fitness_policy.hpp"
#include "execute_bytecode_device.cuh"
#include "fitness_accumulator.cuh"

namespace gagp::gpu_detail {

// Validation is per block/program, alongside normal evaluation preparation.
// Public metadata never grants this shortcut: the normal host verifier and
// typed-view or private-arena typeflow runs first, then this narrower dependency
// check must pass. Private proof is supplied only by the admitted workspace.
__device__ inline bool d_measure_cache_eligible(const DRegionPhase& phase,
    const DExecutionTables& tables, bool private_proven = false, bool trivial_phases = false) {
  const auto& p = phase.program;
  if(private_proven && (p.scalar_constant_index>=0 ||
      (trivial_phases && (p.trivial_constant_index>=0 || p.trivial_load_binding>=0))))return false;
  if ((!p.typed_view && !private_proven) || p.n_locals < 0 || p.n_locals > 8 ||
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
        if (b != BuiltinId::TruncDiv0 && b != BuiltinId::TruncMod0 && b != BuiltinId::IDiv0 && b != BuiltinId::IMod0 && b != BuiltinId::Min &&
            b != BuiltinId::Max && b != BuiltinId::Clip) return false;
        break;
      }
      default: return false;
    }
  }
  return true;
}

template <DPayloadFlavor Flavor, bool EnableRegions = false, bool DirectRoot = false,
    int LaunchThreads = (Flavor == DPayloadFlavor::ArenaMixed || Flavor == DPayloadFlavor::IndexedArena) ? 32 : 1024,
    bool PrivateLayout = false>
// Arena kernels always launch one physical warp; reserving resources for a
// hypothetical 1024-thread block needlessly constrains registers in this VM.
__global__ __launch_bounds__(LaunchThreads) void evaluate_fitness_programs_impl(
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
  if constexpr (PrivateLayout) {
    static_assert(Flavor == DPayloadFlavor::IndexedArena && EnableRegions);
    // The host selects this instantiation only when every fact below already
    // holds. Expose those facts to inlined VM code; retain all runtime checks,
    // logical resources, scratch capacities and external immutable tables.
    workspace.private_intlists = true;
    workspace.compact_intlist_cells = true;
    workspace.private_cell_soa = false;
    workspace.private_warp_copy = true;
    workspace.trivial_phases = true;
    workspace.private_typed_phases = true;
    workspace.private_bound_phases = true;
    workspace.window_executor = true;
    workspace.coordinate_executor = false;
    workspace.unboxed_window_frames = false;
    workspace.window_soa = false;
    workspace.concat_cache_enabled = false;
    workspace.concat_cache = nullptr;
    workspace.payload_string_entries = nullptr;
    workspace.payload_list_entries = nullptr;
    workspace.payload_string_bytes = nullptr;
    workspace.payload_list_values = nullptr;
    workspace.payload_list_index = nullptr;
    workspace.payload_list_epoch_index = nullptr;
    workspace.payload_list_epoch = nullptr;
  }
  const int tid = static_cast<int>(threadIdx.x);
  const int fitness_lanes = workspace.fitness_lanes>0?workspace.fitness_lanes:static_cast<int>(blockDim.x);
  if constexpr ((Flavor == DPayloadFlavor::ArenaMixed || Flavor == DPayloadFlavor::IndexedArena)) {
    const std::size_t slot = static_cast<std::size_t>(blockIdx.x) * blockDim.x + tid;
    if(workspace.payload_string_entries)workspace.payload_string_entries += slot * workspace.payload_entry_capacity;
    if(workspace.payload_list_entries)workspace.payload_list_entries += slot * workspace.payload_entry_capacity;
    if(workspace.payload_string_bytes)workspace.payload_string_bytes += slot * workspace.payload_byte_capacity;
    if(workspace.payload_list_values)workspace.payload_list_values += slot * workspace.payload_value_capacity;
    if(workspace.payload_intlist_cells) {
      // Same number of cells and capacity per lane. Consecutive lanes own
      // different columns, so element i is coalesced across the physical block.
      // Program scheduling/replay never changes a block's scratch ownership.
      if(workspace.private_cell_soa) {
        workspace.payload_intlist_cells += static_cast<std::size_t>(blockIdx.x)*blockDim.x*
            workspace.payload_value_capacity+tid;
        workspace.private_cell_stride=blockDim.x;
      } else workspace.payload_intlist_cells += slot * workspace.payload_value_capacity;
    }
    if(workspace.concat_cache)workspace.concat_cache+=slot*DCONCAT_CACHE_SLOTS;
    if constexpr (Flavor == DPayloadFlavor::IndexedArena) {
      if(workspace.payload_list_epoch_index) {
        workspace.payload_list_epoch_index += slot * workspace.payload_entry_capacity * 2;
        if(workspace.payload_list_epoch)workspace.payload_list_epoch += slot;
      } else if(workspace.payload_list_index)workspace.payload_list_index += slot * workspace.payload_entry_capacity * 2;
    }
  }
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
  const int replay_warps = (fitness_lanes + 31) / 32;
  const bool tiled_replay = workspace.replay_warp_scores != nullptr;
  const bool dense_tiled = tiled_replay && workspace.dense_case_tiles > 0;
  const int scheduled_tiles = dense_tiled ? workspace.dense_case_tiles : replay_warps;
  const std::size_t row = tiled_replay ? logical_idx / scheduled_tiles : logical_idx;
  const int tile = tiled_replay ? logical_idx % scheduled_tiles : 0;
  const std::size_t prog_idx = program_indices ? program_indices[row] : row;

  if constexpr ((Flavor == DPayloadFlavor::ArenaMixed || Flavor == DPayloadFlavor::IndexedArena)) {
    if (tid == 0 && !tiled_replay) {
      workspace.payload_overflows[prog_idx] = 0;
      if(workspace.payload_overflow_reasons)workspace.payload_overflow_reasons[prog_idx]=0;
      if (case_counts) for (int i=0;i<6;++i)case_counts[prog_idx*6+i]=0;
    }
    __syncthreads();
  }
  const DProgramMeta meta = metas[prog_idx];
  const bool dense_lanes = (Flavor == DPayloadFlavor::ArenaMixed || Flavor == DPayloadFlavor::IndexedArena) &&
      workspace.dense_case_lanes && !tiled_replay && meta.case_count > 0 && meta.case_count <= fitness_lanes;
  if(meta.reuse_completed_fitness) {
    // The following tiled reduction still visits every logical slot. Give it
    // a defined neutral value even though host copyback restores this score.
    if(tiled_replay && !dense_tiled && tid==0)workspace.replay_warp_scores[logical_idx]=0.0;
    // This is block-uniform. Initial buffers remain zero for skipped rows;
    // exact completed scores/logical counters are restored after copyback.
    if(next_program) {
      // Reuse the already-admitted dynamic code/score scratch. Adding
      // static shared storage would reduce the public shared-memory limit.
      extern __shared__ unsigned char reuse_queue_scratch[];
      auto* next_reused_program=reinterpret_cast<int*>(reuse_queue_scratch);
      if(tid==0)*next_reused_program=atomicAdd(next_program,1);
      __syncthreads();logical_idx=static_cast<std::size_t>(*next_reused_program);__syncthreads();
    } else {logical_idx+=gridDim.x;__syncthreads();}
    continue;
  }
  const DPayloadTables payload_tables{
      string_payload_entries,
      string_payload_entry_count,
      string_payload_bytes,
      list_payload_entries,
      list_payload_entry_count,
      list_payload_values,
      workspace.shared_list_value_count,
  };
  DExecutionTables execution_tables{
      phase_code,
      phase_consts,
      region_segments, region_segment_count,
      region_phases, region_phase_count,
      region_bindings, region_binding_count,
  };
  execution_tables.register_phase_stack=workspace.register_phase_stack;

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
                                  Flavor == DPayloadFlavor::IntListViews ||
                                  Flavor == DPayloadFlavor::ArenaMixed ||
                                  Flavor == DPayloadFlavor::IndexedArena)) {
    if (workspace.measure_phase_cache) {
      // Untiled arenas retain one score per logical lane. Tiled launches use
      // the tile's physical score slots; both layouts precede the cache.
      const auto score_slots=tiled_replay?static_cast<int>(blockDim.x):fitness_lanes;
      auto* cached_measure = reinterpret_cast<DMeasurePhaseCacheEntry*>(partial_scores + score_slots);
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
                d_measure_cache_eligible(region_phases[index], execution_tables,
                    workspace.private_intlists,workspace.trivial_phases)) cached_phase = index;
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
          if constexpr (Flavor == DPayloadFlavor::ArenaMixed || Flavor == DPayloadFlavor::IndexedArena) {
            cache_payload_state.private_intlists=workspace.private_intlists;
            cache_payload_state.trivial_phases=workspace.trivial_phases;
            cache_payload_state.private_bound_phases=workspace.private_bound_phases;
            cache_payload_state.private_typed_phases=workspace.private_typed_phases;
            cache_payload_state.private_warp_copy=workspace.private_warp_copy;
            cache_payload_state.private_adjacent_concat=workspace.private_adjacent_concat;
            cache_payload_state.simple_phases=workspace.simple_phases;
          }
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

  DResult* shape_results=nullptr;
  if constexpr(DirectRoot) {
    if(meta.shape_result_cache && workspace.shape_result_count>0) {
      shape_results=reinterpret_cast<DResult*>(shared_bytes+workspace.shape_result_offset);
      for(int group=tid;group<meta.shape_case_count;group+=blockDim.x) {
        const int input=workspace.shape_case_representatives[meta.shape_representative_offset+group];
        shape_results[group]=d_execute_region_root(meta,shared_code,all_consts,shared_case_local_vals,
            shared_case_local_set,payload_tables,execution_tables,input,fuel,workspace);
      }
      __syncthreads();
    }
  }
  const int lane_begin = tiled_replay ? tile * 32 : 0;
  const int lane_end = tiled_replay ? min(lane_begin + 32, fitness_lanes) : fitness_lanes;
  unsigned int local_counts[6] = {};
  unsigned int* counts = case_counts ? case_counts + prog_idx * 6 : nullptr;
  const bool coalesce_counts = counts && workspace.coalesce_case_counts;
  // Arena case chunks are contiguous floor(C*lane/L) intervals. For C<=L,
  // case k belongs to lane ceil((k+1)*L/C)-1. Schedule only those lanes,
  // leaving every original empty score slot at +0 for the same reduction.
  if (dense_lanes) {
    for (int lane = tid; lane < fitness_lanes; lane += blockDim.x) partial_scores[lane] = 0.0;
    __syncthreads();
  }
  const int schedule_begin = dense_tiled ? tile*32 + tid : dense_lanes ? tid : lane_begin + tid;
  const int schedule_end = dense_tiled ? min((tile+1)*32,meta.case_count) : dense_lanes ? meta.case_count : lane_end;
  for (int scheduled = schedule_begin; scheduled < schedule_end; scheduled += blockDim.x) {
  const int logical_tid = (dense_lanes || dense_tiled) ?
      static_cast<int>(((static_cast<long long>(scheduled) + 1) * fitness_lanes - 1) / meta.case_count) : scheduled;
  double local_score = 0.0;
  const int chunk_start = (meta.case_count * logical_tid) / fitness_lanes;
  const int chunk_end = (meta.case_count * (logical_tid + 1)) / fitness_lanes;
  const bool strided = (Flavor == DPayloadFlavor::IntListViews || Flavor == DPayloadFlavor::BoundIntListViews) && case_order;
  const int case_begin = strided ? logical_tid : chunk_start;
  const int case_end = strided ? meta.case_count : chunk_end;
  const int case_step = strided ? fitness_lanes : 1;
  for (int local_case = case_begin; local_case < case_end; local_case += case_step) {
    const unsigned weight = workspace.integer_case_weights ? workspace.integer_case_weights[local_case] : 1u;
    int input_case = local_case;
    if constexpr (Flavor == DPayloadFlavor::IntListViews || Flavor == DPayloadFlavor::BoundIntListViews ||
                  Flavor == DPayloadFlavor::ArenaMixed || Flavor == DPayloadFlavor::IndexedArena)
      if (case_order) input_case = case_order[local_case];
    DResult result;
    double resident_score = 0;
    bool exact=false;
    if constexpr (DirectRoot) {
      result = shape_results?shape_results[workspace.shape_case_groups[meta.shape_case_group_offset+input_case]]:
          d_execute_region_root(meta, shared_code, all_consts, shared_case_local_vals,
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
    if (result.payload_capacity_exceeded) {
      if(workspace.payload_overflow_reasons)
        atomicOr(workspace.payload_overflow_reasons+prog_idx,result.payload_capacity_reason);
      if (workspace.payload_overflows) atomicAdd(workspace.payload_overflows + prog_idx, 1u);
      if (workspace.payload_overflow_lane_masks)
        atomicOr(workspace.payload_overflow_lane_masks +
            prog_idx * workspace.payload_overflow_mask_words + logical_tid / 32,
            1u << (logical_tid % 32));
    }
    if (coalesce_counts) {
      local_counts[0] += weight;
      if (result.is_error || !exact) local_counts[5] += weight;
    } else if (counts) { atomicAdd(counts, weight); if(result.is_error || !exact)atomicAdd(counts+5,weight); }
    if (result.is_error) {
      if (coalesce_counts) {
        local_counts[1] += weight;
        if (result.err_code == ErrCode::Timeout) local_counts[2] += weight;
      } else if (counts) {
        atomicAdd(counts + 1, weight);
        if (result.err_code == ErrCode::Timeout) atomicAdd(counts + 2, weight);
      }
      local_score = d_canonicalize_fitness_accumulator(local_score - fitness_error_ceiling(policy, penalty)*weight);
      continue;
    }

    if (counts && result.value.tag == ValueTag::FallbackToken) {
      if (coalesce_counts) local_counts[3] += weight;
      else atomicAdd(counts + 3, weight);
    }
    local_score = d_canonicalize_fitness_accumulator(local_score +
        resident_score*weight);
  }

  if(dense_tiled)workspace.replay_warp_scores[row*fitness_lanes+logical_tid]=local_score;
  else partial_scores[tiled_replay ? logical_tid - lane_begin : logical_tid] = local_score;
  }
  // All physical lanes reach this point, including lanes with no cases.
  // Integer diagnostic counts are exact under the original case-count bounds;
  // fitness retains its original logical-lane order and canonical reduction.
  if (coalesce_counts) {
    // Independent thread scheduling can reach this join at different times.
    // Derive the participating mask from the physical block, never from a
    // transient __activemask() after loops with unequal per-lane trip counts.
    const int active_lanes = min(32, static_cast<int>(blockDim.x) - (tid & ~31));
    const unsigned mask = 0xffffffffu >> (32 - active_lanes);
    __syncwarp(mask);
    for (int field = 0; field < 6; ++field) {
      unsigned value = local_counts[field];
      for (int offset = 16; offset; offset >>= 1) {
        const unsigned other = __shfl_down_sync(mask, value, offset);
        if ((tid & 31) + offset < active_lanes) value += other;
      }
      if ((tid & 31) == 0 && value) atomicAdd(counts + field, value);
    }
  }
  __syncthreads();

  // Logical warp grouping is independent of replay's physical block width.
  if (tiled_replay) {
    if (!dense_tiled && tid == 0) {
      double subtotal = 0.0;
      for (int i = lane_begin; i < lane_end; ++i)
        subtotal = d_canonicalize_fitness_accumulator(subtotal + partial_scores[i - lane_begin]);
      workspace.replay_warp_scores[logical_idx] = subtotal;
    }
  } else {
  const int warp_count = (fitness_lanes + 31) >> 5;
  for (int warp_id = tid; warp_id < warp_count; warp_id += blockDim.x) {
    const int warp_base = warp_id << 5;
    const int warp_end = min(warp_base + 32, fitness_lanes);
    double warp_score = 0.0;
    for (int i = warp_base; i < warp_end; ++i)
      warp_score = d_canonicalize_fitness_accumulator(warp_score + partial_scores[i]);
    partial_scores[warp_base] = warp_score;
  }
  __syncthreads();

  if (tid == 0) {
    double total_score = 0.0;
    for (int warp = 0; warp < warp_count; ++warp) {
      total_score = d_canonicalize_fitness_accumulator(total_score + partial_scores[warp << 5]);
    }
    fitness_out[prog_idx] = total_score;
  }
  } // Ordinary canonical program reduction.
  if constexpr (!EnableRegions && Flavor != DPayloadFlavor::ArenaMixed && Flavor != DPayloadFlavor::IndexedArena) break;
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

// Each affected program is reset once, before independent warp tiles run.
__global__ void reset_payload_replay(int count, const int* indices,
    unsigned int* overflows, unsigned int* counts, unsigned int* reasons=nullptr) {
  const int row = blockIdx.x * blockDim.x + threadIdx.x;
  if (row >= count) return;
  const int program = indices[row];
  overflows[program] = 0;
  if(reasons)reasons[program]=0;
  if (counts) for (int i=0;i<6;++i) counts[program*6+i]=0;
}

__global__ void reduce_payload_replay(int count, int warps, const int* indices,
    const double* partials, double* scores) {
  const int row = blockIdx.x * blockDim.x + threadIdx.x;
  if (row >= count) return;
  double total=0.0;
  for(int warp=0;warp<warps;++warp)
    total=d_canonicalize_fitness_accumulator(total+partials[row*warps+warp]);
  scores[indices?indices[row]:row]=total;
}


// Dense tiles scatter each case score to its original logical lane. Recreate
// both canonical reduction levels; do not reduce in the new physical order.
__global__ void reduce_dense_case_tiles(int count, int lanes, const int* indices,
    const double* partials, double* scores) {
  const int row=blockIdx.x*blockDim.x+threadIdx.x;
  if(row>=count)return;
  double total=0.0;
  for(int base=0;base<lanes;base+=32) {
    double warp=0.0;
    for(int lane=base;lane<min(base+32,lanes);++lane)
      warp=d_canonicalize_fitness_accumulator(warp+partials[static_cast<std::size_t>(row)*lanes+lane]);
    total=d_canonicalize_fitness_accumulator(total+warp);
  }
  scores[indices?indices[row]:row]=total;
}

}  // namespace gagp::gpu_detail
