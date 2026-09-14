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

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << "\n";
  return condition;
}

template <typename Function>
bool rejects(Function&& function, const std::string& message) {
  try {
    function();
  } catch (const std::invalid_argument&) {
    return true;
  } catch (...) {
    std::cerr << "FAIL: " << message << " (wrong exception type)\n";
    return false;
  }
  std::cerr << "FAIL: " << message << "\n";
  return false;
}

CoordinateRecurrence one_dimensional(int direction, std::int64_t offset) {
  CoordinateRecurrence recurrence;
  recurrence.domains = {{-10, 11}};
  recurrence.endpoint = DomainEndpoint::Exclusive;
  recurrence.rank = {{0, direction}};
  recurrence.offsets = {{offset}};
  return recurrence;
}

bool test_static_progress_without_domains() {
  gagp::validate_coordinate_progress(
      1, {{0, 1}}, {{std::numeric_limits<std::int64_t>::min()}},
      DuplicatePolicy::Reject);

  if (!rejects(
          [] {
            gagp::validate_coordinate_progress(
                0, {}, {{-1}}, DuplicatePolicy::Reject);
          },
          "static progress rejects zero dimensions")) return false;
  if (!rejects(
          [] {
            gagp::validate_coordinate_progress(
                2, {{0, 1}, {0, -1}}, {{-1, 0}},
                DuplicatePolicy::Reject);
          },
          "static progress rejects a malformed rank permutation")) return false;
  if (!rejects(
          [] {
            gagp::validate_coordinate_progress(
                1, {{0, 1}}, {{-1}, {1}}, DuplicatePolicy::Reject);
          },
          "static progress rejects an opposing cyclic request")) return false;

  CoordinateRecurrence overflowing;
  overflowing.domains = {{std::numeric_limits<std::int64_t>::min(),
                          std::numeric_limits<std::int64_t>::min()}};
  overflowing.endpoint = DomainEndpoint::Inclusive;
  overflowing.rank = {{0, 1}};
  overflowing.offsets = {{std::numeric_limits<std::int64_t>::min()}};
  return rejects(
      [&] { gagp::validate_coordinate_recurrence(overflowing); },
      "runtime domains still reject an otherwise valid static edge overflow");
}

bool test_valid_rank_patterns() {
  gagp::validate_coordinate_recurrence(one_dimensional(1, -1));
  gagp::validate_coordinate_recurrence(one_dimensional(-1, 1));

  CoordinateRecurrence mixed;
  mixed.domains = {{-10, 10}, {-20, 20}, {-30, 30}};
  mixed.endpoint = DomainEndpoint::Inclusive;
  mixed.rank = {{2, -1}, {0, 1}, {1, -1}};
  mixed.offsets = {
      {2, -3, 1},   // Coordinate 2 decides the edge.
      {-1, 3, 0},   // Coordinate 0 decides after a tie on coordinate 2.
      {0, 4, 0},    // Coordinate 1 increases under its reverse rank.
  };
  gagp::validate_coordinate_recurrence(mixed);
  return true;
}

