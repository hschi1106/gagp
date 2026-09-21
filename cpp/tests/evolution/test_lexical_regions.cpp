#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/cli/commands.hpp"
#include "gagp/cli/json.hpp"
#include "gagp/core/errors.hpp"
#include "gagp/core/value.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/input_spec.hpp"
#include "gagp/evolution/repro/pack.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/payload/payload.hpp"

namespace {

using Expr = std::vector<gagp::evo::AstNode>;
using gagp::ErrCode;
using gagp::ExecResult;
using gagp::Value;
using gagp::ValueTag;
using gagp::evo::AstNode;
using gagp::evo::AstProgram;
using gagp::evo::InputSpec;
using gagp::evo::LexicalBinding;
using gagp::evo::LexicalRegion;
using gagp::evo::NodeKind;
using gagp::evo::ProgramGenome;
using gagp::evo::RType;
using gagp::evo::TraversalDirection;
using gagp::evo::TraversalSpec;
using gagp::evo::VerifyCode;

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << "\n";
  return condition;
}

void append(Expr* target, const Expr& child) {
  target->insert(target->end(), child.begin(), child.end());
}

Expr leaf(NodeKind kind, int i0 = 0, int i1 = 0) {
  return {{kind, i0, i1}};
}

Expr expression(NodeKind kind, std::initializer_list<Expr> children) {
  Expr out{{kind, 0, 0}};
  for (const Expr& child : children) append(&out, child);
  return out;
}

Expr binary(NodeKind kind, const Expr& lhs, const Expr& rhs) {
  return expression(kind, {lhs, rhs});
}

Expr if_expr(const Expr& condition, const Expr& then_expr, const Expr& else_expr) {
  return expression(NodeKind::IF_EXPR, {condition, then_expr, else_expr});
}

Expr let_region(const Expr& initializer, const Expr& body) {
  return expression(NodeKind::LET_REGION, {initializer, body});
}

Expr traverse(const Expr& sequence, const Expr& start_index, const Expr& seed,
              const Expr& step) {
  return expression(NodeKind::TRAVERSE, {sequence, start_index, seed, step});
}

Expr traverse_range(const Expr& sequence, const Expr& start_index,
                    const Expr& begin, const Expr& end, const Expr& seed,
                    const Expr& step) {
  return expression(NodeKind::TRAVERSE_RANGE,
                    {sequence, start_index, begin, end, seed, step});
}

AstProgram return_program(const Expr& result, std::vector<Value> constants,
                          std::vector<std::string> names = {}) {
  AstProgram ast;
  ast.consts = std::move(constants);
  ast.names = std::move(names);
  ast.nodes = {
      {NodeKind::PROGRAM, 0, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::RETURN, 0, 0},
  };
  append(&ast.nodes, result);
  ast.nodes.push_back({NodeKind::BLOCK_NIL, 0, 0});
  return ast;
}

std::vector<std::size_t> node_indices(const AstProgram& ast, NodeKind kind) {
  std::vector<std::size_t> out;
  for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
    if (ast.nodes[i].kind == kind) out.push_back(i);
  }
  return out;
}

void add_let_metadata(AstProgram* ast, std::size_t node_index, int id,
                      RType type) {
  ast->lexical_regions.push_back(
      LexicalRegion{node_index, 1, {LexicalBinding{id, type}}});
}

void add_traversal_metadata(AstProgram* ast, std::size_t node_index,
                            TraversalDirection direction, int element_id,
                            RType element_type, int index_id, int accumulator_id,
                            RType accumulator_type, int body_argument) {
  ast->lexical_regions.push_back(LexicalRegion{
      node_index,
      body_argument,
      {{element_id, element_type}, {index_id, RType::Int},
       {accumulator_id, accumulator_type}},
  });
  ast->traversal_specs.push_back(TraversalSpec{node_index, direction});
}

ProgramGenome genome(const AstProgram& ast) {
  ProgramGenome out;
  out.ast = ast;
  out.meta = gagp::evo::build_genome_meta(ast);
  return out;
}

