#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/core/builtin.hpp"
#include "gagp/core/sequence_rank.hpp"
#include "gagp/runtime/cpu/builtins_cpu.hpp"
#include "gagp/runtime/payload/payload.hpp"
#include "../../src/runtime/cpu/bounded_region.hpp"

namespace {

using gagp::BuiltinId;
using gagp::BuiltinResult;
using gagp::DuplicatePolicy;
using gagp::ErrCode;
using gagp::ExecResult;
using gagp::SequenceProgress;
using gagp::SequenceWindow;
using gagp::Value;
using gagp::ValueTag;
using gagp::WindowEndpoint;
using gagp::WindowEndpointKind;
using gagp::detail::RegionEntry;
using gagp::detail::RegionExecutionLayout;
using gagp::detail::RegionFrame;
using gagp::detail::RegionScratch;
using gagp::detail::RegionState;

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << '\n';
  return condition;
}

ExecResult value(Value result) {
  return ExecResult{false, result, gagp::Err{ErrCode::Value, ""}};
}

ExecResult failure(ErrCode code, const char* message) {
  return ExecResult{true, Value::invalid(), gagp::Err{code, message}};
}

RegionEntry terminal(Value result) { return RegionEntry{true, value(result)}; }

RegionEntry descend() {
  return RegionEntry{false, value(Value::invalid())};
}

bool is_error(const ExecResult& result, ErrCode code) {
  return result.is_error && result.err.code == code;
}

bool exact_value(const Value& left, const Value& right) {
  if (left.tag != right.tag) return false;
  switch (left.tag) {
    case ValueTag::Int:
    case ValueTag::Char:
    case ValueTag::FallbackToken:
      return left.i == right.i;
    case ValueTag::Float:
      return left.f == right.f;
    case ValueTag::Bool:
      return left.b == right.b;
    case ValueTag::String: {
      std::string a;
      std::string b;
      return gagp::payload::lookup_string(left, &a) &&
             gagp::payload::lookup_string(right, &b) && a == b;
    }
    case ValueTag::IntList:
    case ValueTag::FloatList:
    case ValueTag::StringList: {
      std::vector<Value> a;
      std::vector<Value> b;
      if (!gagp::payload::lookup_list(left, &a) ||
          !gagp::payload::lookup_list(right, &b) || a.size() != b.size()) {
        return false;
      }
      for (std::size_t i = 0; i < a.size(); ++i) {
        if (!exact_value(a[i], b[i])) return false;
      }
      return true;
    }
    case ValueTag::Invalid:
      return true;
  }
  return false;
}

WindowEndpoint endpoint(WindowEndpointKind kind, std::uint32_t cut = 0) {
  return WindowEndpoint{kind, cut};
}

SequenceProgress three_way_progress() {
  const WindowEndpoint begin = endpoint(WindowEndpointKind::Begin);
  const WindowEndpoint cut0 = endpoint(WindowEndpointKind::InteriorCut, 0);
  const WindowEndpoint cut1 = endpoint(WindowEndpointKind::InteriorCut, 1);
  const WindowEndpoint end = endpoint(WindowEndpointKind::End);
  return SequenceProgress{
      2,
      {SequenceWindow{begin, cut0}, SequenceWindow{cut0, cut1},
       SequenceWindow{cut1, end}},
      DuplicatePolicy::Reject};
}

RegionState sequence_state(Value source) {
  RegionState result{};
  result[0] = source;
  return result;
}

enum class CutMode { Thirds, Extremes };

struct SequenceAdapter {
  explicit SequenceAdapter(SequenceProgress descriptor,
                           CutMode selected = CutMode::Thirds,
                           bool lengths = false)
      : progress(std::move(descriptor)), mode(selected), return_lengths(lengths) {}

  SequenceProgress progress;
  CutMode mode = CutMode::Thirds;
  bool return_lengths = false;
  std::size_t entries = 0;
  std::size_t prepares = 0;
  std::size_t requests = 0;
  std::size_t combines = 0;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> request_trace;
  Value last_constructed = Value::invalid();

