#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/core/semantic_fuel.hpp"
#include "gagp/core/value.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/input_spec.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/cpu/fitness_cpu.hpp"
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "gagp/runtime/payload/payload.hpp"

namespace {

using Expr = std::vector<gagp::evo::AstNode>;
using gagp::BytecodeProgram;
using gagp::CaseBindings;
using gagp::ErrCode;
using gagp::InputBinding;
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

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << '\n';
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

Expr binary(NodeKind kind, const Expr& left, const Expr& right) {
  return expression(kind, {left, right});
}

Expr let_region(const Expr& initializer, const Expr& body) {
  return expression(NodeKind::LET_REGION, {initializer, body});
}

Expr traverse(const Expr& sequence, const Expr& start, const Expr& seed,
              const Expr& body) {
  return expression(NodeKind::TRAVERSE, {sequence, start, seed, body});
}

Expr traverse_range(const Expr& sequence, const Expr& start,
                    const Expr& begin, const Expr& end, const Expr& seed,
                    const Expr& body) {
  return expression(NodeKind::TRAVERSE_RANGE,
                    {sequence, start, begin, end, seed, body});
}

AstProgram return_program(const Expr& result, std::vector<Value> constants,
                          std::vector<std::string> names = {}) {
  AstProgram ast;
  ast.consts = std::move(constants);
  ast.names = std::move(names);
  ast.nodes = {{NodeKind::PROGRAM, 0, 0},
               {NodeKind::BLOCK_CONS, 0, 0},
               {NodeKind::RETURN, 0, 0}};
  append(&ast.nodes, result);
  ast.nodes.push_back({NodeKind::BLOCK_NIL, 0, 0});
  return ast;
}

std::vector<std::size_t> indices(const AstProgram& ast, NodeKind kind) {
  std::vector<std::size_t> out;
  for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
    if (ast.nodes[i].kind == kind) out.push_back(i);
  }
  return out;
}

void add_let(AstProgram* ast, std::size_t owner, int binder, RType type) {
  ast->lexical_regions.push_back(
      LexicalRegion{owner, 1, {LexicalBinding{binder, type}}});
}

void add_traversal(AstProgram* ast, std::size_t owner,
                   TraversalDirection direction, int element, RType element_type,
                   int index, int accumulator, RType accumulator_type,
                   int body_argument) {
  ast->lexical_regions.push_back(LexicalRegion{
      owner, body_argument,
      {{element, element_type}, {index, RType::Int},
       {accumulator, accumulator_type}}});
  ast->traversal_specs.push_back(TraversalSpec{owner, direction});
}

BytecodeProgram compile_checked(const AstProgram& ast,
                                const std::vector<InputSpec>& inputs = {}) {
  const auto verified = gagp::evo::verify_ast(ast, inputs);
  if (!verified.ok) {
    throw std::runtime_error(
        std::string("lexical GPU fixture failed AST verification: ") +
        gagp::evo::verify_code_name(verified.diagnostic.code) + " " +
        verified.diagnostic.message);
  }
  ProgramGenome genome;
  genome.ast = ast;
  genome.meta = gagp::evo::build_genome_meta(ast);
  std::vector<std::string> input_names;
  for (const auto& input : inputs) input_names.push_back(input.name);
  BytecodeProgram program = gagp::evo::compile_for_eval(
      genome, verified.verified, input_names);
  if (program.instruction_fuel.empty() ||
      program.instruction_fuel.size() != program.code.size()) {
    throw std::runtime_error(
        "lexical GPU fixture did not materialize its semantic fuel schedule");
  }
  const auto fuel_check =
      gagp::validate_semantic_fuel(program.code, program.instruction_fuel);
  if (!fuel_check) {
    throw std::runtime_error("lexical GPU fixture has an invalid fuel schedule: " +
                             fuel_check.message);
  }
  if (!program.bounded_region_segments.empty()) {
    throw std::runtime_error("lexical GPU fixture unexpectedly uses bounded regions");
  }
  return program;
}

