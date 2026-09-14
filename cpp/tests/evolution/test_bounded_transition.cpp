#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/core/bytecode.hpp"
#include "gagp/core/errors.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/input_spec.hpp"
#include "gagp/evolution/transition/bounded_regions.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/payload/payload.hpp"

namespace {

using Expr = std::vector<gagp::evo::AstNode>;
using gagp::BytecodeProgram;
using gagp::ErrCode;
using gagp::ExecResult;
using gagp::Opcode;
using gagp::Value;
using gagp::ValueTag;
using gagp::evo::AsgpDcBinders;
using gagp::evo::AsgpDp1dSpec;
using gagp::evo::AsgpDp2dSpec;
using gagp::evo::AstProgram;
using gagp::evo::InputSpec;
using gagp::evo::NodeKind;
using gagp::evo::ProgramGenome;
using gagp::evo::RType;

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << '\n';
  return condition;
}

void append(Expr* target, const Expr& child) {
  target->insert(target->end(), child.begin(), child.end());
}

Expr leaf(NodeKind kind, int i0 = 0) { return {{kind, i0, 0}}; }

Expr expression(NodeKind kind, std::initializer_list<Expr> children) {
  Expr result{{kind, 0, 0}};
  for (const Expr& child : children) append(&result, child);
  return result;
}

AstProgram return_program(const Expr& result, std::vector<Value> constants,
                          std::vector<std::string> names) {
  AstProgram ast;
  ast.consts = std::move(constants);
  ast.names = std::move(names);
  ast.nodes = {{NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS},
               {NodeKind::RETURN}};
  append(&ast.nodes, result);
  ast.nodes.push_back({NodeKind::BLOCK_NIL});
  return ast;
}

ProgramGenome genome(AstProgram ast) {
  ProgramGenome result;
  result.ast = std::move(ast);
  result.meta = gagp::evo::build_genome_meta(result.ast);
  return result;
}

std::vector<std::string> input_names(const std::vector<InputSpec>& inputs) {
  std::vector<std::string> result;
  for (const InputSpec& input : inputs) result.push_back(input.name);
  return result;
}

BytecodeProgram compile_checked(const ProgramGenome& source,
                                const std::vector<InputSpec>& inputs) {
  const auto verified = gagp::evo::verify_ast(source.ast, inputs);
  if (!verified) {
    throw std::runtime_error(
        std::string("transition fixture failed native verification: ") +
        gagp::evo::verify_code_name(verified.diagnostic.code) + " " +
        verified.diagnostic.message);
  }
  return gagp::evo::compile_for_eval(source, verified.verified,
                                     input_names(inputs));
}

bool exact_value(const Value& left, const Value& right) {
  if (left.tag != right.tag) return false;
  switch (left.tag) {
    case ValueTag::Int:
    case ValueTag::Char:
    case ValueTag::FallbackToken:
      return left.i == right.i;
    case ValueTag::Float:
      return std::memcmp(&left.f, &right.f, sizeof(double)) == 0;
    case ValueTag::Bool:
      return left.b == right.b;
    case ValueTag::String: {
      std::string a;
      std::string b;
      const bool have_a = gagp::payload::lookup_string(left, &a);
      const bool have_b = gagp::payload::lookup_string(right, &b);
      return have_a == have_b && (have_a ? a == b : left.i == right.i);
    }
    case ValueTag::IntList:
    case ValueTag::FloatList:
    case ValueTag::StringList: {
      std::vector<Value> a;
      std::vector<Value> b;
      const bool have_a = gagp::payload::lookup_list(left, &a);
      const bool have_b = gagp::payload::lookup_list(right, &b);
      if (have_a != have_b) return false;
      if (!have_a) return left.i == right.i;
      if (a.size() != b.size()) return false;
      for (std::size_t i = 0; i < a.size(); ++i)
        if (!exact_value(a[i], b[i])) return false;
      return true;
    }
    case ValueTag::Invalid:
      return true;
  }
  return false;
}

bool same_result(const ExecResult& left, const ExecResult& right) {
  if (left.is_error != right.is_error) return false;
  return left.is_error ? left.err.code == right.err.code
                       : exact_value(left.value, right.value);
}

