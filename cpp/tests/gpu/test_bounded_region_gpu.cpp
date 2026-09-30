#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/core/builtin.hpp"
#include "gagp/core/bytecode.hpp"
#include "gagp/core/bytecode_verify.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/cpu/fitness_cpu.hpp"
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "gagp/runtime/gpu/host_pack_gpu.hpp"
#include "gagp/runtime/gpu/region_types_gpu.hpp"
#include "gagp/runtime/payload/payload.hpp"

namespace {

using namespace gagp;

Instr ins(Opcode op) { return {op, 0, 0, false, false}; }
Instr ins_a(Opcode op, int a) { return {op, a, 0, true, false}; }
Instr ins_ab(Opcode op, int a, int b) { return {op, a, b, true, true}; }

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << '\n';
  return condition;
}

bool exact_value(const Value& left, const Value& right) {
  if (left.tag != right.tag) return false;
  switch (left.tag) {
    case ValueTag::Int:
    case ValueTag::Char:
    case ValueTag::FallbackToken:
      return left.i == right.i;
    case ValueTag::Float:
      return left.f == right.f;
    case ValueTag::Bool:
      return left.b == right.b;
    case ValueTag::String: {
      std::string a;
      std::string b;
      return payload::lookup_string(left, &a) &&
             payload::lookup_string(right, &b) && a == b;
    }
    case ValueTag::IntList:
    case ValueTag::FloatList:
    case ValueTag::StringList: {
      std::vector<Value> a;
      std::vector<Value> b;
      if (!payload::lookup_list(left, &a) ||
          !payload::lookup_list(right, &b) || a.size() != b.size()) {
        return false;
      }
      for (std::size_t i = 0; i < a.size(); ++i) {
        if (!exact_value(a[i], b[i])) return false;
      }
      return true;
    }
    case ValueTag::Invalid:
      return true;
  }
  return false;
}

bool same_score(double left, double right) {
  return left == right || (left != left && right != right);
}

RegionValueSlot slot(RegionSlotBank bank, std::uint32_t index) {
  return {bank, index};
}

RegionPhase phase(std::vector<Value> constants, std::vector<Instr> code,
                  std::vector<RegionPhaseBinding> bindings = {},
                  int n_locals = 0) {
  RegionPhase result;
  result.program.consts = std::move(constants);
  result.program.code = std::move(code);
  result.program.n_locals = n_locals;
  result.program.instruction_fuel.assign(result.program.code.size(), 0);
  result.bindings = std::move(bindings);
  return result;
}

RegionPhase constant_phase(Value value) {
  return phase({value}, {ins_a(Opcode::PushConst, 0), ins(Opcode::Return)});
}

RegionPhase load_phase(RegionSlotBank bank, std::uint32_t index) {
  return phase({}, {ins_a(Opcode::Load, 0), ins(Opcode::Return)},
               {{{bank, index}, 0}}, 1);
}

RegionPhase false_phase() {
  return constant_phase(Value::from_bool(false));
}

RegionPhase coordinate_base_predicate_1d() {
  return phase({Value::from_int(0)},
               {ins_a(Opcode::Load, 0), ins_a(Opcode::PushConst, 0),
                ins(Opcode::Eq), ins(Opcode::Return)},
               {{slot(RegionSlotBank::State, 0), 0}}, 1);
}

RegionPhase coordinate_base_predicate_2d() {
  return phase(
      {Value::from_int(0), Value::from_bool(false)},
      {ins_a(Opcode::Load, 0), ins_a(Opcode::PushConst, 0), ins(Opcode::Eq),
       ins_a(Opcode::JmpIfFalse, 8), ins_a(Opcode::Load, 1),
       ins_a(Opcode::PushConst, 0), ins(Opcode::Eq), ins(Opcode::Return),
       ins_a(Opcode::PushConst, 1), ins(Opcode::Return)},
      {{slot(RegionSlotBank::State, 0), 0},
       {slot(RegionSlotBank::State, 1), 1}}, 2);
}

RegionStateTransition offset(std::uint32_t source, std::int64_t amount) {
  RegionStateTransition result;
  result.kind = RegionTransitionKind::CoordinateOffset;
  result.source_state = source;
  result.offset = amount;
  return result;
}

RegionStateTransition window(std::uint32_t source, WindowEndpoint begin,
                             WindowEndpoint end) {
  RegionStateTransition result;
  result.kind = RegionTransitionKind::SequenceWindow;
  result.source_state = source;
  result.window = {begin, end};
  return result;
}

RegionBound literal(std::int64_t value) {
  RegionBound result;
  result.kind = RegionBoundKind::Literal;
  result.literal = value;
  return result;
}

