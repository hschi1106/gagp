#pragma once
#include <algorithm>
#include <optional>
#include <memory>
#include <vector>
#include "gagp/runtime/cpu/builtins_cpu.hpp"
#include "gagp/runtime/payload/payload.hpp"
#include "../view_profile.hpp"
#include "bounded_region.hpp"

namespace gagp::detail {
inline bool cpu_region_views_supported(const BoundedRegionSegment& segment) {
  const auto& plan = segment.plan;
  if (plan.progress != RegionProgressKind::SequenceWindows) return false;
  // Snapshot setup pays off on proven recursive windows. Arbitrary predicates
  // may terminate at the root, where eager payload copying is needless work.
  const auto& predicate = segment.base_predicate.program;
  if ((predicate.code.size() != 1 &&
       !(predicate.code.size() == 2 && predicate.code[1].op == Opcode::Return)) ||
      predicate.code[0].op != Opcode::PushConst || predicate.code[0].a < 0 ||
      static_cast<std::size_t>(predicate.code[0].a) >= predicate.consts.size()) return false;
  const auto& condition = predicate.consts[predicate.code[0].a];
  if (condition.tag != ValueTag::Bool || condition.b) return false;
  if (plan.result_type != ValueTag::Int && plan.result_type != ValueTag::Bool) return false;
  // Memo keys containing lists depend on content identity, not view offsets.
  if (plan.memoized && std::find(plan.state_types.begin(), plan.state_types.end(),
                                ValueTag::IntList) != plan.state_types.end()) return false;
  const auto scalar_constants = [](const RegionPhase& phase) {
    return std::all_of(phase.program.consts.begin(), phase.program.consts.end(), [](const Value& v) {
      return v.tag == ValueTag::Int || v.tag == ValueTag::Bool;
    });
  };
  if ((segment.boundary && !scalar_constants(*segment.boundary)) ||
      !scalar_constants(segment.base_predicate) || !scalar_constants(segment.base_body) ||
      !scalar_constants(segment.combine)) return false;
  for (const auto& phase : segment.preparations) if (!scalar_constants(phase)) return false;
  for (const auto& phase : segment.request_expressions) if (!scalar_constants(phase)) return false;
  return view_region_supported(segment);
}

// Invocation-local owned list contents. Encoded views are never registered or
// exposed outside a proven scalar-returning region. The generic path receives
// original inputs if any registry lookup/type/length check fails.
struct CpuListViews {
  std::vector<Value> values;
  std::vector<Value> lookup_scratch;
  std::vector<std::pair<std::int64_t, Value>> sources;

  bool convert(Value& value) {
    if (value.tag != ValueTag::IntList) return true; // Lazy runtime type checks remain.
    for (const auto& source : sources)
      if (source.first == value.i) { value = source.second; return true; }
    lookup_scratch.clear();
    if (!payload::lookup_list(value, &lookup_scratch) ||
        lookup_scratch.size() != Value::container_len(value) ||
        !std::all_of(lookup_scratch.begin(), lookup_scratch.end(), [](const Value& v) {
          return v.tag == ValueTag::Int;
        })) return false;
    constexpr std::size_t limit = 1024 * 1024; // Unsupported invocation safely falls back.
    if (lookup_scratch.size() > limit - values.size()) return false;
    const auto original = value.i;
    auto view = Value::from_int_list_hash_len(values.size(), lookup_scratch.size());
    view.b = true;
    values.insert(values.end(), lookup_scratch.begin(), lookup_scratch.end());
    sources.emplace_back(original, view); value = view;
    return true;
  }
  bool prepare(RegionState& state, std::size_t count,
               std::vector<std::optional<Value>>& parameters) {
    values.clear(); sources.clear();
    for (std::size_t i=0; i<count; ++i) if (!convert(state[i])) return false;
    for (auto& value : parameters) if (value && !convert(*value)) return false;
    return true;
  }
  static std::int64_t slice_index(std::int64_t value, std::int64_t size) {
    if (value < 0) value += size;
    return std::min(size, std::max<std::int64_t>(0, value));
  }
  BuiltinResult call(BuiltinId id, const Value* args, std::size_t count) const {
    if ((id != BuiltinId::Slice && id != BuiltinId::Index && id != BuiltinId::Len) ||
        count == 0 || args[0].tag != ValueTag::IntList)
      return builtin_call(id, args, count);
    const auto error = [](ErrCode code, const char* message) {
      return BuiltinResult{true, Value::invalid(), {code, message}};
    };
    const auto length = Value::container_len(args[0]);
    const auto offset = Value::container_hash48(args[0]);
    if (offset > values.size() || length > values.size() - offset)
      return error(ErrCode::Value, "list view exceeds invocation storage");
    if (id == BuiltinId::Len) {
      if (count != 1) return error(ErrCode::Type, "len expects one argument");
      return {false, Value::from_int(length), {ErrCode::Value, ""}};
    }
    if ((id == BuiltinId::Slice && count != 3) || (id == BuiltinId::Index && count != 2))
      return error(ErrCode::Type, "invalid list view argument count");
    if (args[1].tag != ValueTag::Int || (count == 3 && args[2].tag != ValueTag::Int))
      return error(ErrCode::Type, "list view expects integer indices");
    if (id == BuiltinId::Index) {
      auto index = args[1].i;
      if (index < 0) index += length;
      if (index < 0 || static_cast<std::uint64_t>(index) >= length)
        return error(ErrCode::Value, "index out of range");
      return {false, values[offset + index], {ErrCode::Value, ""}};
    }
    const auto lo = slice_index(args[1].i, length), hi = slice_index(args[2].i, length);
    auto result = Value::from_int_list_hash_len(offset + lo, hi > lo ? hi-lo : 0);
    result.b = true;
    return {false, result, {ErrCode::Value, ""}};
  }
};
// Private, synchronous interpreter scope; thread-local storage creates no
// workers. Only proven region invocations install a view owner, and only marked
// view values can use it. The ordinary VM and root values remain unchanged.
extern thread_local const CpuListViews* active_cpu_list_views;
class CpuListViewScope {
 public:
  explicit CpuListViewScope(const CpuListViews& owner) : previous_(active_cpu_list_views) {
    active_cpu_list_views = &owner;
  }
  ~CpuListViewScope() { active_cpu_list_views = previous_; }
  CpuListViewScope(const CpuListViewScope&) = delete;
  CpuListViewScope& operator=(const CpuListViewScope&) = delete;
 private:
  const CpuListViews* previous_;
};
// Keep view ownership and exception cleanup out of the ordinary VM stack frame.
// Adapter's parameter reference observes the transactionally converted values.
template <class Adapter>
__attribute__((noinline)) ExecResult execute_with_cpu_views(
    const RegionExecutionLayout& layout, RegionState initial, Adapter& adapter,
    RegionScratch& scratch, int& fuel,
    std::vector<std::optional<Value>>& parameters,
    std::unique_ptr<CpuListViews>* persistent) {
  std::unique_ptr<CpuListViews> local;
  auto& storage = persistent ? *persistent : local;
  if (!storage) storage = std::make_unique<CpuListViews>();
  const auto saved_state = initial;
  const auto saved_parameters = parameters;
  if (!storage->prepare(initial, layout.state_count, parameters)) {
    parameters = saved_parameters;
    return execute_bounded_region(layout, saved_state, adapter, scratch, fuel);
  }
  CpuListViewScope scope(*storage);
  return execute_bounded_region(layout, initial, adapter, scratch, fuel);
}
}  // namespace gagp::detail
