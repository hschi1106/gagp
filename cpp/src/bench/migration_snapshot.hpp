#pragma once

#include <string>
#include <vector>

#include "gagp/cli/json.hpp"
#include "gagp/evolution/genome.hpp"
#include "gagp/core/bytecode.hpp"

namespace gagp::migration {

// Benchmark artifact format, deliberately independent of public typed JSON.
// Scalar bits and decoded payload bytes survive a fresh process without loss.
std::string encode_value(const Value& value, bool require_payload = true);
Value decode_value(const cli_detail::JsonValue& raw);
std::string encode_bytecode(const BytecodeProgram& program, bool require_payload = true);
BytecodeProgram decode_bytecode(const cli_detail::JsonValue& raw);
std::string encode_population(const std::vector<evo::ProgramGenome>& population,
                              bool require_payload = true);
std::vector<evo::ProgramGenome> decode_population(const cli_detail::JsonValue& raw);

}  // namespace gagp::migration