RegionBound operand(std::uint32_t index) {
  RegionBound result;
  result.kind = RegionBoundKind::Operand;
  result.operand = index;
  return result;
}

BoundedRegionSegment unary_segment(Value terminal, bool duplicate = false) {
  BoundedRegionSegment segment;
  RegionPlan& plan = segment.plan;
  plan.state_types = {ValueTag::Int};
  plan.result_type = terminal.tag;
  plan.requests = duplicate
      ? std::vector<RegionRequest>{{{offset(0, -1)}}, {{offset(0, -1)}}}
      : std::vector<RegionRequest>{{{offset(0, -1)}}};
  plan.limits = {16, 16, 1};
  plan.memoized = true;
  plan.duplicate_policy = duplicate ? DuplicatePolicy::Allow
                                    : DuplicatePolicy::Reject;
  plan.coordinate_slots = {0};
  plan.coordinate_rank = {{0, 1}};
  plan.coordinate_domains = {{{literal(0), literal(8)}}};
  plan.coordinate_endpoint = DomainEndpoint::Inclusive;
  segment.boundary = constant_phase(terminal);
  segment.base_predicate = coordinate_base_predicate_1d();
  segment.base_body = constant_phase(terminal);
  segment.combine = load_phase(RegionSlotBank::Result, 0);
  return segment;
}

BytecodeProgram invocation(BoundedRegionSegment segment,
                           std::vector<Value> operands,
                           int caller_locals = 0) {
  BytecodeProgram program;
  program.consts = std::move(operands);
  for (std::size_t i = 0; i < program.consts.size(); ++i) {
    program.code.push_back(ins_a(Opcode::PushConst, static_cast<int>(i)));
  }
  program.code.push_back(ins_a(Opcode::BoundedRegion, 0));
  program.code.push_back(ins(Opcode::Return));
  program.n_locals = caller_locals;
  program.instruction_fuel.assign(program.code.size(), 0);
  program.instruction_fuel[program.consts.size()] = 1;
  program.bounded_region_segments.push_back(std::move(segment));
  return program;
}

BytecodeProgram ordinary_program() {
  BytecodeProgram program;
  program.consts = {Value::from_int(3)};
  program.code = {ins_a(Opcode::PushConst, 0), ins(Opcode::Return)};
  return program;
}

bool verify_fixture(const BytecodeProgram& program, const std::string& label) {
  const BytecodeVerifyResult result = verify_bytecode(program);
  return check(result.ok, label + ": invalid fixture: " +
                              result.diagnostic.message);
}

struct IntendedCase {
  CaseBindings bindings;
  Value expected = Value::invalid();
  ErrCode error = ErrCode::Value;
  bool succeeds = true;
};

bool compare_fitness(const BytecodeProgram& program,
                     const std::vector<IntendedCase>& intended, int fuel,
                     const std::string& label) {
  constexpr double penalty = 7.0;
  std::vector<CaseBindings> cases;
  std::vector<Value> answers;
  double intended_score = 0.0;
  for (std::size_t case_index = 0; case_index < intended.size(); ++case_index) {
    cases.push_back(intended[case_index].bindings);
    answers.push_back(intended[case_index].expected);
    std::vector<std::pair<int, Value>> inputs;
    for (const InputBinding& binding : intended[case_index].bindings) {
      inputs.push_back({binding.idx, binding.value});
    }
    const ExecResult cpu_result = execute_bytecode_cpu(program, inputs, fuel);
    const std::string prefix = label + " case " + std::to_string(case_index);
    if (intended[case_index].succeeds) {
      if (!check(!cpu_result.is_error &&
                     exact_value(cpu_result.value, intended[case_index].expected),
                 prefix + ": CPU fixture missed its intended value")) {
        return false;
      }
      intended_score += (intended[case_index].expected.tag == ValueTag::Int ||
                         intended[case_index].expected.tag == ValueTag::Float)
                            ? 0.0
                            : 1.0;
    } else {
      if (!check(cpu_result.is_error &&
                     cpu_result.err.code == intended[case_index].error,
                 prefix + ": CPU fixture missed its intended error")) {
        return false;
      }
      intended_score -= penalty;
    }
  }

  const std::vector<double> cpu =
      eval_fitness_cpu({program}, cases, answers, fuel, penalty, 32);
  if (!check(cpu.size() == 1 && same_score(cpu[0], intended_score),
             label + ": CPU fitness did not encode the intended outcomes")) {
    return false;
  }

  FitnessSessionGpu session;
  const FitnessSessionInitResult initialized =
      session.init(cases, answers, fuel, 32, penalty);
  if (!check(initialized.ok,
             label + ": GPU initialization failed: " + initialized.err.message)) {
    return false;
  }
  const FitnessEvalResult gpu = session.eval_programs({program});
  return check(gpu.ok, label + ": GPU evaluation failed: " + gpu.err.message) &&
         check(gpu.fitness.size() == 1,
               label + ": GPU fitness result shape changed") &&
         check(same_score(gpu.fitness[0], cpu[0]),
               label + ": CPU/GPU fitness mismatch (cpu=" +
                   std::to_string(cpu[0]) + ", gpu=" +
                   std::to_string(gpu.fitness[0]) + ")");
}

