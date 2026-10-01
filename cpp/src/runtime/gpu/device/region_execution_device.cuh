#pragma once

// Included inside gpu_detail after the ordinary phase interpreter declaration.
// Frames hold values and continuation indices only; phase calls cannot reenter
// structured execution because they use d_run_code_core<Flavor, false>.
__device__ inline bool d_region_addable(std::int64_t value, std::int64_t offset) {
  return !((offset > 0 && value > INT64_MAX - offset) ||
           (offset < 0 && value < INT64_MIN - offset));
}

// Only selected after type-flow and per-phase stack/local proofs. No generic
// phase VM or tagged preset array is reachable from this capability profile.
template <class Frame>
struct DViewRegionBindings {
  const DRegionSegment& segment;
  const DRegionPhase& phase;
  const Frame& frame;
  const Value* caller_locals;
  std::uint64_t caller_set;
  const DExecutionTables& execution;
  __device__ ErrCode init(long long* locals, unsigned& local_set,
      const DPayloadTables& tables) const {
    if (phase.binding_offset < 0 || phase.binding_count < 0 ||
        phase.binding_count > execution.region_binding_count - phase.binding_offset)
      return ErrCode::Value;
    for (int i = 0; i < phase.binding_count; ++i) {
      const auto& b = execution.region_bindings[phase.binding_offset + i];
      if (b.local < 0 || b.local >= 8) return ErrCode::Name;
      Value value;
      switch (b.bank) {
        case RegionSlotBank::State: value = frame.state[b.slot]; break;
        case RegionSlotBank::Prepared: value = frame.prepared[b.slot]; break;
        case RegionSlotBank::Result: value = frame.results[b.slot]; break;
        case RegionSlotBank::Measure:
          value = Value::from_int(Value::container_len(frame.state[segment.sequence_state])); break;
        case RegionSlotBank::Parameter: {
          const int local = segment.parameter_caller_locals[b.slot];
          if (!(caller_set & (std::uint64_t{1} << local))) continue;
          value = caller_locals[local]; break;
        }
      }
      if (!d_convert_list_view(value, tables)) return ErrCode::Value;
      locals[b.local] = value.tag == ValueTag::Bool ? value.b : value.i;
      local_set |= 1u << b.local;
    }
    return static_cast<ErrCode>(-1);
  }
};

template <DPayloadFlavor Flavor, int BindingCapacity, class Frame>
__device__ DResult d_region_phase_impl(
    const DRegionSegment& segment, int phase_index, ValueTag expected,
    const Frame& frame, const Value* caller_locals,
    std::uint64_t caller_set, const DPayloadTables& payload_tables,
    typename DPayloadFlavorTraits<Flavor>::State& payload_state,
    const DExecutionTables& tables, int& fuel, bool result_phase = false) {
  if (phase_index < 0 || phase_index >= tables.region_phase_count)
    return d_error(ErrCode::Value);
  const auto& phase = tables.region_phases[phase_index];
  DLocalPreset presets[BindingCapacity];
  int count = 0;
  if (phase.binding_count < 0 || phase.binding_count > BindingCapacity ||
      phase.binding_offset < 0 ||
      phase.binding_count > tables.region_binding_count - phase.binding_offset)
    return d_error(ErrCode::Value);
  for (int i = 0; i < phase.binding_count; ++i) {
    const auto& binding = tables.region_bindings[phase.binding_offset + i];
    DLocalPreset preset;
    preset.local = binding.local;
    switch (binding.bank) {
      case RegionSlotBank::State: preset.value = frame.state[binding.slot]; break;
      case RegionSlotBank::Prepared: preset.value = frame.prepared[binding.slot]; break;
      case RegionSlotBank::Result: preset.value = frame.results[binding.slot]; break;
      case RegionSlotBank::Measure:
        preset.value = Value::from_int(Value::container_len(frame.state[segment.sequence_state]));
        break;
      case RegionSlotBank::Parameter: {
        const int local = segment.parameter_caller_locals[binding.slot];
        if ((caller_set & (std::uint64_t{1} << local)) == 0) continue;
        preset.value = caller_locals[local];
        preset.expected = segment.parameter_types[binding.slot];
        break;
      }
    }
    presets[count++] = preset;
  }
  DCodeView view;
  view.code = tables.phase_code + phase.program.code_offset;
  view.code_len = phase.program.code_len;
  view.consts = tables.phase_consts ? tables.phase_consts + phase.program.const_offset : nullptr;
  view.const_len = phase.program.const_len;
  view.n_locals = phase.program.n_locals;
  if constexpr (Flavor == DPayloadFlavor::IntListViews) {
    if (phase.program.typed_view && view.n_locals <= 8 && phase.program.verified_stack_bound <= 16)
      return d_run_view_phase(view, presets, count, payload_tables, fuel, expected);
  }
  DResult result;
  if (view.n_locals <= 8 && phase.program.verified_stack_bound <= 16) {
    result = d_run_code_core<Flavor, false, 16, 8>(
        view, nullptr, nullptr, 0, presets, count, payload_tables, payload_state,
        tables, fuel, false);
  } else {
    result = d_run_code_core<Flavor, false>(
        view, nullptr, nullptr, 0, presets, count, payload_tables, payload_state,
        tables, fuel, false);
  }
  // The verifier proves the nominal phase type. A bounded payload operation
  // can represent a result with the internal fallback token; only result phases
  // admit it. Control, preparation, and next-state phases keep exact tag checks.
  if (!result.is_error && result.value.tag != expected &&
      !(result_phase && result.value.tag == ValueTag::FallbackToken))
    return d_error(ErrCode::Type);
  return result;
}

