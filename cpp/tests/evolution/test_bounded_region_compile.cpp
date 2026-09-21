#include <iostream>
#include <stdexcept>

#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/bounded_region.hpp"
#include "gagp/evolution/repro/pack.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "subtree_utils.hpp"

namespace {
using namespace gagp;
using namespace gagp::evo;

void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}

AstProgram counter() {
  AstProgram ast;
  ast.consts = {Value::from_int(0), Value::from_int(1), Value::from_int(3)};
  ast.nodes = {{NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
      {NodeKind::BOUNDED_REGION, 5}, {NodeKind::CONST, 2},
      {NodeKind::LE}, {NodeKind::REGION_VAR, 0}, {NodeKind::CONST, 0},
      {NodeKind::CONST, 1}, {NodeKind::ADD}, {NodeKind::REGION_VAR, 1},
      {NodeKind::CONST, 1}, {NodeKind::CONST, 0}, {NodeKind::BLOCK_NIL}};
  BoundedRegionSpec spec;
  spec.node_index = 3;
  auto& plan = spec.plan;
  plan.state_types = {ValueTag::Int};
  plan.result_type = ValueTag::Int;
  plan.coordinate_slots = {0};
  plan.coordinate_rank = {{0, 1}};
  plan.coordinate_domains = {{{RegionBoundKind::Literal, 0, 0}, {RegionBoundKind::Literal, 5, 0}}};
  RegionStateTransition edge;
  edge.kind = RegionTransitionKind::CoordinateOffset;
  edge.offset = -1;
  plan.requests = {{{edge}}};
  plan.memoized = true;
  plan.limits = {8, 8, 1};
  spec.phases = {{1, {{{RegionSlotBank::State, 0}, 0}}}, {2, {}},
                 {3, {{{RegionSlotBank::Result, 0}, 1}}}, {4, {}}};
  ast.bounded_region_specs.push_back(spec);
  return ast;
}

BytecodeProgram compile(const AstProgram& ast) {
  ProgramGenome genome;
  genome.ast = ast;
  return compile_for_eval(genome);
}

void check_counter_and_fuel() {
  const auto ast = counter();
  const auto checked = verify_ast(ast, {});
  require(checked.ok, "counter AST failed verification");
  ProgramGenome genome;
  genome.ast = ast;
  const auto bytecode = compile_for_eval(genome, checked.verified);
  require(bytecode.bounded_region_segments.size() == 1,
          "bounded region did not use the general evaluator segment");
  for (int fuel = 0; fuel < 29; ++fuel) {
    const auto result = execute_bytecode_cpu(bytecode, {}, fuel);
    require(result.is_error && result.err.code == ErrCode::Timeout, "counter fuel threshold moved");
  }
  const auto result = execute_bytecode_cpu(bytecode, {}, 29);
  require(!result.is_error && result.value.tag == ValueTag::Int && result.value.i == 4,
          "native counter execution failed");
  auto profiled = ast;
  profiled.fuel_specs.push_back({3, {{FuelEvent::Operation, 3}}});
  const auto charged = compile(profiled);
  require(execute_bytecode_cpu(charged, {}, 30).is_error, "root operation profile ignored");
  require(!execute_bytecode_cpu(charged, {}, 31).is_error, "root operation profile overcharged");
}

AstProgram captured_counter() {
  auto ast = counter();
  ast.names = {"unused", "cap"};
  auto& spec = ast.bounded_region_specs[0];
  spec.plan.parameter_types = {ValueTag::Int};
  spec.parameters = {{RegionCaptureKind::Name, 1}};
  spec.phases[1].bindings = {{{RegionSlotBank::Parameter, 0}, 2}};
  ast.nodes[8] = {NodeKind::REGION_VAR, 2};
  return ast;
}

void check_capture_and_compaction() {
  auto ast = captured_counter();
  const auto bytecode = compile(ast);
  const auto name = bytecode.var2idx.at("cap");
  const auto result = execute_bytecode_cpu(bytecode, {{name, Value::from_int(7)}}, 100);
  require(!result.is_error && result.value.i == 10, "named capture lowering failed");
  const auto unset = execute_bytecode_cpu(bytecode, {}, 100);
  require(unset.is_error && unset.err.code == ErrCode::Name, "unset capture was materialized");
  ast.consts[2] = Value::from_int(-1);
  const auto boundary = execute_bytecode_cpu(compile(ast), {}, 100);
  require(!boundary.is_error && boundary.value.i == 0, "unused capture read before boundary");
  ProgramGenome genome;
  genome.ast = captured_counter();
  const auto compact = repro::compact_genome_tables(genome);
  require(compact.ast.bounded_region_specs.size() == 1 && compact.ast.names.size() == 1 &&
      compact.ast.names[0] == "cap" && compact.ast.bounded_region_specs[0].parameters[0].index == 0,
      "compaction lost or failed to remap capture metadata");
  const auto compiled = compile(compact.ast);
  require(execute_bytecode_cpu(compiled, {{compiled.var2idx.at("cap"), Value::from_int(7)}}, 100).value.i == 10,
      "compaction changed native region execution");
}

void check_splice_ownership() {
  const auto donor = captured_counter();
  auto base = counter();
  const auto replaced = subtree::replace_subtree(base, 3, 13, donor, 3, 13);
  require(replaced.bounded_region_specs.size() == 1, "splice lost region metadata");
  require(replaced.bounded_region_specs[0].phases[0].bindings[0].binder_id != 0,
      "splice did not freshen phase binder against base");
  require(replaced.nodes[6].i0 == replaced.bounded_region_specs[0].phases[0].bindings[0].binder_id,
      "splice did not rewrite phase variable");
  const auto bytecode = compile(replaced);
  require(execute_bytecode_cpu(bytecode, {{bytecode.var2idx.at("cap"), Value::from_int(7)}}, 100).value.i == 10,
      "splice changed region result");
}
}  // namespace

int main() {
  try {
    check_counter_and_fuel();
    check_capture_and_compaction();
    check_splice_ownership();
    std::cout << "bounded region compile: OK\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