BoundedRegionSegment coordinate_2d_segment() {
  BoundedRegionSegment segment;
  RegionPlan& plan = segment.plan;
  plan.state_types = {ValueTag::Int, ValueTag::Int};
  plan.result_type = ValueTag::Int;
  plan.bound_operand_count = 2;
  plan.requests = {{{offset(0, -1), offset(1, 1)}},
                   {{offset(0, 0), offset(1, -1)}}};
  plan.limits = {64, 64, 1};
  plan.memoized = true;
  plan.coordinate_slots = {0, 1};
  plan.coordinate_rank = {{0, 1}, {1, 1}};
  plan.coordinate_domains = {{literal(0), operand(2)},
                             {literal(0), operand(3)}};
  plan.coordinate_endpoint = DomainEndpoint::Inclusive;
  segment.boundary = constant_phase(Value::from_int(0));
  segment.base_predicate = coordinate_base_predicate_2d();
  segment.base_body = constant_phase(Value::from_int(1));
  segment.combine = phase(
      {Value::from_int(1)},
      {ins_a(Opcode::Load, 0), ins_a(Opcode::Load, 1), ins(Opcode::Add),
       ins_a(Opcode::PushConst, 0), ins(Opcode::Add), ins(Opcode::Return)},
      {{slot(RegionSlotBank::Result, 0), 0},
       {slot(RegionSlotBank::Result, 1), 1}}, 2);
  return segment;
}

bool test_ordered_coordinate_memo() {
  const BytecodeProgram one_dimensional = invocation(
      unary_segment(Value::from_int(9), true), {Value::from_int(2)});
  const BytecodeProgram two_dimensional = invocation(
      coordinate_2d_segment(),
      {Value::from_int(2), Value::from_int(2), Value::from_int(2),
       Value::from_int(4)});
  return verify_fixture(one_dimensional, "1D memo") &&
         compare_fitness(one_dimensional,
                         {{{}, Value::from_int(9), ErrCode::Value, true}}, 6,
                         "1D ordered duplicate memo") &&
         verify_fixture(two_dimensional, "2D memo") &&
         compare_fitness(two_dimensional,
                         {{{}, Value::from_int(40), ErrCode::Value, true}},
                         1000, "2D ordered memo");
}

WindowEndpoint endpoint(WindowEndpointKind kind, std::uint32_t cut = 0) {
  return {kind, cut};
}

BoundedRegionSegment sequence_segment(ValueTag type) {
  BoundedRegionSegment segment;
  RegionPlan& plan = segment.plan;
  plan.state_types = {type};
  plan.result_type = type;
  plan.preparations = {
      {ValueTag::Int, RegionPreparationKind::InteriorCut},
      {ValueTag::Int, RegionPreparationKind::InteriorCut}};
  const WindowEndpoint begin = endpoint(WindowEndpointKind::Begin);
  const WindowEndpoint first = endpoint(WindowEndpointKind::InteriorCut, 0);
  const WindowEndpoint second = endpoint(WindowEndpointKind::InteriorCut, 1);
  const WindowEndpoint end = endpoint(WindowEndpointKind::End);
  plan.requests = {{{window(0, begin, first)}},
                   {{window(0, first, second)}},
                   {{window(0, second, end)}}};
  plan.limits = {32, 0, 1};
  plan.progress = RegionProgressKind::SequenceWindows;
  plan.sequence_state = 0;
  segment.base_predicate = false_phase();
  segment.base_body = load_phase(RegionSlotBank::State, 0);
  segment.preparations = {
      phase({Value::from_int(3)},
            {ins_a(Opcode::Load, 0), ins_a(Opcode::PushConst, 0),
             ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::IDiv0), 2),
             ins(Opcode::Return)},
            {{slot(RegionSlotBank::Measure, 0), 0}}, 1),
      phase({Value::from_int(2)},
            {ins_a(Opcode::Load, 0), ins_a(Opcode::PushConst, 0),
             ins(Opcode::Mul), ins(Opcode::Return)},
            {{slot(RegionSlotBank::Prepared, 0), 0}}, 1)};
  segment.combine = phase(
      {},
      {ins_a(Opcode::Load, 0), ins_a(Opcode::Load, 1),
       ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Concat), 2),
       ins_a(Opcode::Load, 2),
       ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Concat), 2),
       ins(Opcode::Return)},
      {{slot(RegionSlotBank::Result, 0), 0},
       {slot(RegionSlotBank::Result, 1), 1},
       {slot(RegionSlotBank::Result, 2), 2}}, 3);
  return segment;
}