  RegionEntry enter(RegionFrame& frame, int&) {
    ++entries;
    const Value source = frame.state[0];
    if (!gagp::is_container(source)) {
      return RegionEntry{false, failure(ErrCode::Type, "sequence state")};
    }
    const std::uint32_t length = Value::container_len(source);
    if (length <= 1) {
      return terminal(return_lengths ? Value::from_int(length) : source);
    }
    return descend();
  }

  ExecResult prepare(RegionFrame& frame, int&) {
    ++prepares;
    const std::uint32_t length = Value::container_len(frame.state[0]);
    const std::int64_t raw0 = mode == CutMode::Extremes
                                  ? std::numeric_limits<std::int64_t>::min()
                                  : static_cast<std::int64_t>(length) / 3;
    const std::int64_t raw1 = mode == CutMode::Extremes
                                  ? std::numeric_limits<std::int64_t>::max()
                                  : (static_cast<std::int64_t>(length) * 2) / 3;
    frame.prepared[0] =
        Value::from_int(gagp::clamp_interior_cut(raw0, length));
    frame.prepared[1] =
        Value::from_int(gagp::clamp_interior_cut(raw1, length));
    return value(Value::invalid());
  }

  ExecResult request(RegionFrame& frame, std::uint32_t ordinal,
                     RegionState& next, int&) {
    ++requests;
    const Value source = frame.state[0];
    const std::uint32_t length = Value::container_len(source);
    request_trace.push_back({length, ordinal});
    const std::vector<std::int64_t> cuts{frame.prepared[0].i,
                                         frame.prepared[1].i};
    std::int64_t begin = 0;
    std::int64_t end = 0;
    try {
      const auto resolved = gagp::resolve_sequence_window(
          progress.windows.at(ordinal), length, cuts);
      begin = resolved.first;
      end = resolved.second;
    } catch (const std::invalid_argument&) {
      return failure(ErrCode::Value, "window resolution");
    }
    const BuiltinResult sliced = gagp::builtin_call(
        BuiltinId::Slice,
        std::vector<Value>{source, Value::from_int(begin),
                           Value::from_int(end)});
    if (sliced.is_error)
      return ExecResult{true, Value::invalid(), sliced.err};
    last_constructed = sliced.value;
    next = sequence_state(sliced.value);
    return value(Value::invalid());
  }

  ExecResult combine(RegionFrame& frame, int&) {
    ++combines;
    if (return_lengths) {
      if (frame.results[0].tag != ValueTag::Int ||
          frame.results[1].tag != ValueTag::Int ||
          frame.results[2].tag != ValueTag::Int) {
        return failure(ErrCode::Type, "length result");
      }
      return value(Value::from_int(frame.results[0].i + frame.results[1].i +
                                   frame.results[2].i));
    }
    const BuiltinResult first = gagp::builtin_call(
        BuiltinId::Concat,
        std::vector<Value>{frame.results[0], frame.results[1]});
    if (first.is_error)
      return ExecResult{true, Value::invalid(), first.err};
    const BuiltinResult second = gagp::builtin_call(
        BuiltinId::Concat,
        std::vector<Value>{first.value, frame.results[2]});
    return second.is_error
               ? ExecResult{true, Value::invalid(), second.err}
               : value(second.value);
  }
};

ExecResult execute_checked(const SequenceProgress& progress, Value source,
                           SequenceAdapter& adapter, RegionScratch& scratch,
                           int& fuel, std::uint32_t frame_limit = 32) {
  gagp::validate_sequence_progress(progress);
  RegionExecutionLayout layout;
  layout.state_count = 1;
  layout.request_count = 3;
  layout.frame_limit = frame_limit;
  layout.cell_limit = 0;
  layout.entry_fuel = 1;
  layout.memoized = false;
  return gagp::detail::execute_bounded_region(
      layout, sequence_state(source), adapter, scratch, fuel);
}