bool old_node(NodeKind kind) {
  return kind == NodeKind::ASGP_DC || kind == NodeKind::ASGP_DP1D ||
         kind == NodeKind::ASGP_DP2D;
}

bool old_opcode(Opcode op) {
  return op == Opcode::AsgpDc || op == Opcode::AsgpDp1d ||
         op == Opcode::AsgpDp2d;
}

bool check_transition_shape(const ProgramGenome& lowered,
                            const BytecodeProgram& bytecode,
                            const std::string& label) {
  if (!check(lowered.ast.asgp_dc_binders.empty() &&
                 lowered.ast.asgp_dp1d_specs.empty() &&
                 lowered.ast.asgp_dp2d_specs.empty(),
             label + ": legacy AST metadata remains") ||
      !check(!lowered.ast.bounded_region_specs.empty(),
             label + ": transition emitted no bounded metadata") ||
      !check(bytecode.asgp_dc_segments.empty() &&
                 bytecode.asgp_dp1d_segments.empty() &&
                 bytecode.asgp_dp2d_segments.empty(),
             label + ": legacy bytecode segments remain") ||
      !check(!bytecode.bounded_region_segments.empty(),
             label + ": transition lowered no generic segment")) {
    return false;
  }
  for (const auto& node : lowered.ast.nodes)
    if (!check(!old_node(node.kind), label + ": legacy AST node remains"))
      return false;
  for (const auto& instruction : bytecode.code)
    if (!check(!old_opcode(instruction.op),
               label + ": legacy root opcode remains")) return false;
  return true;
}

int first_non_timeout(const BytecodeProgram& program,
                      const std::vector<std::pair<int, Value>>& inputs) {
  for (int fuel = 0; fuel <= 10000; ++fuel) {
    const ExecResult result =
        gagp::execute_bytecode_cpu(program, inputs, fuel);
    if (!result.is_error || result.err.code != ErrCode::Timeout) return fuel;
  }
  return -1;
}

bool differential(const ProgramGenome& source,
                  const std::vector<InputSpec>& input_types,
                  const std::vector<std::pair<int, Value>>& runtime_inputs,
                  const std::string& label) {
  const BytecodeProgram before = compile_checked(source, input_types);
  const ProgramGenome lowered =
      gagp::evo::transition::lower_bounded_regions(source, input_types);
  const BytecodeProgram after = compile_checked(lowered, input_types);
  if (!check_transition_shape(lowered, after, label)) return false;
  const int boundary = first_non_timeout(before, runtime_inputs);
  if (!check(boundary >= 0, label + ": no finite fuel boundary")) return false;
  for (int fuel = 0; fuel <= boundary + 3; ++fuel) {
    const ExecResult old_result =
        gagp::execute_bytecode_cpu(before, runtime_inputs, fuel);
    const ExecResult new_result =
        gagp::execute_bytecode_cpu(after, runtime_inputs, fuel);
    if (!check(same_result(old_result, new_result),
               label + ": result differs at fuel " +
                   std::to_string(fuel))) return false;
  }
  return true;
}

std::vector<std::pair<std::string, Value>> result_values() {
  return {
      {"Int", Value::from_int(-7)},
      {"Float", Value::from_float(1.25)},
      {"Bool", Value::from_bool(true)},
      {"Char", Value::from_char('q')},
      {"String", gagp::payload::make_string_value(std::string("a\0b", 3))},
      {"IntList", gagp::payload::make_int_list_value(
          {Value::from_int(-1), Value::from_int(4)})},
      {"FloatList", gagp::payload::make_float_list_value(
          {Value::from_float(-0.0), Value::from_float(2.5)})},
      {"StringList", gagp::payload::make_string_list_value(
          {gagp::payload::make_string_value(""),
           gagp::payload::make_string_value("xy")})},
  };
}

std::vector<std::pair<std::string, Value>> source_values() {
  return {
      {"String", gagp::payload::make_string_value("abc")},
      {"IntList", gagp::payload::make_int_list_value(
          {Value::from_int(1), Value::from_int(2), Value::from_int(3)})},
      {"FloatList", gagp::payload::make_float_list_value(
          {Value::from_float(1), Value::from_float(2), Value::from_float(3)})},
      {"StringList", gagp::payload::make_string_list_value(
          {gagp::payload::make_string_value("a"),
           gagp::payload::make_string_value("b"),
           gagp::payload::make_string_value("c")})},
  };
}