int first_non_timeout(const BytecodeProgram& program,
                      const CaseBindings& bindings) {
  std::vector<std::pair<int, Value>> inputs;
  for (const auto& binding : bindings) {
    inputs.push_back({binding.idx, binding.value});
  }
  for (int fuel = 0; fuel <= 4096; ++fuel) {
    const auto result = gagp::execute_bytecode_cpu(program, inputs, fuel);
    if (!result.is_error || result.err.code != ErrCode::Timeout) return fuel;
  }
  return -1;
}

bool same_fitness(double left, double right) {
  return left == right || (left != left && right != right);
}

bool compare_fitness(const BytecodeProgram& program,
                     const std::vector<CaseBindings>& cases,
                     const std::vector<Value>& expected, int fuel,
                     int blocksize, const std::string& label,
                     bool expect_success = true) {
  // FitnessSessionGpu exposes aggregate fitness rather than raw VM values. The
  // fixtures use exact expected outputs so success remains distinguishable from
  // timeout penalties at this public API boundary.
  constexpr double penalty = 3.0;
  const std::vector<double> cpu = gagp::eval_fitness_cpu(
      {program}, cases, expected, fuel, penalty, blocksize);
  double expected_score = 0.0;
  for (const Value& answer : expected) {
    if (!expect_success) expected_score -= penalty;
    else if (answer.tag != ValueTag::Int && answer.tag != ValueTag::Float)
      expected_score += 1.0;
  }
  if (!check(cpu.size() == 1 && same_fitness(cpu[0], expected_score),
             label + ": CPU fixture did not reach its intended outcome")) {
    return false;
  }
  gagp::FitnessSessionGpu session;
  const auto initialized =
      session.init(cases, expected, fuel, blocksize, penalty);
  if (!check(initialized.ok,
             label + ": GPU initialization failed: " + initialized.err.message)) {
    return false;
  }
  const auto gpu = session.eval_programs({program});
  return check(gpu.ok, label + ": GPU evaluation failed: " + gpu.err.message) &&
         check(cpu.size() == 1 && gpu.fitness.size() == 1,
               label + ": fitness result shape changed") &&
         check(same_fitness(cpu[0], gpu.fitness[0]),
               label + ": CPU/GPU fitness mismatch (cpu=" +
                   std::to_string(cpu[0]) + ", gpu=" +
                   std::to_string(gpu.fitness[0]) + ")");
}

bool compare_exact_boundary(const BytecodeProgram& program,
                            const CaseBindings& bindings,
                            const Value& expected, int blocksize,
                            const std::string& label) {
  const int boundary = first_non_timeout(program, bindings);
  if (!check(boundary > 0, label + ": no positive finite fuel boundary")) {
    return false;
  }
  if (!compare_fitness(program, {bindings}, {expected}, boundary, blocksize,
                       label + " at exact fuel")) {
    return false;
  }
  return compare_fitness(program, {bindings}, {expected}, boundary - 1,
                         blocksize, label + " below exact fuel", false);
}

AstProgram nested_lexical_program() {
  // let outer = 5 in let inner = outer + x in outer + inner
  AstProgram ast = return_program(
      let_region(
          leaf(NodeKind::CONST, 0),
          let_region(binary(NodeKind::ADD, leaf(NodeKind::REGION_VAR, 10),
                            leaf(NodeKind::VAR, 0)),
                     binary(NodeKind::ADD, leaf(NodeKind::REGION_VAR, 10),
                            leaf(NodeKind::REGION_VAR, 20)))),
      {Value::from_int(5)}, {"x"});
  const auto lets = indices(ast, NodeKind::LET_REGION);
  if (lets.size() != 2) throw std::logic_error("nested Let fixture shape");
  add_let(&ast, lets[0], 10, RType::Int);
  add_let(&ast, lets[1], 20, RType::Int);
  return ast;
}

