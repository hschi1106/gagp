#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

#include "gagp/core/recurrence_rank.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "recurrence_memo.hpp"

namespace gagp::detail {

using RegionState = std::array<Value, kRecurrenceCoordinateCapacity>;
inline constexpr std::size_t kRegionPreparedCapacity = 4;

// These are execution parameters, never inferred from a grammar search budget.
struct RegionExecutionLayout {
  std::uint32_t state_count = 1;
  std::uint32_t request_count = 1;
  std::uint32_t frame_limit = 64;
  std::uint32_t cell_limit = 128;
  std::uint32_t entry_fuel = 1;
  bool memoized = false;
};

enum class RegionFrameStage : std::uint8_t { Enter, Requests };

// No owning payloads, pointers, or host continuation objects occur in a frame.
// The same fixed state/preparation/result layout can be packed for a device stack.
struct RegionFrame {
  RegionState state{};
  std::array<Value, kRegionPreparedCapacity> prepared{};
  std::array<Value, kRecurrenceRequestCapacity> results{};
  RegionFrameStage stage = RegionFrameStage::Enter;
  std::uint32_t next_request = 0;
};

static_assert(std::is_trivially_copyable_v<RegionFrame>);

struct RegionEntry {
  bool terminal = false;
  ExecResult result;
};

struct RegionScratch {
  std::vector<RegionFrame> frames;
  RecurrenceMemo memo;
  std::size_t peak_frames = 0;
  std::size_t peak_cells = 0;

  std::size_t storage_bytes() const {
    return frames.capacity() * sizeof(RegionFrame) + memo.storage_bytes();
  }
};

inline ExecResult region_failure(ErrCode code, const char* message) {
  return ExecResult{true, Value::invalid(), Err{code, message}};
}

inline bool valid_region_layout(const RegionExecutionLayout& layout,
                                const RegionScratch& scratch) {
  return layout.state_count >= 1 &&
         layout.state_count <= kRecurrenceCoordinateCapacity &&
         layout.request_count >= 1 &&
         layout.request_count <= kRecurrenceRequestCapacity &&
         layout.entry_fuel <= static_cast<std::uint32_t>(std::numeric_limits<int>::max()) &&
         layout.frame_limit <= scratch.frames.max_size() &&
         (!layout.memoized || RecurrenceMemo::supports_limit(layout.cell_limit));
}

inline RecurrenceKey region_key(const RegionState& state, std::uint32_t count) {
  RecurrenceKey key{};
  for (std::uint32_t i = 0; i < count; ++i) key[i] = state[i].i;
  return key;
}

inline void push_region_frame(RegionScratch& scratch, const RegionState& state,
                              std::uint32_t limit) {
  if (scratch.frames.size() == scratch.frames.capacity()) {
    const std::size_t capacity = scratch.frames.capacity();
    const std::size_t remaining = static_cast<std::size_t>(limit) - capacity;
    const std::size_t growth = std::min(std::max<std::size_t>(capacity, 1), remaining);
    scratch.frames.reserve(capacity + growth);
  }
  scratch.frames.emplace_back();
  scratch.frames.back().state = state;
  scratch.peak_frames = std::max(scratch.peak_frames, scratch.frames.size());
}

// Internal engine for verified static-region adapters. It is not an executable
// plugin API: lowering must prove every request transition before calling it.
//
// Adapter methods (all share the caller's remaining fuel):
//   enter(frame, fuel) -> RegionEntry: validate state, boundary, then base.
//   prepare(frame, fuel) -> ExecResult: per-frame values, once after a memo miss.
//   request(frame, ordinal, next_state, fuel) -> ExecResult: ordered next state.
//   combine(frame, fuel) -> ExecResult: result tag checks and the combine body.
// Adapter callbacks execute phase code, not recursive calls to this engine.
// Initial arguments and the containing opcode are evaluated/charged by the VM.
template <class Adapter>
ExecResult execute_bounded_region(const RegionExecutionLayout& layout,
                                  const RegionState& initial,
                                  Adapter& adapter, RegionScratch& scratch,
                                  int& fuel) {
  if (!valid_region_layout(layout, scratch))
    return region_failure(ErrCode::Value, "invalid bounded region execution layout");
  scratch.frames.clear();
  scratch.memo.reset(layout.memoized ? layout.cell_limit : 0);
  scratch.peak_frames = 0;
  scratch.peak_cells = 0;
  if (layout.frame_limit == 0)
    return region_failure(ErrCode::Timeout, "bounded region frame capacity exhausted");
  push_region_frame(scratch, initial, layout.frame_limit);

  for (;;) {
    RegionFrame& frame = scratch.frames.back();
    ExecResult completed;
    bool has_result = false;
    if (frame.stage == RegionFrameStage::Enter) {
      if (fuel < 0 || layout.entry_fuel > static_cast<std::uint32_t>(fuel))
        return region_failure(ErrCode::Timeout, "out of fuel");
      fuel -= static_cast<int>(layout.entry_fuel);
      RegionEntry entry = adapter.enter(frame, fuel);
      if (entry.result.is_error) return entry.result;
      if (entry.terminal) {
        completed = entry.result;
        has_result = true;
      } else {
        if (layout.memoized) {
          for (std::uint32_t i = 0; i < layout.state_count; ++i) {
            if (frame.state[i].tag != ValueTag::Int)
              return region_failure(ErrCode::Type, "memoized coordinates must be Int");
          }
          Value cached;
          if (scratch.memo.find(region_key(frame.state, layout.state_count), &cached)) {
            completed.value = cached;
            has_result = true;
          }
        }
        if (!has_result) {
          ExecResult prepared = adapter.prepare(frame, fuel);
          if (prepared.is_error) return prepared;
          frame.stage = RegionFrameStage::Requests;
        }
      }
    }

    if (!has_result) {
      if (frame.next_request < layout.request_count) {
        RegionState next{};
        ExecResult requested = adapter.request(frame, frame.next_request, next, fuel);
        if (requested.is_error) return requested;
        // Request construction (including any slice) precedes capacity failure;
        // a rejected child receives no frame-entry charge.
        if (scratch.frames.size() >= layout.frame_limit)
          return region_failure(ErrCode::Timeout, "bounded region frame capacity exhausted");
        ++frame.next_request;
        push_region_frame(scratch, next, layout.frame_limit);
        continue;
      }
      completed = adapter.combine(frame, fuel);
      if (completed.is_error) return completed;
      // Only successful nonbase/nonboundary combines enter the cache. A combine
      // error or its result tag error therefore precedes cell exhaustion.
      if (layout.memoized &&
          !scratch.memo.insert(region_key(frame.state, layout.state_count), completed.value))
        return region_failure(ErrCode::Timeout, "bounded region memo capacity exhausted");
      scratch.peak_cells = std::max(scratch.peak_cells, scratch.memo.size());
    }

    scratch.frames.pop_back();
    if (scratch.frames.empty()) return completed;
    RegionFrame& parent = scratch.frames.back();
    parent.results[parent.next_request - 1] = completed.value;
  }
}

}  // namespace gagp::detail
