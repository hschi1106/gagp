#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "../../src/runtime/cpu/bounded_region.hpp"

namespace {

using gagp::ErrCode;
using gagp::ExecResult;
using gagp::Value;
using gagp::ValueTag;
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

ExecResult integer(std::int64_t result) {
  return value(Value::from_int(result));
}

ExecResult failure(ErrCode code, const char* message) {
  return ExecResult{true, Value::invalid(), gagp::Err{code, message}};
}

RegionEntry terminal(Value result) { return RegionEntry{true, value(result)}; }

RegionEntry descend() { return RegionEntry{false, value(Value::invalid())}; }

bool is_error(const ExecResult& result, ErrCode code) {
  return result.is_error && result.err.code == code;
}

bool is_int(const ExecResult& result, std::int64_t expected) {
  return !result.is_error && result.value.tag == ValueTag::Int &&
         result.value.i == expected;
}

RegionState state(std::int64_t first, std::int64_t second = 0) {
  RegionState result{};
  result[0] = Value::from_int(first);
  result[1] = Value::from_int(second);
  return result;
}

struct GridAdapter {
  std::size_t enters = 0;
  std::size_t prepares = 0;
  std::size_t requests = 0;
  std::size_t combines = 0;

  RegionEntry enter(RegionFrame& frame, int&) {
    ++enters;
    if (frame.state[0].tag != ValueTag::Int ||
        frame.state[1].tag != ValueTag::Int) {
      return RegionEntry{false, failure(ErrCode::Type, "grid state")};
    }
    const auto x = frame.state[0].i;
    const auto y = frame.state[1].i;
    if (x < 0 || y < 0) return terminal(Value::from_int(0));
    if (x == 0 && y == 0) return terminal(Value::from_int(1));
    return descend();
  }

  ExecResult prepare(RegionFrame& frame, int&) {
    ++prepares;
    frame.prepared[0] = Value::from_int(frame.state[0].i + frame.state[1].i);
    return integer(0);
  }

  ExecResult request(RegionFrame& frame, std::uint32_t ordinal,
                     RegionState& next, int&) {
    ++requests;
    next = frame.state;
    if (ordinal == 0) {
      --next[0].i;
      ++next[1].i;
    } else {
      --next[1].i;
    }
    return integer(0);
  }

  ExecResult combine(RegionFrame& frame, int&) {
    ++combines;
    if (frame.prepared[0].tag != ValueTag::Int ||
        frame.results[0].tag != ValueTag::Int ||
        frame.results[1].tag != ValueTag::Int) {
      return failure(ErrCode::Type, "grid combine tag");
    }
    return integer(1 + frame.results[0].i + frame.results[1].i);
  }
};

bool test_custom_2d_recurrence() {
  RegionExecutionLayout layout;
  layout.state_count = 2;
  layout.request_count = 2;
  layout.frame_limit = 32;
  layout.cell_limit = 64;
  layout.entry_fuel = 1;
  layout.memoized = true;
  RegionScratch scratch;
  GridAdapter adapter;
  int fuel = 1000;
  const ExecResult result = gagp::detail::execute_bounded_region(
      layout, state(2, 2), adapter, scratch, fuel);
  return check(is_int(result, 40),
               "custom 2D recurrence produced the wrong result") &&
         check(adapter.prepares == adapter.combines,
               "each nonterminal memo miss must prepare and combine once") &&
         check(scratch.peak_frames <= layout.frame_limit &&
                   scratch.peak_cells <= layout.cell_limit,
               "custom recurrence exceeded declared storage limits");
}

struct TraceAdapter {
  std::vector<std::string> trace;

  RegionEntry enter(RegionFrame& frame, int&) {
    const auto n = frame.state[0].i;
    trace.push_back("enter" + std::to_string(n));
    trace.push_back("state" + std::to_string(n));
    if (frame.state[0].tag != ValueTag::Int) {
      return RegionEntry{false, failure(ErrCode::Type, "state")};
    }
    trace.push_back("boundary" + std::to_string(n));
    if (n < 0) return terminal(Value::from_int(0));
    trace.push_back("base" + std::to_string(n));
    if (n == 0) return terminal(Value::from_int(1));
    return descend();
  }

  ExecResult prepare(RegionFrame& frame, int&) {
    trace.push_back("prepare" + std::to_string(frame.state[0].i));
    frame.prepared[0] = frame.state[0];
    return integer(0);
  }

  ExecResult request(RegionFrame& frame, std::uint32_t ordinal,
                     RegionState& next, int&) {
    trace.push_back("request" + std::to_string(frame.state[0].i) + ":" +
                    std::to_string(ordinal));
    next = state(frame.state[0].i - 1);
    return integer(0);
  }