AstProgram nested_range_program(TraversalDirection direction) {
  constexpr int kElement = 101;
  constexpr int kIndex = 102;
  constexpr int kAccumulator = 103;
  constexpr int kCopy = 104;
  const Expr body = let_region(
      leaf(NodeKind::REGION_VAR, kElement),
      binary(NodeKind::ADD,
             binary(NodeKind::MUL,
                    leaf(NodeKind::REGION_VAR, kAccumulator),
                    leaf(NodeKind::CONST, 5)),
             leaf(NodeKind::REGION_VAR, kCopy)));
  AstProgram ast = return_program(
      traverse_range(
          leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
          leaf(NodeKind::CONST, 2), leaf(NodeKind::CONST, 3),
          leaf(NodeKind::CONST, 4), body),
      {gagp::payload::make_int_list_value(
           {Value::from_int(10), Value::from_int(20), Value::from_int(30),
            Value::from_int(40)}),
       Value::from_int(7), Value::from_int(1), Value::from_int(3),
       Value::from_int(0), Value::from_int(100)});
  const auto ranges = indices(ast, NodeKind::TRAVERSE_RANGE);
  const auto lets = indices(ast, NodeKind::LET_REGION);
  if (ranges.size() != 1 || lets.size() != 1) {
    throw std::logic_error("nested TraverseRange fixture shape");
  }
  add_traversal(&ast, ranges[0], direction, kElement, RType::Int, kIndex,
                kAccumulator, RType::Int, 5);
  add_let(&ast, lets[0], kCopy, RType::Int);
  return ast;
}

struct TraversalFixture {
  AstProgram ast;
  Value expected;
  std::string label;
};

TraversalFixture element_fixture(RType source_type,
                                 TraversalDirection direction) {
  constexpr int kElement = 201;
  constexpr int kIndex = 202;
  constexpr int kAccumulator = 203;
  Value source = Value::invalid();
  Value seed = Value::invalid();
  Value expected = Value::invalid();
  Expr body;
  RType element_type = RType::Invalid;
  RType result_type = RType::Invalid;
  std::string label;

  if (source_type == RType::String) {
    source = gagp::payload::make_string_value("az");
    seed = Value::from_char('?');
    expected = Value::from_char(direction == TraversalDirection::Forward ? 'z' : 'a');
    body = leaf(NodeKind::REGION_VAR, kElement);
    element_type = RType::Char;
    result_type = RType::Char;
    label = "String to Char";
  } else if (source_type == RType::IntList) {
    source = gagp::payload::make_int_list_value(
        {Value::from_int(1), Value::from_int(2)});
    seed = Value::from_bool(false);
    expected = Value::from_bool(direction == TraversalDirection::Forward);
    body = binary(NodeKind::GT, leaf(NodeKind::REGION_VAR, kElement),
                  leaf(NodeKind::CONST, 3));
    element_type = RType::Int;
    result_type = RType::Bool;
    label = "IntList to Bool";
  } else if (source_type == RType::FloatList) {
    source = gagp::payload::make_float_list_value(
        {Value::from_float(-1.5), Value::from_float(3.25)});
    seed = Value::from_float(0.0);
    expected = Value::from_float(
        direction == TraversalDirection::Forward ? 3.25 : -1.5);
    body = leaf(NodeKind::REGION_VAR, kElement);
    element_type = RType::Float;
    result_type = RType::Float;
    label = "FloatList to Float";
  } else if (source_type == RType::StringList) {
    const Value first = gagp::payload::make_string_value("first");
    const Value last = gagp::payload::make_string_value("last");
    source = gagp::payload::make_string_list_value({first, last});
    seed = gagp::payload::make_string_value("seed");
    expected = direction == TraversalDirection::Forward ? last : first;
    body = leaf(NodeKind::REGION_VAR, kElement);
    element_type = RType::String;
    result_type = RType::String;
    label = "StringList to String";
  } else {
    throw std::logic_error("unsupported traversal fixture source type");
  }

  AstProgram ast = return_program(
      traverse(leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
               leaf(NodeKind::CONST, 2), body),
      {source, Value::from_int(-9), seed, Value::from_int(1)});
  const auto owners = indices(ast, NodeKind::TRAVERSE);
  if (owners.size() != 1) throw std::logic_error("Traverse fixture shape");
  add_traversal(&ast, owners[0], direction, kElement, element_type, kIndex,
                kAccumulator, result_type, 3);
  return {std::move(ast), expected,
          label + (direction == TraversalDirection::Forward ? " forward"
                                                             : " reverse")};
}