ProgramGenome dc_fixture(const Expr& source, Value source_constant,
                         Value result, std::int64_t split,
                         std::vector<std::string> names =
                             {"xs", "n", "lo", "divide_n", "left", "right"}) {
  const int shift = source.front().kind == NodeKind::VAR ? 1 : 0;
  Expr root = expression(NodeKind::ASGP_DC,
                         {source, leaf(NodeKind::CONST, 1),
                          leaf(NodeKind::CONST, 2),
                          leaf(NodeKind::BOUND_VAR, 4 + shift)});
  AstProgram ast = return_program(
      root, {source_constant, result, Value::from_int(split)},
      std::move(names));
  ast.asgp_dc_binders = {{3, shift, 1 + shift, 2 + shift, 3 + shift,
                          4 + shift, 5 + shift}};
  return genome(std::move(ast));
}

bool test_dc_matrix_and_errors() {
  for (const auto& source : source_values()) {
    for (const auto& result : result_values()) {
      if (!differential(
              dc_fixture(leaf(NodeKind::CONST, 0), source.second,
                         result.second, 1),
              {}, {}, "DC " + source.first + " -> " + result.first)) {
        return false;
      }
    }
  }
  for (const std::int64_t split : {
           std::numeric_limits<std::int64_t>::min(),
           std::numeric_limits<std::int64_t>::max()}) {
    if (!differential(
            dc_fixture(leaf(NodeKind::CONST, 0),
                       gagp::payload::make_string_value("abcd"),
                       Value::from_int(3), split),
            {}, {}, split < 0 ? "DC minimum split" : "DC maximum split")) {
      return false;
    }
  }

  const ProgramGenome input = dc_fixture(
      leaf(NodeKind::VAR, 0), gagp::payload::make_string_value("unused"),
      Value::from_int(2), 1,
      {"source", "xs", "n", "lo", "divide_n", "left", "right"});
  if (!differential(input, {{"source", RType::String}},
                    {{0, Value::from_bool(false)}},
                    "DC wrong input type and fuel ordering")) {
    return false;
  }
  // Raw VM calls do not carry InputSpec. Deliberately violating its declared
  // String type is outside the typed translation contract: legacy DC accepts
  // any sequence tag, while the materialized plan enforces its exact state tag.
  // Retain both observations explicitly instead of counting this as parity.
  const std::vector<InputSpec> schema{{"source", RType::String}};
  const std::vector<std::pair<int, Value>> mismatched_input{
      {0, gagp::payload::make_int_list_value({Value::from_int(1), Value::from_int(2)})}};
  const auto before = compile_checked(input, schema);
  const auto after = compile_checked(
      gagp::evo::transition::lower_bounded_regions(input, schema), schema);
  const auto legacy = gagp::execute_bytecode_cpu(before, mismatched_input, 1000);
  const auto exact = gagp::execute_bytecode_cpu(after, mismatched_input, 1000);
  if (!check(!legacy.is_error && legacy.value.tag == ValueTag::Int && legacy.value.i == 2,
             "legacy DC schema-violation observation changed") ||
      !check(exact.is_error && exact.err.code == ErrCode::Type,
             "bounded DC accepted a state outside its declared exact type")) return false;
  const auto at_entry = gagp::execute_bytecode_cpu(after, mismatched_input, 3);
  const auto before_entry = gagp::execute_bytecode_cpu(after, mismatched_input, 2);
  return check(at_entry.is_error && at_entry.err.code == ErrCode::Type &&
                   before_entry.is_error && before_entry.err.code == ErrCode::Timeout,
               "bounded DC must charge entry before rejecting a mismatched sequence tag");
}

int dependency_arity(NodeKind kind) {
  switch (kind) {
    case NodeKind::DP1_BACKWARD1:
    case NodeKind::DP1_FORWARD1:
      return 1;
    case NodeKind::DP1_BACKWARD2:
    case NodeKind::DP1_FORWARD2:
      return 2;
    case NodeKind::DP1_BACKWARD3:
    case NodeKind::DP1_FORWARD3:
      return 3;
    default:
      throw std::logic_error("not a DP1 dependency kind");
  }
}

