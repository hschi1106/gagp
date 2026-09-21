#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/core/errors.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/grammar/compiled.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/transition/bounded_regions.hpp"
#include "gagp/evolution/transition/linear_rec.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/payload/payload.hpp"

namespace {

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;
using Expr = std::vector<AstNode>;

const std::filesystem::path kRoot = GAGP_REPOSITORY_ROOT;

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

void append(Expr& target, const Expr& child) {
  target.insert(target.end(), child.begin(), child.end());
}

Expr leaf(NodeKind kind, int value = 0) { return {{kind, value, 0}}; }

Expr expression(NodeKind kind, std::initializer_list<Expr> children) {
  Expr result{{kind, 0, 0}};
  for (const auto& child : children) append(result, child);
  return result;
}

ProgramGenome program(Expr result, std::vector<Value> constants,
                      std::vector<std::string> names = {}) {
  ProgramGenome genome;
  genome.ast.names = std::move(names);
  genome.ast.consts = std::move(constants);
  genome.ast.nodes = {{NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS},
                      {NodeKind::RETURN}};
  append(genome.ast.nodes, result);
  genome.ast.nodes.push_back({NodeKind::BLOCK_NIL});
  genome.meta = build_genome_meta(genome.ast);
  return genome;
}

std::vector<std::string> input_names(const std::vector<InputSpec>& inputs) {
  std::vector<std::string> result;
  for (const auto& input : inputs) result.push_back(input.name);
  return result;
}

BytecodeProgram compile_checked(const ProgramGenome& genome,
                                const std::vector<InputSpec>& inputs = {}) {
  const auto verified = verify_ast(genome.ast, inputs);
  require(static_cast<bool>(verified), "differential fixture failed AST verification: " +
      verified.diagnostic.message);
  return compile_for_eval(genome, verified.verified, input_names(inputs));
}

bool same_value(const Value& left, const Value& right) {
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
      std::string a, b;
      return payload::lookup_string(left, &a) &&
             payload::lookup_string(right, &b) && a == b;
    }
    case ValueTag::IntList:
    case ValueTag::FloatList:
    case ValueTag::StringList: {
      std::vector<Value> a, b;
      if (!payload::lookup_list(left, &a) || !payload::lookup_list(right, &b) ||
          a.size() != b.size()) return false;
      for (std::size_t i = 0; i < a.size(); ++i)
        if (!same_value(a[i], b[i])) return false;
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
                       : same_value(left.value, right.value);
}

int first_non_timeout(const BytecodeProgram& program,
                      const std::vector<std::pair<int, Value>>& inputs) {
  for (int fuel = 0; fuel <= 20000; ++fuel) {
    const auto result = execute_bytecode_cpu(program, inputs, fuel);
    if (!result.is_error || result.err.code != ErrCode::Timeout) return fuel;
  }
  return -1;
}

void compare(const BytecodeProgram& specialized, const BytecodeProgram& packaged,
             const std::vector<std::pair<int, Value>>& old_inputs,
             const std::vector<std::pair<int, Value>>& new_inputs,
             const std::string& label, bool exact_boundary = true) {
  const int old_boundary = first_non_timeout(specialized, old_inputs);
  const int new_boundary = first_non_timeout(packaged, new_inputs);
  require(old_boundary >= 0 && new_boundary >= 0,
          label + ": no finite fuel boundary");
  if (exact_boundary)
    require(old_boundary == new_boundary,
            label + ": exact fuel boundary differs (" +
                std::to_string(old_boundary) + " vs " +
                std::to_string(new_boundary) + ")");
  const int cap = std::max(old_boundary, new_boundary) + 3;
  for (int fuel = 0; fuel <= cap; ++fuel) {
    const auto old_result = execute_bytecode_cpu(specialized, old_inputs, fuel);
    const auto new_result = execute_bytecode_cpu(packaged, new_inputs, fuel);
    require(same_result(old_result, new_result),
            label + ": result/error differs at fuel " + std::to_string(fuel));
  }
}

struct TypeCase {
  const char* name;
  RType type;
  Value zero;
  Value one;
  const char* zero_json;
  const char* one_json;
};

std::vector<TypeCase> type_cases() {
  return {
      {"Int", RType::Int, Value::from_int(0), Value::from_int(1), "\"0\"", "\"1\""},
      {"Float", RType::Float, Value::from_float(0.0), Value::from_float(1.0), "0", "1"},
      {"Bool", RType::Bool, Value::from_bool(false), Value::from_bool(true), "false", "true"},
      {"Char", RType::Char, Value::from_char('z'), Value::from_char('a'), "\"z\"", "\"a\""},
      {"String", RType::String, payload::make_string_value(""), payload::make_string_value("a"), "\"\"", "\"a\""},
      {"IntList", RType::IntList, payload::make_int_list_value({}), payload::make_int_list_value({Value::from_int(1)}), "[]", "[\"1\"]"},
      {"FloatList", RType::FloatList, payload::make_float_list_value({}), payload::make_float_list_value({Value::from_float(1.0)}), "[]", "[1]"},
      {"StringList", RType::StringList, payload::make_string_list_value({}), payload::make_string_list_value({payload::make_string_value("a")}), "[]", "[\"a\"]"},
  };
}

std::string constant_json(const TypeCase& type, bool one) {
  return "{\"constant\":{\"type\":\"" + std::string(type.name) +
      "\",\"values\":[" + (one ? type.one_json : type.zero_json) + "]}}";
}

class PackageRoot {
 public:
  PackageRoot(const std::string& package, const std::string& root) {
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    path_ = std::filesystem::temp_directory_path() /
        ("gagp-package-equivalence-" + unique);
    std::filesystem::create_directories(path_ / "packages");
    std::filesystem::copy_file(kRoot / "configs/grammar/packages" / package,
        path_ / "packages" / package);
    std::ofstream out(path_ / "root.json");
    out << root;
    require(static_cast<bool>(out), "failed to write temporary grammar root");
    out.close();
    grammar_ = std::make_unique<CompiledGrammar>(
        compile_grammar(load_definition(path_ / "root.json")));
  }