  ExecResult combine(RegionFrame& frame, int&) {
    trace.push_back("combine" + std::to_string(frame.state[0].i));
    return integer(1 + frame.results[0].i + frame.results[1].i);
  }
};

bool test_exact_callback_trace_and_memo_order() {
  RegionExecutionLayout layout;
  layout.request_count = 2;
  layout.frame_limit = 8;
  layout.cell_limit = 8;
  layout.entry_fuel = 2;
  layout.memoized = true;
  RegionScratch scratch;
  TraceAdapter adapter;
  int fuel = 10;
  const ExecResult result = gagp::detail::execute_bounded_region(
      layout, state(2), adapter, scratch, fuel);
  const std::vector<std::string> expected{
      "enter2", "state2", "boundary2", "base2", "prepare2", "request2:0",
      "enter1", "state1", "boundary1", "base1", "prepare1", "request1:0",
      "enter0", "state0", "boundary0", "base0", "request1:1",
      "enter0", "state0", "boundary0", "base0", "combine1", "request2:1",
      "enter1", "state1", "boundary1", "base1", "combine2"};
  return check(is_int(result, 7), "trace recurrence produced the wrong result") &&
         check(adapter.trace == expected, "callback order or memo behavior changed") &&
         check(fuel == 0, "memo hits or base reevaluation changed entry charging") &&
         check(scratch.peak_cells == 2,
               "base results were cached or successful combines were omitted") &&
         check(scratch.peak_frames == 3,
               "duplicate requests changed depth-first frame ordering");
}

struct DeepAdapter {
  RegionEntry enter(RegionFrame& frame, int&) {
    return frame.state[0].i == 0 ? terminal(Value::from_int(0)) : descend();
  }
  ExecResult prepare(RegionFrame&, int&) { return integer(0); }
  ExecResult request(RegionFrame& frame, std::uint32_t, RegionState& next,
                     int&) {
    next = state(frame.state[0].i - 1);
    return integer(0);
  }
  ExecResult combine(RegionFrame& frame, int&) {
    return integer(frame.results[0].i + 1);
  }
};

bool test_deep_chain_is_iterative_and_bounded() {
  constexpr std::uint32_t kDepth = 100000;
  RegionExecutionLayout layout;
  layout.frame_limit = kDepth + 1;
  layout.cell_limit = 0;
  layout.entry_fuel = 1;
  layout.memoized = false;
  RegionScratch scratch;
  DeepAdapter adapter;
  int fuel = static_cast<int>(kDepth + 1);
  const ExecResult result = gagp::detail::execute_bounded_region(
      layout, state(kDepth), adapter, scratch, fuel);
  const std::size_t frame_bound =
      static_cast<std::size_t>(layout.frame_limit) * sizeof(gagp::detail::RegionFrame);
  std::cout << "deep_peak_frames=" << scratch.peak_frames
            << " deep_peak_cells=" << scratch.peak_cells
            << " deep_storage_bytes=" << scratch.storage_bytes() << '\n';
  return check(is_int(result, kDepth), "deep iterative chain returned the wrong value") &&
         check(fuel == 0, "deep chain used the wrong number of entry charges") &&
         check(scratch.peak_frames == layout.frame_limit,
               "deep chain did not reach its declared frame depth") &&
         check(scratch.peak_cells == 0,
               "nonmemoized deep chain allocated logical memo cells") &&
         check(scratch.storage_bytes() <= frame_bound,
               "deep chain storage exceeded its declared fixed frame bound");
}

struct EntryErrorAdapter {
  int enters = 0;
  int prepares = 0;
  RegionEntry enter(RegionFrame&, int&) {
    ++enters;
    return descend();
  }
  ExecResult prepare(RegionFrame&, int&) { ++prepares; return integer(0); }
  ExecResult request(RegionFrame&, std::uint32_t, RegionState&, int&) {
    return integer(0);
  }
  ExecResult combine(RegionFrame&, int&) { return integer(0); }
};

bool test_entry_charge_precedes_state_error() {
  RegionExecutionLayout layout;
  layout.entry_fuel = 3;
  layout.memoized = true;
  RegionScratch scratch;
  EntryErrorAdapter adapter;
  RegionState invalid = state(0);
  invalid[0] = Value::from_bool(true);
  int fuel = 2;
  ExecResult result = gagp::detail::execute_bounded_region(
      layout, invalid, adapter, scratch, fuel);
  if (!check(is_error(result, ErrCode::Timeout) && adapter.enters == 0 && fuel == 2,
             "state validation ran before the entry charge")) {
    return false;
  }
  fuel = 3;
  result = gagp::detail::execute_bounded_region(
      layout, invalid, adapter, scratch, fuel);
  return check(is_error(result, ErrCode::Type) && adapter.enters == 1 &&
                   adapter.prepares == 0 && fuel == 0,
               "paid entry did not expose the state error");
}

struct FailureAdapter {
  enum class Mode { Request, Combine, Successful } mode = Mode::Successful;
  int enters = 0;
  int prepares = 0;
  int requests = 0;
  int combines = 0;