ExecResult run(const AstProgram& ast, const std::vector<InputSpec>& inputs = {},
               const std::vector<Value>& values = {}, int fuel = 50000) {
  const auto verified = gagp::evo::verify_ast(ast, inputs);
  if (!verified.ok) {
    return {true, Value::invalid(),
            {ErrCode::Value,
             std::string("test AST did not verify: ") +
                 gagp::evo::verify_code_name(verified.diagnostic.code) + " " +
                 verified.diagnostic.message}};
  }
  std::vector<std::string> input_names;
  input_names.reserve(inputs.size());
  std::vector<std::pair<int, Value>> runtime_inputs;
  runtime_inputs.reserve(values.size());
  for (std::size_t i = 0; i < inputs.size(); ++i) input_names.push_back(inputs[i].name);
  for (std::size_t i = 0; i < values.size(); ++i) {
    runtime_inputs.push_back({static_cast<int>(i), values[i]});
  }
  return gagp::execute_bytecode_cpu(
      gagp::evo::compile_for_eval(genome(ast), verified.verified, input_names),
      runtime_inputs, fuel);
}

bool is_int(const ExecResult& result, std::int64_t expected,
            const std::string& label) {
  if (result.is_error) {
    return check(false, label + ": runtime error " +
                            gagp::err_code_name(result.err.code) + " " +
                            result.err.message);
  }
  if (result.value.tag != ValueTag::Int) {
    return check(false, label + ": result was not Int");
  }
  return check(result.value.i == expected,
               label + ": expected " + std::to_string(expected) + " but got " +
                   std::to_string(result.value.i));
}

bool exact_value(const Value& actual, const Value& expected) {
  if (actual.tag != expected.tag) return false;
  switch (actual.tag) {
    case ValueTag::Int:
    case ValueTag::Char:
      return actual.i == expected.i;
    case ValueTag::Float:
      return actual.f == expected.f;
    case ValueTag::Bool:
      return actual.b == expected.b;
    case ValueTag::String: {
      std::string actual_string;
      std::string expected_string;
      return gagp::payload::lookup_string(actual, &actual_string) &&
             gagp::payload::lookup_string(expected, &expected_string) &&
             actual_string == expected_string;
    }
    case ValueTag::IntList:
    case ValueTag::FloatList:
    case ValueTag::StringList: {
      std::vector<Value> actual_elements;
      std::vector<Value> expected_elements;
      if (!gagp::payload::lookup_list(actual, &actual_elements) ||
          !gagp::payload::lookup_list(expected, &expected_elements) ||
          actual_elements.size() != expected_elements.size()) {
        return false;
      }
      for (std::size_t i = 0; i < actual_elements.size(); ++i) {
        if (!exact_value(actual_elements[i], expected_elements[i])) return false;
      }
      return true;
    }
    case ValueTag::FallbackToken:
      return actual.i == expected.i;
    case ValueTag::Invalid:
      return true;
  }
  return false;
}

bool expect_verify_code(const AstProgram& ast, VerifyCode expected,
                        const std::string& label) {
  const auto result = gagp::evo::verify_ast(ast, {});
  return check(!result.ok, label + " should fail verification") &&
         check(result.diagnostic.code == expected,
               label + " expected " + gagp::evo::verify_code_name(expected) +
                   " but got " +
                   gagp::evo::verify_code_name(result.diagnostic.code));
}

bool test_lexical_capture_and_namespaces() {
  // let outer = 5 in let inner = outer + x in outer + inner
  AstProgram ast = return_program(
      let_region(
          leaf(NodeKind::CONST, 0),
          let_region(binary(NodeKind::ADD, leaf(NodeKind::REGION_VAR, 10),
                            leaf(NodeKind::VAR, 0)),
                     binary(NodeKind::ADD, leaf(NodeKind::REGION_VAR, 10),
                            leaf(NodeKind::REGION_VAR, 20)))),
      {Value::from_int(5)}, {"x"});
  const auto lets = node_indices(ast, NodeKind::LET_REGION);
  if (!check(lets.size() == 2, "nested LetRegion fixture shape")) return false;
  add_let_metadata(&ast, lets[0], 10, RType::Int);
  add_let_metadata(&ast, lets[1], 20, RType::Int);

  const std::vector<InputSpec> inputs{{"x", RType::Int}};
  const auto verified = gagp::evo::verify_ast(ast, inputs);
  if (!check(verified.ok && verified.verified.return_type == RType::Int,
             "nested lexical capture and ordinary local should verify")) {
    return false;
  }
  return is_int(run(ast, inputs, {Value::from_int(2)}), 12,
                "nested regions should capture outer IDs independently of names");
}

