#include <cuda_runtime.h>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>
#include <utility>
#include <vector>
#include "gagp/core/builtin.hpp"
#include "gagp/core/bytecode.hpp"
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "gagp/runtime/payload/payload.hpp"
#include "../../src/runtime/gpu/device/builtins_device.cuh"
using namespace gagp;
Instr ins(Opcode op) { return {op, 0, 0, false, false}; }
Instr ins_a(Opcode op, int a) { return {op, a, 0, true, false}; }
Instr ins_ab(Opcode op, int a, int b) { return {op, a, b, true, true}; }

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << '\n';
  return condition;
}

struct ScopedEnvironment {
  std::string name,previous;bool existed;
  explicit ScopedEnvironment(const char* key):name(key),existed(std::getenv(key)!=nullptr) {
    if(existed)previous=std::getenv(key);
  }
  ~ScopedEnvironment(){if(existed)setenv(name.c_str(),previous.c_str(),1);else unsetenv(name.c_str());}
};

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
               {{slot(bank, index), 0}}, 1);
}

RegionPhase zero_base_predicate() {
  return phase(
      {Value::from_int(0)},
      {ins_a(Opcode::Load, 0), ins_a(Opcode::PushConst, 0), ins(Opcode::Eq),
       ins(Opcode::Return)},
      {{slot(RegionSlotBank::State, 0), 0}}, 1);
}

Value nominal_value(ValueTag tag) {
  switch (tag) {
    case ValueTag::Int: return Value::from_int(0);
    case ValueTag::Float: return Value::from_float(0.0);
    case ValueTag::Bool: return Value::from_bool(false);
    case ValueTag::Char: return Value::from_char(0);
    case ValueTag::String: return payload::make_string_value("");
    case ValueTag::IntList: return payload::make_int_list_value({});
    case ValueTag::FloatList: return payload::make_float_list_value({});
    case ValueTag::StringList: return payload::make_string_list_value({});
    case ValueTag::FallbackToken:
    case ValueTag::Invalid: return Value::invalid();
  }
  return Value::invalid();
}

RegionStateTransition offset(std::int64_t amount) {
  RegionStateTransition result;
  result.kind = RegionTransitionKind::CoordinateOffset;
  result.source_state = 0;
  result.offset = amount;
  return result;
}

RegionBound literal(std::int64_t value) {
  RegionBound result;
  result.kind = RegionBoundKind::Literal;
  result.literal = value;
  return result;
}

BoundedRegionSegment unary_segment(ValueTag result_type,
                                   RegionPhase base_body,
                                   RegionPhase combine,
                                   std::vector<std::int64_t> request_offsets = {-1}) {
  BoundedRegionSegment segment;
  RegionPlan& plan = segment.plan;
  plan.state_types = {ValueTag::Int};
  plan.result_type = result_type;
  for (const std::int64_t amount : request_offsets) {
    plan.requests.push_back({{offset(amount)}});
  }
  plan.limits = {128, 128, 1};
  plan.memoized = true;
  plan.duplicate_policy = DuplicatePolicy::Reject;
  plan.coordinate_slots = {0};
  plan.coordinate_rank = {{0, 1}};
  plan.coordinate_domains = {{{literal(0), literal(8)}}};
  plan.coordinate_endpoint = DomainEndpoint::Inclusive;
  segment.boundary = constant_phase(nominal_value(result_type));
  segment.base_predicate = zero_base_predicate();
  segment.base_body = std::move(base_body);
  segment.combine = std::move(combine);
  return segment;
}

BytecodeProgram invocation(BoundedRegionSegment segment, std::int64_t state) {
  BytecodeProgram program;
  program.consts = {Value::from_int(state)};
  program.code = {ins_a(Opcode::PushConst, 0),
                  ins_a(Opcode::BoundedRegion, 0), ins(Opcode::Return)};
  program.instruction_fuel = {0, 1, 0};
  program.bounded_region_segments.push_back(std::move(segment));
  return program;
}

using ExtendedPayloadState = gagp::gpu_detail::DPayloadFlavorTraits<
    gagp::gpu_detail::DPayloadFlavor::ExtendedMixed>::State;

