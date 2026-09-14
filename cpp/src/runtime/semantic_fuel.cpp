#include "gagp/core/semantic_fuel.hpp"

#include <deque>
#include <limits>
#include <utility>
#include <vector>

namespace gagp {
namespace {

SemanticFuelValidation fail(std::size_t instruction_index, std::string message) {
  return SemanticFuelValidation{false, instruction_index, std::move(message)};
}

bool is_known_opcode(Opcode op) {
  const int value = static_cast<int>(op);
  return value >= static_cast<int>(Opcode::PushConst) &&
         value <= static_cast<int>(Opcode::AsgpDp2d);
}

bool is_jump(Opcode op) {
  return op == Opcode::Jmp || op == Opcode::JmpIfFalse ||
         op == Opcode::JmpIfTrue;
}

template <typename Callback>
void for_each_successor(const std::vector<Instr>& code, std::size_t ip,
                        Callback callback) {
  const Instr& instruction = code[ip];
  if (instruction.op == Opcode::Return) return;

  if (is_jump(instruction.op)) {
    const std::size_t target = static_cast<std::size_t>(instruction.a);
    if (target < code.size()) callback(target);
    if (instruction.op == Opcode::Jmp) return;
  }

  if (ip + 1 < code.size()) callback(ip + 1);
}

bool phase_has_semantic_fuel(const PhaseProgram& phase) noexcept {
  return !phase.instruction_fuel.empty();
}

}  // namespace

SemanticFuelValidation validate_semantic_fuel(
    const std::vector<Instr>& code,
    const std::vector<std::uint32_t>& costs) {
  if (costs.empty()) return {};

  if (costs.size() != code.size()) {
    return fail(0, "instruction fuel plan length must match bytecode length");
  }

  constexpr std::uint32_t max_cost =
      static_cast<std::uint32_t>(std::numeric_limits<int>::max());
  for (std::size_t ip = 0; ip < code.size(); ++ip) {
    if (costs[ip] > max_cost) {
      return fail(ip, "instruction fuel cost exceeds INT_MAX");
    }
    if (!is_known_opcode(code[ip].op)) {
      return fail(ip, "unknown opcode value");
    }
    if (is_jump(code[ip].op)) {
      if (!code[ip].has_a) {
        return fail(ip, "jump instruction requires operand a");
      }
      if (code[ip].a < 0 ||
          static_cast<std::size_t>(code[ip].a) > code.size()) {
        return fail(ip, "jump target is out of range");
      }
    }
  }

  std::vector<std::size_t> indegree(code.size(), 0);
  std::size_t zero_cost_count = 0;
  for (std::size_t ip = 0; ip < code.size(); ++ip) {
    if (costs[ip] != 0) continue;
    ++zero_cost_count;
    for_each_successor(code, ip, [&](std::size_t successor) {
      if (costs[successor] == 0) ++indegree[successor];
    });
  }

  std::deque<std::size_t> ready;
  for (std::size_t ip = 0; ip < code.size(); ++ip) {
    if (costs[ip] == 0 && indegree[ip] == 0) ready.push_back(ip);
  }

  std::size_t visited = 0;
  while (!ready.empty()) {
    const std::size_t ip = ready.front();
    ready.pop_front();
    ++visited;
    for_each_successor(code, ip, [&](std::size_t successor) {
      if (costs[successor] == 0 && --indegree[successor] == 0) {
        ready.push_back(successor);
      }
    });
  }

  if (visited != zero_cost_count) {
    for (std::size_t ip = 0; ip < code.size(); ++ip) {
      if (costs[ip] == 0 && indegree[ip] != 0) {
        return fail(ip, "zero-cost control-flow cycle");
      }
    }
  }
  return {};
}

bool has_semantic_fuel(const BytecodeProgram& program) noexcept {
  if (!program.instruction_fuel.empty()) return true;
  for (const AsgpDcSegment& segment : program.asgp_dc_segments) {
    if (phase_has_semantic_fuel(segment.solve) ||
        phase_has_semantic_fuel(segment.divide) ||
        phase_has_semantic_fuel(segment.combine)) {
      return true;
    }
  }
  for (const AsgpDp1dSegment& segment : program.asgp_dp1d_segments) {
    if (phase_has_semantic_fuel(segment.solve) ||
        phase_has_semantic_fuel(segment.transition)) {
      return true;
    }
  }
  for (const AsgpDp2dSegment& segment : program.asgp_dp2d_segments) {
    if (phase_has_semantic_fuel(segment.solve) ||
        phase_has_semantic_fuel(segment.transition)) {
      return true;
    }
  }
  return false;
}

}  // namespace gagp
