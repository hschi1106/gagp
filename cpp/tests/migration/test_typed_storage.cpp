#include <iostream>
#include <stdexcept>

#include "gagp/migration/typed_storage.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"

using namespace gagp;
using namespace gagp::evo;
namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
AstProgram loop(bool read_after, bool read_before_write = false) {
  AstProgram ast;
  ast.names = {"x", "i", "seen"};
  ast.consts = {Value::from_int(2), Value::from_bool(true), Value::from_float(7)};
  ast.nodes = {{NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS},
      {NodeKind::FOR_RANGE, 1}, {NodeKind::CONST, 0}};
  if (read_before_write) {
    const std::vector<AstNode> read{{NodeKind::BLOCK_CONS}, {NodeKind::ASSIGN, 2},
                                  {NodeKind::VAR, 0}};
    ast.nodes.insert(ast.nodes.end(), read.begin(), read.end());
  }
  const std::vector<AstNode> tail{{NodeKind::BLOCK_CONS}, {NodeKind::ASSIGN, 0},
      {NodeKind::CONST, 1}, {NodeKind::BLOCK_NIL}, {NodeKind::BLOCK_CONS},
      {NodeKind::RETURN}, {read_after ? NodeKind::VAR : NodeKind::CONST, read_after ? 0 : 2},
      {NodeKind::BLOCK_NIL}};
  ast.nodes.insert(ast.nodes.end(), tail.begin(), tail.end());
  return ast;
}
const std::vector<InputSpec> schema{{"x", RType::Float}};
void rejects(const AstProgram& ast) {
  require(verify_ast(ast, schema).ok, "rejected proof fixture must be a valid native AST");
  try { (void)migration::normalize_typed_storage(ast, schema); }
  catch (const std::invalid_argument& error) {
    require(std::string(error.what()).find("cannot prove read") != std::string::npos,
            "storage proof lost rejection diagnostic");
    return;
  }
  throw std::runtime_error("unsafe storage split accepted");
}
void equivalent(const AstProgram& ast) {
  const auto original_key = ast_cache_key(ast);
  const auto normalized = migration::normalize_typed_storage(ast, schema);
  require(normalized.renamed_uses > 0, "fixture did not exercise splitting");
  require(ast_cache_key(ast) == original_key, "normalization mutated input");
  require(normalized.ast.nodes.size() == ast.nodes.size(), "normalization changed node budget");
  ProgramGenome old_genome, new_genome;
  old_genome.ast = ast; new_genome.ast = normalized.ast;
  const auto before = compile_for_eval(old_genome, verify_ast(ast, schema).verified);
  const auto after = compile_for_eval(new_genome, verify_ast(normalized.ast, schema).verified);
  for (int fuel = 0; fuel <= 150; ++fuel) {
    const auto bind = [](const BytecodeProgram& code) {
      std::vector<std::pair<int, Value>> values;
      const auto it = code.var2idx.find("x");
      if (it != code.var2idx.end()) values.emplace_back(it->second, Value::from_float(7));
      return values;
    };
    const auto a = execute_bytecode_cpu(before, bind(before), fuel);
    const auto b = execute_bytecode_cpu(after, bind(after), fuel);
    require(a.is_error == b.is_error, "normalization changed error/fuel boundary");
    if (a.is_error) require(a.err.code == b.err.code, "normalization changed error code");
    else require(a.value.tag == b.value.tag && a.value.i == b.value.i && a.value.b == b.value.b,
                 "normalization changed scalar result");
  }
  require(ast_cache_key(migration::normalize_typed_storage(ast, schema).ast) ==
          ast_cache_key(normalized.ast), "normalization is not deterministic");
}
void check_population_names() {
  AstProgram ast;
  ast.names = {"t0"};
  ast.consts = {Value::from_int(7)};
  ast.nodes = {{NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::ASSIGN, 0},
      {NodeKind::CONST, 0}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
      {NodeKind::VAR, 0}, {NodeKind::BLOCK_NIL}};
  const auto integer = migration::normalize_typed_storage(ast, {}, true);
  ast.names.insert(ast.names.begin(), "unused");
  ast.nodes[2].i0 = ast.nodes[6].i0 = 1;
  const auto reordered = migration::normalize_typed_storage(ast, {}, true);
  const auto name = [](const AstProgram& a) { return a.names.at(a.nodes[2].i0); };
  require(name(integer.ast) == name(reordered.ast), "canonical name depends on table order");
  ast.consts[0] = Value::from_float(7);
  const auto floating = migration::normalize_typed_storage(ast, {}, true);
  require(name(integer.ast) != name(floating.ast), "population types alias a storage name");
  require(floating.ast.nodes[2].i0 == floating.ast.nodes[6].i0,
          "assignment/read use different canonical storage");
  const auto checked = verify_ast(floating.ast, {});
  ProgramGenome genome; genome.ast = floating.ast;
  const auto result = execute_bytecode_cpu(compile_for_eval(genome, checked.verified), {}, 100);
  require(!result.is_error && result.value.tag == ValueTag::Float && result.value.f == 7,
          "canonical storage changed execution");
  ast.names.push_back(name(floating.ast));
  bool rejected = false;
  try { (void)migration::normalize_typed_storage(ast, {}, true); }
  catch (const std::invalid_argument&) { rejected = true; }
  require(rejected, "canonical source-name collision accepted");
}

