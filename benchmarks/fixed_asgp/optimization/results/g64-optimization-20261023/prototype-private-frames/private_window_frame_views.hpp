#pragma once

// Isolated prototype. Existing window execution writes state0 first whenever
// it creates/reuses a root, child or direct leaf. Reset tag metadata at that
// write; merely constructing/accessing a view must not reset a live frame.
#include "private_window_frames_gpu.hpp"

#ifdef __CUDACC__
#define GAGP_FRAME_VIEW_HD __host__ __device__
#else
#define GAGP_FRAME_VIEW_HD
#endif

namespace gagp::gpu_detail {

struct InitializingPackedWindowStateReference : PackedWindowValueReference {
  bool initialize;
  GAGP_FRAME_VIEW_HD InitializingPackedWindowStateReference(
      PackedWindowValueReference value, bool first)
      : PackedWindowValueReference(value), initialize(first) {}
  GAGP_FRAME_VIEW_HD InitializingPackedWindowStateReference(
      const InitializingPackedWindowStateReference&) = default;
  GAGP_FRAME_VIEW_HD InitializingPackedWindowStateReference& operator=(Value value) {
    if (initialize) tag.tags = 0;
    PackedWindowValueReference::operator=(value);
    return *this;
  }
  GAGP_FRAME_VIEW_HD InitializingPackedWindowStateReference& operator=(
      const InitializingPackedWindowStateReference& other) {
    // Snapshot before a state0 store clears the shared tag word, including
    // assignment from another proxy into the same frame.
    return *this = static_cast<Value>(other);
  }
};

struct InitializingPackedWindowStates {
  PackedWindowSlots slots;
  GAGP_FRAME_VIEW_HD InitializingPackedWindowStateReference operator[](unsigned index) {
    return {slots[index], index == 0};
  }
  GAGP_FRAME_VIEW_HD Value operator[](unsigned index) const { return slots[index]; }
};

template<unsigned States> struct InitializingPackedWindowFrameView {
  InitializingPackedWindowStates state;
  PackedWindowSlots prepared, results;
  std::int32_t& next_request;
  std::uint32_t& tags;
  GAGP_FRAME_VIEW_HD explicit InitializingPackedWindowFrameView(
      PackedPrivateWindowFrameView<States> frame)
      : state{frame.state}, prepared(frame.prepared), results(frame.results),
        next_request(frame.next_request), tags(frame.tags) {}
};

template<unsigned States>
GAGP_FRAME_VIEW_HD inline InitializingPackedWindowFrameView<States> initializing_packed_aos_view(
    PackedPrivateWindowFrameStorage<States>& storage) {
  return InitializingPackedWindowFrameView<States>(packed_window_aos_view(storage));
}

template<unsigned States>
GAGP_FRAME_VIEW_HD inline InitializingPackedWindowFrameView<States> initializing_packed_soa_view(
    std::int64_t* base, std::size_t index, std::size_t stride) {
  return InitializingPackedWindowFrameView<States>(packed_window_soa_view<States>(base, index, stride));
}

template<unsigned States> struct PackedWindowStorageOwner {
  PackedPrivateWindowFrameStorage<States> storage;
  // Deliberately leave payloads untouched. References bind only after this
  // first base has completed construction; state0's ordered store clears tags.
  GAGP_FRAME_VIEW_HD PackedWindowStorageOwner() {}
};

// Stack-local direct leaf only. The storage base is constructed before the
// view base; copying this object would leave references into another object.
// Global scratch is allocated by sizeof(Storage), NEVER sizeof(LocalFrame).
template<unsigned States> struct PackedPrivateWindowLocalFrame
    : private PackedWindowStorageOwner<States>, InitializingPackedWindowFrameView<States> {
  GAGP_FRAME_VIEW_HD PackedPrivateWindowLocalFrame()
      : PackedWindowStorageOwner<States>(), InitializingPackedWindowFrameView<States>(
            packed_window_aos_view(PackedWindowStorageOwner<States>::storage)) {}
  PackedPrivateWindowLocalFrame(const PackedPrivateWindowLocalFrame&) = delete;
  PackedPrivateWindowLocalFrame& operator=(const PackedPrivateWindowLocalFrame&) = delete;
  GAGP_FRAME_VIEW_HD ~PackedPrivateWindowLocalFrame() = default;
};

static_assert(!std::is_copy_constructible_v<PackedPrivateWindowLocalFrame<1>>);
static_assert(!std::is_copy_assignable_v<PackedPrivateWindowLocalFrame<2>>);

} // namespace gagp::gpu_detail
#undef GAGP_FRAME_VIEW_HD
