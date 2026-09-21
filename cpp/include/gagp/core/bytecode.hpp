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

struct BytecodeProgram {
  std::vector<Value> consts;
  std::vector<Instr> code;
  int n_locals = 0;
  std::unordered_map<std::string, int> var2idx;
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
  return false;
}

}  // namespace gagp
