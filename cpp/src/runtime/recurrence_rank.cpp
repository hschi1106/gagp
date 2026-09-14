#include "gagp/core/recurrence_rank.hpp"

#include <limits>
#include <set>
#include <stdexcept>

namespace gagp {
namespace {

void validate_endpoint(DomainEndpoint endpoint) {
  if (endpoint != DomainEndpoint::Exclusive &&
      endpoint != DomainEndpoint::Inclusive) {
    throw std::invalid_argument("invalid coordinate domain endpoint policy");
  }
}

void validate_domain_layout(const std::vector<CoordinateDomain>& domains,
                            DomainEndpoint endpoint) {
  if (domains.empty() || domains.size() > kRecurrenceCoordinateCapacity) {
    throw std::invalid_argument("coordinate dimension must be between 1 and 4");
  }
  validate_endpoint(endpoint);
  for (const CoordinateDomain& domain : domains) {
    if (domain.lower > domain.upper) {
      throw std::invalid_argument("coordinate domain lower bound exceeds upper bound");
    }
  }
}

bool has_empty_product(const std::vector<CoordinateDomain>& domains,
                       DomainEndpoint endpoint) {
  if (endpoint != DomainEndpoint::Exclusive) return false;
  for (const CoordinateDomain& domain : domains) {
    if (domain.lower == domain.upper) return true;
  }
  return false;
}

bool addition_is_representable(std::int64_t value, std::int64_t offset) {
  if (offset > 0) {
    return value <= std::numeric_limits<std::int64_t>::max() - offset;
  }
  if (offset < 0) {
    return value >= std::numeric_limits<std::int64_t>::min() - offset;
  }
  return true;
}

}  // namespace

void validate_coordinate_progress(
    std::size_t dimensions,
    const std::vector<RankAxis>& rank,
    const std::vector<std::vector<std::int64_t>>& offsets,
    DuplicatePolicy duplicate_policy) {
  if (dimensions == 0 || dimensions > kRecurrenceCoordinateCapacity) {
    throw std::invalid_argument("coordinate dimension must be between 1 and 4");
  }
  if (offsets.empty() || offsets.size() > kRecurrenceRequestCapacity) {
    throw std::invalid_argument("recurrence request count must be between 1 and 8");
  }
  if (duplicate_policy != DuplicatePolicy::Reject &&
      duplicate_policy != DuplicatePolicy::Allow) {
    throw std::invalid_argument("invalid recurrence duplicate policy");
  }
  if (rank.size() != dimensions) {
    throw std::invalid_argument("rank must contain every coordinate exactly once");
  }

  std::vector<bool> ranked(dimensions, false);
  for (const RankAxis& axis : rank) {
    if (axis.coordinate >= dimensions || ranked[axis.coordinate]) {
      throw std::invalid_argument("rank is not a coordinate permutation");
    }
    if (axis.direction != 1 && axis.direction != -1) {
      throw std::invalid_argument("rank direction must be +1 or -1");
    }
    ranked[axis.coordinate] = true;
  }

  std::set<std::vector<std::int64_t>> unique_offsets;
  for (const std::vector<std::int64_t>& offset : offsets) {
    if (offset.size() != dimensions) {
      throw std::invalid_argument("recurrence offset dimension mismatch");
    }
    if (duplicate_policy == DuplicatePolicy::Reject &&
        !unique_offsets.insert(offset).second) {
      throw std::invalid_argument("duplicate recurrence offset");
    }

    bool strictly_decreasing = false;
    for (const RankAxis& axis : rank) {
      const std::int64_t delta = offset[axis.coordinate];
      if (delta == 0) continue;
      strictly_decreasing = axis.direction == 1 ? delta < 0 : delta > 0;
      break;
    }
    if (!strictly_decreasing) {
      throw std::invalid_argument("recurrence edge does not strictly decrease rank");
    }
  }
}

void validate_coordinate_recurrence(const CoordinateRecurrence& recurrence) {
  validate_domain_layout(recurrence.domains, recurrence.endpoint);
  const std::size_t dimension = recurrence.domains.size();
  validate_coordinate_progress(dimension, recurrence.rank, recurrence.offsets,
                               recurrence.duplicate_policy);

  const bool empty_product = has_empty_product(recurrence.domains,
                                                recurrence.endpoint);
  for (const std::vector<std::int64_t>& offset : recurrence.offsets) {
    if (empty_product) continue;
    for (std::size_t coordinate = 0; coordinate < dimension; ++coordinate) {
      const CoordinateDomain& domain = recurrence.domains[coordinate];
      const std::int64_t maximum = recurrence.endpoint == DomainEndpoint::Inclusive
                                       ? domain.upper
                                       : domain.upper - 1;
      const std::int64_t delta = offset[coordinate];
      if (!addition_is_representable(domain.lower, delta) ||
          !addition_is_representable(maximum, delta)) {
        throw std::invalid_argument("recurrence coordinate addition can overflow");
      }
    }
  }
}

std::size_t bounded_coordinate_cardinality(
    const std::vector<CoordinateDomain>& domains,
    DomainEndpoint endpoint,
    std::size_t limit) {
  validate_domain_layout(domains, endpoint);
  if (has_empty_product(domains, endpoint)) return 0;

  std::size_t cardinality = 1;
  for (const CoordinateDomain& domain : domains) {
    const std::uint64_t distance =
        static_cast<std::uint64_t>(domain.upper) -
        static_cast<std::uint64_t>(domain.lower);
    const bool full_inclusive =
        endpoint == DomainEndpoint::Inclusive &&
        distance == std::numeric_limits<std::uint64_t>::max();
    if (full_inclusive) {
      throw std::invalid_argument("coordinate domain cardinality exceeds limit");
    }

    const std::uint64_t extent =
        endpoint == DomainEndpoint::Inclusive ? distance + 1U : distance;
    const std::size_t available = limit / cardinality;
    if (extent > static_cast<std::uint64_t>(available)) {
      throw std::invalid_argument("coordinate domain cardinality exceeds limit");
    }
    cardinality *= static_cast<std::size_t>(extent);
  }
  return cardinality;
}

}  // namespace gagp