Value sequence(ValueTag tag) {
  if (tag == ValueTag::String) return payload::make_string_value("abcd");
  if (tag == ValueTag::IntList) {
    return payload::make_int_list_value(
        {Value::from_int(-1), Value::from_int(0), Value::from_int(2),
         Value::from_int(9)});
  }
  if (tag == ValueTag::FloatList) {
    return payload::make_float_list_value(
        {Value::from_float(-0.0), Value::from_float(1.5),
         Value::from_float(2.25), Value::from_float(-4.0)});
  }
  return payload::make_string_list_value(
      {payload::make_string_value(""), payload::make_string_value("a"),
       payload::make_string_value("b"), payload::make_string_value("tail")});
}

bool test_sequence_divide_and_conquer() {
  for (const ValueTag tag : {ValueTag::String, ValueTag::IntList,
                             ValueTag::FloatList, ValueTag::StringList}) {
    const Value source = sequence(tag);
    const BytecodeProgram program = invocation(sequence_segment(tag), {source});
    if (!verify_fixture(program, "sequence divide-and-conquer") ||
        !compare_fitness(program,
                         {{{}, source, ErrCode::Value, true}}, 8,
                         "sequence divide-and-conquer tag " +
                             std::to_string(static_cast<int>(tag)))) {
      return false;
    }
  }
  return true;
}

BytecodeProgram captured_counter_program() {
  BoundedRegionSegment segment = unary_segment(Value::from_int(-1));
  segment.plan.parameter_types = {ValueTag::Int};
  segment.parameter_locals = {1};
  segment.base_body = load_phase(RegionSlotBank::Parameter, 0);
  segment.combine = phase(
      {Value::from_int(1)},
      {ins_a(Opcode::Load, 0), ins_a(Opcode::PushConst, 0),
       ins(Opcode::Add), ins(Opcode::Return)},
      {{slot(RegionSlotBank::Result, 0), 0}}, 1);
  BytecodeProgram program;
  program.n_locals = 2;
  program.code = {ins_a(Opcode::Load, 0),
                  ins_a(Opcode::BoundedRegion, 0), ins(Opcode::Return)};
  program.bounded_region_segments = {std::move(segment)};
  return program;
}

bool test_captured_locals_are_per_case_and_lazy() {
  const BytecodeProgram program = captured_counter_program();
  const std::vector<IntendedCase> cases{
      {{{0, Value::from_int(3)}, {1, Value::from_int(10)}},
       Value::from_int(13), ErrCode::Value, true},
      {{{0, Value::from_int(3)}, {1, Value::from_int(20)}},
       Value::from_int(23), ErrCode::Value, true},
      {{{0, Value::from_int(2)}}, Value::from_int(0), ErrCode::Name, false},
      {{{0, Value::from_int(2)}, {1, Value::from_bool(true)}},
       Value::from_int(0), ErrCode::Type, false},
      {{{0, Value::from_int(9)}}, Value::from_int(-1), ErrCode::Value, true},
  };
  return verify_fixture(program, "captured counter") &&
         compare_fitness(program, cases, 100, "captured local snapshot");
}

bool test_boundary_base_and_fuel() {
  const BytecodeProgram paths = invocation(
      unary_segment(Value::from_int(5)), {Value::from_int(-1)});
  if (!verify_fixture(paths, "boundary path") ||
      !compare_fitness(paths,
                       {{{}, Value::from_int(5), ErrCode::Value, true}}, 2,
                       "boundary path")) {
    return false;
  }
  const BytecodeProgram base = invocation(
      unary_segment(Value::from_int(6)), {Value::from_int(0)});
  if (!verify_fixture(base, "base path") ||
      !compare_fitness(base,
                       {{{}, Value::from_int(6), ErrCode::Value, true}}, 2,
                       "base path")) {
    return false;
  }
  const BytecodeProgram recursive = invocation(
      unary_segment(Value::from_int(7)), {Value::from_int(1)});
  return verify_fixture(recursive, "fuel boundary") &&
         compare_fitness(recursive,
                         {{{}, Value::from_int(0), ErrCode::Timeout, false}}, 2,
                         "below exact region fuel") &&
         compare_fitness(recursive,
                         {{{}, Value::from_int(7), ErrCode::Value, true}}, 3,
                         "exact region fuel");
}

