#pragma once

#include <cstdint>
#include <type_traits>

#include "gagp/core/region_plan.hpp"
#include "gagp/runtime/gpu/device_types_gpu.hpp"

namespace gagp::gpu_detail {

inline constexpr std::uint32_t DMAX_REGION_FRAMES = 128;
inline constexpr std::uint32_t DMAX_REGION_MEMO = 128;
inline constexpr std::uint32_t DMAX_REGION_STATES = 4;
inline constexpr std::uint32_t DMAX_REGION_REQUESTS = 8;
inline constexpr std::uint32_t DMAX_REGION_PREPARATIONS = 4;
inline constexpr std::uint32_t DMAX_REGION_PARAMETERS = 32;
inline constexpr std::uint32_t DMAX_REGION_BOUND_OPERANDS = 8;
inline constexpr std::uint32_t DMAX_REGION_REQUEST_EXPRESSIONS = 32;

static_assert(DMAX_REGION_STATES == kRecurrenceCoordinateCapacity);
static_assert(DMAX_REGION_REQUESTS == kRecurrenceRequestCapacity);
static_assert(DMAX_REGION_PREPARATIONS == kRegionPreparationCapacity);
static_assert(DMAX_REGION_PARAMETERS == kRegionParameterCapacity);
static_assert(DMAX_REGION_BOUND_OPERANDS == kRegionBoundOperandCapacity);
static_assert(DMAX_REGION_REQUEST_EXPRESSIONS ==
              DMAX_REGION_STATES * DMAX_REGION_REQUESTS);

// Per-invocation values live in explicitly allocated device storage. A CUDA
// thread owns one logical slice and reuses it only after the previous invocation
// returns. Production slices are interleaved within each block; single-thread
// probes use a stride of one.
struct DRegionFrame {
  Value state[DMAX_REGION_STATES];
  Value prepared[DMAX_REGION_PREPARATIONS];
  Value results[DMAX_REGION_REQUESTS];
  int next_request;
};

template <unsigned States>
struct DCompactRegionFrameStorage {
  Value state[States];
  Value prepared[1];
  Value results[2];
  int next_request;
};

using DCompactRegionFrame = DCompactRegionFrameStorage<1>;
using DCompactPairRegionFrame = DCompactRegionFrameStorage<2>;

#ifdef __CUDACC__
#define GAGP_REGION_HD __host__ __device__
#else
#define GAGP_REGION_HD
#endif
struct DWindowStateProxy {
  std::int64_t& i;
  ValueTag tag;
  bool& view;
  GAGP_REGION_HD operator Value() const {Value v;v.i=i;v.tag=tag;v.b=tag==ValueTag::IntList && view;return v;}
  GAGP_REGION_HD DWindowStateProxy& operator=(Value v){i=v.i;if(tag==ValueTag::IntList)view=v.b;return *this;}
};
template<unsigned States> struct DWindowStates {
  std::int64_t values[States];
  bool view;
  GAGP_REGION_HD DWindowStateProxy operator[](unsigned i){return {values[i],i?ValueTag::Int:ValueTag::IntList,view};}
  GAGP_REGION_HD Value operator[](unsigned i) const {Value v;v.i=values[i];v.tag=i?ValueTag::Int:ValueTag::IntList;v.b=i==0 && view;return v;}
};
struct DWindowInt {
  std::int64_t i;
  static constexpr ValueTag tag=ValueTag::Int;
  GAGP_REGION_HD operator Value() const{return Value::from_int(i);}
  GAGP_REGION_HD DWindowInt& operator=(Value v){i=v.i;return *this;}
};
template<unsigned States> struct DUnboxedWindowFrame {
  DWindowStates<States> state;
  DWindowInt prepared[1];
  DWindowInt results[2];
  int next_request;
};
struct DUnboxedCoordinateFrame {
  DWindowInt state[1];
  DWindowInt prepared[1];
  DWindowInt results[2];
  int next_request;
};
#undef GAGP_REGION_HD

struct DRegionWorkspace {
  DRegionFrame* frames = nullptr;
  std::int64_t* memo_keys = nullptr;
  Value* memo_values = nullptr;
  std::uint32_t frame_capacity = 0;
  std::uint32_t memo_capacity = 0;
  std::uint32_t slot_stride = 1;
  bool compact_frames = false;
  bool two_state_frames = false;
  bool window_executor = false;
  bool coordinate_executor = false;
  bool unboxed_window_frames = false;
};

static_assert(std::is_trivially_copyable<DRegionFrame>::value);
static_assert(std::is_standard_layout<DRegionFrame>::value);
static_assert(std::is_trivially_copyable<DRegionWorkspace>::value);

struct DRegionPhase {
  DPhaseMeta program{};
  int binding_offset = 0;
  int binding_count = 0;
};

struct DRegionPhaseBinding {
  RegionSlotBank bank = RegionSlotBank::State;
  std::uint32_t slot = 0;
  int local = -1;
};

struct DRegionSegment {
  std::uint32_t state_count = 0;
  ValueTag state_types[DMAX_REGION_STATES] = {
      ValueTag::Invalid, ValueTag::Invalid, ValueTag::Invalid,
      ValueTag::Invalid};
  ValueTag result_type = ValueTag::Invalid;

