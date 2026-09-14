#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/core/recurrence_rank.hpp"

namespace {

using gagp::CoordinateDomain;
using gagp::CoordinateRecurrence;
using gagp::DomainEndpoint;
using gagp::DuplicatePolicy;
using gagp::RankAxis;

using Coordinate = std::vector<std::int64_t>;

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << '\n';
  return condition;
}

bool validator_accepts(const CoordinateRecurrence& recurrence) {
  try {
    gagp::validate_coordinate_recurrence(recurrence);
    return true;
  } catch (const std::invalid_argument&) {
    return false;
  }
}

bool cardinality_rejects(const std::vector<CoordinateDomain>& domains,
                         DomainEndpoint endpoint, std::size_t limit) {
  try {
    (void)gagp::bounded_coordinate_cardinality(domains, endpoint, limit);
    return false;
  } catch (const std::invalid_argument&) {
    return true;
  }
}

bool rank_less(const Coordinate& left, const Coordinate& right,
               const std::vector<RankAxis>& rank) {
  for (const RankAxis& axis : rank) {
    const auto coordinate = static_cast<std::size_t>(axis.coordinate);
    if (left[coordinate] == right[coordinate]) continue;
    return axis.direction == 1 ? left[coordinate] < right[coordinate]
                               : left[coordinate] > right[coordinate];
  }
  return false;
}

void enumerate_vectors(std::size_t dimension, std::int64_t lower,
                       std::int64_t upper, Coordinate* current,
                       std::vector<Coordinate>* out) {
  if (current->size() == dimension) {
    out->push_back(*current);
    return;
  }
  for (std::int64_t value = lower; value <= upper; ++value) {
    current->push_back(value);
    enumerate_vectors(dimension, lower, upper, current, out);
    current->pop_back();
  }
}

std::vector<Coordinate> vectors(std::size_t dimension, std::int64_t lower,
                                std::int64_t upper) {
  std::vector<Coordinate> result;
  Coordinate current;
  enumerate_vectors(dimension, lower, upper, &current, &result);
  return result;
}

std::size_t grid_index(const Coordinate& coordinate) {
  std::size_t result = 0;
  for (const std::int64_t value : coordinate) {
    result = result * 5U + static_cast<std::size_t>(value + 2);
  }
  return result;
}

bool inside_small_domain(const Coordinate& coordinate) {
  return std::all_of(coordinate.begin(), coordinate.end(),
                     [](std::int64_t value) {
                       return value >= -2 && value <= 2;
                     });
}

bool accepted_edges_are_acyclic(const std::vector<RankAxis>& rank,
                                const std::vector<Coordinate>& offsets) {
  const auto coordinates = vectors(2, -2, 2);
  std::vector<std::vector<std::size_t>> outgoing(coordinates.size());
  std::vector<std::size_t> indegree(coordinates.size(), 0);
  for (const Coordinate& source : coordinates) {
    const std::size_t from = grid_index(source);
    for (const Coordinate& offset : offsets) {
      Coordinate target{source[0] + offset[0], source[1] + offset[1]};
      if (!inside_small_domain(target)) continue;
      if (!rank_less(target, source, rank)) return false;
      const std::size_t to = grid_index(target);
      outgoing[from].push_back(to);
      ++indegree[to];
    }
  }

  std::vector<std::size_t> ready;
  for (std::size_t node = 0; node < indegree.size(); ++node) {
    if (indegree[node] == 0) ready.push_back(node);
  }
  std::size_t visited = 0;
  while (!ready.empty()) {
    const std::size_t node = ready.back();
    ready.pop_back();
    ++visited;
    for (const std::size_t successor : outgoing[node]) {
      if (--indegree[successor] == 0) ready.push_back(successor);
    }
  }
  return visited == coordinates.size();
}

bool test_all_signed_2d_ranks_and_offsets() {
  const std::vector<CoordinateDomain> domains{{-2, 2}, {-2, 2}};
  const auto coordinates = vectors(2, -2, 2);
  const auto offsets = vectors(2, -2, 2);
  std::size_t cases = 0;

  for (const std::vector<std::uint32_t>& permutation :
       {std::vector<std::uint32_t>{0, 1},
        std::vector<std::uint32_t>{1, 0}}) {
    for (const int first_direction : {-1, 1}) {
      for (const int second_direction : {-1, 1}) {
        const std::vector<RankAxis> rank{
            {permutation[0], first_direction},
            {permutation[1], second_direction}};
        std::vector<Coordinate> accepted_offsets;

        for (const Coordinate& offset : offsets) {
          bool expected = true;
          for (const Coordinate& source : coordinates) {
            const Coordinate target{source[0] + offset[0],
                                    source[1] + offset[1]};
            expected = expected && rank_less(target, source, rank);
          }
          CoordinateRecurrence recurrence{
              domains, DomainEndpoint::Inclusive, rank, {offset},
              DuplicatePolicy::Reject};
          const bool accepted = validator_accepts(recurrence);
          if (!check(accepted == expected,
                     "validator disagrees with direct lexicographic order at case " +
                         std::to_string(cases))) {
            return false;
          }
          if (accepted) accepted_offsets.push_back(offset);
          ++cases;
        }

        if (!check(accepted_edges_are_acyclic(rank, accepted_offsets),
                   "accepted offsets form a cycle in the finite 2D domain")) {
          return false;
        }
      }
    }
  }
  return check(cases == 200,
               "signed-permutation rank enumeration covered the wrong case count");
}