void check_branch() {
  AstProgram ast;
  ast.names = {"x", "_typed_storage_0_2"}; // Exercise collision avoidance.
  ast.consts = {Value::from_bool(true), Value::from_bool(false)};
  ast.nodes = {{NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::IF_STMT},
      {NodeKind::CONST, 0}, {NodeKind::BLOCK_CONS}, {NodeKind::ASSIGN, 0},
      {NodeKind::CONST, 1}, {NodeKind::BLOCK_NIL}, {NodeKind::BLOCK_CONS},
      {NodeKind::ASSIGN, 0}, {NodeKind::CONST, 0}, {NodeKind::BLOCK_NIL},
      {NodeKind::BLOCK_CONS}, {NodeKind::RETURN}, {NodeKind::VAR, 0}, {NodeKind::BLOCK_NIL}};
  equivalent(ast);
  ast.consts[0] = Value::from_bool(false);
  equivalent(ast);
}
void check_short_circuit_and_return() {
  auto ast = loop(false, true);
  // The loop always returns after its first write, so there is no back edge
  // carrying Bool into the initial Float read.
  const std::vector<AstNode> stop{{NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
                                {NodeKind::CONST, 2}};
  ast.nodes.insert(ast.nodes.begin() + 10, stop.begin(), stop.end());
  equivalent(ast);

  ast = loop(false, true);
  ast.consts.push_back(Value::from_bool(false));
  const std::vector<AstNode> short_circuit{{NodeKind::AND}, {NodeKind::CONST, 3},
      {NodeKind::EQ}, {NodeKind::VAR, 0}, {NodeKind::CONST, 2}};
  ast.nodes.erase(ast.nodes.begin() + 6);
  ast.nodes.insert(ast.nodes.begin() + 6, short_circuit.begin(), short_circuit.end());
  equivalent(ast);
  ast.consts[3] = Value::from_bool(true);
  rejects(ast);
}
void check_capture() {
  AstProgram ast;
  ast.names = {"x"};
  ast.consts = {Value::from_int(0), Value::from_int(1), Value::from_int(3)};
  ast.nodes = {{NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::ASSIGN, 0},
      {NodeKind::CONST, 2}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
      {NodeKind::BOUNDED_REGION, 5}, {NodeKind::CONST, 2},
      {NodeKind::LE}, {NodeKind::REGION_VAR, 0}, {NodeKind::CONST, 0},
      {NodeKind::REGION_VAR, 2}, {NodeKind::ADD}, {NodeKind::REGION_VAR, 1},
      {NodeKind::CONST, 1}, {NodeKind::CONST, 0}, {NodeKind::BLOCK_NIL}};
  BoundedRegionSpec spec;
  spec.node_index = 6;
  auto& plan = spec.plan;
  plan.state_types = {ValueTag::Int}; plan.result_type = ValueTag::Int;
  plan.parameter_types = {ValueTag::Int}; spec.parameters = {{RegionCaptureKind::Name, 0}};
  plan.coordinate_slots = {0}; plan.coordinate_rank = {{0, 1}};
  plan.coordinate_domains = {{{RegionBoundKind::Literal, 0, 0}, {RegionBoundKind::Literal, 5, 0}}};
  RegionStateTransition edge; edge.kind = RegionTransitionKind::CoordinateOffset; edge.offset = -1;
  plan.requests = {{{edge}}}; plan.memoized = true; plan.limits = {8, 8, 1};
  spec.phases = {{1, {{{RegionSlotBank::State, 0}, 0}}},
      {2, {{{RegionSlotBank::Parameter, 0}, 2}}},
      {3, {{{RegionSlotBank::Result, 0}, 1}}}, {4, {}}};
  ast.bounded_region_specs.push_back(spec);
  ast.fuel_specs.push_back({3, {{FuelEvent::Operation, 4}}});
  equivalent(ast);
  // Captures are reads too, despite having no ordinary VAR node in the tape.
  ast.names.push_back("i");
  ast.consts.push_back(Value::from_bool(true));
  const std::vector<AstNode> overwrite{{NodeKind::BLOCK_CONS}, {NodeKind::FOR_RANGE, 1},
      {NodeKind::CONST, 1}, {NodeKind::BLOCK_CONS}, {NodeKind::ASSIGN, 0},
      {NodeKind::CONST, 3}, {NodeKind::BLOCK_NIL}};
  ast.nodes.insert(ast.nodes.begin() + 4, overwrite.begin(), overwrite.end());
  ast.bounded_region_specs.front().node_index += overwrite.size();
  rejects(ast);
}
}
int main() {
  try {
    rejects(loop(true));
    rejects(loop(false, true));
    equivalent(loop(false));
    check_population_names();
    check_branch();
    check_short_circuit_and_return();
    check_capture();
    std::cout << "typed storage: branch, loop fixed point, capture, and fuel checks passed\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
