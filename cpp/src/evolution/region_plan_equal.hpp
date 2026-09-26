#pragma once
#include <algorithm>
#include <tuple>
#include "gagp/core/region_plan.hpp"

namespace gagp::evo {
// Equivalent to equality of canonical region-plan encodings. Validation retains
// canonical inactive-field and invalid-enum rejection without constructing JSON.
inline bool same_region_plan(const RegionPlan& a, const RegionPlan& b) {
  validate_region_plan(a);
  validate_region_plan(b);
  if (std::tie(a.version, a.state_types, a.result_type, a.parameter_types,
          a.request_expression_types, a.bound_operand_count, a.limits.frames,
          a.limits.cells, a.limits.entry_fuel, a.memoized, a.duplicate_policy,
          a.progress, a.coordinate_slots, a.coordinate_endpoint, a.sequence_state) !=
      std::tie(b.version, b.state_types, b.result_type, b.parameter_types,
          b.request_expression_types, b.bound_operand_count, b.limits.frames,
          b.limits.cells, b.limits.entry_fuel, b.memoized, b.duplicate_policy,
          b.progress, b.coordinate_slots, b.coordinate_endpoint, b.sequence_state)) return false;
  const auto equal = [](const auto& x, const auto& y, const auto& compare) {
    return x.size() == y.size() && std::equal(x.begin(), x.end(), y.begin(), compare);
  };
  if (!equal(a.preparations, b.preparations, [](const auto& x, const auto& y) {
        return x.type == y.type && x.kind == y.kind;
      })) return false;
  if (!equal(a.requests, b.requests, [&](const auto& x, const auto& y) {
        return equal(x.states, y.states, [](const auto& u, const auto& v) {
          return std::tie(u.kind, u.source_state, u.offset, u.expression,
                     u.window.begin.kind, u.window.begin.cut, u.window.end.kind, u.window.end.cut) ==
                 std::tie(v.kind, v.source_state, v.offset, v.expression,
                     v.window.begin.kind, v.window.begin.cut, v.window.end.kind, v.window.end.cut);
        });
      })) return false;
  if (!equal(a.coordinate_rank, b.coordinate_rank, [](const auto& x, const auto& y) {
        return x.coordinate == y.coordinate && x.direction == y.direction;
      })) return false;
  const auto bound_equal = [](const auto& x, const auto& y) {
    return std::tie(x.kind, x.literal, x.operand) == std::tie(y.kind, y.literal, y.operand);
  };
  return equal(a.coordinate_domains, b.coordinate_domains, [&](const auto& x, const auto& y) {
    return bound_equal(x.lower, y.lower) && bound_equal(x.upper, y.upper);
  });
}
}  // namespace gagp::evo