__global__ void inspect_extended_payload(ExtendedPayloadState* arena, int* failures) {
  using namespace gagp::gpu_detail;
  constexpr auto flavor = DPayloadFlavor::ExtendedMixed;
  auto& state = *arena;
  DPayloadTables tables{};
  Value list;
  ErrCode error = ErrCode::Value;
  list = d_make_list_hash_len(ValueTag::IntList, d_hash_list_payload(nullptr, 0), 0);
  if (!d_register_local_list(state, list, 0, 0)) { *failures = 1; return; }
  #pragma unroll 1
  for (int i = 0; i < 50; ++i) {
    Value element = Value::from_int(i % 2 ? i : 0), singleton;
    if (!d_builtin_call<flavor>(BuiltinId::Singleton, &element, 1, tables, state, singleton, error)) {
      *failures = 2; return;
    }
    Value args[] = {list, singleton}, next;
    if (!d_builtin_call<flavor>(BuiltinId::Concat, args, 2, tables, state, next, error)) {
      *failures = 3; return;
    }
    list = next;
  }
  const Value* values = nullptr;
  int count = 0;
  if (!d_lookup_list_payload(tables, state, list, values, count) || count != 50) {
    *failures = 4; return;
  }
  for (int i = 0; i < count; ++i)
    if (values[i].tag != ValueTag::Int || values[i].i != (i % 2 ? i : 0)) { *failures = 5; return; }
  bool exhausted = false;
  #pragma unroll 1
  for (int i = 0; i < 20; ++i) {
    Value args[] = {list, list}, next;
    if (!d_builtin_call<flavor>(BuiltinId::Concat, args, 2, tables, state, next, error)) {
      exhausted = error == ErrCode::Value; break;
    }
    list = next;
  }
  if (!exhausted) { *failures = 6; return; }
  // A 50-leaf binary tuple recurrence has 99 nodes. Each three-field tuple
  // uses three Singletons and two Concats: 495 entries before input slices.
  state.list_entry_count = 0;
  state.list_values_used = 0;
  #pragma unroll 1
  for (int mode = 0; mode < 2; ++mode) {
  #pragma unroll 1
  for (int node = 0; node < 99; ++node) {
    Value fields[3];
    #pragma unroll 1
    for (int field = 0; field < 3; ++field) {
      Value value = mode == 0 ? Value::from_int(field + node) : Value::from_char(field + node);
      if (!d_builtin_call<flavor>(BuiltinId::Singleton, &value, 1, tables, state, fields[field], error)) {
        *failures = 7; return;
      }
    }
    Value args[] = {fields[0], fields[1]}, pair, tuple;
    if (!d_builtin_call<flavor>(BuiltinId::Concat, args, 2, tables, state, pair, error)) {
      *failures = 8; return;
    }
    args[0] = pair; args[1] = fields[2];
    if (!d_builtin_call<flavor>(BuiltinId::Concat, args, 2, tables, state, tuple, error)) {
      *failures = 9; return;
    }
  }
  }
  *failures = state.list_entry_count == 495 && state.list_values_used == 792 &&
              state.string_entry_count == 495 && state.string_bytes_used == 792 ? 0 : 10;
}

bool test_subordinate_memo_coordinate_fitness() {
  auto segment = unary_segment(ValueTag::Int, load_phase(RegionSlotBank::State, 1),
      phase({}, {ins_a(Opcode::Load, 0), ins_a(Opcode::Load, 1), ins(Opcode::Add), ins(Opcode::Return)},
            {{slot(RegionSlotBank::Result, 0), 0}, {slot(RegionSlotBank::State, 1), 1}}, 2));
  segment.plan.state_types.push_back(ValueTag::Int);
  segment.plan.coordinate_slots.push_back(1);
  segment.plan.coordinate_rank.push_back({1, 1});
  segment.plan.coordinate_domains.push_back({literal(0), literal(1)});
  segment.plan.duplicate_policy = DuplicatePolicy::Allow;
  segment.plan.request_expression_types = {ValueTag::Int};
  RegionStateTransition transition;
  transition.kind = RegionTransitionKind::Expression;
  transition.expression = 0;
  segment.plan.requests[0].states.push_back(transition);
  segment.request_expressions.push_back(phase({Value::from_int(1), Value::from_int(0)},
      {ins_a(Opcode::Load, 0), ins_a(Opcode::PushConst, 0), ins(Opcode::Sub),
       ins_a(Opcode::PushConst, 1), ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Max), 2),
       ins(Opcode::Return)}, {{slot(RegionSlotBank::State, 1), 0}}, 1));
  auto program = invocation(std::move(segment), 5);
  program.consts.push_back(Value::from_int(1));
  program.code.insert(program.code.begin() + 1, ins_a(Opcode::PushConst, 1));
  program.instruction_fuel.insert(program.instruction_fuel.begin() + 1, 0);
  auto other = program;
  other.consts[1] = Value::from_int(0);
  std::vector<CaseBindings> cases(32);
  std::vector<Value> answers(32, Value::from_int(1));
  const auto cpu = eval_fitness_cpu({program, other}, cases, answers, 1000, 1000.0);
  FitnessSessionGpu session;
  if (!session.init(cases, answers, 1000, 32, 1000.0).ok) return false;
  const auto gpu = session.eval_programs({program, other}, true);
  return check(gpu.ok && gpu.fitness == cpu && cpu == std::vector<double>({0.0, -32.0}),
               "subordinate clamped memo coordinate CPU/GPU mismatch") &&
         check(gpu.case_counts[0][1] == 0 && gpu.case_counts[1][1] == 0,
               "subordinate memo coordinate execution error");
}

