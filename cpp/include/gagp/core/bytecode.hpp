#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "gagp/core/opcode.hpp"
#include "gagp/core/region_plan.hpp"
#include "gagp/core/value.hpp"

namespace gagp {

struct Instr {
  Opcode op = Opcode::PushConst;
  int a = 0;
  int b = 0;
  bool has_a = false;
  bool has_b = false;
};

struct PhaseProgram {
  std::vector<Value> consts;
  std::vector<Instr> code;
  int n_locals = 0;
  std::unordered_map<std::string, int> var2idx;
  std::unordered_map<int, int> binder_locals;
  // Empty preserves legacy unit charges; otherwise one semantic cost per instruction.
  std::vector<std::uint32_t> instruction_fuel;
};

struct RegionPhaseBinding {
  RegionValueSlot source;
  int local = 0;
};

struct RegionPhase {
  PhaseProgram program;
  std::vector<RegionPhaseBinding> bindings;
};

struct BoundedRegionSegment {
  RegionPlan plan;
  // Snapshot each caller local, including its set/unset state, once on entry.
  // Types correspond positionally to plan.parameter_types.
  std::vector<int> parameter_locals;
  std::optional<RegionPhase> boundary;
  RegionPhase base_predicate;
  RegionPhase base_body;
  std::vector<RegionPhase> preparations;
  std::vector<RegionPhase> request_expressions;
  RegionPhase combine;
};

struct AsgpDcSegment {
  int solve_xs_name = 0;
  int solve_n_name = 0;
  int solve_lo_name = 0;
  int divide_n_name = 0;
  int combine_left_name = 0;
  int combine_right_name = 0;
  PhaseProgram solve;
  PhaseProgram divide;
  PhaseProgram combine;
};

struct AsgpDp1dSegment {
  int lo = 0;
  int hi = 0;
  int base_state = 0;
  Value boundary_value = Value::invalid();
  int dep_kind = 0;
  std::vector<int> dep_offsets;
  int solve_state_name = 0;
  int transition_state_name = 0;
  std::vector<int> transition_dep_names;
  PhaseProgram solve;
  PhaseProgram transition;
};

struct AsgpDp2dSegment {
  int i_lo = 0;
  int i_hi = 0;
  int j_lo = 0;
  int j_hi = 0;
  int base_i = 0;
  int base_j = 0;
  Value boundary_value = Value::invalid();
  int dep_kind = 0;
  int solve_i_name = 0;
  int solve_j_name = 0;
  int transition_i_name = 0;
  int transition_j_name = 0;
  std::vector<int> transition_dep_names;
  PhaseProgram solve;
  PhaseProgram transition;
};

struct BytecodeProgram {
  std::vector<Value> consts;
  std::vector<Instr> code;
  int n_locals = 0;
  std::unordered_map<std::string, int> var2idx;
  std::vector<AsgpDcSegment> asgp_dc_segments;
  std::vector<AsgpDp1dSegment> asgp_dp1d_segments;
  std::vector<AsgpDp2dSegment> asgp_dp2d_segments;
  // Empty preserves legacy unit charges; otherwise one semantic cost per instruction.
  std::vector<std::uint32_t> instruction_fuel;
  std::vector<BoundedRegionSegment> bounded_region_segments;
};

inline bool has_bounded_region(const PhaseProgram& phase) noexcept {
  for (const Instr& instruction : phase.code) {
    if (instruction.op == Opcode::BoundedRegion) return true;
  }
  return false;
}

inline bool has_bounded_region(const BytecodeProgram& program) noexcept {
  if (!program.bounded_region_segments.empty()) return true;
  for (const Instr& instruction : program.code) {
    if (instruction.op == Opcode::BoundedRegion) return true;
  }
  for (const AsgpDcSegment& segment : program.asgp_dc_segments) {
    if (has_bounded_region(segment.solve) ||
        has_bounded_region(segment.divide) ||
        has_bounded_region(segment.combine)) {
      return true;
    }
  }
  for (const AsgpDp1dSegment& segment : program.asgp_dp1d_segments) {
    if (has_bounded_region(segment.solve) ||
        has_bounded_region(segment.transition)) {
      return true;
    }
  }
  for (const AsgpDp2dSegment& segment : program.asgp_dp2d_segments) {
    if (has_bounded_region(segment.solve) ||
        has_bounded_region(segment.transition)) {
      return true;
    }
  }
  return false;
}

}  // namespace gagp
