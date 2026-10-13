#pragma once
#include "bounded_region.hpp"

namespace gagp::detail {

// Verified one-coordinate regions need no preparation/state arrays per frame.
// A single binding frame feeds the existing phase executor; tagged child values
// retain the generic fallback/type behavior. Memo identity and capacity are unchanged.
template <class Adapter>
ExecResult execute_coordinate_region(const RegionExecutionLayout& layout,
    const RegionState& initial, Adapter& adapter, RegionScratch& scratch, int& fuel) {
  if (!valid_region_layout(layout, scratch))
    return region_failure(ErrCode::Value, "invalid bounded region execution layout");
  auto& frames = scratch.coordinate_frames;
  frames.clear();
  scratch.frames.clear();
  scratch.memo.reset(layout.memoized ? layout.cell_limit : 0);
  scratch.peak_frames = scratch.peak_cells = 0;
  if (layout.frame_limit == 0)
    return region_failure(ErrCode::Timeout, "bounded region frame capacity exhausted");
  const auto push = [&](std::int64_t coordinate) {
    if (frames.size() == frames.capacity()) {
      const auto capacity = frames.capacity();
      const auto growth = std::min(std::max<std::size_t>(capacity, 1),
          static_cast<std::size_t>(layout.frame_limit) - capacity);
      frames.reserve(capacity + growth);
    }
    frames.emplace_back();
    frames.back().coordinate = coordinate;
    scratch.peak_frames = std::max(scratch.peak_frames, frames.size());
  };
  push(initial[0].i);
  // The initial type check must follow its first entry charge.
  if (initial[0].tag != ValueTag::Int) {
    if (fuel < 0 || layout.entry_fuel > static_cast<unsigned>(fuel))
      return region_failure(ErrCode::Timeout, "out of fuel");
    fuel -= static_cast<int>(layout.entry_fuel);
    return region_failure(ErrCode::Type, "bounded region state type mismatch");
  }
  RegionFrame binding;
  const auto& segment = adapter.segment;
  const auto& domain = adapter.domains[0];
  for (;;) {
    auto& frame = frames.back();
    ExecResult result;
    bool completed = false;
    binding.state[0] = Value::from_int(frame.coordinate);
    if (frame.next_request < 0) {
      if (fuel < 0 || layout.entry_fuel > static_cast<unsigned>(fuel))
        return region_failure(ErrCode::Timeout, "out of fuel");
      fuel -= static_cast<int>(layout.entry_fuel);
      const bool boundary = frame.coordinate < domain.lower ||
          (segment.plan.coordinate_endpoint == DomainEndpoint::Exclusive ?
           frame.coordinate >= domain.upper : frame.coordinate > domain.upper);
      bool base = false;
      if (!boundary) {
        result = adapter.run(segment.base_predicate, ValueTag::Bool, binding, fuel, false, 1);
        if (result.is_error) return result;
        base = result.value.b;
      }
      if (boundary || base) {
        result = adapter.run(boundary ? *segment.boundary : segment.base_body,
            ValueTag::Int, binding, fuel, true, boundary ? 0 : 2);
        if (result.is_error) return result;
        completed = true;
      } else {
        if (layout.memoized && scratch.memo.find({frame.coordinate, 0, 0, 0}, &result.value))
          completed = true;
        if (!completed) frame.next_request = 0;
      }
    }
    if (!completed && static_cast<unsigned>(frame.next_request) < layout.request_count) {
      const auto offset = segment.plan.requests[frame.next_request].states[0].offset;
      const auto value = frame.coordinate;
      if ((offset > 0 && value > std::numeric_limits<std::int64_t>::max() - offset) ||
          (offset < 0 && value < std::numeric_limits<std::int64_t>::min() - offset))
        return region_failure(ErrCode::Value, "bounded region coordinate overflow");
      if (frames.size() >= layout.frame_limit)
        return region_failure(ErrCode::Timeout, "bounded region frame capacity exhausted");
      ++frame.next_request;
      push(value + offset);
      continue;
    }
    if (!completed) {
      for (std::size_t i = 0; i < layout.request_count; ++i) binding.results[i] = frame.results[i];
      result = adapter.combine(binding, fuel);
      if (result.is_error) return result;
      if (layout.memoized && !scratch.memo.insert({frame.coordinate, 0, 0, 0}, result.value))
        return region_failure(ErrCode::Timeout, "bounded region memo capacity exhausted");
      scratch.peak_cells = std::max(scratch.peak_cells, scratch.memo.size());
    }
    frames.pop_back();
    if (frames.empty()) return result;
    auto& parent = frames.back();
    parent.results[parent.next_request - 1] = result.value;
  }
}

template <class Adapter>
ExecResult execute_selected_region(const RegionExecutionLayout& layout,
    const RegionState& initial, Adapter& adapter, RegionScratch& scratch, int& fuel) {
  if (adapter.profiles && adapter.profiles->coordinate)
    return execute_coordinate_region(layout, initial, adapter, scratch, fuel);
  return execute_bounded_region(layout, initial, adapter, scratch, fuel);
}
}  // namespace gagp::detail