bool test_workspace_thread_isolation() {
  auto program = captured_counter_program();
  auto& segment = program.bounded_region_segments[0];
  segment.plan.requests.push_back(segment.plan.requests[0]);
  segment.plan.duplicate_policy = DuplicatePolicy::Allow;
  if (!verify_fixture(program, "thread-isolated memo counter")) return false;
  auto second = program;
  second.bounded_region_segments[0].combine.program.consts[0] = Value::from_int(2);
  std::vector<CaseBindings> cases;
  std::vector<Value> answers;
  for (int i = 0; i < 2053; ++i) {
    const int depth = i % 8;
    cases.push_back({{0, Value::from_int(depth)}, {1, Value::from_int(100 + i)}});
    answers.push_back(Value::from_int(100 + i + depth));
  }
  for (const int blocksize : {256, 1024}) {
    for (const int fuel : {6, 100}) {
      FitnessSessionGpu session;
      const auto initialized = session.init(cases, answers, fuel, blocksize, 7.0);
      if (!check(initialized.ok, "thread-isolation session init")) return false;
      const std::vector<BytecodeProgram> programs{program, second};
      const auto cpu = eval_fitness_cpu(programs, cases, answers, fuel, 7.0, blocksize);
      for (int repeat = 0; repeat < 2; ++repeat) {
        const auto gpu = session.eval_programs(programs);
        if (!gpu.ok || gpu.fitness != cpu) {
          std::cerr << "block=" << blocksize << " fuel=" << fuel << " repeat=" << repeat
                    << " error=" << gpu.err.message << '\n';
          for (std::size_t i = 0; i < cpu.size(); ++i) {
            std::cerr << "cpu[" << i << "]=" << cpu[i];
            if (i < gpu.fitness.size()) std::cerr << " gpu=" << gpu.fitness[i];
            std::cerr << '\n';
          }
        }
        if (!check(gpu.ok && gpu.fitness == cpu,
                   "divergent thread/case/program memo state must remain isolated")) return false;
      }
    }
  }
  return true;
}

bool test_exact_capacity_exhaustion() {
  BoundedRegionSegment frames_ok = unary_segment(Value::from_int(1));
  frames_ok.plan.limits.frames = 2;
  const BytecodeProgram exact_frames =
      invocation(std::move(frames_ok), {Value::from_int(1)});
  BoundedRegionSegment frames_short = unary_segment(Value::from_int(1));
  frames_short.plan.limits.frames = 1;
  const BytecodeProgram exhausted_frames =
      invocation(std::move(frames_short), {Value::from_int(1)});

  BoundedRegionSegment cells_ok = unary_segment(Value::from_int(1));
  cells_ok.plan.limits.cells = 1;
  const BytecodeProgram exact_cells =
      invocation(std::move(cells_ok), {Value::from_int(1)});
  BoundedRegionSegment cells_short = unary_segment(Value::from_int(1));
  cells_short.plan.limits.cells = 0;
  const BytecodeProgram exhausted_cells =
      invocation(std::move(cells_short), {Value::from_int(1)});

  return compare_fitness(exact_frames,
                         {{{}, Value::from_int(1), ErrCode::Value, true}}, 3,
                         "exact two-frame capacity") &&
         compare_fitness(exhausted_frames,
                         {{{}, Value::from_int(0), ErrCode::Timeout, false}}, 3,
                         "one-frame exhaustion") &&
         compare_fitness(exact_cells,
                         {{{}, Value::from_int(1), ErrCode::Value, true}}, 3,
                         "exact one-cell capacity") &&
         compare_fitness(exhausted_cells,
                         {{{}, Value::from_int(0), ErrCode::Timeout, false}}, 3,
                         "zero-cell exhaustion");
}

bool pack_rejects(const BytecodeProgram& program, const std::string& label) {
  try {
    (void)gpu_detail::pack_programs_with_shared_case_count({program}, 1, 0);
  } catch (const std::invalid_argument&) {
    return true;
  }
  return check(false, label + ": host pack unexpectedly accepted program");
}

