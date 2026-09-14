#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gagp {

inline constexpr std::size_t kRecurrenceCoordinateCapacity = 4;
inline constexpr std::size_t kRecurrenceRequestCapacity = 8;

struct CoordinateDomain {
  std::int64_t lower = 0;
  std::int64_t upper = 0;
};

enum class DomainEndpoint {
  Exclusive,
  Inclusive,
};

struct RankAxis {
  std::uint32_t coordinate = 0;
  // +1: larger coordinate has larger rank; -1: smaller has larger rank.
  int direction = 1;
};

enum class DuplicatePolicy {
  Reject,
  Allow,
};

struct CoordinateRecurrence {
  std::vector<CoordinateDomain> domains;
  DomainEndpoint endpoint = DomainEndpoint::Exclusive;
  std::vector<RankAxis> rank;
  std::vector<std::vector<std::int64_t>> offsets;
  DuplicatePolicy duplicate_policy = DuplicatePolicy::Reject;
};

// Proves ordered dependency offsets strictly decrease a signed lexicographic
// rank, independently of the coordinate bounds supplied at runtime.
void validate_coordinate_progress(
    std::size_t dimensions,
    const std::vector<RankAxis>& rank,
    const std::vector<std::vector<std::int64_t>>& offsets,
    DuplicatePolicy duplicate_policy);

// Throws invalid_argument for malformed bounds/rank/requests, unproved progress,
// or any in-domain coordinate + offset that cannot be represented as int64_t.
// Requests remain ordered and are never deduplicated. An empty exclusive product
// still requires a valid static rank, but has no coordinate additions to check.
void validate_coordinate_recurrence(const CoordinateRecurrence& recurrence);

// Checks the domain layout independently of the dependency proof. Returns the
// exact Cartesian cardinality, or throws invalid_argument if it exceeds limit.
// This computes a count only; it neither allocates nor promises byte-size safety.
std::size_t bounded_coordinate_cardinality(
    const std::vector<CoordinateDomain>& domains,
    DomainEndpoint endpoint,
    std::size_t limit);

}  // namespace gagp