bool forward_dp1(NodeKind kind) {
  return kind == NodeKind::DP1_FORWARD1 ||
         kind == NodeKind::DP1_FORWARD2 ||
         kind == NodeKind::DP1_FORWARD3;
}

ProgramGenome dp1_fixture(NodeKind kind, Value result,
                          bool input_state = false) {
  const int arity = dependency_arity(kind);
  const int shift = input_state ? 1 : 0;
  std::vector<int> offsets;
  std::vector<int> deps;
  std::vector<std::string> names;
  if (input_state) names.push_back("state_input");
  names.push_back("solve_state");
  names.push_back("transition_state");
  for (int i = 0; i < arity; ++i) {
    offsets.push_back(i == 0 ? 1 : 2);
    deps.push_back(shift + 2 + i);
    names.push_back("dep" + std::to_string(i));
  }
  if (arity == 2) offsets[1] = 1;
  if (arity == 3) offsets[2] = 2;
  const int state = forward_dp1(kind) ? 0 : 3;
  const Expr root = expression(
      NodeKind::ASGP_DP1D,
      {input_state ? leaf(NodeKind::VAR, 0) : leaf(NodeKind::CONST, 0),
       leaf(NodeKind::CONST, 1), leaf(NodeKind::BOUND_VAR, deps.front())});
  AstProgram ast = return_program(
      root, {Value::from_int(state), result, result}, std::move(names));
  ast.asgp_dp1d_specs = {{3, 0, forward_dp1(kind) ? 4 : 3,
                          forward_dp1(kind) ? 3 : 0, 2,
                          kind, std::move(offsets), shift, 1 + shift,
                          std::move(deps)}};
  if (input_state) ast.nodes[4].i0 = 0;
  return genome(std::move(ast));
}

bool test_dp1_directions_arities_and_input_error() {
  const std::vector<NodeKind> kinds{
      NodeKind::DP1_BACKWARD1, NodeKind::DP1_BACKWARD2,
      NodeKind::DP1_BACKWARD3, NodeKind::DP1_FORWARD1,
      NodeKind::DP1_FORWARD2, NodeKind::DP1_FORWARD3};
  for (const NodeKind kind : kinds) {
    for (const auto& result : result_values()) {
      if (!differential(dp1_fixture(kind, result.second), {}, {},
                        "DP1 dependency kind " +
                            std::to_string(static_cast<int>(kind)) + " " +
                            result.first)) {
        return false;
      }
    }
  }
  return differential(dp1_fixture(NodeKind::DP1_BACKWARD2,
                                  Value::from_int(9), true),
                      {{"state_input", RType::Int}},
                      {{0, Value::from_bool(true)}},
                      "DP1 wrong input type and fuel ordering");
}

int dp2_arity(NodeKind kind) {
  switch (kind) {
    case NodeKind::DP2_DIAGONAL_BACKWARD:
    case NodeKind::DP2_DIAGONAL_FORWARD:
      return 1;
    case NodeKind::DP2_CROSS_BACKWARD:
    case NodeKind::DP2_CROSS_FORWARD:
      return 2;
    case NodeKind::DP2_NEIGHBORHOOD_BACKWARD3:
    case NodeKind::DP2_NEIGHBORHOOD_FORWARD3:
      return 3;
    default:
      throw std::logic_error("not a DP2 dependency kind");
  }
}

bool forward_dp2(NodeKind kind) {
  return kind == NodeKind::DP2_CROSS_FORWARD ||
         kind == NodeKind::DP2_DIAGONAL_FORWARD ||
         kind == NodeKind::DP2_NEIGHBORHOOD_FORWARD3;
}

ProgramGenome dp2_fixture(NodeKind kind, Value result) {
  const int arity = dp2_arity(kind);
  std::vector<std::string> names{"solve_i", "solve_j", "transition_i",
                                 "transition_j"};
  std::vector<int> deps;
  for (int i = 0; i < arity; ++i) {
    deps.push_back(4 + i);
    names.push_back("dep" + std::to_string(i));
  }
  const int state = forward_dp2(kind) ? 0 : 2;
  const Expr root = expression(
      NodeKind::ASGP_DP2D,
      {leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
       leaf(NodeKind::CONST, 2), leaf(NodeKind::BOUND_VAR, deps.front())});
  AstProgram ast = return_program(
      root, {Value::from_int(state), Value::from_int(state), result, result},
      std::move(names));
  ast.asgp_dp2d_specs = {{3, 0, forward_dp2(kind) ? 3 : 2,
                          0, forward_dp2(kind) ? 3 : 2,
                          forward_dp2(kind) ? 2 : 0,
                          forward_dp2(kind) ? 2 : 0, 3, kind,
                          0, 1, 2, 3, std::move(deps)}};
  return genome(std::move(ast));
}