bool test_lexical_region_rejections() {
  AstProgram own_initializer = return_program(
      let_region(leaf(NodeKind::REGION_VAR, 7), leaf(NodeKind::REGION_VAR, 7)),
      {});
  add_let_metadata(&own_initializer, 3, 7, RType::Int);
  if (!expect_verify_code(own_initializer, VerifyCode::UndefinedBinder,
                          "LetRegion initializer scope")) {
    return false;
  }

  AstProgram duplicate = return_program(
      if_expr(leaf(NodeKind::CONST, 0),
              let_region(leaf(NodeKind::CONST, 1), leaf(NodeKind::REGION_VAR, 8)),
              let_region(leaf(NodeKind::CONST, 2), leaf(NodeKind::REGION_VAR, 8))),
      {Value::from_bool(true), Value::from_int(1), Value::from_int(2)});
  const auto lets = node_indices(duplicate, NodeKind::LET_REGION);
  add_let_metadata(&duplicate, lets[0], 8, RType::Int);
  add_let_metadata(&duplicate, lets[1], 8, RType::Int);
  if (!expect_verify_code(duplicate, VerifyCode::DuplicateBinder,
                          "globally duplicated lexical ID")) {
    return false;
  }

  AstProgram mismatch = return_program(
      let_region(leaf(NodeKind::CONST, 0), leaf(NodeKind::REGION_VAR, 9)),
      {Value::from_int(3)});
  add_let_metadata(&mismatch, 3, 9, RType::Float);
  return expect_verify_code(mismatch, VerifyCode::TypeMismatch,
                            "LetRegion exact initializer type");
}

AstProgram string_traversal(TraversalDirection direction) {
  constexpr int kElement = 101;
  constexpr int kIndex = 102;
  constexpr int kAccumulator = 103;
  const Expr step = binary(
      NodeKind::ADD,
      binary(NodeKind::MUL, leaf(NodeKind::REGION_VAR, kAccumulator),
             leaf(NodeKind::CONST, 3)),
      expression(NodeKind::CALL_ORD, {leaf(NodeKind::REGION_VAR, kElement)}));
  AstProgram ast = return_program(
      traverse(leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
               leaf(NodeKind::CONST, 2), step),
      {gagp::payload::make_string_value("abc"), Value::from_int(0),
       Value::from_int(0), Value::from_int(100)});
  add_traversal_metadata(&ast, 3, direction, kElement, RType::Char, kIndex,
                         kAccumulator, RType::Int, 3);
  return ast;
}

bool test_string_traversal_directions_and_fuel() {
  AstProgram forward = string_traversal(TraversalDirection::Forward);
  if (!is_int(run(forward), 979899,
              "forward String traversal should expose Char elements in order")) {
    return false;
  }
  if (!is_int(run(string_traversal(TraversalDirection::Reverse)), 999897,
              "reverse String traversal should expose Char elements in reverse order")) {
    return false;
  }

  const auto verified = gagp::evo::verify_ast(forward, {});
  if (!check(verified.ok, "fuel schedule traversal fixture should verify")) return false;
  const auto bytecode = gagp::evo::compile_for_eval(genome(forward), verified.verified);
  return check(!bytecode.instruction_fuel.empty() &&
                   bytecode.instruction_fuel.size() == bytecode.code.size() &&
                   std::all_of(bytecode.instruction_fuel.begin(),
                               bytecode.instruction_fuel.end(),
                               [](std::uint32_t cost) { return cost == 1; }),
               "lexical lowering should emit a nonempty unit fuel schedule");
}

bool test_typed_empty_seed() {
  constexpr int kElement = 110;
  constexpr int kIndex = 111;
  constexpr int kAccumulator = 112;
  const Value empty = gagp::payload::make_int_list_value({});
  const Value seed = gagp::payload::make_string_value("kept");
  AstProgram ast = return_program(
      traverse(leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
               leaf(NodeKind::CONST, 2),
               leaf(NodeKind::REGION_VAR, kAccumulator)),
      {empty, Value::from_int(4), seed});
  add_traversal_metadata(&ast, 3, TraversalDirection::Forward, kElement,
                         RType::Int, kIndex, kAccumulator, RType::String, 3);
  const auto verified = gagp::evo::verify_ast(ast, {});
  if (!check(verified.ok && verified.verified.return_type == RType::String,
             "empty traversal should retain the seed's exact type")) {
    return false;
  }
  const ExecResult result = run(ast);
  std::string actual;
  return check(!result.is_error && result.value.tag == ValueTag::String &&
                   gagp::payload::lookup_string(result.value, &actual) &&
                   actual == "kept",
               "empty traversal should return its typed seed unchanged");
}

