#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "gagp/core/semantic_fuel.hpp"

namespace {

using namespace gagp;

Instr ins(Opcode op) { return Instr{op, 0, 0, false, false}; }
Instr ins_a(Opcode op, int a) { return Instr{op, a, 0, true, false}; }

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << "\n";
  return condition;
}

bool expect_failure(const std::vector<Instr>& code,
                    const std::vector<std::uint32_t>& costs,
                    std::size_t instruction_index,
                    const std::string& message_fragment,
                    const std::string& label) {
  const SemanticFuelValidation result = validate_semantic_fuel(code, costs);
  return check(!result, label + " should fail") &&
         check(result.instruction_index == instruction_index,
               label + " reported the wrong instruction") &&
         check(result.message.find(message_fragment) != std::string::npos,
               label + " reported: " + result.message);
}

}  // namespace

int main() {
  using namespace gagp;

  const std::vector<Instr> malformed{
      ins_a(static_cast<Opcode>(255), -1), ins(Opcode::Return)};
  if (!check(validate_semantic_fuel(malformed, {}).ok,
             "empty plan retains the legacy validation path")) {
    return 1;
  }

  const std::vector<Instr> simple{ins(Opcode::Return)};
  if (!expect_failure(simple, {1, 1}, 0, "length", "wrong plan length")) return 1;
  if (!expect_failure(
          simple,
          {static_cast<std::uint32_t>(std::numeric_limits<int>::max()) + 1U},
          0, "INT_MAX", "oversized cost")) {
    return 1;
  }

  const std::vector<Instr> diamond{
      ins_a(Opcode::JmpIfTrue, 2), ins_a(Opcode::Jmp, 3),
      ins_a(Opcode::Jmp, 3), ins(Opcode::Return)};
  if (!check(validate_semantic_fuel(diamond, {0, 0, 0, 0}).ok,
             "zero-cost acyclic diamond is accepted")) {
    return 1;
  }

  if (!expect_failure({ins_a(Opcode::Jmp, 0)}, {0}, 0, "cycle",
                      "zero-cost self-cycle")) {
    return 1;
  }
  if (!expect_failure(
          {ins_a(Opcode::Jmp, 1), ins_a(Opcode::Jmp, 0)}, {0, 0}, 0,
          "cycle", "zero-cost two-node cycle")) {
    return 1;
  }

  const std::vector<Instr> charged_cycle{
      ins_a(Opcode::Jmp, 1), ins_a(Opcode::Jmp, 2), ins_a(Opcode::Jmp, 0)};
  if (!check(validate_semantic_fuel(charged_cycle, {0, 0, 1}).ok,
             "cycle containing a charged instruction is accepted")) {
    return 1;
  }

  const std::vector<Instr> unreachable_cycle{
      ins(Opcode::Return), ins_a(Opcode::Jmp, 2), ins_a(Opcode::Jmp, 1)};
  if (!expect_failure(unreachable_cycle, {0, 0, 0}, 1, "cycle",
                      "unreachable zero-cost cycle")) {
    return 1;
  }

  if (!expect_failure({ins(Opcode::Jmp)}, {0}, 0, "operand",
                      "jump without operand")) {
    return 1;
  }
  if (!expect_failure({ins_a(Opcode::Jmp, 2)}, {0}, 0, "out of range",
                      "jump beyond bytecode")) {
    return 1;
  }
  if (!expect_failure({ins(static_cast<Opcode>(255))}, {0}, 0, "unknown opcode",
                      "unknown opcode")) {
    return 1;
  }

  constexpr std::size_t chain_size = 100000;
  std::vector<Instr> long_chain(chain_size, ins(Opcode::Neg));
  long_chain.back() = ins(Opcode::Return);
  if (!check(validate_semantic_fuel(long_chain,
                                    std::vector<std::uint32_t>(chain_size, 0)).ok,
             "large zero-cost chain validates without recursion")) {
    return 1;
  }

  BytecodeProgram program;
  if (!check(!has_semantic_fuel(program), "empty program has no semantic fuel")) return 1;
  program.instruction_fuel = {1};
  if (!check(has_semantic_fuel(program), "root semantic fuel is detected")) return 1;
  program.instruction_fuel.clear();

  program.asgp_dc_segments.emplace_back();
  program.asgp_dc_segments.back().combine.instruction_fuel = {1};
  if (!check(has_semantic_fuel(program), "ASGP-DC phase semantic fuel is detected")) return 1;
  program.asgp_dc_segments.clear();

  program.asgp_dp1d_segments.emplace_back();
  program.asgp_dp1d_segments.back().transition.instruction_fuel = {1};
  if (!check(has_semantic_fuel(program), "ASGP-DP1D phase semantic fuel is detected")) return 1;
  program.asgp_dp1d_segments.clear();

  program.asgp_dp2d_segments.emplace_back();
  program.asgp_dp2d_segments.back().solve.instruction_fuel = {1};
  if (!check(has_semantic_fuel(program), "ASGP-DP2D phase semantic fuel is detected")) return 1;

  return 0;
}
