#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include "gagp/cli/codec.hpp"
#include "gagp/cli/json.hpp"

namespace {

bool check(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
    return false;
  }
  return true;
}

bool test_subnormal_number_is_accepted() {
  const gagp::cli_detail::JsonValue root =
      gagp::cli_detail::JsonParser("{\"x\":3.340886621450795e-309}").parse();
  const auto it = root.object_v.find("x");
  if (!check(it != root.object_v.end(), "subnormal field missing")) {
    return false;
  }
  if (!check(it->second.kind == gagp::cli_detail::JsonValue::Kind::Number,
             "subnormal field should parse as number")) {
    return false;
  }
  if (!check(it->second.number_v > 0.0, "subnormal number should not be rounded to zero")) {
    return false;
  }
  return true;
}

bool test_overflow_number_is_rejected() {
  try {
    (void)gagp::cli_detail::JsonParser("{\"x\":1e9999}").parse();
  } catch (const std::runtime_error& err) {
    return std::string(err.what()).find("out of range") != std::string::npos;
  }
  std::cerr << "FAIL: overflowing JSON number should be rejected\n";
  return false;
}

bool decode_programs_rejects(const std::string& json, const std::string& needle) {
  try {
    const gagp::cli_detail::JsonValue root = gagp::cli_detail::JsonParser(json).parse();
    (void)gagp::cli_detail::decode_programs(root);
  } catch (const std::runtime_error& err) {
    return std::string(err.what()).find(needle) != std::string::npos;
  }
  std::cerr << "FAIL: decode_programs should reject " << needle << "\n";
  return false;
}

bool bytecode_document_rejects(const std::string& version,
                               const std::string& needle) {
  try {
    const auto root = gagp::cli_detail::JsonParser(
        "{\"format_version\":\"" + version + "\"}").parse();
    (void)gagp::cli_detail::require_bytecode_json_format(root);
  } catch (const std::runtime_error& err) {
    return check(std::string(err.what()).find(needle) != std::string::npos,
                 "bytecode document rejection should explain migration");
  }
  std::cerr << "FAIL: bytecode document should reject " << version << "\n";
  return false;
}

bool test_bytecode_document_versions() {
  try {
    const auto request = gagp::cli_detail::JsonParser(
        R"({"format_version":"bytecode-json-v2"})").parse();
    const auto fixture = gagp::cli_detail::JsonParser(
        R"({"format_version":"bytecode-fixture-v2"})").parse();
    if (!check(gagp::cli_detail::require_bytecode_json_format(request) ==
                   gagp::cli_detail::BytecodeJsonFormat::Request,
               "current bytecode request version should decode")) return false;
    if (!check(gagp::cli_detail::require_bytecode_json_format(fixture) ==
                   gagp::cli_detail::BytecodeJsonFormat::Fixture,
               "current bytecode fixture version should decode")) return false;
  } catch (const std::runtime_error& err) {
    std::cerr << "FAIL: current bytecode document version failed: " << err.what() << "\n";
    return false;
  }
  constexpr const char* migration =
      "bytecode migration is unsupported because source AST/type provenance is absent";
  for (const std::string& old : {
           "bytecode-json", "bytecode-fixture", "migration-bytecode-v1"}) {
    if (!bytecode_document_rejects(old, migration)) return false;
  }
  return bytecode_document_rejects(
      "bytecode-json-v3", "expected bytecode-json-v2 or bytecode-fixture-v2");
}