template <DPayloadFlavor Flavor, class Frame>
__device__ DResult d_region_phase(
    const DRegionSegment& segment, int phase_index, ValueTag expected,
    const Frame& frame, const Value* caller_locals,
    std::uint64_t caller_set, const DPayloadTables& payload_tables,
    typename DPayloadFlavorTraits<Flavor>::State& payload_state,
    const DExecutionTables& tables, int& fuel, bool result_phase = false) {
  if (phase_index < 0 || phase_index >= tables.region_phase_count)
    return d_error(ErrCode::Value);
  const auto& constant = tables.region_phases[phase_index].program;
  if (constant.scalar_constant_index >= 0) {
    if (fuel < 0 || constant.scalar_constant_fuel > static_cast<unsigned>(fuel))
      return d_error(ErrCode::Timeout);
    fuel -= static_cast<int>(constant.scalar_constant_fuel);
    const auto value = tables.phase_consts[constant.scalar_constant_index];
    if (value.tag != expected) return d_error(ErrCode::Type);
    return d_ok(value);
  }
  if constexpr (Flavor == DPayloadFlavor::BoundIntListViews) {
    const auto& phase = tables.region_phases[phase_index];
    if (!phase.program.typed_view || phase.program.n_locals > 8 ||
        phase.program.verified_stack_bound > 16) return d_error(ErrCode::Value);
    DCodeView view;
    view.code = tables.phase_code + phase.program.code_offset;
    view.code_len = phase.program.code_len;
    view.consts = tables.phase_consts ? tables.phase_consts + phase.program.const_offset : nullptr;
    view.const_len = phase.program.const_len;
    view.n_locals = phase.program.n_locals;
    return d_run_bound_view_phase(view,
        DViewRegionBindings<Frame>{segment, phase, frame, caller_locals, caller_set, tables},
        payload_tables, fuel, expected);
  } else {
  // Most phases bind only a few slots. Avoid constructing the maximum-size
  // preset array on every phase invocation while retaining the full capacity.
  if (tables.region_phases[phase_index].binding_count <= 4) {
    return d_region_phase_impl<Flavor, 4>(
        segment, phase_index, expected, frame, caller_locals, caller_set,
        payload_tables, payload_state, tables, fuel, result_phase);
  }
  constexpr int kBindings = DMAX_REGION_STATES + DMAX_REGION_PARAMETERS +
                            DMAX_REGION_PREPARATIONS + DMAX_REGION_REQUESTS + 1;
  return d_region_phase_impl<Flavor, kBindings>(
      segment, phase_index, expected, frame, caller_locals, caller_set,
      payload_tables, payload_state, tables, fuel, result_phase);
  }
}

