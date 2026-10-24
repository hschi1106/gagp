// Isolated root/child/local-leaf field contract, not VM or speed validation.
#include "private_window_frame_views.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

using namespace gagp;
using namespace gagp::gpu_detail;

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void same(Value actual, Value expected) {
  require(actual.tag == expected.tag && actual.b == expected.b &&
      packed_window_value_bits(actual) == packed_window_value_bits(expected), "Value changed");
}

template<unsigned States, class Frame>
void fields(Frame& frame, Value input) {
  frame.state[0] = input;
  const auto first_tag_word = static_cast<std::uint32_t>(input.tag) |
      (static_cast<std::uint32_t>(input.b) << kPackedWindowTagBits);
  require(frame.tags == first_tag_word, "state0 did not initialize metadata");
  if constexpr (States == 2) frame.state[1] = Value::from_int(-29);
  frame.prepared[0] = Value::from_int(7);
  frame.results[0] = Value::from_fallback_token(INT64_MIN + 5);
  frame.results[1] = input;
  frame.next_request = -1;
  const Frame& view = frame;
  auto snapshot = view.state[0];
  static_assert(std::is_same_v<decltype(snapshot), Value>);
  same(snapshot, input);
  same(view.prepared[0], Value::from_int(7));
  same(view.results[0], Value::from_fallback_token(INT64_MIN + 5));
  same(view.results[1], input);
  if constexpr (States == 2) {
    // Assignment of a later state preserves already-live prepared/results.
    frame.state[1] = input;
    same(view.state[1], input);
    same(view.results[0], Value::from_fallback_token(INT64_MIN + 5));
    frame.state[0] = frame.state[1];
  } else frame.state[0] = frame.state[0];
  same(view.state[0], input);
  require(frame.tags == first_tag_word, "state proxy assignment lost the source before clearing tags");
  require(frame.prepared[0].i == 7 && frame.results[0].i == INT64_MIN + 5,
          "metadata initialization overwrote unrelated payload words");
  require(frame.next_request++ == -1 && frame.next_request == 0, "counter changed");
}

template<unsigned States> void run() {
  const std::array<std::int64_t, 8> bits{0, -1, INT64_MIN, INT64_MAX,
      0x7ff0000000000000LL, 0x7ff8000000000001LL, 0x7ff0000000000001LL,
      static_cast<std::int64_t>(0xfff8000000000001ULL)};
  for (unsigned tag = 0; tag <= kPackedWindowTagMask; ++tag) for (bool boolean : {false, true})
      for (auto word : bits) {
    const auto input = packed_window_restore(word, static_cast<ValueTag>(tag), boolean);
    PackedPrivateWindowFrameStorage<States> storage;
    std::memset(&storage, 0xa5, sizeof(storage));
    auto frame = initializing_packed_aos_view(storage);
    fields<States>(frame, input);
    // Reconstructing an accessor must preserve all live metadata.
    auto again = initializing_packed_aos_view(storage);
    same(again.state[0], input);
    require(again.next_request == 0, "accessor reconstruction changed counter");
    fields<States>(again, input);
    alignas(PackedPrivateWindowLocalFrame<States>) unsigned char local[sizeof(PackedPrivateWindowLocalFrame<States>)];
    std::memset(local, 0xa5, sizeof(local));
    auto* leaf = new (local) PackedPrivateWindowLocalFrame<States>;
    fields<States>(*leaf, input);
    leaf->~PackedPrivateWindowLocalFrame<States>();
    leaf = new (local) PackedPrivateWindowLocalFrame<States>;
    fields<States>(*leaf, input);
    leaf->~PackedPrivateWindowLocalFrame<States>();
  }
  // SoA accessor isolation on a reused block with a partial active warp.
  constexpr unsigned stride = 32, depth = 3, active = 19;
  std::array<std::int64_t, stride * depth * (States + 4)> words;
  words.fill(-1);
  for (unsigned d = 0; d < depth; ++d) for (unsigned lane = 0; lane < active; ++lane) {
    auto frame = initializing_packed_soa_view<States>(words.data() + lane, d * stride, stride);
    fields<States>(frame, Value::from_int(d * 100 + lane));
  }
  for (unsigned d = 0; d < depth; ++d) for (unsigned lane = 0; lane < active; ++lane) {
    const auto frame = initializing_packed_soa_view<States>(words.data() + lane, d * stride, stride);
    same(frame.state[0], Value::from_int(d * 100 + lane));
  }
}

int main() {
  try {run<1>(); run<2>(); std::cout << "Packed frame root/child/local-leaf access oracle passed\n"; return 0;}
  catch (const std::exception& error) {std::cerr << error.what() << '\n'; return 1;}
}