bool test_all_empty_accumulator_types() {
  struct SeedCase {
    RType type;
    Value value;
    const char* label;
  };
  const Value string_a = gagp::payload::make_string_value("a");
  const std::vector<SeedCase> cases{
      {RType::Int, Value::from_int(-17), "Int"},
      {RType::Float, Value::from_float(2.5), "Float"},
      {RType::Bool, Value::from_bool(true), "Bool"},
      {RType::Char, Value::from_char('Q'), "Char"},
      {RType::String, gagp::payload::make_string_value("seed"), "String"},
      {RType::IntList,
       gagp::payload::make_int_list_value(
           {Value::from_int(4), Value::from_int(5)}),
       "IntList"},
      {RType::FloatList,
       gagp::payload::make_float_list_value({Value::from_float(1.25)}),
       "FloatList"},
      {RType::StringList, gagp::payload::make_string_list_value({string_a}),
       "StringList"},
  };
  const Value empty = gagp::payload::make_int_list_value({});
  for (const SeedCase& item : cases) {
    AstProgram ast = return_program(
        traverse(leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
                 leaf(NodeKind::CONST, 2), leaf(NodeKind::REGION_VAR, 503)),
        {empty, Value::from_int(-4), item.value});
    add_traversal_metadata(&ast, 3, TraversalDirection::Forward, 501,
                           RType::Int, 502, 503, item.type, 3);
    const auto verified = gagp::evo::verify_ast(ast, {});
    if (!check(verified.ok && verified.verified.return_type == item.type,
               std::string("empty traversal should verify for ") + item.label)) {
      return false;
    }
    const ExecResult result = run(ast);
    if (!check(!result.is_error && exact_value(result.value, item.value),
               std::string("empty traversal should preserve owned ") +
                   item.label + " seed")) {
      return false;
    }
  }
  return true;
}

bool test_float_and_string_list_elements() {
  const Value floats = gagp::payload::make_float_list_value(
      {Value::from_float(1.25), Value::from_float(-3.5)});
  AstProgram float_ast = return_program(
      traverse(leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
               leaf(NodeKind::CONST, 2), leaf(NodeKind::REGION_VAR, 601)),
      {floats, Value::from_int(0), Value::from_float(0.0)});
  add_traversal_metadata(&float_ast, 3, TraversalDirection::Forward, 601,
                         RType::Float, 602, 603, RType::Float, 3);
  const ExecResult float_result = run(float_ast);
  if (!check(!float_result.is_error &&
                 exact_value(float_result.value, Value::from_float(-3.5)),
             "FloatList traversal should bind Float elements")) {
    return false;
  }

  const Value first = gagp::payload::make_string_value("first");
  const Value last = gagp::payload::make_string_value("last");
  const Value strings = gagp::payload::make_string_list_value({first, last});
  AstProgram string_ast = return_program(
      traverse(leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
               leaf(NodeKind::CONST, 2), leaf(NodeKind::REGION_VAR, 611)),
      {strings, Value::from_int(0), gagp::payload::make_string_value("")});
  add_traversal_metadata(&string_ast, 3, TraversalDirection::Forward, 611,
                         RType::String, 612, 613, RType::String, 3);
  const ExecResult string_result = run(string_ast);
  return check(!string_result.is_error && exact_value(string_result.value, last),
               "StringList traversal should bind owned String elements");
}

AstProgram int_range(TraversalDirection direction, std::int64_t begin,
                     std::int64_t end, const Expr& step,
                     std::vector<Value> extra_constants = {}) {
  constexpr int kElement = 201;
  constexpr int kIndex = 202;
  constexpr int kAccumulator = 203;
  std::vector<Value> constants{
      gagp::payload::make_int_list_value(
          {Value::from_int(10), Value::from_int(20), Value::from_int(30),
           Value::from_int(40)}),
      Value::from_int(0), Value::from_int(begin), Value::from_int(end),
      Value::from_int(0),
  };
  constants.insert(constants.end(), extra_constants.begin(), extra_constants.end());
  AstProgram ast = return_program(
      traverse_range(leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
                     leaf(NodeKind::CONST, 2), leaf(NodeKind::CONST, 3),
                     leaf(NodeKind::CONST, 4), step),
      std::move(constants));
  add_traversal_metadata(&ast, 3, direction, kElement, RType::Int, kIndex,
                         kAccumulator, RType::Int, 5);
  return ast;
}

