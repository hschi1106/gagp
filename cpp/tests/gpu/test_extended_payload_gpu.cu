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

bool test_tiled_replay_entry_growth() {
  ScopedEnvironment restore_tiles("GAGP_GPU_TILED_REPLAY"),
      restore_global("GAGP_GPU_GLOBAL_PAYLOAD"), restore_index("GAGP_GPU_PAYLOAD_INDEX");
  setenv("GAGP_GPU_TILED_REPLAY","1",1);
  setenv("GAGP_GPU_GLOBAL_PAYLOAD","0",1);
  setenv("GAGP_GPU_PAYLOAD_INDEX","1",1);
  // Each iteration replaces local 1, but all distinct singleton payload entries
  // remain resident for this invocation. 5000 entries exceed replay's initial
  // 4096 entries while 5000 cells fit its 65536-cell arena and CPU staging.
  BytecodeProgram growing;
  growing.n_locals=2;
  growing.consts={Value::from_int(0),Value::from_int(1),Value::from_int(5000)};
  growing.code={ins_a(Opcode::PushConst,0),ins_a(Opcode::Store,0),
      ins_a(Opcode::Load,0),
      ins_ab(Opcode::CallBuiltin,static_cast<int>(BuiltinId::Singleton),1),
      ins_a(Opcode::Store,1),ins_a(Opcode::Load,0),ins_a(Opcode::PushConst,1),
      ins(Opcode::Add),ins_a(Opcode::Store,0),ins_a(Opcode::Load,0),
      ins_a(Opcode::PushConst,2),ins(Opcode::Lt),ins_a(Opcode::JmpIfTrue,2),
      ins_a(Opcode::Load,1),ins(Opcode::Return)};
  growing.instruction_fuel.assign(growing.code.size(),1);
  const auto answer=payload::make_int_list_value({Value::from_int(4999)});
  BytecodeProgram unaffected;
  unaffected.consts={answer};
  unaffected.code={ins_a(Opcode::PushConst,0),ins(Opcode::Return)};
  unaffected.instruction_fuel.assign(unaffected.code.size(),1);
  const std::vector<BytecodeProgram> programs{growing,unaffected};
  const std::vector<CaseBindings> cases(3);
  const std::vector<Value> answers(3,answer);
  FitnessPolicy policy{FitnessMetric::Levenshtein};
  policy.list_allocation_limit=65536;
  std::vector<CpuFitnessCaseCounts> cpu_counts;
  const auto cpu=eval_fitness_cpu(programs,cases,answers,100000,1000,33,policy,nullptr,&cpu_counts);
  FitnessSessionGpu session;
  if(!check(session.init(cases,answers,100000,33,1000,policy).ok,"entry growth session init"))return false;
  const auto gpu=session.eval_programs(programs,true);
  if(!check(gpu.ok && gpu.fitness==cpu && cpu==std::vector<double>({0,0}),
      "entry growth replay changed exact result")) {
    std::cerr<<gpu.err.message<<'\n';return false;
  }
  if(!check(gpu.payload_replay_programs==1 && gpu.payload_replay_launches>=2 &&
      gpu.payload_replay_cases==3*gpu.payload_replay_launches,
      "entry capacity growth must retry only the overflowing program"))return false;
  for(std::size_t p=0;p<programs.size();++p)
    if(!check(cpu_counts[p].cases==3 && cpu_counts[p].errors==0 &&
        gpu.case_counts[p][0]==cpu_counts[p].cases &&
        gpu.case_counts[p][1]==cpu_counts[p].errors &&
        gpu.case_counts[p][2]==cpu_counts[p].timeouts,
        "entry growth retries retained errors or double-counted cases"))return false;
  return true;
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
  ScopedEnvironment restore_index("GAGP_GPU_PAYLOAD_INDEX"),restore_global("GAGP_GPU_GLOBAL_PAYLOAD"),restore_tiles("GAGP_GPU_TILED_REPLAY");
  for(const char* global:{"0","1"}) for(const char* indexed:{"0","1"}) for(unsigned limit:{65536u,100u}) for(const char* tiled:{"0","1"}) for(int lanes:{33,512}) {
    if(tiled[0]=='0' && lanes==33)continue;
    setenv("GAGP_GPU_TILED_REPLAY",tiled,1);
    setenv("GAGP_GPU_GLOBAL_PAYLOAD",global,1);
    setenv("GAGP_GPU_PAYLOAD_INDEX",indexed,1);
    FitnessPolicy policy{FitnessMetric::Levenshtein,FitnessProximity::Discounted};
    policy.list_allocation_limit=limit;
    const auto cpu=eval_fitness_cpu({program,short_program,program},cases,answers,10000,1000.0,lanes,policy);
    FitnessSessionGpu session;
    if(!check(session.init(cases,answers,10000,lanes,1000.0,policy).ok,"replay session init"))return false;
    const auto gpu=session.eval_programs({program,short_program,program},true);
    if(!check(gpu.ok && gpu.fitness==cpu,"GPU arena replay differs from CPU logical-lane reduction")) {
      std::cerr<<"tiled="<<tiled<<" global="<<global<<" index="<<indexed<<" limit="<<limit<<" ok="<<gpu.ok<<" error="<<gpu.err.message
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
    if(!check(gpu.case_counts[0][0]==513 && gpu.case_counts[1][0]==513 && gpu.case_counts[2][0]==513,"replay diagnostics double-count cases"))return false;
    if(!check(gpu.timing.initial_kernel_wall_ms>0 &&
        (gpu.timing.replay_kernel_event_ms>0)==(limit==65536),
        "initial/replay timing must track launched work"))return false;
    if(limit==65536) {
      if(!check(gpu.payload_replay_programs==2 && gpu.payload_replay_cases==1026 && gpu.payload_replay_launches==1,
          "physical overflow must replay only the affected program"))return false;
      if(!check(gpu.case_counts[0][1]==0 && gpu.case_counts[1][1]==0 && gpu.case_counts[2][1]==0,"successful replay retained physical errors"))return false;
    } else if(!check(gpu.payload_replay_programs==0 && gpu.case_counts[0][1]==513 && gpu.case_counts[2][1]==513,
        "logical allocation errors must not trigger physical replay"))return false;
  }
  return true;
}

BytecodeProgram arena_window_program(RegionPhase base, RegionPhase combine, bool carry=false) {
  BoundedRegionSegment segment;
  auto& plan=segment.plan;
  plan.progress=RegionProgressKind::SequenceWindows;
  plan.state_types={ValueTag::IntList};
  if(carry)plan.state_types.push_back(ValueTag::Int);
  plan.result_type=ValueTag::IntList;
  plan.sequence_state=0;
  plan.preparations={{ValueTag::Int,RegionPreparationKind::InteriorCut}};
  plan.memoized=false;
  plan.limits={32,0,1};
  RegionStateTransition left,right,copy;
  left.kind=right.kind=RegionTransitionKind::SequenceWindow;
  left.source_state=right.source_state=0;
  left.window={{WindowEndpointKind::Begin,0},{WindowEndpointKind::InteriorCut,0}};
  right.window={{WindowEndpointKind::InteriorCut,0},{WindowEndpointKind::End,0}};
  plan.requests={{{left}},{{right}}};
  if(carry) {
    copy.kind=RegionTransitionKind::CopyState;copy.source_state=1;
    for(auto& request:plan.requests)request.states.push_back(copy);
    plan.requests[1].states[1]=RegionStateTransition{};
    plan.requests[1].states[1].kind=RegionTransitionKind::Expression;
    plan.requests[1].states[1].expression=0;
    plan.request_expression_types={ValueTag::Int};
    segment.request_expressions={load_phase(RegionSlotBank::State,1)};
  }
  segment.base_predicate=constant_phase(Value::from_bool(false));
  segment.preparations={constant_phase(Value::from_int(1))};
  segment.base_body=std::move(base);segment.combine=std::move(combine);
  BytecodeProgram program;
  program.n_locals=1;
  program.code={ins_a(Opcode::Load,0)};
  if(carry) {
    program.consts={Value::from_int(INT64_MIN)};
    program.code.push_back(ins_a(Opcode::PushConst,0));
  }
  program.code.push_back(ins_a(Opcode::BoundedRegion,0));
  program.code.push_back(ins(Opcode::Return));
  program.instruction_fuel.assign(program.code.size(),0);
  program.instruction_fuel[program.code.size()-2]=1;
  program.bounded_region_segments.push_back(std::move(segment));
  return program;
}

bool test_arena_window_executor() {
  ScopedEnvironment restore_window("GAGP_ARENA_WINDOW_EXECUTOR"),
      restore_global("GAGP_GPU_GLOBAL_PAYLOAD"),restore_compact("GAGP_COMPACT_FRAMES"),
      restore_index("GAGP_GPU_PAYLOAD_INDEX"),restore_tiled("GAGP_GPU_TILED_REPLAY");
  setenv("GAGP_GPU_GLOBAL_PAYLOAD","1",1);
  setenv("GAGP_COMPACT_FRAMES","1",1);
  setenv("GAGP_GPU_PAYLOAD_INDEX","1",1);
  setenv("GAGP_GPU_TILED_REPLAY","1",1);
  const auto concat=phase({},
      {ins_a(Opcode::Load,0),ins_a(Opcode::Load,1),
       ins_ab(Opcode::CallBuiltin,static_cast<int>(BuiltinId::Concat),2),ins(Opcode::Return)},
      {{slot(RegionSlotBank::Result,0),0},{slot(RegionSlotBank::Result,1),1}},2);
  const auto identity=arena_window_program(load_phase(RegionSlotBank::State,0),concat);
  const auto carried=arena_window_program(load_phase(RegionSlotBank::State,0),concat,true);
  const auto missing=Value::from_int_list_hash_len(928374651,1);
  const auto fallback_base=phase({missing,Value::from_int(7)},
      {ins_a(Opcode::PushConst,0),ins_a(Opcode::PushConst,1),
       ins_ab(Opcode::CallBuiltin,static_cast<int>(BuiltinId::Singleton),1),
       ins_ab(Opcode::CallBuiltin,static_cast<int>(BuiltinId::Concat),2),ins(Opcode::Return)});
  const auto fallback=arena_window_program(fallback_base,load_phase(RegionSlotBank::Result,0));
  const auto empty=payload::make_int_list_value({});
  const auto fallback_index_base=phase({missing,Value::from_int(0)},
      {ins_a(Opcode::PushConst,0),ins_a(Opcode::PushConst,1),
       ins_ab(Opcode::CallBuiltin,static_cast<int>(BuiltinId::Index),2),
       ins_ab(Opcode::CallBuiltin,static_cast<int>(BuiltinId::Singleton),1),ins(Opcode::Return)});
  const auto wrong_tag=arena_window_program(fallback_index_base,concat);
  auto frame_limited=identity;frame_limited.bounded_region_segments[0].plan.limits.frames=1;
  const auto one=payload::make_int_list_value({Value::from_int(INT64_MAX)});
  const auto four=payload::make_int_list_value({Value::from_int(1),Value::from_int(2),Value::from_int(3),Value::from_int(4)});
  const std::vector<CaseBindings> cases{{{0,empty}},{{0,one}},{{0,four}}};
  const std::vector<Value> answers{empty,one,four};
  FitnessPolicy policy{FitnessMetric::Exact};policy.list_allocation_limit=65536;
  const std::vector<BytecodeProgram> programs{identity,carried,fallback,wrong_tag,frame_limited};
  // Phases have zero instruction fuel; root/each recursive entry cost one.
  // 0..10 spans every fuel boundary of the four-element (seven-entry) tree.
  for(int fuel=0;fuel<=10;++fuel) for(const char* enabled:{"0","1"}) {
    setenv("GAGP_ARENA_WINDOW_EXECUTOR",enabled,1);
    std::vector<CpuFitnessCaseCounts> counts;
    const auto cpu=eval_fitness_cpu(programs,cases,answers,fuel,1000,33,policy,nullptr,&counts);
    FitnessSessionGpu session;
    if(!check(session.init(cases,answers,fuel,33,1000,policy).ok,"arena window fuel init"))return false;
    const auto gpu=session.eval_programs(programs,true);
    if(!check(gpu.ok && gpu.fitness==cpu,"arena window list/fallback/fuel parity")) {
      std::cerr<<gpu.err.message<<" fuel="<<fuel<<" enabled="<<enabled<<'\n';return false;
    }
    if(!check((gpu.execution_profile.find("-arena-window")!=std::string::npos)==(enabled[0]=='1'),
        "arena window opt-in selection"))return false;
    for(std::size_t p=0;p<programs.size();++p)
      if(!check(gpu.case_counts[p][0]==counts[p].cases && gpu.case_counts[p][1]==counts[p].errors &&
          gpu.case_counts[p][2]==counts[p].timeouts,"arena window status parity"))return false;
    if(fuel==10 && !check(gpu.case_counts[2][3]==0 && gpu.case_counts[2][1]==0 && gpu.case_counts[3][1]==3 &&
        gpu.case_counts[4][2]==1,"arena window fallback/type/frame paths not exercised"))return false;
  }
  // Large constant leaves force physical arena replay; a small logical budget
  // instead produces the same semantic error on CPU and GPU.
  std::vector<Value> repeated(3000,Value::from_int(7));
  const auto large=arena_window_program(constant_phase(payload::make_int_list_value(repeated)),concat);
  repeated.resize(12000,Value::from_int(7));
  const std::vector<CaseBindings> large_cases{{{0,four}}};
  const std::vector<Value> large_answers{payload::make_int_list_value(repeated)};
  for(unsigned limit:{65536u,8u}) for(const char* enabled:{"0","1"}) {
    policy.list_allocation_limit=limit;
    setenv("GAGP_ARENA_WINDOW_EXECUTOR",enabled,1);
    std::vector<CpuFitnessCaseCounts> counts;
    const auto cpu=eval_fitness_cpu({large},large_cases,large_answers,100,1000,33,policy,nullptr,&counts);
    FitnessSessionGpu session;
    if(!check(session.init(large_cases,large_answers,100,33,1000,policy).ok,"arena window resource init"))return false;
    const auto gpu=session.eval_programs({large},true);
    if(!check(gpu.ok && gpu.fitness==cpu && gpu.case_counts[0][0]==1 &&
        gpu.case_counts[0][1]==counts[0].errors && gpu.case_counts[0][2]==counts[0].timeouts &&
        (limit==65536 ? gpu.payload_replay_programs==1 && cpu[0]==0 : gpu.payload_replay_programs==0 && counts[0].errors==1),
        "arena window replay/logical resource parity"))return false;
  }
  // A population with an unsupported request shape must retain generic routing.
  // Use a valid coordinate segment as the ineligible peer instead.
  const auto peer=invocation(unary_segment(ValueTag::IntList,constant_phase(empty),
      load_phase(RegionSlotBank::Result,0)),1);
  setenv("GAGP_ARENA_WINDOW_EXECUTOR","1",1);
  policy.list_allocation_limit=65536;
  FitnessSessionGpu mixed;
  if(!check(mixed.init(cases,answers,100,33,1000,policy).ok,"arena window mixed init"))return false;
  const auto gpu=mixed.eval_programs({identity,peer},true);
  return check(gpu.ok && gpu.fitness==eval_fitness_cpu({identity,peer},cases,answers,100,1000,33,policy) &&
      gpu.execution_profile.find("-arena-window")==std::string::npos,"mixed unsupported layout entered arena window executor");
}

bool test_global_window_aliases() {
  ScopedEnvironment restore_global("GAGP_GPU_GLOBAL_PAYLOAD"),restore_aliases("GAGP_GPU_GLOBAL_WINDOW_ALIASES");
  setenv("GAGP_GPU_GLOBAL_PAYLOAD","1",1);
  std::vector<Value> values;for(int i=0;i<200;++i)values.push_back(Value::from_int(i-100));
  const auto source=payload::make_int_list_value(values);
  BytecodeProgram program;program.n_locals=1;
  program.consts={source,Value::from_int(0),Value::from_int(200)};
  for(int i=0;i<50;++i) {
    program.code.push_back(ins_a(Opcode::PushConst,0));
    program.code.push_back(ins_a(Opcode::PushConst,1));
    program.code.push_back(ins_a(Opcode::PushConst,2));
    program.code.push_back(ins_ab(Opcode::CallBuiltin,static_cast<int>(BuiltinId::Slice),3));
    program.code.push_back(ins_a(Opcode::Store,0));
  }
  program.code.push_back(ins_a(Opcode::Load,0));program.code.push_back(ins(Opcode::Return));
  program.instruction_fuel.assign(program.code.size(),0);
  std::vector<CaseBindings> cases(33);std::vector<Value> answers(33,source);
  FitnessPolicy policy{FitnessMetric::Exact}; // No logical budget activates aliases implicitly.
  const auto cpu=eval_fitness_cpu({program},cases,answers,1000,1000.0,512,policy);
  for(const char* aliases:{"0","1"}) {
    setenv("GAGP_GPU_GLOBAL_WINDOW_ALIASES",aliases,1);
    FitnessSessionGpu session;
    if(!check(session.init(cases,answers,1000,512,1000.0,policy).ok,"global aliases init"))return false;
    const auto gpu=session.eval_programs({program},true);
    if(!check(gpu.ok && gpu.fitness==cpu && cpu[0]==0 && gpu.case_counts[0][1]==0,
        "global window aliases changed exact list output"))return false;
    if(!check(gpu.payload_replay_programs==(aliases[0]=='1'?0u:1u),
        "logical-zero window aliases did not avoid physical copying"))return false;
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

__global__ void inspect_fused_concat(int* failure) {
  using namespace gpu_detail;
  *failure=0;
  for(int type=0;type<3;++type) for(int scenario=0;scenario<11;++scenario) {
    const ValueTag tag=type==0?ValueTag::IntList:type==1?ValueTag::FloatList:ValueTag::StringList;
    Value source[3];
    if(type==0) {source[0]=Value::from_int(9007199254740993LL);source[1]=Value::from_int(-3);source[2]=Value::from_int(7);}
    else if(type==1) {source[0]=Value::from_float(-0.0);source[1]=Value::from_float(1.25);source[2]=Value::from_float(0);source[2].i=0x7ff8000000000042LL;}
    else {source[0]=Value::from_string_hash_len(17,2);source[1]=Value::from_string_hash_len(23,1);source[2]=Value::from_string_hash_len(31,3);}
    const Value a=d_make_list_hash_len(tag,d_hash_list_payload(source,2),2);
    const Value b=d_make_list_hash_len(tag,d_hash_list_payload(source+2,1),1);
    const Value empty=d_make_list_hash_len(tag,d_hash_list_payload(nullptr,0),0);
    DListPayloadEntry external[3]={{tag,empty.i,0,0},{tag,b.i,2,1},{tag,a.i,0,2}};
    DPayloadTables tables{};tables.list_entries=external;tables.list_entry_count=3;tables.list_values=source;
    DListPayloadEntry entries[2][16]{};Value cells[2][16]{};
    DArenaPayloadState states[2];Value outputs[2];ErrCode errors[2];bool success[2];
    for(int variant=0;variant<2;++variant) {
      auto& state=states[variant];state.list_entries=entries[variant];state.list_values=cells[variant];
      state.list_entry_capacity=scenario==5?0:16;state.kMaxListValues=scenario==4?2:16;
      state.logical_list_limit=scenario==8?0:scenario==6?2:3;
      state.fused_list_concat=variant!=0;
      if(scenario==9) {
        // Two colliding source registrations must still select the first.
        cells[variant][0]=source[2];cells[variant][1]=source[1];cells[variant][2]=source[0];
        entries[variant][0]={tag,a.i,0,2};entries[variant][1]={tag,a.i,1,2};
        state.list_entry_count=2;state.list_values_used=3;
      }
      Value args[2]={a,b};
      if(scenario==1)args[0]=empty;
      if(scenario==2)args[1]=empty;
      if(scenario==3)args[0]=d_make_list_hash_len(tag,12345,2);
      if(scenario==10)args[1]=Value::from_int(3);
      outputs[variant]=Value::invalid();errors[variant]=ErrCode::Value;
      success[variant]=d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Concat,args,2,tables,state,outputs[variant],errors[variant]);
    }
    bool equal=success[0]==success[1] && errors[0]==errors[1] && outputs[0].tag==outputs[1].tag && outputs[0].i==outputs[1].i;
    equal &= states[0].list_values_used==states[1].list_values_used && states[0].list_entry_count==states[1].list_entry_count &&
        states[0].logical_list_used==states[1].logical_list_used && states[0].physical_capacity_exceeded==states[1].physical_capacity_exceeded;
    for(int i=0;i<states[0].list_values_used;++i)
      equal &= cells[0][i].tag==cells[1][i].tag && cells[0][i].i==cells[1][i].i && cells[0][i].b==cells[1][i].b;
    for(int i=0;i<states[0].list_entry_count;++i)
      equal &= entries[0][i].tag==entries[1][i].tag && entries[0][i].packed==entries[1][i].packed &&
          entries[0][i].offset==entries[1][i].offset && entries[0][i].len==entries[1][i].len;
    if(!equal){*failure=type*100+scenario+1;return;}
  }
}

bool test_fused_concat() {
  int* failure=nullptr;
  if(cudaMallocManaged(&failure,sizeof(int))!=cudaSuccess)return false;
  *failure=-1;inspect_fused_concat<<<1,1>>>(failure);
  const auto status=cudaDeviceSynchronize();
  const bool passed=status==cudaSuccess && *failure==0;
  if(!passed)std::cerr<<"fused concat parity failure "<<*failure<<'\n';
  cudaFree(failure);
  if(!passed)return false;
  ScopedEnvironment restore("GAGP_GPU_FUSED_LIST_CONCAT");setenv("GAGP_GPU_FUSED_LIST_CONCAT","1",1);
  return test_extended_payload() && test_extended_list_fitness();
}

int main() {
  // Session initialization selects the least-used visible device before raw kernels.
  FitnessSessionGpu selection;
  if (!selection.init({{}}, {Value::from_int(0)}, 1, 32, 1.0).ok) return 1;
  if (!test_fused_concat() || !test_extended_payload() || !test_private_list_index() || !test_extended_list_fitness() || !test_payload_replay_fitness() || !test_tiled_replay_entry_growth() || !test_global_payload_program_queue() || !test_global_window_aliases() || !test_arena_window_executor() ||
      !test_subordinate_memo_coordinate_fitness() || !test_larger_memo_fitness()) return 1;
  std::cout << "gagp_test_extended_payload_gpu: OK\n";
}