ProgramGenome dp2_input_fixture() {
  const Expr root = expression(
      NodeKind::ASGP_DP2D,
      {leaf(NodeKind::VAR, 0), leaf(NodeKind::VAR, 1),
       leaf(NodeKind::CONST, 0), leaf(NodeKind::BOUND_VAR, 6)});
  AstProgram ast = return_program(
      root, {Value::from_int(4), Value::from_int(0)},
      {"i_input", "j_input", "solve_i", "solve_j", "transition_i",
       "transition_j", "dep"});
  ast.asgp_dp2d_specs = {{3, 0, 2, 0, 2, 0, 0, 1,
                          NodeKind::DP2_DIAGONAL_BACKWARD,
                          2, 3, 4, 5, {6}}};
  return genome(std::move(ast));
}

ProgramGenome dp2_second_expression_zero_div_fixture() {
  const Expr divide_by_zero = expression(
      NodeKind::MOD,
      {leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1)});
  const Expr root = expression(
      NodeKind::ASGP_DP2D,
      {leaf(NodeKind::VAR, 0), divide_by_zero,
       leaf(NodeKind::CONST, 2), leaf(NodeKind::BOUND_VAR, 5)});
  AstProgram ast = return_program(
      root, {Value::from_int(1), Value::from_int(0), Value::from_int(4),
             Value::from_int(0)},
      {"i_input", "solve_i", "solve_j", "transition_i",
       "transition_j", "dep"});
  ast.asgp_dp2d_specs = {{3, 0, 2, 0, 2, 0, 0, 3,
                          NodeKind::DP2_DIAGONAL_BACKWARD,
                          1, 2, 3, 4, {5}}};
  return genome(std::move(ast));
}

bool test_dp2_patterns_and_result_types() {
  const std::vector<NodeKind> kinds{
      NodeKind::DP2_CROSS_BACKWARD, NodeKind::DP2_CROSS_FORWARD,
      NodeKind::DP2_DIAGONAL_BACKWARD, NodeKind::DP2_DIAGONAL_FORWARD,
      NodeKind::DP2_NEIGHBORHOOD_BACKWARD3,
      NodeKind::DP2_NEIGHBORHOOD_FORWARD3};
  for (const NodeKind kind : kinds) {
    for (const auto& result : result_values()) {
      if (!differential(dp2_fixture(kind, result.second), {}, {},
                        "DP2 kind " +
                            std::to_string(static_cast<int>(kind)) + " " +
                            result.first)) {
        return false;
      }
    }
  }
  const ProgramGenome inputs = dp2_input_fixture();
  const std::vector<InputSpec> types{{"i_input", RType::Int},
                                     {"j_input", RType::Int}};
  if (!differential(inputs, types,
                    {{0, Value::from_bool(false)}},
                    "DP2 second initial Name before first state type")) {
    return false;
  }
  if (!differential(dp2_second_expression_zero_div_fixture(),
                    {{"i_input", RType::Int}},
                    {{0, Value::from_bool(false)}},
                    "DP2 second initial ZeroDiv before first state type")) {
    return false;
  }
  if (!differential(inputs, types,
                    {{0, Value::from_bool(false)},
                     {1, Value::from_bool(true)}},
                    "DP2 first initial state type ordering")) {
    return false;
  }
  return differential(inputs, types,
                      {{0, Value::from_int(1)},
                       {1, Value::from_bool(true)}},
                      "DP2 second initial state type ordering");
}

}  // namespace

int main() {
  try {
    gagp::payload::clear();
    if (!test_dc_matrix_and_errors() ||
        !test_dp1_directions_arities_and_input_error() ||
        !test_dp2_patterns_and_result_types()) {
      return 1;
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