void enumerate_domain_points(const std::vector<CoordinateDomain>& domains,
                             DomainEndpoint endpoint, std::size_t axis,
                             Coordinate* point, std::size_t* count) {
  if (axis == domains.size()) {
    ++*count;
    return;
  }
  const CoordinateDomain domain = domains[axis];
  for (std::int64_t value = domain.lower;; ++value) {
    const bool inside = endpoint == DomainEndpoint::Inclusive
                            ? value <= domain.upper
                            : value < domain.upper;
    if (!inside) break;
    point->push_back(value);
    enumerate_domain_points(domains, endpoint, axis + 1, point, count);
    point->pop_back();
    if (value == domain.upper) break;
  }
}

std::size_t explicit_cardinality(
    const std::vector<CoordinateDomain>& domains, DomainEndpoint endpoint) {
  Coordinate point;
  std::size_t count = 0;
  enumerate_domain_points(domains, endpoint, 0, &point, &count);
  return count;
}

bool check_small_cardinality_case(
    const std::vector<CoordinateDomain>& domains, DomainEndpoint endpoint,
    std::size_t* cases) {
  const std::size_t expected = explicit_cardinality(domains, endpoint);
  const std::size_t actual =
      gagp::bounded_coordinate_cardinality(domains, endpoint, expected);
  if (!check(actual == expected,
             "bounded cardinality differs from explicit tuple enumeration")) {
    return false;
  }
  if (expected == 0) {
    if (!check(!cardinality_rejects(domains, endpoint, 0),
               "empty coordinate product should fit a zero limit")) {
      return false;
    }
  } else if (!check(cardinality_rejects(domains, endpoint, expected - 1),
                    "cardinality above the exact limit was accepted")) {
    return false;
  }
  ++*cases;
  return true;
}

bool enumerate_small_domains(std::size_t dimension, std::size_t axis,
                             std::vector<CoordinateDomain>* domains,
                             std::size_t* cases) {
  static const std::vector<CoordinateDomain> choices{
      {-2, -2}, {-2, 0}, {-1, 1}, {0, 2}, {2, 2}};
  if (axis == dimension) {
    return check_small_cardinality_case(*domains, DomainEndpoint::Exclusive,
                                        cases) &&
           check_small_cardinality_case(*domains, DomainEndpoint::Inclusive,
                                        cases);
  }
  for (const CoordinateDomain domain : choices) {
    domains->push_back(domain);
    if (!enumerate_small_domains(dimension, axis + 1, domains, cases)) {
      return false;
    }
    domains->pop_back();
  }
  return true;
}

bool test_cardinality_against_explicit_enumeration() {
  std::size_t cases = 0;
  for (std::size_t dimension = 1;
       dimension <= gagp::kRecurrenceCoordinateCapacity; ++dimension) {
    std::vector<CoordinateDomain> domains;
    if (!enumerate_small_domains(dimension, 0, &domains, &cases)) return false;
  }
  return check(cases == 1560,
               "small cardinality enumeration covered the wrong case count");
}

bool test_cardinality_integer_boundaries() {
  using Limits = std::numeric_limits<std::int64_t>;
  if (!check(gagp::bounded_coordinate_cardinality(
                 {{Limits::max(), Limits::max()}}, DomainEndpoint::Inclusive,
                 1) == 1,
             "inclusive INT64_MAX singleton was not counted safely") ||
      !check(gagp::bounded_coordinate_cardinality(
                 {{Limits::min(), Limits::min()}}, DomainEndpoint::Inclusive,
                 1) == 1,
             "inclusive INT64_MIN singleton was not counted safely") ||
      !check(gagp::bounded_coordinate_cardinality(
                 {{Limits::max(), Limits::max()}}, DomainEndpoint::Exclusive,
                 0) == 0,
             "exclusive extreme empty domain was not counted safely") ||
      !check(cardinality_rejects({{Limits::min(), Limits::max()}},
                                 DomainEndpoint::Inclusive,
                                 std::numeric_limits<std::size_t>::max()),
             "full inclusive Int domain should exceed every size_t limit")) {
    return false;
  }

  if (std::numeric_limits<std::size_t>::digits >= 64) {
    const std::size_t maximum = std::numeric_limits<std::size_t>::max();
    if (!check(gagp::bounded_coordinate_cardinality(
                   {{Limits::min(), Limits::max()}},
                   DomainEndpoint::Exclusive, maximum) == maximum,
               "full-width exclusive domain should fit the exact limit") ||
        !check(cardinality_rejects(
                   {{Limits::min(), Limits::max()}, {0, 2}},
                   DomainEndpoint::Exclusive, maximum),
               "coordinate product overflow should exceed the limit")) {
      return false;
    }
  }
  return true;
}

}  // namespace

int main() {
  if (!test_all_signed_2d_ranks_and_offsets()) return 1;
  if (!test_cardinality_against_explicit_enumeration()) return 1;
  if (!test_cardinality_integer_boundaries()) return 1;
  std::cout << "gagp_test_recurrence_rank_properties: OK\n";
  return 0;
}
