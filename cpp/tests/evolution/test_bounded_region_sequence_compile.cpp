#include "../../src/runtime/cpu/list_views.hpp"
#include "gagp/runtime/cpu/execution_session.hpp"
#include <cstdlib>
#include <limits>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/bounded_region.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/payload/payload.hpp"

namespace {

using namespace gagp;
using namespace gagp::evo;

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

ValueTag tag_of(const Value& value) { return value.tag; }

bool exact_value(const Value& left, const Value& right) {
  if (left.tag != right.tag) return false;
  switch (left.tag) {
    case ValueTag::Int:
    case ValueTag::Char:
    case ValueTag::FallbackToken: return left.i == right.i;
    case ValueTag::Float: return left.f == right.f;
    case ValueTag::Bool: return left.b == right.b;
    case ValueTag::String: {
      std::string a;
      std::string b;
      return payload::lookup_string(left, &a) && payload::lookup_string(right, &b) && a == b;
    }
    case ValueTag::IntList:
    case ValueTag::FloatList:
    case ValueTag::StringList: {
      std::vector<Value> a;
      std::vector<Value> b;
      if (!payload::lookup_list(left, &a) || !payload::lookup_list(right, &b) ||
          a.size() != b.size()) return false;
      for (std::size_t i = 0; i < a.size(); ++i)
        if (!exact_value(a[i], b[i])) return false;
      return true;
    }
    case ValueTag::Invalid: return true;
  }
  return false;
}

WindowEndpoint endpoint(WindowEndpointKind kind, std::uint32_t cut = 0) {
  return WindowEndpoint{kind, cut};
}

RegionStateTransition window(WindowEndpoint begin, WindowEndpoint end) {
  RegionStateTransition result;
  result.kind = RegionTransitionKind::SequenceWindow;
  result.source_state = 0;
  result.window = {begin, end};
  return result;
}

RegionPlan three_way_plan(ValueTag source, ValueTag result = ValueTag::Invalid,
                          bool parameter = false) {
  RegionPlan plan;
  plan.state_types = {source};
  plan.result_type = result == ValueTag::Invalid ? source : result;
  if (parameter) plan.parameter_types = {ValueTag::Int};
  plan.preparations = {
      {ValueTag::Int, RegionPreparationKind::InteriorCut},
      {ValueTag::Int, RegionPreparationKind::InteriorCut}};
  const auto begin = endpoint(WindowEndpointKind::Begin);
  const auto cut0 = endpoint(WindowEndpointKind::InteriorCut, 0);
  const auto cut1 = endpoint(WindowEndpointKind::InteriorCut, 1);
  const auto end = endpoint(WindowEndpointKind::End);
  plan.requests = {{{window(begin, cut0)}},
                   {{window(cut0, cut1)}},
                   {{window(cut1, end)}}};
  plan.limits = {32, 0, 1};
  plan.progress = RegionProgressKind::SequenceWindows;
  plan.sequence_state = 0;
  return plan;
}

RegionAstBinding binding(RegionSlotBank bank, std::uint32_t slot, int id) {
  return {{bank, slot}, id};
}

std::vector<RegionAstPhase> identity_phases() {
  return {
      {1, {}},
      {2, {binding(RegionSlotBank::State, 0, 100)}},
      {3, {binding(RegionSlotBank::Measure, 0, 101)}},
      {4, {binding(RegionSlotBank::Measure, 0, 102)}},
      {5, {binding(RegionSlotBank::Result, 0, 103),
           binding(RegionSlotBank::Result, 1, 104),
           binding(RegionSlotBank::Result, 2, 105)}},
  };
}

AstProgram identity_program(Value source) {
  AstProgram ast;
  ast.consts = {source, Value::from_bool(false), Value::from_int(3),
                Value::from_int(2)};
  const RegionPlan plan = three_way_plan(tag_of(source));
  ast.nodes = {
      {NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
      {NodeKind::BOUNDED_REGION, static_cast<int>(bounded_region_arity(plan))},
      {NodeKind::CONST, 0},
      {NodeKind::CONST, 1},
      {NodeKind::REGION_VAR, 100},
      {NodeKind::CALL_IDIV0}, {NodeKind::REGION_VAR, 101}, {NodeKind::CONST, 2},
      {NodeKind::CALL_IDIV0}, {NodeKind::MUL}, {NodeKind::REGION_VAR, 102},
      {NodeKind::CONST, 3}, {NodeKind::CONST, 2},
      {NodeKind::CALL_CONCAT}, {NodeKind::CALL_CONCAT},
      {NodeKind::REGION_VAR, 103}, {NodeKind::REGION_VAR, 104},
      {NodeKind::REGION_VAR, 105},
      {NodeKind::BLOCK_NIL},
  };
  ast.bounded_region_specs = {{3, plan, {}, identity_phases()}};
  return ast;
}

AstProgram count_program(Value source) {
  AstProgram ast;
  ast.consts = {source, Value::from_bool(false), Value::from_int(3),
                Value::from_int(2)};
  const RegionPlan plan = three_way_plan(tag_of(source), ValueTag::Int);
  ast.nodes = {
      {NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
      {NodeKind::BOUNDED_REGION, static_cast<int>(bounded_region_arity(plan))},
      {NodeKind::CONST, 0},
      {NodeKind::CONST, 1},
      {NodeKind::CALL_LEN}, {NodeKind::REGION_VAR, 100},
      {NodeKind::CALL_IDIV0}, {NodeKind::REGION_VAR, 101}, {NodeKind::CONST, 2},
      {NodeKind::CALL_IDIV0}, {NodeKind::MUL}, {NodeKind::REGION_VAR, 102},
      {NodeKind::CONST, 3}, {NodeKind::CONST, 2},
      {NodeKind::ADD}, {NodeKind::ADD}, {NodeKind::REGION_VAR, 103},
      {NodeKind::REGION_VAR, 104}, {NodeKind::REGION_VAR, 105},
      {NodeKind::BLOCK_NIL},
  };
  ast.bounded_region_specs = {{3, plan, {}, identity_phases()}};
  return ast;
}

BytecodeProgram compile_checked(const AstProgram& ast) {
  const AstVerifyResult verified = verify_ast(ast, {});
  require(verified.ok, "native sequence region failed type verification: " +
                           verified.diagnostic.message);
  ProgramGenome genome;
  genome.ast = ast;
  return compile_for_eval(genome, verified.verified);
}

ExecResult run(const AstProgram& ast, int fuel = 1000,
               const std::vector<std::pair<int, Value>>& inputs = {}) {
  return execute_bytecode_cpu(compile_checked(ast), inputs, fuel);
}

Value string_value(std::size_t length) {
  const char data[] = {'a', '\0', static_cast<char>(0xff), 'z'};
  return payload::make_string_value(std::string(data, length));
}

Value int_list(std::size_t length) {
  const std::vector<Value> all{Value::from_int(-1), Value::from_int(0),
                               Value::from_int(4), Value::from_int(9)};
  return payload::make_int_list_value(
      std::vector<Value>(all.begin(), all.begin() + length));
}

Value float_list(std::size_t length) {
  const std::vector<Value> all{Value::from_float(-0.0), Value::from_float(1.5),
                               Value::from_float(2.25), Value::from_float(-4.0)};
  return payload::make_float_list_value(
      std::vector<Value>(all.begin(), all.begin() + length));
}

Value string_list(std::size_t length) {
  const std::vector<Value> all{
      payload::make_string_value(""), payload::make_string_value("a"),
      payload::make_string_value(std::string(1, static_cast<char>(0xff))),
      payload::make_string_value(std::string("b\0", 2))};
  return payload::make_string_list_value(
      std::vector<Value>(all.begin(), all.begin() + length));
}

void check_identity_and_fuel() {
  for (const Value& source : {string_value(4), int_list(4), float_list(4),
                              string_list(4)}) {
    const ExecResult result = run(identity_program(source));
    require(!result.is_error && exact_value(result.value, source),
            "native three-way sequence region changed its typed payload");
  }

  for (const Value& source : {int_list(0), int_list(1)}) {
    const AstProgram ast = identity_program(source);
    const ExecResult short_timeout = run(ast, 4);
    require(short_timeout.is_error && short_timeout.err.code == ErrCode::Timeout,
            "length<=1 base path used too little fuel");
    const ExecResult exact = run(ast, 5);
    require(!exact.is_error && exact_value(exact.value, source),
            "length<=1 base path failed at exact fuel five");
  }

  const AstProgram four = identity_program(int_list(4));
  const ExecResult long_timeout = run(four, 42);
  require(long_timeout.is_error && long_timeout.err.code == ErrCode::Timeout,
          "four-element three-way traversal used too little fuel");
  require(!run(four, 43).is_error,
          "four-element three-way traversal failed at exact fuel 43");

  const ExecResult count = run(count_program(string_value(4)));
  require(!count.is_error && count.value.tag == ValueTag::Int && count.value.i == 4,
          "sequence state to Int result lowering failed");
}

AstProgram lexical_capture_program(Value source) {
  AstProgram ast;
  ast.consts = {Value::from_int(7), source, Value::from_int(-1),
                Value::from_int(3), Value::from_int(2)};
  const RegionPlan plan = three_way_plan(tag_of(source), tag_of(source), true);
  ast.nodes = {
      {NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
      {NodeKind::LET_REGION}, {NodeKind::CONST, 0},
      {NodeKind::BOUNDED_REGION, static_cast<int>(bounded_region_arity(plan))},
      {NodeKind::LET_REGION}, {NodeKind::CONST, 1}, {NodeKind::REGION_VAR, 70},
      {NodeKind::LET_REGION}, {NodeKind::REGION_VAR, 60},
      {NodeKind::EQ}, {NodeKind::REGION_VAR, 62}, {NodeKind::CONST, 2},
      {NodeKind::REGION_VAR, 61},
      {NodeKind::CALL_IDIV0}, {NodeKind::REGION_VAR, 101}, {NodeKind::CONST, 3},
      {NodeKind::CALL_IDIV0}, {NodeKind::MUL}, {NodeKind::REGION_VAR, 102},
      {NodeKind::CONST, 4}, {NodeKind::CONST, 3},
      {NodeKind::CALL_CONCAT}, {NodeKind::CALL_CONCAT},
      {NodeKind::REGION_VAR, 103}, {NodeKind::REGION_VAR, 104},
      {NodeKind::REGION_VAR, 105}, {NodeKind::BLOCK_NIL},
  };
  ast.lexical_regions = {
      {3, 1, {{50, RType::Int}}},
      {6, 1, {{70, tag_of(source) == ValueTag::String ? RType::String :
                       tag_of(source) == ValueTag::IntList ? RType::IntList :
                       tag_of(source) == ValueTag::FloatList ? RType::FloatList :
                                                              RType::StringList}}},
      {9, 1, {{62, RType::Int}}},
  };
  ast.bounded_region_specs = {{
      5, plan, {{RegionCaptureKind::Lexical, 50}},
      {{1, {binding(RegionSlotBank::Parameter, 0, 60)}},
       {2, {binding(RegionSlotBank::State, 0, 61)}},
       {3, {binding(RegionSlotBank::Measure, 0, 101)}},
       {4, {binding(RegionSlotBank::Measure, 0, 102)}},
       {5, {binding(RegionSlotBank::Result, 0, 103),
            binding(RegionSlotBank::Result, 1, 104),
            binding(RegionSlotBank::Result, 2, 105)}}}}};
  return ast;
}

void check_lexical_ownership() {
  const Value source = string_list(4);
  const ExecResult result = run(lexical_capture_program(source));
  require(!result.is_error && exact_value(result.value, source),
          "outer lexical capture or nested source/phase Let lost ownership");
}

AstProgram lazy_named_capture_program(Value source) {
  AstProgram ast;
  ast.names = {"cap"};
  ast.consts = {source, Value::from_int(0), Value::from_int(3),
                Value::from_int(2)};
  const RegionPlan plan = three_way_plan(tag_of(source), tag_of(source), true);
  ast.nodes = {
      {NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
      {NodeKind::BOUNDED_REGION, static_cast<int>(bounded_region_arity(plan))},
      {NodeKind::CONST, 0},
      {NodeKind::EQ}, {NodeKind::REGION_VAR, 200}, {NodeKind::CONST, 1},
      {NodeKind::REGION_VAR, 201},
      {NodeKind::CALL_IDIV0}, {NodeKind::REGION_VAR, 202}, {NodeKind::CONST, 2},
      {NodeKind::CALL_IDIV0}, {NodeKind::MUL}, {NodeKind::REGION_VAR, 203},
      {NodeKind::CONST, 3}, {NodeKind::CONST, 2},
      {NodeKind::CALL_CONCAT}, {NodeKind::CALL_CONCAT},
      {NodeKind::REGION_VAR, 204}, {NodeKind::REGION_VAR, 205},
      {NodeKind::REGION_VAR, 206}, {NodeKind::BLOCK_NIL},
  };
  ast.bounded_region_specs = {{
      3, plan, {{RegionCaptureKind::Name, 0}},
      {{1, {binding(RegionSlotBank::Parameter, 0, 200)}},
       {2, {binding(RegionSlotBank::State, 0, 201)}},
       {3, {binding(RegionSlotBank::Measure, 0, 202)}},
       {4, {binding(RegionSlotBank::Measure, 0, 203)}},
       {5, {binding(RegionSlotBank::Result, 0, 204),
            binding(RegionSlotBank::Result, 1, 205),
            binding(RegionSlotBank::Result, 2, 206)}}}}};
  return ast;
}

void check_lazy_capture_and_implicit_var_guard() {
  const Value singleton = int_list(1);
  const BytecodeProgram short_program = compile_checked(lazy_named_capture_program(singleton));
  const int local = short_program.var2idx.at("cap");
  const ExecResult short_wrong = execute_bytecode_cpu(
      short_program, {{local, Value::from_bool(true)}}, 100);
  require(!short_wrong.is_error && exact_value(short_wrong.value, singleton),
          "length<=1 base eagerly read a named predicate capture");

  const Value many = int_list(4);
  const BytecodeProgram long_program = compile_checked(lazy_named_capture_program(many));
  const ExecResult unset = execute_bytecode_cpu(long_program, {}, 1000);
  require(unset.is_error && unset.err.code == ErrCode::Name,
          "used named capture did not preserve unset state");
  const ExecResult wrong = execute_bytecode_cpu(
      long_program, {{long_program.var2idx.at("cap"), Value::from_bool(true)}}, 1000);
  require(wrong.is_error && wrong.err.code == ErrCode::Type,
          "used named capture did not enforce its exact declared type");
  const ExecResult present = execute_bytecode_cpu(
      long_program, {{long_program.var2idx.at("cap"), Value::from_int(1)}}, 1000);
  require(!present.is_error && exact_value(present.value, many),
          "used named capture did not flow through its explicit parameter binder");

  AstProgram malicious = lazy_named_capture_program(many);
  malicious.nodes[5] = {NodeKind::VAR, 0};
  malicious.nodes.erase(malicious.nodes.begin() + 6,
                        malicious.nodes.begin() + 8);
  ProgramGenome genome;
  genome.ast = std::move(malicious);
  bool rejected = false;
  try {
    (void)compile_for_eval(genome);
  } catch (const std::invalid_argument& error) {
    rejected = std::string(error.what()).find("implicit variable capture") !=
               std::string::npos;
  }
  require(rejected,
          "structure-only compilation accepted an implicit phase VAR capture");
}

void check_cpu_region_views() {
  struct Restore {
    bool present = std::getenv("GAGP_CPU_REGION_VIEWS") != nullptr;
    std::string value = present ? std::getenv("GAGP_CPU_REGION_VIEWS") : "";
    ~Restore() { if (present) setenv("GAGP_CPU_REGION_VIEWS",value.c_str(),1); else unsetenv("GAGP_CPU_REGION_VIEWS"); }
  } restore;
  for (std::size_t length=0; length<=4; ++length) {
    for (int variant=0; variant<3; ++variant) {
      auto program = compile_checked(count_program(int_list(length)));
      auto& segment = program.bounded_region_segments[0];
      if (variant) {
        auto& body = segment.base_body.program;
        body.consts = {Value::from_int(std::numeric_limits<std::int64_t>::min()),
                       Value::from_int(std::numeric_limits<std::int64_t>::max()), Value::from_int(-1)};
        body.code = {{Opcode::Load,0,0,true,false}};
        if (variant==2) {
          body.code.push_back({Opcode::PushConst,0,0,true,false});
          body.code.push_back({Opcode::PushConst,1,0,true,false});
          body.code.push_back({Opcode::CallBuiltin,static_cast<int>(BuiltinId::Slice),3,true,true});
        }
        body.code.push_back({Opcode::PushConst,2,0,true,false});
        body.code.push_back({Opcode::CallBuiltin,static_cast<int>(BuiltinId::Index),2,true,true});
        body.code.push_back({Opcode::Return,0,0,false,false});
        body.instruction_fuel.assign(body.code.size(),1);
      }
      require(detail::cpu_region_views_supported(segment), "scalar view fixture failed capability proof");
      for (unsigned frames : {0u,1u,32u}) {
        segment.plan.limits.frames = frames;
        unsetenv("GAGP_CPU_REGION_VIEWS"); CpuExecutionSession reference(program);
        std::vector<ExecResult> expected;
        for (int fuel=0; fuel<=100; ++fuel) expected.push_back(reference.execute({},fuel));
        setenv("GAGP_CPU_REGION_VIEWS","1",1); CpuExecutionSession candidate(program);
        for (int fuel=0; fuel<=100; ++fuel) {
          const auto actual = candidate.execute({},fuel); const auto& old = expected[fuel];
          require(actual.is_error == old.is_error && (actual.is_error ? actual.err.code == old.err.code : exact_value(actual.value,old.value)),
              "CPU list view changed scalar output/error/fuel/frame boundary");
        }
      }
    }
  }
  auto terminal = compile_checked(count_program(int_list(4)));
  auto& predicate = terminal.bounded_region_segments[0].base_predicate.program;
  predicate.consts[predicate.code[0].a] = Value::from_bool(true);
  require(!detail::cpu_region_views_supported(terminal.bounded_region_segments[0]),
      "terminal predicate should not pay eager view setup");
  unsetenv("GAGP_CPU_REGION_VIEWS");const auto terminal_reference=execute_bytecode_cpu(terminal,{},100);
  setenv("GAGP_CPU_REGION_VIEWS","1",1);const auto terminal_actual=execute_bytecode_cpu(terminal,{},100);
  require(terminal_reference.is_error==terminal_actual.is_error &&
      (terminal_reference.is_error ? terminal_reference.err.code==terminal_actual.err.code : exact_value(terminal_reference.value,terminal_actual.value)),
      "terminal profile fallback changed execution");
  // Unsupported result types and container-key memo keep the reference path.
  auto unsupported = compile_checked(identity_program(int_list(4)));
  require(!detail::cpu_region_views_supported(unsupported.bounded_region_segments[0]), "escaping list admitted as a private view");
  auto memo = compile_checked(count_program(int_list(4))).bounded_region_segments[0];
  memo.plan.memoized = true;
  require(!detail::cpu_region_views_supported(memo), "list-valued memo state admitted");
  detail::CpuListViews views; detail::RegionState state{};
  state[0] = int_list(4); std::vector<std::optional<Value>> parameters;
  require(views.prepare(state,1,parameters), "valid payload did not convert");
  const Value bad[]{Value::from_int_list_hash_len(100,1),Value::from_int(0)};
  require(views.call(BuiltinId::Index,bad,2).is_error,"out of storage view was dereferenced");
  const auto mutable_token = Value::from_int_list_hash_len(1299182,1);
  payload::register_list(mutable_token,{Value::from_int(7)});
  state[0] = mutable_token;
  require(views.prepare(state,1,parameters), "mutable source did not convert");
  const Value lookup[]{state[0],Value::from_int(0)};
  payload::register_list(mutable_token,{Value::from_int(11)});
  require(views.call(BuiltinId::Index,lookup,2).value.i == 7,
      "invocation view did not own its input snapshot");
  state[0] = mutable_token;
  require(views.prepare(state,1,parameters), "next invocation did not convert");
  const Value refreshed[]{state[0],Value::from_int(0)};
  require(views.call(BuiltinId::Index,refreshed,2).value.i == 11,
      "invocation retained a stale registry snapshot");
  require(detail::active_cpu_list_views == nullptr, "view owner leaked from execution");
  auto marked_external = mutable_token; marked_external.b = true;
  const Value external_args[]{marked_external,Value::from_int(0)};
  require(builtin_call(BuiltinId::Index,external_args,2).value.i == 11,
      "marked external value was interpreted as a view without an owner");
  {
    detail::CpuListViewScope outer(views);
    require(builtin_call(BuiltinId::Index,refreshed,2).value.i == 11,
        "scoped builtin did not read the invocation view");
    detail::CpuListViews nested;
    try {
      detail::CpuListViewScope inner(nested);
      require(builtin_call(BuiltinId::Index,refreshed,2).is_error,
          "view outside active owner's storage was dereferenced");
      throw 1;
    } catch (int) {}
    require(detail::active_cpu_list_views == &views,
        "nested scope did not restore owner after unwinding");
  }
  require(detail::active_cpu_list_views == nullptr, "view scope did not restore generic builtins");
  const auto short_token = Value::from_int_list_hash_len(892731,4);
  payload::register_list(short_token,{Value::from_int(1)});
  state[0] = short_token;
  require(!views.prepare(state,1,parameters),"short registered payload did not decline view conversion");
  const auto missing = Value::from_int_list_hash_len(879872331,4);
  state[0] = missing;
  require(!views.prepare(state,1,parameters),"missing payload did not decline view conversion");
  auto program = compile_checked(count_program(short_token));
  unsetenv("GAGP_CPU_REGION_VIEWS"); const auto a=execute_bytecode_cpu(program,{},100);
  setenv("GAGP_CPU_REGION_VIEWS","1",1); const auto b=execute_bytecode_cpu(program,{},100);
  require(a.is_error == b.is_error && (a.is_error ? a.err.code==b.err.code : exact_value(a.value,b.value)),
      "failed conversion did not restore generic region inputs");
}

}  // namespace

int main() {
  try {
    payload::clear();
    check_identity_and_fuel();
    check_cpu_region_views();
    check_lexical_ownership();
    check_lazy_capture_and_implicit_var_guard();
    std::cout << "gagp_test_bounded_region_sequence_compile: OK\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
