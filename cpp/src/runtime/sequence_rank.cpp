#include "gagp/core/sequence_rank.hpp"

#include <array>
#include <set>
#include <stdexcept>

namespace gagp {
namespace {

void validate_endpoint(const WindowEndpoint& endpoint,
                       std::uint32_t cut_count) {
  switch (endpoint.kind) {
    case WindowEndpointKind::Begin:
    case WindowEndpointKind::End:
      if (endpoint.cut != 0) {
        throw std::invalid_argument(
            "begin and end sequence endpoints require canonical cut zero");
      }
      return;
    case WindowEndpointKind::InteriorCut:
      if (endpoint.cut >= cut_count) {
        throw std::invalid_argument("sequence endpoint cut index is out of range");
      }
      return;
  }
  throw std::invalid_argument("invalid sequence window endpoint kind");
}

void validate_window(const SequenceWindow& window, std::uint32_t cut_count) {
  validate_endpoint(window.begin, cut_count);
  validate_endpoint(window.end, cut_count);
  if (window.begin.kind == WindowEndpointKind::Begin &&
      window.end.kind == WindowEndpointKind::End) {
    throw std::invalid_argument("sequence dependency cannot retain the full window");
  }
}

std::array<std::uint32_t, 4> window_key(const SequenceWindow& window) {
  return {
      static_cast<std::uint32_t>(window.begin.kind),
      window.begin.cut,
      static_cast<std::uint32_t>(window.end.kind),
      window.end.cut,
  };
}

std::int64_t resolve_endpoint(const WindowEndpoint& endpoint,
                              std::uint32_t length,
                              const std::vector<std::int64_t>& cuts) {
  switch (endpoint.kind) {
    case WindowEndpointKind::Begin:
      return 0;
    case WindowEndpointKind::End:
      return static_cast<std::int64_t>(length);
    case WindowEndpointKind::InteriorCut: {
      const std::int64_t cut = cuts[endpoint.cut];
      if (cut <= 0 || cut >= static_cast<std::int64_t>(length)) {
        throw std::invalid_argument("resolved sequence cut is not strictly interior");
      }
      return cut;
    }
  }
  throw std::invalid_argument("invalid sequence window endpoint kind");
}

}  // namespace

void validate_sequence_progress(const SequenceProgress& progress) {
  if (progress.cut_count > kSequenceCutCapacity) {
    throw std::invalid_argument("sequence cut count exceeds capacity");
  }
  if (progress.windows.empty() ||
      progress.windows.size() > kRecurrenceRequestCapacity) {
    throw std::invalid_argument("sequence window count must be between 1 and 8");
  }
  if (progress.duplicate_policy != DuplicatePolicy::Reject &&
      progress.duplicate_policy != DuplicatePolicy::Allow) {
    throw std::invalid_argument("invalid sequence duplicate policy");
  }

  std::set<std::array<std::uint32_t, 4>> unique_windows;
  for (const SequenceWindow& window : progress.windows) {
    validate_window(window, progress.cut_count);
    if (progress.duplicate_policy == DuplicatePolicy::Reject &&
        !unique_windows.insert(window_key(window)).second) {
      throw std::invalid_argument("duplicate sequence dependency window");
    }
  }
}

std::int64_t clamp_interior_cut(std::int64_t raw, std::uint32_t length) {
  if (length < 2) {
    throw std::invalid_argument("interior sequence cut requires length at least two");
  }
  const std::int64_t upper = static_cast<std::int64_t>(length) - 1;
  if (raw < 1) return 1;
  if (raw > upper) return upper;
  return raw;
}

std::pair<std::int64_t, std::int64_t> resolve_sequence_window(
    const SequenceWindow& window,
    std::uint32_t length,
    const std::vector<std::int64_t>& cuts) {
  if (length < 2) {
    throw std::invalid_argument("sequence window resolution requires length at least two");
  }
  if (cuts.size() > kSequenceCutCapacity) {
    throw std::invalid_argument("sequence cut count exceeds capacity");
  }
  validate_window(window, static_cast<std::uint32_t>(cuts.size()));
  const std::int64_t begin = resolve_endpoint(window.begin, length, cuts);
  const std::int64_t end = resolve_endpoint(window.end, length, cuts);
  return {begin, end};
}

}  // namespace gagp