bool test_rank_and_request_shapes() {
  CoordinateRecurrence recurrence = one_dimensional(1, -1);
  recurrence.domains.clear();
  if (!rejects([&] { gagp::validate_coordinate_recurrence(recurrence); },
               "zero coordinate dimensions are rejected")) return false;

  recurrence = one_dimensional(1, -1);
  recurrence.domains.assign(gagp::kRecurrenceCoordinateCapacity + 1, {0, 2});
  recurrence.rank.clear();
  if (!rejects([&] { gagp::validate_coordinate_recurrence(recurrence); },
               "coordinates beyond capacity are rejected")) return false;

  recurrence = one_dimensional(1, -1);
  recurrence.offsets.clear();
  if (!rejects([&] { gagp::validate_coordinate_recurrence(recurrence); },
               "zero requests are rejected")) return false;

  recurrence = one_dimensional(1, -1);
  recurrence.offsets.assign(gagp::kRecurrenceRequestCapacity + 1, {-1});
  if (!rejects([&] { gagp::validate_coordinate_recurrence(recurrence); },
               "requests beyond capacity are rejected")) return false;

  CoordinateRecurrence two_dimensional;
  two_dimensional.domains = {{0, 4}, {0, 4}};
  two_dimensional.rank = {{0, 1}, {1, 1}};
  two_dimensional.offsets = {{-1, 0}};

  recurrence = two_dimensional;
  recurrence.rank.pop_back();
  if (!rejects([&] { gagp::validate_coordinate_recurrence(recurrence); },
               "an incomplete rank is rejected")) return false;
  recurrence = two_dimensional;
  recurrence.rank = {{0, 1}, {0, -1}};
  if (!rejects([&] { gagp::validate_coordinate_recurrence(recurrence); },
               "a repeated rank coordinate is rejected")) return false;
  recurrence = two_dimensional;
  recurrence.rank = {{0, 1}, {2, -1}};
  if (!rejects([&] { gagp::validate_coordinate_recurrence(recurrence); },
               "an out-of-range rank coordinate is rejected")) return false;
  recurrence = two_dimensional;
  recurrence.rank[0].direction = 0;
  if (!rejects([&] { gagp::validate_coordinate_recurrence(recurrence); },
               "zero rank direction is rejected")) return false;
  recurrence = two_dimensional;
  recurrence.rank[0].direction = 2;
  if (!rejects([&] { gagp::validate_coordinate_recurrence(recurrence); },
               "non-unit rank direction is rejected")) return false;
  recurrence = two_dimensional;
  recurrence.offsets = {{-1}};
  return rejects([&] { gagp::validate_coordinate_recurrence(recurrence); },
                 "an offset with the wrong dimension is rejected");
}

bool test_strict_progress_and_duplicates() {
  CoordinateRecurrence recurrence = one_dimensional(1, 0);
  if (!rejects([&] { gagp::validate_coordinate_recurrence(recurrence); },
               "a zero edge is rejected")) return false;

  recurrence = one_dimensional(1, 1);
  if (!rejects([&] { gagp::validate_coordinate_recurrence(recurrence); },
               "an increasing edge under forward rank is rejected")) return false;
  recurrence = one_dimensional(-1, -1);
  if (!rejects([&] { gagp::validate_coordinate_recurrence(recurrence); },
               "a decreasing edge under reverse rank is rejected")) return false;
  recurrence = one_dimensional(1, -1);
  recurrence.offsets = {{-1}, {1}};
  if (!rejects([&] { gagp::validate_coordinate_recurrence(recurrence); },
               "opposing requests that form a cycle are rejected")) return false;

  recurrence.domains = {{-5, 5}, {-5, 5}};
  recurrence.rank = {{0, 1}, {1, 1}};
  recurrence.offsets = {{1, -1}};
  if (!rejects([&] { gagp::validate_coordinate_recurrence(recurrence); },
               "a later decrease cannot rescue an earlier increase")) return false;

  recurrence = one_dimensional(1, -1);
  recurrence.offsets = {{-1}, {-1}};
  if (!rejects([&] { gagp::validate_coordinate_recurrence(recurrence); },
               "duplicate edges are rejected by default")) return false;
  recurrence.duplicate_policy = DuplicatePolicy::Allow;
  gagp::validate_coordinate_recurrence(recurrence);

  recurrence = one_dimensional(1, -1);
  recurrence.duplicate_policy = static_cast<DuplicatePolicy>(99);
  return rejects([&] { gagp::validate_coordinate_recurrence(recurrence); },
                 "an unknown duplicate policy is rejected");
}

