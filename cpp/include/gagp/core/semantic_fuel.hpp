#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "gagp/core/bytecode.hpp"

namespace gagp {

struct SemanticFuelValidation {
  bool ok = true;
  std::size_t instruction_index = 0;
  std::string message;

  explicit operator bool() const { return ok; }
};

SemanticFuelValidation validate_semantic_fuel(
    const std::vector<Instr>& code,
    const std::vector<std::uint32_t>& costs);

std::size_t bytecode_instruction_count(const BytecodeProgram& program);

bool has_semantic_fuel(const BytecodeProgram& program) noexcept;

}  // namespace gagp