bool test_range_clamping_direction_and_empty() {
  constexpr int kElement = 201;
  constexpr int kAccumulator = 203;
  const Expr sum = binary(NodeKind::ADD,
                          leaf(NodeKind::REGION_VAR, kAccumulator),
                          leaf(NodeKind::REGION_VAR, kElement));
  if (!is_int(run(int_range(TraversalDirection::Forward, -9, 99, sum)), 100,
              "TraverseRange should clamp begin and end independently")) {
    return false;
  }
  if (!is_int(run(int_range(TraversalDirection::Forward,
                            std::numeric_limits<std::int64_t>::min(),
                            std::numeric_limits<std::int64_t>::max(), sum)),
              100,
              "TraverseRange should clamp extreme Int bounds to the full range")) {
    return false;
  }

  const Expr decimal = binary(
      NodeKind::ADD,
      binary(NodeKind::MUL, leaf(NodeKind::REGION_VAR, kAccumulator),
             leaf(NodeKind::CONST, 5)),
      leaf(NodeKind::REGION_VAR, kElement));
  if (!is_int(run(int_range(TraversalDirection::Forward, 1, 3, decimal,
                            {Value::from_int(100)})),
              2030, "forward TraverseRange should visit begin through end-1")) {
    return false;
  }
  if (!is_int(run(int_range(TraversalDirection::Reverse, 1, 3, decimal,
                            {Value::from_int(100)})),
              3020, "reverse TraverseRange should visit end-1 through begin")) {
    return false;
  }

  const Expr body_error = binary(NodeKind::MOD, leaf(NodeKind::CONST, 5),
                                 leaf(NodeKind::CONST, 6));
  AstProgram empty = int_range(TraversalDirection::Forward, 3, 1, body_error,
                               {Value::from_int(9), Value::from_int(0)});
  empty.consts[4] = Value::from_int(77);
  return is_int(run(empty), 77,
                "end <= begin should return seed without evaluating the body");
}

bool test_wrapping_exposed_index() {
  constexpr int kIndex = 202;
  AstProgram ast = int_range(TraversalDirection::Forward, 1, 3,
                             leaf(NodeKind::REGION_VAR, kIndex));
  ast.consts[1] = Value::from_int(std::numeric_limits<std::int64_t>::max());
  if (!is_int(run(ast), std::numeric_limits<std::int64_t>::min() + 2,
              "exposed index should preserve frozen Int ADD behavior at INT64_MAX")) {
    return false;
  }
  ast.consts[1] = Value::from_int(9007199254740993LL);
  return is_int(run(ast), 9007199254740994LL,
                "exposed index should preserve frozen Int ADD rounding above 2^53");
}

bool test_lazy_seed_body_and_runtime_check_order() {
  constexpr int kElement = 301;
  constexpr int kIndex = 302;
  constexpr int kAccumulator = 303;
  const Value singleton = gagp::payload::make_int_list_value({Value::from_int(1)});
  const Expr zero_div = binary(NodeKind::MOD, leaf(NodeKind::CONST, 3),
                               leaf(NodeKind::CONST, 4));
  const Expr lazy_seed = if_expr(leaf(NodeKind::CONST, 5), zero_div,
                                 leaf(NodeKind::CONST, 2));
  const Expr lazy_body = if_expr(leaf(NodeKind::CONST, 5), zero_div,
                                 leaf(NodeKind::REGION_VAR, kAccumulator));
  AstProgram ast = return_program(
      traverse(leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1), lazy_seed,
               lazy_body),
      {singleton, Value::from_int(0), Value::from_int(7), Value::from_int(1),
       Value::from_int(0), Value::from_bool(false)});
  add_traversal_metadata(&ast, 3, TraversalDirection::Forward, kElement,
                         RType::Int, kIndex, kAccumulator, RType::Int, 3);
  if (!is_int(run(ast), 7,
              "lazy IfExpr branches in seed and body should not raise errors")) {
    return false;
  }

  // The start value is declared Int so verification succeeds, but the runtime
  // value is deliberately Bool. It must be rejected before the failing seed.
  AstProgram ordered = return_program(
      traverse(leaf(NodeKind::CONST, 0), leaf(NodeKind::VAR, 0), zero_div,
               leaf(NodeKind::REGION_VAR, kAccumulator)),
      {singleton, Value::from_int(0), Value::from_int(0), Value::from_int(1),
       Value::from_int(0)},
      {"start"});
  add_traversal_metadata(&ordered, 3, TraversalDirection::Forward, kElement,
                         RType::Int, kIndex, kAccumulator, RType::Int, 3);
  const ExecResult result = run(ordered, {{"start", RType::Int}},
                                {Value::from_bool(false)});
  return check(result.is_error && result.err.code == ErrCode::Type,
               "Traverse should type-check start before evaluating seed");
}