template <class Frame>
__device__ inline bool d_region_endpoint(
    const WindowEndpoint& endpoint, std::uint32_t length,
    const Frame& frame, std::int64_t& out) {
  if (endpoint.kind == WindowEndpointKind::Begin) { out = 0; return true; }
  if (endpoint.kind == WindowEndpointKind::End) { out = length; return true; }
  out = frame.prepared[endpoint.cut].i;
  return out > 0 && out < static_cast<std::int64_t>(length);
}

#include "window_execution_device.cuh"

template <DPayloadFlavor Flavor, class Frame>
__device__ __noinline__ DResult d_run_bounded_region_impl(
    const DRegionSegment& segment, const Value* operands,
    const Value* caller_locals, std::uint64_t caller_set,
    const DPayloadTables& payload_tables,
    typename DPayloadFlavorTraits<Flavor>::State& payload_state,
    const DExecutionTables& tables, int& fuel, DRegionWorkspace workspace) {
  if (segment.state_count == 0 || segment.state_count > DMAX_REGION_STATES ||
      segment.request_count == 0 || segment.request_count > DMAX_REGION_REQUESTS ||
      segment.limits.frames > DMAX_REGION_FRAMES || segment.limits.cells > DMAX_REGION_MEMO)
    return d_error(ErrCode::Value);
  for (std::uint32_t i = 0; i < segment.bound_operand_count; ++i)
    if (operands[segment.state_count + i].tag != ValueTag::Int) return d_error(ErrCode::Type);

  std::int64_t lower[DMAX_REGION_STATES];
  std::int64_t upper[DMAX_REGION_STATES];
  bool empty_domain = false;
  if (segment.progress == RegionProgressKind::Coordinates) {
    for (std::uint32_t axis = 0; axis < segment.coordinate_count; ++axis) {
      const auto& domain = segment.coordinate_domains[axis];
      const RegionBound bounds[2] = {domain.lower, domain.upper};
      std::int64_t values[2];
      for (int side = 0; side < 2; ++side) {
        if (bounds[side].kind == RegionBoundKind::Literal) values[side] = bounds[side].literal;
        else {
          const Value value = operands[bounds[side].operand];
          if (value.tag != ValueTag::Int) return d_error(ErrCode::Type);
          values[side] = value.i;
        }
      }
      lower[axis] = values[0]; upper[axis] = values[1];
      if (lower[axis] > upper[axis]) return d_error(ErrCode::Value);
      empty_domain |= segment.coordinate_endpoint == DomainEndpoint::Exclusive &&
                      lower[axis] == upper[axis];
    }
    if (!empty_domain) {
      for (std::uint32_t axis = 0; axis < segment.coordinate_count; ++axis) {
        const auto max_value = segment.coordinate_endpoint == DomainEndpoint::Inclusive
                                   ? upper[axis] : upper[axis] - 1;
        for (std::uint32_t request = 0; request < segment.request_count; ++request) {
          const auto offset = segment.requests[request][segment.coordinate_slots[axis]].offset;
          if (!d_region_addable(lower[axis], offset) || !d_region_addable(max_value, offset))
            return d_error(ErrCode::Value);
        }
      }
    }
  }
  if (segment.limits.frames == 0) return d_error(ErrCode::Timeout);

  if (!workspace.frames || workspace.frame_capacity < segment.limits.frames ||
      (segment.memoized && segment.limits.cells > 0 &&
       (!workspace.memo_keys || !workspace.memo_values ||
        workspace.memo_capacity < segment.limits.cells)))
    return d_error(ErrCode::Value);
  Frame* frames = reinterpret_cast<Frame*>(workspace.frames);
  std::int64_t* memo_keys = workspace.memo_keys;
  Value* memo_values = workspace.memo_values;
  const std::size_t stride = workspace.slot_stride;
  std::uint32_t memo_count = 0;
  std::uint32_t depth = 1;
  for (std::uint32_t i = 0; i < segment.state_count; ++i) frames[0].state[i] = operands[i];
  frames[0].next_request = -1;
  for (;;) {
    auto& frame = frames[(depth - 1) * stride];
    bool completed = false;
    DResult result;
    if (frame.next_request < 0) {
      if (fuel < 0 || segment.limits.entry_fuel > static_cast<std::uint32_t>(fuel))
        return d_error(ErrCode::Timeout);
      fuel -= static_cast<int>(segment.limits.entry_fuel);
      for (std::uint32_t i = 0; i < segment.state_count; ++i)
        if (frame.state[i].tag != segment.state_types[i]) return d_error(ErrCode::Type);

      bool boundary = false;
      bool base = false;
      if (segment.progress == RegionProgressKind::Coordinates) {
        for (std::uint32_t axis = 0; axis < segment.coordinate_count; ++axis) {
          const auto value = frame.state[segment.coordinate_slots[axis]].i;
          boundary |= value < lower[axis] ||
                      (segment.coordinate_endpoint == DomainEndpoint::Inclusive
                           ? value > upper[axis] : value >= upper[axis]);
        }
      } else base = Value::container_len(frame.state[segment.sequence_state]) <= 1;
      if (!boundary && !base) {
        result = d_region_phase<Flavor>(segment, segment.base_predicate_phase,
            ValueTag::Bool, frame, caller_locals, caller_set, payload_tables, payload_state, tables, fuel);
        if (result.is_error) return result;
        base = result.value.b;
      }
      if (boundary || base) {
        result = d_region_phase<Flavor>(segment,
            boundary ? segment.boundary_phase : segment.base_body_phase, segment.result_type,
            frame, caller_locals, caller_set, payload_tables, payload_state, tables, fuel, true);
        if (result.is_error) return result;
        completed = true;
      } else {
        if (segment.memoized) {
          for (std::uint32_t cell = 0; cell < memo_count; ++cell) {
            bool matches = true;
            for (std::uint32_t axis = 0; axis < segment.state_count; ++axis)
              matches &= memo_keys[(cell * DMAX_REGION_STATES + axis) * stride] == frame.state[axis].i;
            if (matches) { result = d_ok(memo_values[cell * stride]); completed = true; break; }
          }
        }
        if (!completed) {
          for (std::uint32_t i = 0; i < segment.preparation_count; ++i) {
            result = d_region_phase<Flavor>(segment, segment.preparation_phases[i],
                segment.preparation_types[i], frame, caller_locals, caller_set,
                payload_tables, payload_state, tables, fuel);
            if (result.is_error) return result;
            if (segment.preparation_kinds[i] == RegionPreparationKind::InteriorCut) {
              const std::int64_t high = Value::container_len(frame.state[segment.sequence_state]) - 1;
              result.value = Value::from_int(result.value.i < 1 ? 1 : result.value.i > high ? high : result.value.i);
            }
            frame.prepared[i] = result.value;
          }
          frame.next_request = 0;
        }
      }
    }
    if (!completed && static_cast<std::uint32_t>(frame.next_request) < segment.request_count) {
      Value next[DMAX_REGION_STATES];
      const auto request = static_cast<std::uint32_t>(frame.next_request);
      for (std::uint32_t state = 0; state < segment.state_count; ++state) {
        const auto& transition = segment.requests[request][state];
        switch (transition.kind) {
          case RegionTransitionKind::CopyState: next[state] = frame.state[transition.source_state]; break;
          case RegionTransitionKind::CoordinateOffset: {
            const auto value = frame.state[transition.source_state].i;
            if (!d_region_addable(value, transition.offset)) return d_error(ErrCode::Value);
            next[state] = Value::from_int(value + transition.offset);
            break;
          }
          case RegionTransitionKind::SequenceWindow: {
            const auto source = frame.state[transition.source_state];
            const auto length = Value::container_len(source);
            std::int64_t begin, end;
            if (length < 2 || !d_region_endpoint(transition.window.begin, length, frame, begin) ||
                !d_region_endpoint(transition.window.end, length, frame, end)) return d_error(ErrCode::Value);
            const Value args[]{source, Value::from_int(begin), Value::from_int(end)};
            ErrCode error = ErrCode::Value;
            if (!d_builtin_call<Flavor>(BuiltinId::Slice, args, 3, payload_tables, payload_state, next[state], error))
              return d_error(error);
            break;
          }
          case RegionTransitionKind::Expression: {
            result = d_region_phase<Flavor>(segment,
                segment.request_expression_phases[transition.expression], segment.state_types[state],
                frame, caller_locals, caller_set, payload_tables, payload_state, tables, fuel);
            if (result.is_error) return result;
            next[state] = result.value;
            break;
          }
        }
      }
      if (depth >= segment.limits.frames) return d_error(ErrCode::Timeout);
      ++frame.next_request;
      for (std::uint32_t i = 0; i < segment.state_count; ++i) frames[depth * stride].state[i] = next[i];
      frames[depth * stride].next_request = -1;
      ++depth;
      continue;
    }
    if (!completed) {
      for (std::uint32_t i = 0; i < segment.request_count; ++i)
        if (frame.results[i].tag != segment.result_type &&
            frame.results[i].tag != ValueTag::FallbackToken) return d_error(ErrCode::Type);
      result = d_region_phase<Flavor>(segment, segment.combine_phase, segment.result_type,
          frame, caller_locals, caller_set, payload_tables, payload_state, tables, fuel, true);
      if (result.is_error) return result;
      if (result.value.tag != frame.results[0].tag) return d_error(ErrCode::Type);
      if (segment.memoized) {
        if (memo_count >= segment.limits.cells) return d_error(ErrCode::Timeout);
        for (std::uint32_t i = 0; i < segment.state_count; ++i) memo_keys[(memo_count * DMAX_REGION_STATES + i) * stride] = frame.state[i].i;
        memo_values[memo_count * stride] = result.value;
        ++memo_count;
      }
    }
    if (--depth == 0) return result;
    auto& parent = frames[(depth - 1) * stride];
    if (parent.next_request > 1 && result.value.tag != parent.results[0].tag)
      return d_error(ErrCode::Type);
    parent.results[parent.next_request - 1] = result.value;
  }
}

