#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/core/sequence_rank.hpp"

namespace {

using gagp::DuplicatePolicy;
using gagp::SequenceProgress;
using gagp::SequenceWindow;
using gagp::WindowEndpoint;
using gagp::WindowEndpointKind;

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

WindowEndpoint begin() { return {WindowEndpointKind::Begin, 0}; }
WindowEndpoint end() { return {WindowEndpointKind::End, 0}; }
WindowEndpoint cut(std::uint32_t index) {
  return {WindowEndpointKind::InteriorCut, index};
}

bool test_valid_static_windows() {
  SequenceProgress progress;
  progress.cut_count = 2;
  progress.windows = {
      {begin(), cut(0)},
      {cut(0), end()},
      {cut(0), cut(1)},
      {cut(1), cut(0)},
      {cut(0), cut(0)},
      {end(), begin()},
      {begin(), begin()},
      {end(), end()},
  };
  gagp::validate_sequence_progress(progress);
  return check(progress.windows.size() == gagp::kRecurrenceRequestCapacity,
               "the request-capacity boundary is accepted");
}

bool test_static_shape_and_duplicates() {
  SequenceProgress progress;
  progress.windows.clear();
  if (!rejects([&] { gagp::validate_sequence_progress(progress); },
               "zero windows are rejected")) return false;

  progress.windows.assign(gagp::kRecurrenceRequestCapacity + 1,
                          SequenceWindow{end(), begin()});
  progress.duplicate_policy = DuplicatePolicy::Allow;
  if (!rejects([&] { gagp::validate_sequence_progress(progress); },
               "windows beyond request capacity are rejected")) return false;

  progress = {};
  progress.cut_count = gagp::kSequenceCutCapacity + 1;
  progress.windows = {{end(), begin()}};
  if (!rejects([&] { gagp::validate_sequence_progress(progress); },
               "cuts beyond capacity are rejected")) return false;

  progress = {};
  progress.windows = {{begin(), end()}};
  if (!rejects([&] { gagp::validate_sequence_progress(progress); },
               "the full Begin-to-End range is rejected")) return false;

  progress = {};
  progress.cut_count = 1;
  progress.windows = {{cut(1), end()}};
  if (!rejects([&] { gagp::validate_sequence_progress(progress); },
               "an out-of-range interior cut is rejected")) return false;

  progress.windows = {{{WindowEndpointKind::Begin, 1}, end()}};
  if (!rejects([&] { gagp::validate_sequence_progress(progress); },
               "a noncanonical Begin cut field is rejected")) return false;
  progress.windows = {{begin(), {WindowEndpointKind::End, 2}}};
  if (!rejects([&] { gagp::validate_sequence_progress(progress); },
               "a noncanonical End cut field is rejected")) return false;
  progress.windows = {{{static_cast<WindowEndpointKind>(99), 0}, end()}};
  if (!rejects([&] { gagp::validate_sequence_progress(progress); },
               "an unknown endpoint kind is rejected")) return false;

  progress.windows = {{begin(), cut(0)}, {begin(), cut(0)}};
  if (!rejects([&] { gagp::validate_sequence_progress(progress); },
               "duplicate windows are rejected by default")) return false;
  progress.duplicate_policy = DuplicatePolicy::Allow;
  gagp::validate_sequence_progress(progress);

  progress.duplicate_policy = static_cast<DuplicatePolicy>(99);
  return rejects([&] { gagp::validate_sequence_progress(progress); },
                 "an unknown duplicate policy is rejected");
}

bool test_cut_clamping() {
  if (!check(gagp::clamp_interior_cut(
                 std::numeric_limits<std::int64_t>::min(), 2) == 1,
             "INT64_MIN clamps to the first interior cut")) return false;
  if (!check(gagp::clamp_interior_cut(
                 std::numeric_limits<std::int64_t>::max(), 10) == 9,
             "INT64_MAX clamps to length minus one")) return false;
  if (!check(gagp::clamp_interior_cut(4, 10) == 4,
             "an interior raw cut remains unchanged")) return false;
  if (!check(gagp::clamp_interior_cut(
                 std::numeric_limits<std::int64_t>::max(),
                 std::numeric_limits<std::uint32_t>::max()) ==
                 static_cast<std::int64_t>(
                     std::numeric_limits<std::uint32_t>::max()) - 1,
             "maximum uint32 length clamps without overflow")) return false;
  if (!rejects([] { (void)gagp::clamp_interior_cut(0, 0); },
               "zero length has no interior cut")) return false;
  return rejects([] { (void)gagp::clamp_interior_cut(0, 1); },
                 "singleton length has no interior cut");
}

bool test_runtime_window_resolution() {
  const std::vector<std::int64_t> cuts{3, 7};
  if (!check(gagp::resolve_sequence_window({begin(), cut(0)}, 10, cuts) ==
                 std::pair<std::int64_t, std::int64_t>{0, 3},
             "ordered prefix window resolves exactly")) return false;
  if (!check(gagp::resolve_sequence_window({cut(1), end()}, 10, cuts) ==
                 std::pair<std::int64_t, std::int64_t>{7, 10},
             "ordered suffix window resolves exactly")) return false;
  if (!check(gagp::resolve_sequence_window({cut(1), cut(0)}, 10, cuts) ==
                 std::pair<std::int64_t, std::int64_t>{7, 3},
             "reversed window order is preserved")) return false;
  if (!check(gagp::resolve_sequence_window({cut(0), cut(0)}, 10, cuts) ==
                 std::pair<std::int64_t, std::int64_t>{3, 3},
             "empty interior window order is preserved")) return false;
  if (!check(gagp::resolve_sequence_window({end(), begin()}, 10, cuts) ==
                 std::pair<std::int64_t, std::int64_t>{10, 0},
             "reversed endpoint window is not normalized")) return false;

  if (!check(gagp::resolve_sequence_window({cut(1), end()}, 10, {-99, 4}) ==
                 std::pair<std::int64_t, std::int64_t>{4, 10},
             "only referenced cuts require runtime interior validity")) return false;
  if (!rejects(
          [] {
            (void)gagp::resolve_sequence_window({cut(0), end()}, 10, {0});
          },
          "a referenced Begin-boundary cut is rejected")) return false;
  if (!rejects(
          [] {
            (void)gagp::resolve_sequence_window({begin(), cut(0)}, 10, {10});
          },
          "a referenced End-boundary cut is rejected")) return false;
  if (!rejects(
          [] {
            (void)gagp::resolve_sequence_window({cut(1), end()}, 10, {4});
          },
          "a missing referenced cut is rejected")) return false;
  if (!rejects(
          [] {
            (void)gagp::resolve_sequence_window({begin(), end()}, 10, {});
          },
          "runtime resolution also rejects the full window")) return false;
  if (!rejects(
          [] {
            (void)gagp::resolve_sequence_window({end(), begin()}, 1, {});
          },
          "runtime resolution requires a nonbase length")) return false;
  return rejects(
      [] {
        (void)gagp::resolve_sequence_window(
            {cut(0), end()}, 10, {1, 2, 3, 4, 5});
      },
      "runtime resolution rejects cuts beyond capacity");
}

}  // namespace

int main() {
  if (!test_valid_static_windows()) return 1;
  if (!test_static_shape_and_duplicates()) return 1;
  if (!test_cut_clamping()) return 1;
  if (!test_runtime_window_resolution()) return 1;
  std::cout << "gagp_test_sequence_rank: OK\n";
  return 0;
}