bool test_larger_memo_fitness() {
  auto segment = unary_segment(ValueTag::Int, constant_phase(Value::from_int(7)),
      load_phase(RegionSlotBank::Result, 0), {-2, -1});
  segment.plan.limits.cells = 256;
  segment.plan.coordinate_domains[0].upper = literal(256);
  auto program = invocation(std::move(segment), 200);
  std::vector<CaseBindings> cases(8);
  std::vector<Value> answers(8, Value::from_int(7));
  const auto cpu = eval_fitness_cpu({program}, cases, answers, 2000, 1000.0);
  FitnessSessionGpu session;
  if (!session.init(cases, answers, 2000, 32, 1000.0).ok) return false;
  const auto gpu = session.eval_programs({program}, true);
  return check(gpu.ok && gpu.fitness == cpu && cpu == std::vector<double>({0.0}),
               "200-cell memo CPU/GPU mismatch") &&
         check(gpu.case_counts[0][1] == 0, "200-cell memo returned runtime errors");
}

bool test_extended_list_fitness() {
  auto combine = phase({},
      {ins_a(Opcode::Load, 0), ins_a(Opcode::Load, 1),
       ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Singleton), 1),
       ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Concat), 2), ins(Opcode::Return)},
      {{slot(RegionSlotBank::Result, 0), 0}, {slot(RegionSlotBank::State, 0), 1}}, 2);
  auto segment = unary_segment(ValueTag::IntList,
      constant_phase(payload::make_int_list_value({})), std::move(combine));
  segment.plan.coordinate_domains[0].upper = literal(50);
  auto program = invocation(std::move(segment), 50);
  std::vector<Value> values;
  for (int i = 1; i <= 50; ++i) values.push_back(Value::from_int(i));
  std::vector<CaseBindings> cases(1024);
  std::vector<Value> answers(1024, payload::make_int_list_value(values));
  const auto cpu = eval_fitness_cpu({program}, cases, answers, 1000, 1000.0);
  FitnessSessionGpu session;
  const auto initialized = session.init(cases, answers, 1000, 64, 1000.0);
  if (!check(initialized.ok, "extended list fitness session init")) return false;
  const auto gpu = session.eval_programs({program}, true);
  return check(gpu.ok && gpu.fitness == cpu && cpu[0] == 1024.0,
               "50-element list full-case CPU/GPU fitness differs") &&
         check(gpu.case_counts[0][0] == 1024 && gpu.case_counts[0][1] == 0,
               "50-element list fitness produced errors");
}

__global__ void inspect_private_list_index(gpu_detail::DIndexedExtendedPayloadState* state, int* failures) {
  using namespace gpu_detail;
  *failures=0;
  DPayloadTables tables;
  Value external[1]={Value::from_int(991)};
  tables.list_values=external;
  for(int i=0;i<800;++i) {
    state->list_values[i]=Value::from_int(i);
    const auto token=d_make_list_hash_len(ValueTag::IntList,i+1,1);
    if(!d_register_local_list(*state,token,i,1)){*failures=1;return;}
  }
  for(int i=799;i>=0;--i) {
    const Value* values=nullptr;int count=0;
    const auto token=d_make_list_hash_len(ValueTag::IntList,i+1,1);
    if(!d_lookup_list_payload(tables,*state,token,values,count) || count!=1 || values[0].i!=i){*failures=2;return;}
  }
  // Keep the first canonical identity even when the nonlogical policy records
  // another physical entry with the same key and a different backing offset.
  const auto first=d_make_list_hash_len(ValueTag::IntList,1,1);
  if(!d_register_local_list(*state,first,799,1) || d_find_local_list_entry(*state,first)!=0){*failures=3;return;}
  state->logical_list_limit=65536;
  const int before=state->list_entry_count;
  if(!d_register_local_list(*state,first,798,1) || state->list_entry_count!=before){*failures=4;return;}
  const auto alias=d_make_list_hash_len(ValueTag::IntList,99999,1);
  if(!d_register_local_list(*state,alias,-1,1)){*failures=5;return;}
  const Value* values=nullptr;int count=0;
  if(!d_lookup_list_payload(tables,*state,alias,values,count) || values[0].i!=991){*failures=6;return;}
}