  RegionEntry enter(RegionFrame& frame, int&) {
    ++enters;
    if (frame.state[0].i == 0) return terminal(Value::from_int(4));
    return descend();
  }
  ExecResult prepare(RegionFrame&, int&) {
    ++prepares;
    return integer(0);
  }
  ExecResult request(RegionFrame& frame, std::uint32_t ordinal,
                     RegionState& next, int&) {
    ++requests;
    if (mode == Mode::Request && ordinal == 0)
      return failure(ErrCode::ZeroDiv, "request construction");
    next = state(frame.state[0].i - 1);
    return integer(0);
  }
  ExecResult combine(RegionFrame&, int&) {
    ++combines;
    if (mode == Mode::Combine) return failure(ErrCode::Type, "combine");
    return integer(9);
  }
};

bool test_error_and_capacity_precedence() {
  RegionScratch scratch;
  RegionExecutionLayout layout;
  layout.request_count = 2;
  layout.frame_limit = 1;
  layout.entry_fuel = 1;
  FailureAdapter request_error;
  request_error.mode = FailureAdapter::Mode::Request;
  int fuel = 20;
  ExecResult result = gagp::detail::execute_bounded_region(
      layout, state(1), request_error, scratch, fuel);
  if (!check(is_error(result, ErrCode::ZeroDiv) && request_error.requests == 1 &&
                 request_error.combines == 0,
             "first request error did not precede later work and frame exhaustion")) {
    return false;
  }

  FailureAdapter rejected_child;
  fuel = 20;
  result = gagp::detail::execute_bounded_region(
      layout, state(1), rejected_child, scratch, fuel);
  if (!check(is_error(result, ErrCode::Timeout) && rejected_child.enters == 1 &&
                 rejected_child.requests == 1 && fuel == 19,
             "rejected child consumed an entry charge or skipped request construction")) {
    return false;
  }

  layout.frame_limit = 4;
  layout.request_count = 1;
  layout.memoized = true;
  layout.cell_limit = 0;
  FailureAdapter combine_error;
  combine_error.mode = FailureAdapter::Mode::Combine;
  fuel = 20;
  result = gagp::detail::execute_bounded_region(
      layout, state(1), combine_error, scratch, fuel);
  if (!check(is_error(result, ErrCode::Type) && combine_error.combines == 1 &&
                 scratch.peak_cells == 0,
             "memo exhaustion incorrectly preceded a combine error")) {
    return false;
  }

  FailureAdapter memo_full;
  fuel = 20;
  result = gagp::detail::execute_bounded_region(
      layout, state(1), memo_full, scratch, fuel);
  return check(is_error(result, ErrCode::Timeout) && memo_full.combines == 1 &&
                   scratch.peak_cells == 0,
               "zero memo capacity did not reject a successful combine");
}

struct CountingTerminalAdapter {
  int enters = 0;
  int prepares = 0;
  int requests = 0;
  int combines = 0;
  RegionEntry enter(RegionFrame&, int&) {
    ++enters;
    return terminal(Value::from_int(1));
  }
  ExecResult prepare(RegionFrame&, int&) { ++prepares; return integer(0); }
  ExecResult request(RegionFrame&, std::uint32_t, RegionState&, int&) {
    ++requests;
    return integer(0);
  }
  ExecResult combine(RegionFrame&, int&) { ++combines; return integer(0); }
};

bool test_layout_and_fuel_boundaries() {
  const auto no_callbacks = [](const CountingTerminalAdapter& adapter) {
    return adapter.enters == 0 && adapter.prepares == 0 &&
           adapter.requests == 0 && adapter.combines == 0;
  };
  const std::vector<RegionExecutionLayout> malformed{
      RegionExecutionLayout{0, 1, 1, 1, 1, false},
      RegionExecutionLayout{5, 1, 1, 1, 1, false},
      RegionExecutionLayout{1, 0, 1, 1, 1, false},
      RegionExecutionLayout{1, 9, 1, 1, 1, false},
      RegionExecutionLayout{1, 1, 1, 1,
          static_cast<std::uint32_t>(std::numeric_limits<int>::max()) + 1U,
          false}};
  for (std::size_t i = 0; i < malformed.size(); ++i) {
    RegionScratch scratch;
    CountingTerminalAdapter adapter;
    int fuel = 7;
    const ExecResult result = gagp::detail::execute_bounded_region(
        malformed[i], state(0), adapter, scratch, fuel);
    if (!check(is_error(result, ErrCode::Value) && no_callbacks(adapter) && fuel == 7,
               "malformed layout was not rejected before execution " +
                   std::to_string(i))) {
      return false;
    }
  }

  RegionExecutionLayout zero_frames;
  zero_frames.frame_limit = 0;
  RegionScratch scratch;
  CountingTerminalAdapter adapter;
  int fuel = 7;
  ExecResult result = gagp::detail::execute_bounded_region(
      zero_frames, state(0), adapter, scratch, fuel);
  if (!check(is_error(result, ErrCode::Timeout) && no_callbacks(adapter) && fuel == 7,
             "zero root frame capacity ran callbacks or charged entry")) {
    return false;
  }

  RegionExecutionLayout charged;
  charged.entry_fuel = 1;
  fuel = 0;
  result = gagp::detail::execute_bounded_region(
      charged, state(0), adapter, scratch, fuel);
  if (!check(is_error(result, ErrCode::Timeout) && adapter.enters == 0 && fuel == 0,
             "zero fuel entered a charged root")) {
    return false;
  }
  fuel = -1;
  result = gagp::detail::execute_bounded_region(
      charged, state(0), adapter, scratch, fuel);
  if (!check(is_error(result, ErrCode::Timeout) && adapter.enters == 0 && fuel == -1,
             "negative fuel entered a charged root")) {
    return false;
  }

  charged.entry_fuel = 0;
  fuel = 0;
  result = gagp::detail::execute_bounded_region(
      charged, state(0), adapter, scratch, fuel);
  return check(is_int(result, 1) && adapter.enters == 1 && fuel == 0,
               "zero-cost terminal root did not run at zero fuel");
}

struct TokenAdapter {
  Value token = Value::invalid();
  int prepares = 0;
  int combines = 0;
  bool saw_equal_children = false;