Value make_source(ValueTag tag, std::size_t length) {
  if (tag == ValueTag::String) {
    const char bytes[] = {'\0', 'A', static_cast<char>(0xff), 'Z'};
    return gagp::payload::make_string_value(std::string(bytes, length));
  }
  if (tag == ValueTag::IntList) {
    const std::vector<Value> all{Value::from_int(-3), Value::from_int(0),
                                 Value::from_int(5), Value::from_int(9)};
    return gagp::payload::make_int_list_value(
        std::vector<Value>(all.begin(), all.begin() + length));
  }
  if (tag == ValueTag::FloatList) {
    const std::vector<Value> all{Value::from_float(-0.0), Value::from_float(1.5),
                                 Value::from_float(-2.25), Value::from_float(8.0)};
    return gagp::payload::make_float_list_value(
        std::vector<Value>(all.begin(), all.begin() + length));
  }
  const std::vector<Value> all{
      gagp::payload::make_string_value(""),
      gagp::payload::make_string_value(std::string("a\0", 2)),
      gagp::payload::make_string_value(std::string(1, static_cast<char>(0xff))),
      gagp::payload::make_string_value("tail")};
  return gagp::payload::make_string_list_value(
      std::vector<Value>(all.begin(), all.begin() + length));
}

bool test_all_resident_sequence_types_and_sizes() {
  const SequenceProgress progress = three_way_progress();
  for (const ValueTag tag : {ValueTag::String, ValueTag::IntList,
                             ValueTag::FloatList, ValueTag::StringList}) {
    for (const std::size_t length : {std::size_t{0}, std::size_t{1},
                                     std::size_t{2}, std::size_t{4}}) {
      const Value source = make_source(tag, length);
      SequenceAdapter adapter(progress);
      RegionScratch scratch;
      int fuel = 100;
      const ExecResult result =
          execute_checked(progress, source, adapter, scratch, fuel);
      const std::size_t expected_entries = length <= 1 ? 1 : (length == 2 ? 4 : 7);
      const std::size_t expected_nonbase = length <= 1 ? 0 : (length == 2 ? 1 : 2);
      if (!check(!result.is_error && result.value.tag == tag &&
                     exact_value(result.value, source),
                 "three-way decomposition failed to reconstruct resident payload") ||
          !check(adapter.entries == expected_entries,
                 "three-way decomposition entered the wrong number of frames") ||
          !check(adapter.prepares == expected_nonbase &&
                     adapter.combines == expected_nonbase &&
                     adapter.requests == expected_nonbase * 3,
                 "cut preparation or ordered request counts changed")) {
        return false;
      }
      if (length == 2 &&
          !check(adapter.request_trace ==
                     std::vector<std::pair<std::uint32_t, std::uint32_t>>{
                         {2, 0}, {2, 1}, {2, 2}},
                 "coincident cuts changed the three request order")) {
        return false;
      }
      if (length == 4 &&
          !check(adapter.request_trace ==
                     std::vector<std::pair<std::uint32_t, std::uint32_t>>{
                         {4, 0}, {4, 1}, {4, 2},
                         {2, 0}, {2, 1}, {2, 2}},
                 "nested three-way requests changed depth-first order")) {
        return false;
      }
    }
  }
  return true;
}

bool test_exact_entry_fuel_threshold() {
  const SequenceProgress progress = three_way_progress();
  const Value source = make_source(ValueTag::IntList, 4);
  for (int supplied = 0; supplied <= 7; ++supplied) {
    SequenceAdapter adapter(progress);
    RegionScratch scratch;
    int fuel = supplied;
    const ExecResult result =
        execute_checked(progress, source, adapter, scratch, fuel);
    if (supplied < 7) {
      if (!check(is_error(result, ErrCode::Timeout),
                 "three-way tree ran below its exact entry-fuel threshold")) {
        return false;
      }
    } else if (!check(!result.is_error && exact_value(result.value, source) &&
                          fuel == 0 && adapter.entries == 7,
                      "three-way tree failed at its exact entry-fuel threshold")) {
      return false;
    }
  }
  return true;
}