bool test_private_list_index() {
  gpu_detail::DIndexedExtendedPayloadState* state=nullptr;int* failures=nullptr;
  if(cudaMallocManaged(&state,sizeof(*state))!=cudaSuccess)return false;
  if(cudaMallocManaged(&failures,sizeof(int))!=cudaSuccess){cudaFree(state);return false;}
  new(state)gpu_detail::DIndexedExtendedPayloadState{};
  *failures=-1;
  inspect_private_list_index<<<1,1>>>(state,failures);
  const auto status=cudaDeviceSynchronize();
  const bool passed=status==cudaSuccess && *failures==0;
  if(!passed)std::cerr<<"private payload index error "<<*failures<<'\n';
  cudaFree(state);cudaFree(failures);return passed;
}

bool test_payload_replay_fitness() {
  // Two appends per frame allocate >8192 cells without exceeding the source
  // logical arena. The original ExtendedMixed evaluation must overflow.
  auto combine = phase({},
      {ins_a(Opcode::Load, 0), ins_a(Opcode::Load, 1),
       ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Singleton), 1),
       ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Concat), 2),
       ins_a(Opcode::Load, 1),
       ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Singleton), 1),
       ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Concat), 2), ins(Opcode::Return)},
      {{slot(RegionSlotBank::Result, 0), 0}, {slot(RegionSlotBank::State, 0), 1}}, 2);
  auto segment=unary_segment(ValueTag::IntList,
      constant_phase(payload::make_int_list_value({})),std::move(combine));
  segment.plan.coordinate_domains[0].upper=literal(100);
  auto program=invocation(std::move(segment),100);
  auto short_program=program;short_program.consts[0]=Value::from_int(1);
  std::vector<Value> values;
  for(int i=1;i<=100;++i){values.push_back(Value::from_int(i));values.push_back(Value::from_int(i));}
  // Uneven chunks and nonintegral penalties exercise the original 512 logical
  // lanes when replay uses only 32 physical CUDA threads.
  std::vector<CaseBindings> cases(513);
  std::vector<Value> answers;
  for(int i=0;i<513;++i) {
    auto expected=values;
    for(int j=0;j<i%7;++j)expected[j]=Value::from_int(-1);
    answers.push_back(payload::make_int_list_value(expected));
  }
  ScopedEnvironment restore_index("GAGP_GPU_PAYLOAD_INDEX"),restore_global("GAGP_GPU_GLOBAL_PAYLOAD");
  for(const char* global:{"0","1"}) for(const char* indexed:{"0","1"}) for(unsigned limit:{65536u,100u}) {
    setenv("GAGP_GPU_GLOBAL_PAYLOAD",global,1);
    setenv("GAGP_GPU_PAYLOAD_INDEX",indexed,1);
    FitnessPolicy policy{FitnessMetric::Levenshtein,FitnessProximity::Discounted};
    policy.list_allocation_limit=limit;
    const auto cpu=eval_fitness_cpu({program,short_program},cases,answers,10000,1000.0,512,policy);
    FitnessSessionGpu session;
    if(!check(session.init(cases,answers,10000,512,1000.0,policy).ok,"replay session init"))return false;
    const auto gpu=session.eval_programs({program,short_program},true);
    if(!check(gpu.ok && gpu.fitness==cpu,"GPU arena replay differs from CPU 512-lane reduction")) {
      std::cerr<<"global="<<global<<" index="<<indexed<<" limit="<<limit<<" ok="<<gpu.ok<<" error="<<gpu.err.message
          <<" profile="<<gpu.execution_profile<<" replay_programs="<<gpu.payload_replay_programs
          <<" replay_cases="<<gpu.payload_replay_cases<<" launches="<<gpu.payload_replay_launches<<'\n';
      std::cerr.precision(17);
      for(std::size_t i=0;i<cpu.size();++i) {
        std::cerr<<"program "<<i<<" cpu="<<cpu[i];
        if(i<gpu.fitness.size())std::cerr<<" gpu="<<gpu.fitness[i];
        if(i<gpu.case_counts.size())std::cerr<<" cases="<<gpu.case_counts[i][0]<<" errors="<<gpu.case_counts[i][1]<<" timeouts="<<gpu.case_counts[i][2];
        std::cerr<<'\n';
      }
      return false;
    }
    if(!check((gpu.execution_profile.find("-global-arena")!=std::string::npos)==(global[0]=='1'),
        "global payload flag did not select the expected executor"))return false;
    if(!check(gpu.case_counts[0][0]==513 && gpu.case_counts[1][0]==513,"replay diagnostics double-count cases"))return false;
    if(limit==65536) {
      if(!check(gpu.payload_replay_programs==1 && gpu.payload_replay_cases==513 && gpu.payload_replay_launches==1,
          "physical overflow must replay only the affected program"))return false;
      if(!check(gpu.case_counts[0][1]==0 && gpu.case_counts[1][1]==0,"successful replay retained physical errors"))return false;
    } else if(!check(gpu.payload_replay_programs==0 && gpu.case_counts[0][1]==513,
        "logical allocation errors must not trigger physical replay"))return false;
  }
  return true;
}

