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

bool test_phase_instruction_fuel_decoding() {
  const std::string json = R"([{
    "n_locals":0,
    "consts":[{"type":"string","value":"x"}],
    "code":[{"op":"PUSH_CONST","a":0},{"op":"ASGP_DC","a":0},{"op":"RETURN"}],
    "segments":{"asgp_dc":[{
      "solve_xs_name":0,"solve_n_name":1,"solve_lo_name":2,
      "divide_n_name":3,"combine_left_name":4,"combine_right_name":5,
      "solve":{"n_locals":3,"consts":[],"code":[{"op":"LOAD","a":1}],
        "binder_locals":[{"name":0,"local":0},{"name":1,"local":1},{"name":2,"local":2}],
        "instruction_fuel":[7]},
      "divide":{"n_locals":1,"consts":[],"code":[{"op":"LOAD","a":0}],
        "binder_locals":[{"name":3,"local":0}]},
      "combine":{"n_locals":2,"consts":[],"code":[{"op":"LOAD","a":0}],
        "binder_locals":[{"name":4,"local":0},{"name":5,"local":1}]}
    }]}
  }])";
  try {
    const auto root = gagp::cli_detail::JsonParser(json).parse();
    const auto programs = gagp::cli_detail::decode_programs(root);
    return check(programs.size() == 1 && programs[0].asgp_dc_segments.size() == 1 &&
                     programs[0].asgp_dc_segments[0].solve.instruction_fuel.size() == 1 &&
                     programs[0].asgp_dc_segments[0].solve.instruction_fuel[0] == 7,
                 "phase instruction_fuel should be preserved");
  } catch (const std::runtime_error& err) {
    std::cerr << "FAIL: valid phase instruction_fuel decode failed: " << err.what() << "\n";
    return false;
  }
}

bool test_bytecode_asgp_dp_segment_arity_is_validated() {
  const std::string phase = R"({"n_locals":0,"consts":[],"code":[]})";

  const std::string bad_dp1 = R"([
    {
      "n_locals": 0,
      "consts": [],
      "code": [],
      "segments": {
        "asgp_dp1d": [
          {
            "lo": 0,
            "hi": 3,
            "base_state": 0,
            "boundary_value": {"type":"int","value":0},
            "dep_kind": -1,
            "dep_offsets": [1, 2],
            "solve_state_name": 0,
            "transition_state_name": 1,
            "transition_dep_names": [2],
            "solve": )" + phase + R"(,
            "transition": )" + phase + R"(
          }
        ]
      }
    }
  ])";
  if (!decode_programs_rejects(bad_dp1, "ASGP-DP1D dependency arity mismatch")) {
    return false;
  }

  const std::string bad_dp2 = R"([
    {
      "n_locals": 0,
      "consts": [],
      "code": [],
      "segments": {
        "asgp_dp2d": [
          {
            "i_lo": 0,
            "i_hi": 3,
            "j_lo": 0,
            "j_hi": 3,
            "base_i": 0,
            "base_j": 0,
            "boundary_value": {"type":"int","value":0},
            "dep_kind": 0,
            "solve_i_name": 0,
            "solve_j_name": 1,
            "transition_i_name": 2,
            "transition_j_name": 3,
            "transition_dep_names": [4],
            "solve": )" + phase + R"(,
            "transition": )" + phase + R"(
          }
        ]
      }
    }
  ])";
  if (!decode_programs_rejects(bad_dp2, "ASGP-DP2D dependency arity mismatch")) {
    return false;
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
  if (!test_instruction_fuel_decoding()) return 1;
  if (!test_phase_instruction_fuel_decoding()) return 1;
  if (!test_bytecode_asgp_dp_segment_arity_is_validated()) return 1;
  if (!test_bytecode_verifier_runs_at_decode_boundary()) return 1;
  std::cout << "gagp_test_cli_json: OK\n";
  return 0;
}
