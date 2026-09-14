#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "gagp/core/recurrence_rank.hpp"

namespace gagp {

inline constexpr std::uint32_t kSequenceCutCapacity = 4;

enum class WindowEndpointKind {
  Begin,
  End,
  InteriorCut,
};

struct WindowEndpoint {
  WindowEndpointKind kind = WindowEndpointKind::Begin;
  std::uint32_t cut = 0;
};

struct SequenceWindow {
  WindowEndpoint begin;
  WindowEndpoint end;
};

struct SequenceProgress {
  std::uint32_t cut_count = 0;
  std::vector<SequenceWindow> windows;
  DuplicatePolicy duplicate_policy = DuplicatePolicy::Reject;
};

void validate_sequence_progress(const SequenceProgress& progress);

std::int64_t clamp_interior_cut(std::int64_t raw, std::uint32_t length);

std::pair<std::int64_t, std::int64_t> resolve_sequence_window(
    const SequenceWindow& window,
    std::uint32_t length,
    const std::vector<std::int64_t>& cuts);

}  // namespace gagp