bool test_global_payload_program_queue() {
  ScopedEnvironment restore("GAGP_GPU_GLOBAL_PAYLOAD");setenv("GAGP_GPU_GLOBAL_PAYLOAD","1",1);
  // More programs than bounded physical blocks, no regions, distinct rows.
  // This exercises the persistent queue, mixed payload/scalar programs and the
  // host overlap callback while retaining the same logical case reduction.
  std::vector<BytecodeProgram> programs(96);
  for(std::size_t i=0;i<programs.size();++i) {
    programs[i].consts={Value::from_int(i%11)};
    programs[i].code={ins_a(Opcode::PushConst,0),ins(Opcode::Return)};
    programs[i].instruction_fuel={0,0};
  }
  programs[0].code.insert(programs[0].code.begin()+1,
      ins_ab(Opcode::CallBuiltin,static_cast<int>(BuiltinId::Singleton),1));
  programs[0].instruction_fuel={0,0,0};
  std::vector<CaseBindings> cases(33);std::vector<Value> answers;
  for(int i=0;i<33;++i)answers.push_back(Value::from_int(i%7));
  FitnessPolicy policy{FitnessMetric::Absolute,FitnessProximity::Discounted};
  policy.list_allocation_limit=65536;
  const auto cpu=eval_fitness_cpu(programs,cases,answers,1000,1000.0,512,policy);
  FitnessSessionGpu session;
  if(!check(session.init(cases,answers,1000,512,1000.0,policy).ok,"global payload queue init"))return false;
  bool callback=false;
  const auto gpu=session.eval_programs(programs,true,[&]{callback=true;});
  if(!check(gpu.ok && gpu.fitness==cpu && callback,"global payload mixed program queue/overlap parity")) {
    std::cerr<<gpu.err.message<<'\n';return false;
  }
  if(!check(gpu.execution_profile.find("-global-arena")!=std::string::npos && gpu.payload_replay_programs==0,
      "global initial payload executor selection"))return false;
  for(const auto& count:gpu.case_counts)
    if(!check(count[0]==33 && count[1]==0,"global payload queue case ownership"))return false;
  return true;
}

bool test_extended_payload() {
  int* failures = nullptr;
  if (cudaMallocManaged(&failures, sizeof(int)) != cudaSuccess) return false;
  ExtendedPayloadState* arena = nullptr;
  if (cudaMallocManaged(&arena, sizeof(ExtendedPayloadState)) != cudaSuccess) {
    cudaFree(failures);
    return false;
  }
  // Keep test-kernel optimization bounded; the fitness tests below exercise the
  // production executor's local arena. These builtin checks use identical state.
  new (arena) ExtendedPayloadState{};
  *failures = -1;
  inspect_extended_payload<<<1, 1>>>(arena, failures);
  const auto status = cudaDeviceSynchronize();
  const bool passed = status == cudaSuccess && *failures == 0;
  if (!passed) std::cerr << "extended payload error " << *failures << '\n';
  cudaFree(arena);
  cudaFree(failures);
  return passed;
}

int main() {
  // Session initialization selects the least-used visible device before raw kernels.
  FitnessSessionGpu selection;
  if (!selection.init({{}}, {Value::from_int(0)}, 1, 32, 1.0).ok) return 1;
  if (!test_extended_payload() || !test_private_list_index() || !test_extended_list_fitness() || !test_payload_replay_fitness() || !test_global_payload_program_queue() ||
      !test_subordinate_memo_coordinate_fitness() || !test_larger_memo_fitness()) return 1;
  std::cout << "gagp_test_extended_payload_gpu: OK\n";
}
