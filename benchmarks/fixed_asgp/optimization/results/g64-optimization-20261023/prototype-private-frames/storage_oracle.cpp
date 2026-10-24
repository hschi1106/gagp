// CPU storage oracle for an isolated draft. No GPU performance claim.
#include "private_window_frames_gpu.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace gagp;
using namespace gagp::gpu_detail;

void require(bool pass, const char* message) {
  if (!pass) throw std::runtime_error(message);
}
std::int64_t bits(Value value) { return packed_window_value_bits(value); }
Value make_value(std::int64_t payload, unsigned tag, bool boolean) {
  return packed_window_restore(payload, static_cast<ValueTag>(tag), boolean);
}
void same(Value actual, Value expected) {
  require(actual.tag == expected.tag && actual.b == expected.b && bits(actual) == bits(expected),
          "packed frame changed payload/tag/bool");
}

template<unsigned States> void check() {
  const std::array<std::int64_t, 10> payloads{0, 1, -1,
      std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max(),
      0x7ff0000000000000LL, 0x7ff8000000000001LL, 0x0000ffffffffffffLL,
      0x7ff0000000000001LL, static_cast<std::int64_t>(0xfff8000000000001ULL)};
  for (const auto payload : payloads) for (unsigned tag = 0; tag <= 15; ++tag)
      for (bool boolean : {false, true}) {
    PackedPrivateWindowFrameStorage<States> storage;
    std::memset(&storage, 0xa5, sizeof(storage));
    auto frame = packed_window_aos_view(storage);
    frame.initialize_tags();
    std::array<Value, States + 3> expected;
    for (unsigned i = 0; i < States; ++i) {
      expected[i] = make_value(payload, (tag + i) & 15, boolean);
      frame.state[i] = expected[i];
    }
    expected[States] = make_value(payload ^ 0x1234, (tag + States) & 15, !boolean);
    frame.prepared[0] = expected[States];
    for (unsigned i = 0; i < 2; ++i) {
      expected[States + 1 + i] = make_value(payload ^ (i + 7), (tag + States + i + 1) & 15, boolean);
      frame.results[i] = expected[States + 1 + i];
    }
    for (unsigned i = 0; i < States; ++i) same(frame.state[i], expected[i]);
    same(frame.prepared[0], expected[States]);
    for (unsigned i = 0; i < 2; ++i) same(frame.results[i], expected[States + 1 + i]);
    // Slot updates cannot clear another field's tag/boolean plane.
    frame.prepared[0] = make_value(42, static_cast<unsigned>(ValueTag::Int), false);
    same(frame.results[0], expected[States + 1]);
    same(frame.state[0], expected[0]);
    frame.results[1] = frame.results[0];
    same(frame.results[1], expected[States + 1]);
    const auto& readonly = frame;
    const auto snapshot = readonly.results[0];
    frame.results[0] = make_value(99, static_cast<unsigned>(ValueTag::Int), false);
    same(snapshot, expected[States + 1]);
    frame.results[1].tag = frame.results[0].tag;
    frame.results[1].b = frame.results[0].b;
    require(static_cast<Value>(frame.results[1]).tag == ValueTag::Int, "tag reference assignment rebound storage");
    frame.next_request = -1;
    require(frame.next_request++ == -1 && frame.next_request == 0, "frame counter changed");
    // Reusing dirty storage initializes metadata before lazy payload writes.
    frame.initialize_tags();
    frame.state[0] = make_value(payload, tag, boolean);
    same(frame.state[0], make_value(payload, tag, boolean));
  }
}
int main() {
  try {
    check<1>(); check<2>();
    require(!packed_window_value_supported(make_value(0, 16, false)), "unknown tag was silently admitted");
    std::cout << "Packed private-window CPU storage oracle passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