Value empty_sequence(RType type) {
  if (type == RType::String) return gagp::payload::make_string_value("");
  if (type == RType::IntList) return gagp::payload::make_int_list_value({});
  if (type == RType::FloatList) return gagp::payload::make_float_list_value({});
  if (type == RType::StringList) return gagp::payload::make_string_list_value({});
  throw std::logic_error("unsupported empty sequence type");
}

RType element_type(RType sequence_type) {
  if (sequence_type == RType::String) return RType::Char;
  if (sequence_type == RType::IntList) return RType::Int;
  if (sequence_type == RType::FloatList) return RType::Float;
  if (sequence_type == RType::StringList) return RType::String;
  return RType::Invalid;
}

TraversalFixture empty_fixture(RType source_type, RType result_type,
                               Value seed, const std::string& label) {
  constexpr int kElement = 301;
  constexpr int kIndex = 302;
  constexpr int kAccumulator = 303;
  AstProgram ast = return_program(
      traverse(leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
               leaf(NodeKind::CONST, 2),
               leaf(NodeKind::REGION_VAR, kAccumulator)),
      {empty_sequence(source_type), Value::from_int(
           std::numeric_limits<std::int64_t>::max()), seed});
  const auto owners = indices(ast, NodeKind::TRAVERSE);
  if (owners.size() != 1) throw std::logic_error("empty Traverse fixture shape");
  add_traversal(&ast, owners[0], TraversalDirection::Reverse, kElement,
                element_type(source_type), kIndex, kAccumulator, result_type, 3);
  return {std::move(ast), seed, label};
}

bool test_nested_lexical_and_changed_inputs(int blocksize) {
  const BytecodeProgram program = compile_checked(
      nested_lexical_program(), {{"x", RType::Int}});
  const CaseBindings x2{{0, Value::from_int(2)}};
  if (!compare_exact_boundary(program, x2, Value::from_int(12), blocksize,
                              "nested lexical capture")) {
    return false;
  }
  return compare_fitness(
      program,
      {x2, {{0, Value::from_int(7)}}},
      {Value::from_int(12), Value::from_int(17)}, 4096, blocksize,
      "nested lexical changed inputs");
}

bool test_nested_range_directions(int blocksize) {
  const BytecodeProgram forward =
      compile_checked(nested_range_program(TraversalDirection::Forward));
  const BytecodeProgram reverse =
      compile_checked(nested_range_program(TraversalDirection::Reverse));
  return compare_exact_boundary(forward, {}, Value::from_int(2030), blocksize,
                                "nested forward bounded range") &&
         compare_exact_boundary(reverse, {}, Value::from_int(3020), blocksize,
                                "nested reverse bounded range");
}