  ~PackageRoot() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const CompiledGrammar& grammar() const { return *grammar_; }

 private:
  std::filesystem::path path_;
  std::unique_ptr<CompiledGrammar> grammar_;
};

std::string root_prefix(const std::string& package, const std::string& entry,
                        const std::string& inputs = "[]") {
  return "{\"format_version\":\"grammar-definition-v1\",\"imports\":[\"packages/" +
      package + "\"],\"entry\":{\"nonterminal\":\"" + entry +
      "\",\"type\":\"";
}

ProgramGenome generated(const CompiledGrammar& grammar, std::uint64_t seed = 0) {
  return generate_derivation(grammar, seed).genome;
}

std::vector<std::size_t> child_roots(const AstProgram& ast,
                                     const VerifiedAst& verified,
                                     std::size_t owner) {
  std::vector<std::size_t> result;
  auto child = owner + 1;
  for (int i = 0; i < node_prefix_arity(ast.nodes.at(owner)); ++i) {
    result.push_back(child);
    child = verified.subtree_end.at(child);
  }
  return result;
}

std::size_t bounded_owner(const AstProgram& ast) {
  for (std::size_t i = 0; i < ast.nodes.size(); ++i)
    if (ast.nodes[i].kind == NodeKind::BOUNDED_REGION) return i;
  throw std::runtime_error("package program has no bounded owner");
}

ProgramGenome old_linear(const TypeCase& type) {
  auto root = expression(NodeKind::LINEAR_REC, {
      leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
      leaf(NodeKind::CONST, 2), leaf(NodeKind::BOUND_VAR, 1),
      leaf(NodeKind::CONST, 3)});
  auto result = program(std::move(root), {
      payload::make_int_list_value({Value::from_int(1), Value::from_int(2),
                                    Value::from_int(3)}),
      Value::from_int(0), type.zero, type.one}, {"element", "accumulator", "index"});
  result.ast.linear_rec_binders = {{3, 0, 1, 2}};
  result.meta = build_genome_meta(result.ast);
  return result;
}

std::string linear_root(const TypeCase& type) {
  const std::string expression = "{\"template\":\"Package.LinearRec.IntList." +
      std::string(type.name) + "\",\"holes\":{\"source\":{\"constant\":{\"type\":\"IntList\",\"values\":[[\"1\",\"2\",\"3\"]]}},\"start\":{\"constant\":{\"type\":\"Int\",\"values\":[\"0\"]}},\"empty\":" +
      constant_json(type, false) + ",\"step\":{\"bound\":\"accumulator\"},\"last\":" +
      constant_json(type, true) + "}}";
  return root_prefix("linear_rec_intlist_int.json", "Main") + type.name +
      "\"},\"search_limits\":{\"max_nodes\":96,\"max_depth\":32},\"execution_limits\":{\"fuel\":20000},\"nonterminals\":[{\"id\":\"Main\",\"type\":\"" +
      type.name + "\",\"scope\":[],\"alternatives\":[{\"id\":\"one\",\"weight\":1,\"expression\":" + expression + "}]}]}";
}

ProgramGenome old_dc(const TypeCase& type) {
  auto root = expression(NodeKind::ASGP_DC, {
      leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
      leaf(NodeKind::CONST, 2), leaf(NodeKind::BOUND_VAR, 4)});
  auto result = program(std::move(root), {
      payload::make_int_list_value({Value::from_int(1), Value::from_int(2),
                                    Value::from_int(3)}),
      type.one, Value::from_int(1)}, {"xs", "n", "lo", "divide_n", "left", "right"});
  result.ast.asgp_dc_binders = {{3, 0, 1, 2, 3, 4, 5}};
  result.meta = build_genome_meta(result.ast);
  return result;
}

std::string dc_root(const TypeCase& type) {
  const std::string expression = "{\"template\":\"Package.DC.IntList." +
      std::string(type.name) + "\",\"holes\":{\"source\":{\"constant\":{\"type\":\"IntList\",\"values\":[[\"1\",\"2\",\"3\"]]}},\"solve\":" +
      constant_json(type, true) + ",\"divide\":{\"constant\":{\"type\":\"Int\",\"values\":[\"1\"]}},\"combine\":{\"bound\":\"left\"}}}";
  return root_prefix("dc_intlist_int.json", "Main") + type.name +
      "\"},\"search_limits\":{\"max_nodes\":96,\"max_depth\":32},\"execution_limits\":{\"fuel\":20000},\"nonterminals\":[{\"id\":\"Main\",\"type\":\"" +
      type.name + "\",\"scope\":[],\"alternatives\":[{\"id\":\"one\",\"weight\":1,\"expression\":" + expression + "}]}]}";
}

ProgramGenome old_linear_input() {
  auto root = expression(NodeKind::LINEAR_REC, {
      leaf(NodeKind::VAR, 0), leaf(NodeKind::CONST, 0),
      leaf(NodeKind::CONST, 1), leaf(NodeKind::BOUND_VAR, 2),
      leaf(NodeKind::CONST, 2)});
  auto result = program(std::move(root),
      {Value::from_int(0), Value::from_int(0), Value::from_int(1)},
      {"source", "element", "accumulator", "index"});
  result.ast.linear_rec_binders = {{3, 1, 2, 3}};
  result.meta = build_genome_meta(result.ast);
  return result;
}

std::string linear_input_root() {
  const std::string expression =
      "{\"template\":\"Package.LinearRec.IntList.Int\",\"holes\":{"
      "\"source\":{\"input\":\"source\"},"
      "\"start\":{\"constant\":{\"type\":\"Int\",\"values\":[\"0\"]}},"
      "\"empty\":{\"constant\":{\"type\":\"Int\",\"values\":[\"0\"]}},"
      "\"step\":{\"bound\":\"accumulator\"},"
      "\"last\":{\"constant\":{\"type\":\"Int\",\"values\":[\"1\"]}}}}";
  return root_prefix("linear_rec_intlist_int.json", "Main") +
      "Int\"},\"inputs\":[{\"name\":\"source\",\"type\":\"IntList\"}],"
      "\"search_limits\":{\"max_nodes\":96,\"max_depth\":32},"
      "\"execution_limits\":{\"fuel\":20000},\"nonterminals\":[{"
      "\"id\":\"Main\",\"type\":\"Int\",\"scope\":[],\"alternatives\":[{"
      "\"id\":\"one\",\"weight\":1,\"expression\":" + expression + "}]}]}";
}

ProgramGenome old_dc_input() {
  auto root = expression(NodeKind::ASGP_DC, {
      leaf(NodeKind::VAR, 0), leaf(NodeKind::CONST, 0),
      leaf(NodeKind::CONST, 1), leaf(NodeKind::BOUND_VAR, 5)});
  auto result = program(std::move(root),
      {Value::from_int(1), Value::from_int(1)},
      {"source", "xs", "n", "lo", "divide_n", "left", "right"});
  result.ast.asgp_dc_binders = {{3, 1, 2, 3, 4, 5, 6}};
  result.meta = build_genome_meta(result.ast);
  return result;
}

std::string dc_input_root() {
  const std::string expression =
      "{\"template\":\"Package.DC.IntList.Int\",\"holes\":{"
      "\"source\":{\"input\":\"source\"},"
      "\"solve\":{\"constant\":{\"type\":\"Int\",\"values\":[\"1\"]}},"
      "\"divide\":{\"constant\":{\"type\":\"Int\",\"values\":[\"1\"]}},"
      "\"combine\":{\"bound\":\"left\"}}}";
  return root_prefix("dc_intlist_int.json", "Main") +
      "Int\"},\"inputs\":[{\"name\":\"source\",\"type\":\"IntList\"}],"
      "\"search_limits\":{\"max_nodes\":96,\"max_depth\":32},"
      "\"execution_limits\":{\"fuel\":20000},\"nonterminals\":[{"
      "\"id\":\"Main\",\"type\":\"Int\",\"scope\":[],\"alternatives\":[{"
      "\"id\":\"one\",\"weight\":1,\"expression\":" + expression + "}]}]}";
}

ProgramGenome find_dp(const CompiledGrammar& grammar, bool two_dimensional,
                      Value* solve, Value* boundary) {
  for (std::uint64_t seed = 0; seed < 10000; ++seed) {
    auto genome = generated(grammar, seed);
    const auto verified = verify_ast(genome.ast, two_dimensional ?
        std::vector<InputSpec>{{"row", RType::Int}, {"column", RType::Int}} :
        std::vector<InputSpec>{{"state", RType::Int}});
    require(static_cast<bool>(verified), "generated DP package failed verification");
    const auto owner = bounded_owner(genome.ast);
    const auto& spec = genome.ast.bounded_region_specs.front();
    const auto children = child_roots(genome.ast, verified.verified, owner);
    const bool wanted = two_dimensional ?
        (spec.plan.requests.size() == 1 && spec.plan.coordinate_rank[0].direction == 1) :
        (spec.plan.requests.size() == 1 && spec.plan.coordinate_rank[0].direction == 1);
    const std::size_t solve_child = two_dimensional ? 3 : 2;
    const std::size_t transition_child = two_dimensional ? 4 : 3;
    const std::size_t boundary_child = two_dimensional ? 5 : 4;
    if (!wanted || genome.ast.nodes[children[solve_child]].kind != NodeKind::CONST ||
        genome.ast.nodes[children[transition_child]].kind != NodeKind::REGION_VAR ||
        genome.ast.nodes[children[boundary_child]].kind != NodeKind::CONST) continue;
    *solve = genome.ast.consts.at(genome.ast.nodes[children[solve_child]].i0);
    *boundary = genome.ast.consts.at(genome.ast.nodes[children[boundary_child]].i0);
    return genome;
  }
  throw std::runtime_error("no deterministic seed materialized the requested DP shape");
}

ProgramGenome old_dp1(Value solve, Value boundary) {
  auto root = expression(NodeKind::ASGP_DP1D, {
      leaf(NodeKind::VAR, 0), leaf(NodeKind::CONST, 0),
      leaf(NodeKind::BOUND_VAR, 3)});
  auto result = program(std::move(root), {solve, boundary},
      {"state", "solve_state", "transition_state", "d1"});
  result.ast.asgp_dp1d_specs = {{3, 0, 5, 0, 1,
      NodeKind::DP1_BACKWARD1, {1}, 1, 2, {3}}};
  result.meta = build_genome_meta(result.ast);
  return result;
}

ProgramGenome old_dp2(Value solve, Value boundary) {
  auto root = expression(NodeKind::ASGP_DP2D, {
      leaf(NodeKind::VAR, 0), leaf(NodeKind::VAR, 1),
      leaf(NodeKind::CONST, 0), leaf(NodeKind::BOUND_VAR, 6)});
  auto result = program(std::move(root), {solve, boundary},
      {"row", "column", "solve_i", "solve_j", "transition_i",
       "transition_j", "d1"});
  result.ast.asgp_dp2d_specs = {{3, 0, 3, 0, 3, 0, 0, 1,
      NodeKind::DP2_DIAGONAL_BACKWARD, 2, 3, 4, 5, {6}}};
  result.meta = build_genome_meta(result.ast);
  return result;
}

std::string dp_root(const TypeCase& type, bool two_dimensional) {
  const std::string family = two_dimensional ? "DP2D" : "DP1D";
  const std::string package = two_dimensional ? "dp2d_int.json" : "dp1d_int.json";
  const std::string ins = two_dimensional ?
      "[{\"name\":\"row\",\"type\":\"Int\"},{\"name\":\"column\",\"type\":\"Int\"}]" :
      "[{\"name\":\"state\",\"type\":\"Int\"}]";
  return root_prefix(package, "Package." + family + "." + type.name) + type.name +
      "\"},\"inputs\":" + ins +
      ",\"search_limits\":{\"max_nodes\":96,\"max_depth\":32},\"execution_limits\":{\"fuel\":20000}}";
}

void test_value_and_fuel_matrix() {
  for (const auto& type : type_cases()) {
    {
      PackageRoot root("linear_rec_intlist_int.json", linear_root(type));
      const auto package = generated(root.grammar());
      compare(compile_checked(old_linear(type)), compile_checked(package), {}, {},
              "LinearRec " + std::string(type.name));
    }
    {
      PackageRoot root("dc_intlist_int.json", dc_root(type));
      const auto package = generated(root.grammar());
      compare(compile_checked(old_dc(type)), compile_checked(package), {}, {},
              "DC " + std::string(type.name));
    }
    {
      PackageRoot root("dp1d_int.json", dp_root(type, false));
      Value solve, boundary;
      const auto package = find_dp(root.grammar(), false, &solve, &boundary);
      const auto old = old_dp1(solve, boundary);
      const auto old_code = compile_checked(old, {{"state", RType::Int}});
      const auto new_code = compile_checked(package, {{"state", RType::Int}});
      compare(old_code, new_code, {{old_code.var2idx.at("state"), Value::from_int(3)}},
              {{new_code.var2idx.at("state"), Value::from_int(3)}},
              "DP1D " + std::string(type.name));
      compare(old_code, new_code, {{old_code.var2idx.at("state"), Value::from_bool(true)}},
              {{new_code.var2idx.at("state"), Value::from_bool(true)}},
              "DP1D type error " + std::string(type.name));
    }
    {
      PackageRoot root("dp2d_int.json", dp_root(type, true));
      Value solve, boundary;
      const auto package = find_dp(root.grammar(), true, &solve, &boundary);
      const auto old = old_dp2(solve, boundary);
      const std::vector<InputSpec> schema{{"row", RType::Int}, {"column", RType::Int}};
      const auto old_code = compile_checked(old, schema);
      const auto new_code = compile_checked(package, schema);
      compare(old_code, new_code,
          {{old_code.var2idx.at("row"), Value::from_int(2)},
           {old_code.var2idx.at("column"), Value::from_int(2)}},
          {{new_code.var2idx.at("row"), Value::from_int(2)},
           {new_code.var2idx.at("column"), Value::from_int(2)}},
          "DP2D " + std::string(type.name));
      compare(old_code, new_code,
          {{old_code.var2idx.at("row"), Value::from_int(2)},
           {old_code.var2idx.at("column"), Value::from_bool(true)}},
          {{new_code.var2idx.at("row"), Value::from_int(2)},
           {new_code.var2idx.at("column"), Value::from_bool(true)}},
          "DP2D type error " + std::string(type.name));
    }
  }
}

void test_sequence_type_errors() {
  const std::vector<InputSpec> schema{{"source", RType::IntList}};
  {
    PackageRoot root("linear_rec_intlist_int.json", linear_input_root());
    const auto package = generated(root.grammar());
    const auto old_code = compile_checked(old_linear_input(), schema);
    const auto new_code = compile_checked(package, schema);
    compare(old_code, new_code,
        {{old_code.var2idx.at("source"), Value::from_bool(true)}},
        {{new_code.var2idx.at("source"), Value::from_bool(true)}},
        "LinearRec source type error");
  }
  {
    PackageRoot root("dc_intlist_int.json", dc_input_root());
    const auto package = generated(root.grammar());
    const auto old_code = compile_checked(old_dc_input(), schema);
    const auto new_code = compile_checked(package, schema);
    compare(old_code, new_code,
        {{old_code.var2idx.at("source"), Value::from_bool(true)}},
        {{new_code.var2idx.at("source"), Value::from_bool(true)}},
        "DC source type error");
  }
}

}  // namespace

int main() {
  try {
    payload::clear();
    test_value_and_fuel_matrix();
    test_sequence_type_errors();
    std::cout << "grammar package CPU equivalence: values, errors, and fuel boundaries passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