bool test_host_pack_validation_and_profile_limits() {
  BoundedRegionSegment maximum = unary_segment(Value::from_int(1));
  maximum.plan.limits.frames = gpu_detail::DMAX_REGION_FRAMES;
  maximum.plan.limits.cells = gpu_detail::DMAX_REGION_MEMO;
  const BytecodeProgram maximum_program =
      invocation(std::move(maximum), {Value::from_int(1)});
  try {
    const gpu_detail::PackResult packed =
        gpu_detail::pack_programs_with_shared_case_count({maximum_program}, 1, 0);
    if (!check(packed.metas.size() == 1 && packed.metas[0].is_valid &&
                   packed.metas[0].region_offset == 0 &&
                   packed.metas[0].region_count == 1 &&
                   packed.region_segments.size() == 1 &&
                   !packed.region_phases.empty(),
               "host pack rejected the exact 128-frame/128-cell profile")) {
      return false;
    }
  } catch (const std::invalid_argument& error) {
    return check(false, "host pack rejected the exact profile limit: " +
                            std::string(error.what()));
  }

  BoundedRegionSegment too_many_frames = unary_segment(Value::from_int(1));
  too_many_frames.plan.limits.frames = gpu_detail::DMAX_REGION_FRAMES + 1;
  BoundedRegionSegment too_many_cells = unary_segment(Value::from_int(1));
  too_many_cells.plan.limits.cells = gpu_detail::DMAX_REGION_MEMO + 1;
  BytecodeProgram malformed =
      invocation(unary_segment(Value::from_int(1)), {Value::from_int(1)});
  malformed.bounded_region_segments[0].plan.requests.clear();

  BytecodeProgram valid = maximum_program;
  // Exercise concurrent verification, including an invalid late input.
  std::vector<BytecodeProgram> batch(64, valid);
  const auto packed_batch = gpu_detail::pack_programs_with_shared_case_count(batch, 1, 0);
  if (!check(packed_batch.metas.size() == batch.size(), "parallel valid pack")) return false;
  batch.back() = malformed;
  bool rejected = false;
  try { (void)gpu_detail::pack_programs_with_shared_case_count(batch, 1, 0); }
  catch (const std::invalid_argument&) { rejected = true; }
  if (!check(rejected, "parallel pack must reject malformed last region")) return false;
  try {
    (void)gpu_detail::pack_programs_with_shared_case_count({valid, malformed}, 1,
                                                           0);
  } catch (const std::invalid_argument&) {
    return pack_rejects(invocation(std::move(too_many_frames),
                                   {Value::from_int(1)}),
                        "129 frames") &&
           pack_rejects(invocation(std::move(too_many_cells),
                                   {Value::from_int(1)}),
                        "129 cells") &&
           pack_rejects(malformed, "malformed region plan");
  }
  return check(false,
               "host pack did not validate all region plans before upload");
}

bool test_phase_binding_capacities() {
  for (const bool leaf : {false, true}) {
    for (const int count : {1, 2, 4, 5, 8, 9, 32}) {
      auto segment = unary_segment(Value::from_int(-1));
      segment.plan.parameter_types.assign(count, ValueTag::Int);
      std::vector<RegionPhaseBinding> bindings;
      CaseBindings inputs;
      for (int i = 0; i < count; ++i) {
        segment.parameter_locals.push_back(i);
        bindings.push_back({slot(RegionSlotBank::Parameter, i), i});
        inputs.push_back({i, Value::from_int(100 + i)});
      }
      segment.base_body = phase(
          {}, {ins_a(Opcode::Load, count - 1), ins(Opcode::Return)},
          std::move(bindings), count);
      if (leaf) {
        segment.base_body.program.code.pop_back();
        segment.base_body.program.instruction_fuel.pop_back();
      }
      segment.base_body.program.instruction_fuel[0] = 3;
      auto program = invocation(std::move(segment), {Value::from_int(0)});
      program.n_locals = count;
      const Value answer = Value::from_int(99 + count);
      auto missing = inputs;
      missing.pop_back();
      auto wrong_type = inputs;
      wrong_type.back().value = Value::from_float(1.0);
      const std::string label = std::string(leaf ? "leaf" : "returned") +
          " phase binding capacity " + std::to_string(count);
      if (!verify_fixture(program, label) ||
          !compare_fitness(program,
                           {{inputs, answer, ErrCode::Value, true},
                            {missing, answer, ErrCode::Name, false},
                            {wrong_type, answer, ErrCode::Type, false}},
                           5, label) ||
          !compare_fitness(program,
                           {{inputs, answer, ErrCode::Timeout, false},
                            {missing, answer, ErrCode::Timeout, false},
                            {wrong_type, answer, ErrCode::Timeout, false}},
                           4, label + " below instruction fuel")) return false;
    }
  }
  return true;
}

bool test_leaf_phase_types_and_fuel() {
  const std::vector<Value> terminals{
      Value::from_int(17), Value::from_float(1.25), Value::from_bool(true),
      Value::from_char('a'), payload::make_string_value("leaf"),
      payload::make_int_list_value({Value::from_int(2)}),
      payload::make_float_list_value({Value::from_float(2.5)}),
      payload::make_string_list_value({payload::make_string_value("item")})};
  for (const auto terminal : terminals) {
    auto segment = unary_segment(terminal);
    for (auto* leaf : {&segment.base_body, &segment.combine}) {
      leaf->program.code.pop_back();
      leaf->program.instruction_fuel = {2};
    }
    const auto program = invocation(std::move(segment), {Value::from_int(1)});
    if (!verify_fixture(program, "leaf result types") ||
        !compare_fitness(program, {{{}, terminal, ErrCode::Value, true}},
                         7, "leaf exact fuel") ||
        !compare_fitness(program, {{{}, terminal, ErrCode::Timeout, false}},
                         6, "leaf below exact fuel")) return false;
  }
  return true;
}