  std::uint32_t parameter_count = 0;
  ValueTag parameter_types[DMAX_REGION_PARAMETERS] = {
      ValueTag::Invalid, ValueTag::Invalid, ValueTag::Invalid,
      ValueTag::Invalid, ValueTag::Invalid, ValueTag::Invalid,
      ValueTag::Invalid, ValueTag::Invalid, ValueTag::Invalid,
      ValueTag::Invalid, ValueTag::Invalid, ValueTag::Invalid,
      ValueTag::Invalid, ValueTag::Invalid, ValueTag::Invalid,
      ValueTag::Invalid, ValueTag::Invalid, ValueTag::Invalid,
      ValueTag::Invalid, ValueTag::Invalid, ValueTag::Invalid,
      ValueTag::Invalid, ValueTag::Invalid, ValueTag::Invalid,
      ValueTag::Invalid, ValueTag::Invalid, ValueTag::Invalid,
      ValueTag::Invalid, ValueTag::Invalid, ValueTag::Invalid,
      ValueTag::Invalid, ValueTag::Invalid};
  int parameter_caller_locals[DMAX_REGION_PARAMETERS] = {
      -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1};

  std::uint32_t preparation_count = 0;
  RegionPreparationKind preparation_kinds[DMAX_REGION_PREPARATIONS] = {
      RegionPreparationKind::Identity, RegionPreparationKind::Identity,
      RegionPreparationKind::Identity, RegionPreparationKind::Identity};
  ValueTag preparation_types[DMAX_REGION_PREPARATIONS] = {
      ValueTag::Invalid, ValueTag::Invalid, ValueTag::Invalid,
      ValueTag::Invalid};

  std::uint32_t request_expression_count = 0;
  ValueTag request_expression_types[DMAX_REGION_REQUEST_EXPRESSIONS] = {
      ValueTag::Invalid, ValueTag::Invalid, ValueTag::Invalid,
      ValueTag::Invalid, ValueTag::Invalid, ValueTag::Invalid,
      ValueTag::Invalid, ValueTag::Invalid, ValueTag::Invalid,
      ValueTag::Invalid, ValueTag::Invalid, ValueTag::Invalid,
      ValueTag::Invalid, ValueTag::Invalid, ValueTag::Invalid,
      ValueTag::Invalid, ValueTag::Invalid, ValueTag::Invalid,
      ValueTag::Invalid, ValueTag::Invalid, ValueTag::Invalid,
      ValueTag::Invalid, ValueTag::Invalid, ValueTag::Invalid,
      ValueTag::Invalid, ValueTag::Invalid, ValueTag::Invalid,
      ValueTag::Invalid, ValueTag::Invalid, ValueTag::Invalid,
      ValueTag::Invalid, ValueTag::Invalid};

  std::uint32_t bound_operand_count = 0;
  std::uint32_t request_count = 0;
  RegionStateTransition
      requests[DMAX_REGION_REQUESTS][DMAX_REGION_STATES] = {};
  RegionExecutionLimits limits{};
  bool memoized = false;
  RegionProgressKind progress = RegionProgressKind::Coordinates;

  std::uint32_t coordinate_count = 0;
  std::uint32_t coordinate_slots[DMAX_REGION_STATES] = {};
  RankAxis coordinate_rank[DMAX_REGION_STATES] = {};
  RegionCoordinateDomain coordinate_domains[DMAX_REGION_STATES] = {};
  DomainEndpoint coordinate_endpoint = DomainEndpoint::Exclusive;
  std::uint32_t sequence_state = 0;

  int boundary_phase = -1;
  int base_predicate_phase = -1;
  int base_body_phase = -1;
  int combine_phase = -1;
  int preparation_phases[DMAX_REGION_PREPARATIONS] = {-1, -1, -1, -1};
  int request_expression_phases[DMAX_REGION_REQUEST_EXPRESSIONS] = {
      -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1};
};

static_assert(std::is_trivially_copyable<DPhaseMeta>::value);
static_assert(std::is_standard_layout<DPhaseMeta>::value);
static_assert(std::is_trivially_copyable<RegionStateTransition>::value);
static_assert(std::is_standard_layout<RegionStateTransition>::value);
static_assert(std::is_trivially_copyable<RegionExecutionLimits>::value);
static_assert(std::is_standard_layout<RegionExecutionLimits>::value);
static_assert(std::is_trivially_copyable<RankAxis>::value);
static_assert(std::is_standard_layout<RankAxis>::value);
static_assert(std::is_trivially_copyable<RegionCoordinateDomain>::value);
static_assert(std::is_standard_layout<RegionCoordinateDomain>::value);
static_assert(std::is_trivially_copyable<DRegionPhase>::value);
static_assert(std::is_standard_layout<DRegionPhase>::value);
static_assert(std::is_trivially_copyable<DRegionPhaseBinding>::value);
static_assert(std::is_standard_layout<DRegionPhaseBinding>::value);
static_assert(std::is_trivially_copyable<DRegionSegment>::value);
static_assert(std::is_standard_layout<DRegionSegment>::value);

}  // namespace gagp::gpu_detail