template <DPayloadFlavor Flavor>
__device__ DResult d_run_bounded_region(
    const DRegionSegment& segment, const Value* operands,
    const Value* caller_locals, std::uint64_t caller_set,
    const DPayloadTables& payload_tables,
    typename DPayloadFlavorTraits<Flavor>::State& payload_state,
    const DExecutionTables& tables, int& fuel, DRegionWorkspace workspace) {
  if constexpr (Flavor == DPayloadFlavor::BoundIntListViews) {
    if(workspace.window_executor) {
      if(workspace.two_state_frames)return d_run_window_region<DCompactPairRegionFrame>(segment,operands,caller_locals,
          caller_set,payload_tables,payload_state,tables,fuel,workspace);
      return d_run_window_region<DCompactRegionFrame>(segment,operands,caller_locals,
          caller_set,payload_tables,payload_state,tables,fuel,workspace);
    }
  }
  if (workspace.compact_frames) {
    if (segment.state_count > (workspace.two_state_frames ? 2u : 1u) || segment.preparation_count > 1 || segment.request_count > 2)
      return d_error(ErrCode::Value);
    if (workspace.two_state_frames)
      return d_run_bounded_region_impl<Flavor, DCompactPairRegionFrame>(segment, operands,
          caller_locals, caller_set, payload_tables, payload_state, tables, fuel, workspace);
    return d_run_bounded_region_impl<Flavor, DCompactRegionFrame>(segment, operands,
        caller_locals, caller_set, payload_tables, payload_state, tables, fuel, workspace);
  }
  return d_run_bounded_region_impl<Flavor, DRegionFrame>(segment, operands,
      caller_locals, caller_set, payload_tables, payload_state, tables, fuel, workspace);
}