bool test_phase_frame_boundaries() {
  for (const int length : {16, 17}) {
    for (const int locals : {8, 9}) {
      auto segment = unary_segment(Value::from_int(-1));
      std::vector<Instr> code{ins_a(Opcode::PushConst, 0)};
      for (int i = 0; i < length - 2; ++i) code.push_back(ins(Opcode::Neg));
      code.push_back(ins(Opcode::Return));
      segment.base_body = phase({Value::from_int(7)}, std::move(code), {}, locals);
      const auto program = invocation(std::move(segment), {Value::from_int(0)});
      const std::string label = "phase frame length=" + std::to_string(length) +
                                " locals=" + std::to_string(locals);
      if (!verify_fixture(program, label) || !compare_fitness(program,
                           {{{}, Value::from_int(length % 2 ? -7 : 7), ErrCode::Value, true}},
                           100, label)) return false;
    }
  }
  auto segment = unary_segment(Value::from_int(-1));
  segment.base_body = phase(
      {Value::from_bool(false), Value::from_int(7)},
      {ins_a(Opcode::PushConst, 0), ins_a(Opcode::JmpIfFalse, 4),
       ins_a(Opcode::PushConst, 1), ins(Opcode::Return),
       ins_a(Opcode::PushConst, 1), ins(Opcode::Return)});
  const auto program = invocation(std::move(segment), {Value::from_int(0)});
  return verify_fixture(program, "branching phase") &&
         compare_fitness(program, {{{}, Value::from_int(7), ErrCode::Value, true}},
                         100, "branching phase fallback");
}