bool test_frame_capacity_after_request_construction() {
  const SequenceProgress progress = three_way_progress();
  const Value source = make_source(ValueTag::String, 4);
  SequenceAdapter adapter(progress);
  RegionScratch scratch;
  int fuel = 20;
  const ExecResult result =
      execute_checked(progress, source, adapter, scratch, fuel, 1);
  std::string first_slice;
  return check(is_error(result, ErrCode::Timeout),
               "frame capacity should reject the first child") &&
         check(adapter.entries == 1 && adapter.prepares == 1 &&
                   adapter.requests == 1 && adapter.combines == 0 && fuel == 19,
               "capacity failure occurred before request construction or charged a child") &&
         check(adapter.last_constructed.tag == ValueTag::String &&
                   gagp::payload::lookup_string(adapter.last_constructed, &first_slice) &&
                   first_slice == std::string("\0", 1),
               "rejected child slice was not constructed before frame exhaustion");
}

bool test_extreme_cut_clamping_and_distinct_result_type() {
  const SequenceProgress progress = three_way_progress();
  const Value source = make_source(ValueTag::FloatList, 4);
  SequenceAdapter extreme(progress, CutMode::Extremes);
  RegionScratch scratch;
  int fuel = 100;
  ExecResult result = execute_checked(progress, source, extreme, scratch, fuel);
  if (!check(!result.is_error && result.value.tag == ValueTag::FloatList &&
                 exact_value(result.value, source),
             "INT64_MIN/MAX cut clamping changed reconstruction") ||
      !check(extreme.request_trace ==
                 std::vector<std::pair<std::uint32_t, std::uint32_t>>{
                     {4, 0}, {4, 1}, {2, 0},
                     {2, 1}, {2, 2}, {4, 2}},
             "extreme clamped cuts changed request order")) {
    return false;
  }

  SequenceAdapter lengths(progress, CutMode::Thirds, true);
  RegionScratch length_scratch;
  fuel = 100;
  result = execute_checked(progress, make_source(ValueTag::StringList, 4),
                           lengths, length_scratch, fuel);
  return check(!result.is_error && result.value.tag == ValueTag::Int &&
                   result.value.i == 4,
               "sequence-state recurrence could not produce a distinct Int result");
}

bool test_validation_and_typed_state_before_decomposition() {
  SequenceProgress malformed = three_way_progress();
  malformed.windows[0] = SequenceWindow{
      endpoint(WindowEndpointKind::Begin), endpoint(WindowEndpointKind::End)};
  SequenceAdapter unused(malformed);
  RegionScratch scratch;
  int fuel = 10;
  try {
    (void)execute_checked(malformed, make_source(ValueTag::IntList, 4),
                          unused, scratch, fuel);
    return check(false, "nonproper full-window dependency reached execution");
  } catch (const std::invalid_argument&) {
    if (!check(unused.entries == 0 && unused.prepares == 0 &&
                   unused.requests == 0 && unused.combines == 0 && fuel == 10,
               "progress validation happened after execution began")) {
      return false;
    }
  }

  const SequenceProgress valid = three_way_progress();
  SequenceAdapter typed(valid);
  RegionScratch typed_scratch;
  fuel = 1;
  const ExecResult result = execute_checked(
      valid, Value::from_bool(true), typed, typed_scratch, fuel);
  return check(is_error(result, ErrCode::Type) && typed.entries == 1 &&
                   typed.prepares == 0 && typed.requests == 0 && fuel == 0,
               "per-frame typed source check did not precede decomposition");
}

}  // namespace

int main() {
  gagp::payload::clear();
  if (!test_all_resident_sequence_types_and_sizes()) return 1;
  if (!test_exact_entry_fuel_threshold()) return 1;
  if (!test_frame_capacity_after_request_construction()) return 1;
  if (!test_extreme_cut_clamping_and_distinct_result_type()) return 1;
  if (!test_validation_and_typed_state_before_decomposition()) return 1;
  std::cout << "gagp_test_bounded_sequence: OK\n";
  return 0;
}