bool test_codec_round_trip_and_malformed_metadata() {
  AstProgram ast = string_traversal(TraversalDirection::Reverse);
  const std::string encoded = gagp::cli_detail::encode_ast_json(ast);
  const AstProgram decoded = gagp::cli_detail::decode_ast_json(
      gagp::cli_detail::JsonParser(encoded).parse());
  if (!check(decoded.lexical_regions.size() == 1 &&
                 decoded.lexical_regions[0].node_index == 3 &&
                 decoded.lexical_regions[0].body_argument == 3 &&
                 decoded.lexical_regions[0].bindings.size() == 3 &&
                 decoded.lexical_regions[0].bindings[0].type == RType::Char &&
                 decoded.traversal_specs.size() == 1 &&
                 decoded.traversal_specs[0].direction ==
                     TraversalDirection::Reverse &&
                 gagp::cli_detail::encode_ast_json(decoded) == encoded,
             "AST codec should canonically round-trip lexical metadata")) {
    return false;
  }

  auto malformed = gagp::cli_detail::JsonParser(encoded).parse();
  malformed.object_v.at("lexical_regions")
      .array_v.at(0)
      .object_v.at("bindings")
      .array_v.at(0)
      .object_v.at("type")
      .string_v = "Any";
  try {
    (void)gagp::cli_detail::decode_ast_json(malformed);
  } catch (const std::runtime_error&) {
    return true;
  }
  return check(false, "AST codec should reject malformed lexical binding types");
}

bool test_gpu_reproduction_pack_requires_compiled_preparation() {
  AstProgram ast = return_program(
      let_region(leaf(NodeKind::CONST, 0), leaf(NodeKind::REGION_VAR, 401)),
      {Value::from_int(6)});
  add_let_metadata(&ast, 3, 401, RType::Int);
  const auto verified = gagp::evo::verify_ast(ast, {});
  if (!check(verified.ok, "GPU reproduction rejection fixture should verify")) {
    return false;
  }
  try {
    (void)gagp::evo::repro::pack_population(
        {genome(ast)}, gagp::evo::repro::PreprocessOutput{},
        gagp::evo::repro::GpuReproConfig{});
  } catch (const std::invalid_argument& err) {
    return check(std::string(err.what()).find("complete preparation metadata") !=
                     std::string::npos,
                 "GPU reproduction should report its compiled-preparation boundary");
  }
  return check(false, "GPU reproduction should reject missing compiled preparation");
}

}  // namespace

int main() {
  gagp::payload::clear();
  if (!test_lexical_capture_and_namespaces()) return 1;
  if (!test_lexical_region_rejections()) return 1;
  if (!test_string_traversal_directions_and_fuel()) return 1;
  if (!test_typed_empty_seed()) return 1;
  if (!test_all_empty_accumulator_types()) return 1;
  if (!test_float_and_string_list_elements()) return 1;
  if (!test_range_clamping_direction_and_empty()) return 1;
  if (!test_wrapping_exposed_index()) return 1;
  if (!test_lazy_seed_body_and_runtime_check_order()) return 1;
  if (!test_codec_round_trip_and_malformed_metadata()) return 1;
  if (!test_gpu_reproduction_pack_requires_compiled_preparation()) return 1;
  std::cout << "gagp_test_lexical_regions: OK\n";
  return 0;
}