  RegionEntry enter(RegionFrame& frame, int&) {
    return frame.state[0].i == 0 ? terminal(token) : descend();
  }
  ExecResult prepare(RegionFrame&, int&) {
    ++prepares;
    return integer(0);
  }
  ExecResult request(RegionFrame& frame, std::uint32_t, RegionState& next,
                     int&) {
    next = state(frame.state[0].i - 1);
    return integer(0);
  }
  ExecResult combine(RegionFrame& frame, int&) {
    ++combines;
    saw_equal_children = frame.results[0].tag == token.tag &&
                         frame.results[1].tag == token.tag &&
                         frame.results[0].i == token.i &&
                         frame.results[1].i == token.i;
    return value(token);
  }
};

bool test_exact_compact_tag_and_scratch_reuse() {
  RegionExecutionLayout layout;
  layout.request_count = 2;
  layout.frame_limit = 8;
  layout.cell_limit = 8;
  layout.entry_fuel = 0;
  layout.memoized = true;
  RegionScratch scratch;
  TokenAdapter first;
  first.token = Value::from_string_list_hash_len(UINT64_C(0x123456789abc), 7);
  int fuel = 0;
  ExecResult result = gagp::detail::execute_bounded_region(
      layout, state(2), first, scratch, fuel);
  if (!check(!result.is_error && result.value.tag == ValueTag::StringList &&
                 result.value.i == first.token.i && first.saw_equal_children,
             "memo transport changed a compact nonresident StringList value")) {
    return false;
  }

  TokenAdapter second;
  second.token = Value::from_string_list_hash_len(UINT64_C(0x111111111111), 3);
  result = gagp::detail::execute_bounded_region(
      layout, state(1), second, scratch, fuel);
  return check(!result.is_error && result.value.tag == ValueTag::StringList &&
                   result.value.i == second.token.i && second.prepares == 1 &&
                   second.combines == 1 && scratch.peak_frames == 2 &&
                   scratch.peak_cells == 1,
               "reused scratch exposed stale frames, memo cells, peaks, or values");
}

}  // namespace

int main() {
  if (!test_custom_2d_recurrence()) return 1;
  if (!test_exact_callback_trace_and_memo_order()) return 1;
  if (!test_deep_chain_is_iterative_and_bounded()) return 1;
  if (!test_entry_charge_precedes_state_error()) return 1;
  if (!test_error_and_capacity_precedence()) return 1;
  if (!test_layout_and_fuel_boundaries()) return 1;
  if (!test_exact_compact_tag_and_scratch_reuse()) return 1;
  std::cout << "gagp_test_bounded_region: OK\n";
  return 0;
}