bool test_instruction_fuel_decoding() {
  const std::string valid = R"([{
    "n_locals":0,
    "consts":[{"type":"int","value":1}],
    "code":[{"op":"PUSH_CONST","a":0},{"op":"RETURN"}],
    "instruction_fuel":[3,0]
  }])";
  try {
    const auto root = gagp::cli_detail::JsonParser(valid).parse();
    const auto programs = gagp::cli_detail::decode_programs(root);
    if (!check(programs.size() == 1 && programs[0].instruction_fuel.size() == 2 &&
                   programs[0].instruction_fuel[0] == 3 &&
                   programs[0].instruction_fuel[1] == 0,
               "root instruction_fuel should be preserved")) {
      return false;
    }
  } catch (const std::runtime_error& err) {
    std::cerr << "FAIL: valid instruction_fuel decode failed: " << err.what() << "\n";
    return false;
  }

  for (const std::string& legacy : {
           R"([{"n_locals":0,"consts":[{"type":"int","value":1}],"code":[{"op":"PUSH_CONST","a":0},{"op":"RETURN"}],"instruction_fuel":[]}])",
           R"([{"n_locals":0,"consts":[{"type":"int","value":1}],"code":[{"op":"PUSH_CONST","a":0},{"op":"RETURN"}]}])",
       }) {
    try {
      const auto root = gagp::cli_detail::JsonParser(legacy).parse();
      const auto programs = gagp::cli_detail::decode_programs(root);
      if (!check(programs.size() == 1 && programs[0].instruction_fuel.empty(),
                 "empty or absent instruction_fuel should preserve legacy charging")) {
        return false;
      }
    } catch (const std::runtime_error& err) {
      std::cerr << "FAIL: legacy instruction_fuel decode failed: " << err.what() << "\n";
      return false;
    }
  }

  const std::string wrong_length = R"([{
    "n_locals":0,
    "consts":[{"type":"int","value":1}],
    "code":[{"op":"PUSH_CONST","a":0},{"op":"RETURN"}],
    "instruction_fuel":[1]
  }])";
  if (!decode_programs_rejects(wrong_length, "invalid_fuel_schedule")) return false;

  const std::string zero_cost_cycle = R"([{
    "n_locals":0,
    "consts":[],
    "code":[{"op":"JMP","a":0}],
    "instruction_fuel":[0]
  }])";
  if (!decode_programs_rejects(zero_cost_cycle, "invalid_fuel_schedule")) return false;

  for (const std::string& invalid_cost : {
           "-1", "1.5", "2147483648", "4294967296", "1e100", "null",
       }) {
    const std::string json =
        R"([{"n_locals":0,"consts":[{"type":"int","value":1}],"code":[{"op":"PUSH_CONST","a":0},{"op":"RETURN"}],"instruction_fuel":[)" +
        invalid_cost + R"(,0]}])";
    if (!decode_programs_rejects(json, "instruction_fuel costs")) return false;
  }

  for (const std::string& invalid_field : {"null", "{}"}) {
    const std::string json =
        R"([{"n_locals":0,"consts":[{"type":"int","value":1}],"code":[{"op":"PUSH_CONST","a":0},{"op":"RETURN"}],"instruction_fuel":)" +
        invalid_field + R"(}])";
    if (!decode_programs_rejects(json, "instruction_fuel must be array")) return false;
  }

  return true;
}

bool test_removed_segment_keys_are_rejected() {
  for (const std::string& key : {"asgp_dc", "asgp_dp1d", "asgp_dp2d"}) {
    const std::string json =
        R"([{"n_locals":0,"consts":[],"code":[],"segments":{")" +
        key + R"(":[]}}])";
    if (!decode_programs_rejects(json, "unsupported")) return false;
  }
  return true;
}

bool test_bytecode_verifier_runs_at_decode_boundary() {
  const std::string valid = R"([
    {
      "n_locals": 0,
      "consts": [{"type":"int","value":1}],
      "code": [{"op":"PUSH_CONST","a":0},{"op":"RETURN"}]
    }
  ])";
  try {
    const auto root = gagp::cli_detail::JsonParser(valid).parse();
    if (!check(gagp::cli_detail::decode_programs(root).size() == 1,
               "valid bytecode should decode")) return false;
  } catch (const std::runtime_error& err) {
    std::cerr << "FAIL: valid bytecode decode failed: " << err.what() << "\n";
    return false;
  }

  const std::string underflow = R"([
    {"n_locals":0,"consts":[],"code":[{"op":"RETURN"}]}
  ])";
  if (!decode_programs_rejects(underflow, "stack_underflow")) return false;

  const std::string fallthrough = R"([
    {
      "n_locals":0,
      "consts":[{"type":"int","value":1}],
      "code":[{"op":"PUSH_CONST","a":0}]
    }
  ])";
  if (!decode_programs_rejects(fallthrough, "invalid_fallthrough")) return false;

  const std::string runtime_type_error = R"([
    {
      "n_locals":0,
      "consts":[{"type":"bool","value":true}],
      "code":[{"op":"PUSH_CONST","a":0},{"op":"NEG"}]
    }
  ])";
  try {
    const auto root = gagp::cli_detail::JsonParser(runtime_type_error).parse();
    if (!check(gagp::cli_detail::decode_programs(root).size() == 1,
               "runtime TypeError bytecode should remain decodable")) return false;
  } catch (const std::runtime_error& err) {
    std::cerr << "FAIL: runtime error bytecode was treated as malformed: " << err.what() << "\n";
    return false;
  }
  return true;
}

}  // namespace

int main() {
  if (!test_subnormal_number_is_accepted()) return 1;
  if (!test_overflow_number_is_rejected()) return 1;
  if (!test_bytecode_document_versions()) return 1;
  if (!test_instruction_fuel_decoding()) return 1;
  if (!test_removed_segment_keys_are_rejected()) return 1;
  if (!test_bytecode_verifier_runs_at_decode_boundary()) return 1;
  std::cout << "gagp_test_cli_json: OK\n";
  return 0;
}