bool test_domain_and_addition_boundaries() {
  CoordinateRecurrence recurrence = one_dimensional(1, -1);
  recurrence.domains = {{2, 1}};
  if (!rejects([&] { gagp::validate_coordinate_recurrence(recurrence); },
               "a reversed exclusive domain is rejected")) return false;
  recurrence.endpoint = DomainEndpoint::Inclusive;
  if (!rejects([&] { gagp::validate_coordinate_recurrence(recurrence); },
               "a reversed inclusive domain is rejected")) return false;

  recurrence = one_dimensional(-1, 1);
  recurrence.endpoint = DomainEndpoint::Inclusive;
  recurrence.domains = {{std::numeric_limits<std::int64_t>::max(),
                         std::numeric_limits<std::int64_t>::max()}};
  if (!rejects([&] { gagp::validate_coordinate_recurrence(recurrence); },
               "positive dependency overflow is rejected")) return false;

  recurrence = one_dimensional(1, -1);
  recurrence.endpoint = DomainEndpoint::Inclusive;
  recurrence.domains = {{std::numeric_limits<std::int64_t>::min(),
                         std::numeric_limits<std::int64_t>::min()}};
  if (!rejects([&] { gagp::validate_coordinate_recurrence(recurrence); },
               "negative dependency overflow is rejected")) return false;

  recurrence = one_dimensional(-1, 1);
  recurrence.domains = {{std::numeric_limits<std::int64_t>::max(),
                         std::numeric_limits<std::int64_t>::max()}};
  recurrence.endpoint = DomainEndpoint::Exclusive;
  gagp::validate_coordinate_recurrence(recurrence);

  recurrence.endpoint = DomainEndpoint::Inclusive;
  recurrence.domains = {{0, 0}};
  recurrence.rank = {{0, 1}};
  recurrence.offsets = {{std::numeric_limits<std::int64_t>::min()}};
  gagp::validate_coordinate_recurrence(recurrence);
  recurrence.rank = {{0, -1}};
  recurrence.offsets = {{std::numeric_limits<std::int64_t>::max()}};
  gagp::validate_coordinate_recurrence(recurrence);

  recurrence.endpoint = static_cast<DomainEndpoint>(99);
  return rejects([&] { gagp::validate_coordinate_recurrence(recurrence); },
                 "an unknown endpoint policy is rejected");
}

bool test_bounded_cardinality() {
  if (!check(gagp::bounded_coordinate_cardinality(
                 {{0, 3}, {-2, 2}}, DomainEndpoint::Exclusive, 12) == 12,
             "exclusive multidimensional cardinality is exact")) return false;
  if (!check(gagp::bounded_coordinate_cardinality(
                 {{0, 3}, {-2, 2}}, DomainEndpoint::Inclusive, 20) == 20,
             "inclusive multidimensional cardinality is exact")) return false;
  if (!check(gagp::bounded_coordinate_cardinality(
                 {{7, 7}, {0, 10}}, DomainEndpoint::Exclusive, 0) == 0,
             "an empty product has cardinality zero even with limit zero")) return false;
  if (!rejects(
          [] {
            (void)gagp::bounded_coordinate_cardinality(
                {{0, 1}}, DomainEndpoint::Exclusive, 0);
          },
          "a nonempty product exceeds limit zero")) return false;
  if (!rejects(
          [] {
            (void)gagp::bounded_coordinate_cardinality(
                {{0, 3}, {-2, 2}}, DomainEndpoint::Exclusive, 11);
          },
          "a product one above the limit is rejected")) return false;
  if (!rejects(
          [] {
            (void)gagp::bounded_coordinate_cardinality(
                {}, DomainEndpoint::Exclusive, 0);
          },
          "cardinality rejects an empty domain layout")) return false;

  const std::int64_t minimum = std::numeric_limits<std::int64_t>::min();
  const std::int64_t maximum = std::numeric_limits<std::int64_t>::max();
  if (std::numeric_limits<std::size_t>::max() ==
      std::numeric_limits<std::uint64_t>::max()) {
    if (!check(gagp::bounded_coordinate_cardinality(
                   {{minimum, maximum}}, DomainEndpoint::Exclusive,
                   std::numeric_limits<std::size_t>::max()) ==
                   std::numeric_limits<std::size_t>::max(),
               "full signed exclusive span is handled without overflow")) return false;
  }
  return rejects(
      [&] {
        (void)gagp::bounded_coordinate_cardinality(
            {{minimum, maximum}}, DomainEndpoint::Inclusive,
            std::numeric_limits<std::size_t>::max());
      },
      "full signed inclusive span exceeds every size_t limit");
}

}  // namespace

int main() {
  if (!test_static_progress_without_domains()) return 1;
  if (!test_valid_rank_patterns()) return 1;
  if (!test_rank_and_request_shapes()) return 1;
  if (!test_strict_progress_and_duplicates()) return 1;
  if (!test_domain_and_addition_boundaries()) return 1;
  if (!test_bounded_cardinality()) return 1;
  std::cout << "gagp_test_recurrence_rank: OK\n";
  return 0;
}