bool test_production_capability_dispatch_sequence() {
  constexpr int fuel = 1000;
  constexpr double penalty = 7.0;
  const std::vector<CaseBindings> cases{{}};
  const std::vector<Value> answers{Value::from_int(10)};
  const BytecodeProgram region = invocation(
      unary_segment(Value::from_int(7)), {Value::from_int(2)});
  const BytecodeProgram ordinary = ordinary_program();
  if (!verify_fixture(region, "region capability dispatch") ||
      !verify_fixture(ordinary, "ordinary capability dispatch")) {
    return false;
  }

  for (const auto* program : {&region, &ordinary}) {
    if (!check(!execute_bytecode_cpu(*program, {}, fuel).is_error,
               "capability dispatch fixture unexpectedly fails on CPU")) {
      return false;
    }
  }

  FitnessSessionGpu session;
  const FitnessSessionInitResult initialized =
      session.init(cases, answers, fuel, 256, penalty);
  if (!check(initialized.ok, "capability dispatch GPU initialization failed: " +
                                 initialized.err.message)) {
    return false;
  }

  const auto compare_population = [&](const std::vector<BytecodeProgram>& programs,
                                      const std::string& label) {
    const std::vector<double> cpu =
        eval_fitness_cpu(programs, cases, answers, fuel, penalty, 32);
    const FitnessEvalResult gpu = session.eval_programs(programs);
    if (!check(gpu.ok, label + ": GPU evaluation failed: " + gpu.err.message) ||
        !check(gpu.fitness.size() == cpu.size(),
               label + ": GPU fitness result shape changed")) {
      return false;
    }
    for (std::size_t i = 0; i < cpu.size(); ++i) {
      if (!check(same_score(gpu.fitness[i], cpu[i]),
                 label + ": CPU/GPU fitness mismatch at program " +
                     std::to_string(i) + " (cpu=" + std::to_string(cpu[i]) +
                     ", gpu=" + std::to_string(gpu.fitness[i]) + ")")) {
        return false;
      }
    }
    return true;
  };

  std::vector<BytecodeProgram> reuse_population;
  reuse_population.reserve(32);
  for (int i = 0; i < 32; ++i) {
    if (i % 4 == 0) {
      reuse_population.push_back(ordinary);
      reuse_population.back().consts[0] = Value::from_int(1000 + i);
    } else {
      const Value terminal = Value::from_int(100 + i);
      BoundedRegionSegment segment = unary_segment(terminal);
      segment.plan.limits.frames = gpu_detail::DMAX_REGION_FRAMES;
      segment.plan.limits.cells = gpu_detail::DMAX_REGION_MEMO;
      BytecodeProgram generic =
          invocation(std::move(segment), {Value::from_int(2)});
      const std::string label =
          "grid-stride region fixture " + std::to_string(i);
      if (!verify_fixture(generic, label)) return false;
      const ExecResult result = execute_bytecode_cpu(generic, {}, fuel);
      if (!check(!result.is_error && exact_value(result.value, terminal),
                 label + ": CPU fixture missed its distinct base value")) {
        return false;
      }
      reuse_population.push_back(std::move(generic));
    }
  }
  for (std::size_t i = 0; i < reuse_population.size(); ++i) {
    if (!check(!execute_bytecode_cpu(reuse_population[i], {}, fuel).is_error,
               "grid-stride workspace fixture unexpectedly fails on CPU at program " +
                   std::to_string(i))) {
      return false;
    }
  }

  const BytecodeProgram memo_a = invocation(
      unary_segment(Value::from_int(9), true), {Value::from_int(2)});
  const BytecodeProgram memo_b = invocation(
      unary_segment(Value::from_int(41), true), {Value::from_int(2)});
  auto wide_segment = unary_segment(Value::from_int(7), true);
  wide_segment.plan.state_types.assign(4, ValueTag::Int);
  wide_segment.plan.coordinate_slots = {0, 1, 2, 3};
  wide_segment.plan.coordinate_rank = {{0, 1}, {1, 1}, {2, 1}, {3, 1}};
  wide_segment.plan.coordinate_domains.assign(4, {literal(0), literal(100)});
  wide_segment.plan.requests.assign(8, {{{offset(0, -1), offset(1, 0),
                                         offset(2, 0), offset(3, 0)}}});
  for (int i = 0; i < 4; ++i) {
    wide_segment.plan.preparations.push_back({ValueTag::Int, RegionPreparationKind::Identity});
    wide_segment.preparations.push_back(constant_phase(Value::from_int(10 + i)));
  }
  wide_segment.base_body = load_phase(RegionSlotBank::State, 3);
  wide_segment.combine = phase({},
      {ins_a(Opcode::Load, 0), ins_a(Opcode::Load, 1), ins(Opcode::Add),
       ins_a(Opcode::Load, 2), ins(Opcode::Add), ins_a(Opcode::Load, 3),
       ins(Opcode::Add), ins(Opcode::Return)},
      {{slot(RegionSlotBank::Result, 7), 0}, {slot(RegionSlotBank::Prepared, 3), 1},
       {slot(RegionSlotBank::State, 2), 2}, {slot(RegionSlotBank::State, 1), 3}}, 4);
  const auto wide = invocation(std::move(wide_segment),
      {Value::from_int(2), Value::from_int(3), Value::from_int(5), Value::from_int(17)});
  if (!verify_fixture(wide, "maximum frame slot layout") ||
      !check(exact_value(execute_bytecode_cpu(wide, {}, fuel).value, Value::from_int(59)),
             "maximum frame slot fixture result")) return false;
  std::vector<BytecodeProgram> reversed_population = reuse_population;
  std::reverse(reversed_population.begin(), reversed_population.end());
  return compare_population({memo_a}, "initial memo workspace") &&
         compare_population({memo_b}, "same-layout memo must not retain results") &&
         compare_population({memo_a}, "same-layout memo repeated") &&
         compare_population({wide, memo_b}, "maximum state/preparation/request slots") &&
         compare_population({memo_a}, "shrink all frame slot banks") &&
         compare_population({wide, ordinary, memo_b}, "regrow all frame slot banks") &&
         compare_population({region, ordinary},
                            "mixed region/ordinary dispatch") &&
         compare_population(reuse_population,
                            "grid-stride region workspace reuse") &&
         compare_population(reuse_population,
                            "repeat maximum workspace layout") &&
         compare_population(reversed_population,
                            "reversed schedule preserves output positions") &&
         compare_population({ordinary}, "ordinary-only dispatch") &&
         compare_population({memo_b}, "shrink workspace after ordinary dispatch");
}

}  // namespace

int main() {
  payload::clear();
  if (!test_workspace_thread_isolation()) return 1;
  if (!test_phase_binding_capacities()) return 1;
  if (!test_leaf_phase_types_and_fuel()) return 1;
  if (!test_phase_frame_boundaries()) return 1;
  if (!test_production_capability_dispatch_sequence()) return 1;
  if (!test_ordered_coordinate_memo()) return 1;
  if (!test_sequence_divide_and_conquer()) return 1;
  if (!test_captured_locals_are_per_case_and_lazy()) return 1;
  if (!test_boundary_base_and_fuel()) return 1;
  if (!test_exact_capacity_exhaustion()) return 1;
  if (!test_host_pack_validation_and_profile_limits()) return 1;
  std::cout << "gagp_test_bounded_region_gpu: OK\n";
  return 0;
}