bool test_sequence_and_result_matrix() {
  for (const RType source : {RType::String, RType::IntList, RType::FloatList,
                             RType::StringList}) {
    for (const auto direction : {TraversalDirection::Forward,
                                 TraversalDirection::Reverse}) {
      const TraversalFixture fixture = element_fixture(source, direction);
      if (!compare_fitness(compile_checked(fixture.ast), {{}},
                           {fixture.expected}, 4096, 128, fixture.label)) {
        return false;
      }
    }
  }

  const Value string_element = gagp::payload::make_string_value("one");
  const std::vector<TraversalFixture> empty_cases{
      empty_fixture(RType::String, RType::IntList,
                    gagp::payload::make_int_list_value(
                        {Value::from_int(-1), Value::from_int(4)}),
                    "empty String preserves IntList"),
      empty_fixture(RType::IntList, RType::FloatList,
                    gagp::payload::make_float_list_value(
                        {Value::from_float(-0.0), Value::from_float(2.5)}),
                    "empty IntList preserves FloatList"),
      empty_fixture(RType::FloatList, RType::StringList,
                    gagp::payload::make_string_list_value({string_element}),
                    "empty FloatList preserves StringList"),
      empty_fixture(RType::StringList, RType::String,
                    gagp::payload::make_string_value("kept"),
                    "empty StringList preserves String"),
  };
  for (const auto& fixture : empty_cases) {
    if (!compare_exact_boundary(compile_checked(fixture.ast), {},
                                fixture.expected, 128, fixture.label)) {
      return false;
    }
  }
  return true;
}

// More than 64 hidden declarations, but only one traversal is live at a time.
// The outer binding must remain live through all siblings and repeated execution.
bool test_sibling_traversals_reuse_temporary_storage() {
  constexpr int count = 12;
  std::vector<Expr> terms;
  for (int i = 0; i < count; ++i) {
    const int base = 1000 + 3 * i;
    terms.push_back(traverse(
        leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
        leaf(NodeKind::REGION_VAR, 900),
        binary(NodeKind::ADD, leaf(NodeKind::REGION_VAR, base),
               leaf(NodeKind::REGION_VAR, base + 2))));
  }
  // A balanced sum keeps operand stack pressure independent of this slot test.
  while (terms.size() > 1) {
    std::vector<Expr> next;
    for (std::size_t i = 0; i < terms.size(); i += 2) {
      next.push_back(i + 1 == terms.size() ? terms[i] :
          binary(NodeKind::ADD, terms[i], terms[i + 1]));
    }
    terms = std::move(next);
  }
  auto ast = return_program(let_region(leaf(NodeKind::VAR, 0),
      binary(NodeKind::ADD, terms[0], leaf(NodeKind::REGION_VAR, 900))),
      {gagp::payload::make_int_list_value({Value::from_int(1), Value::from_int(2)}),
       Value::from_int(0)}, {"x"});
  add_let(&ast, 3, 900, RType::Int);
  const auto owners = indices(ast, NodeKind::TRAVERSE);
  for (int i = 0; i < count; ++i) {
    const int base = 1000 + 3 * i;
    add_traversal(&ast, owners.at(i), TraversalDirection::Forward,
                  base, RType::Int, base + 1, base + 2, RType::Int, 3);
  }
  const auto program = compile_checked(ast, {{"x", RType::Int}});
  if (!check(program.n_locals <= 64, "disjoint traversal lifetimes must fit GPU locals"))
    return false;
  return compare_exact_boundary(program, {{0, Value::from_int(5)}},
                                Value::from_int(101), 256, "sibling traversals") &&
      compare_fitness(program, {{{0, Value::from_int(2)}}, {{0, Value::from_int(5)}}},
                      {Value::from_int(62), Value::from_int(101)}, 4096, 1024,
                      "sibling traversal changed inputs");
}

}  // namespace

int main() {
  try {
    gagp::payload::clear();
    if (!test_sibling_traversals_reuse_temporary_storage() ||
        !test_nested_lexical_and_changed_inputs(128) ||
        !test_nested_range_directions(128) ||
        !test_sequence_and_result_matrix()) {
      return 1;
    }
    // Exercise the canonical launch width on representative nested programs.
    if (!test_nested_lexical_and_changed_inputs(1024) ||
        !test_nested_range_directions(1024)) {
      return 1;
    }
    std::cout << "gagp_test_lexical_region_gpu: OK\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
