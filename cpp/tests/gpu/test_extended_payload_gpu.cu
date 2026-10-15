#include <cuda_runtime.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
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
  // Scalar-region rollback removes aliases and duplicates while retaining
  // every surviving probe chain and the first identity of each entry.
  d_restore_local_list_entries(*state,800);
  if(d_find_local_list_entry(*state,alias)>=0){*failures=7;return;}
  for(int i=0;i<800;++i) {
    const auto token=d_make_list_hash_len(ValueTag::IntList,i+1,1);
    if(d_find_local_list_entry(*state,token)!=i){*failures=8;return;}
  }
  // Reusing a discarded entry must not resurrect its old key or cause logical
  // deduplication to omit a new registration.
  if(!d_register_local_list(*state,alias,-1,1) || state->list_entry_count!=801){*failures=9;return;}
  d_restore_local_list_entries(*state,0);
  if(d_find_local_list_entry(*state,first)>=0 || d_find_local_list_entry(*state,alias)>=0){*failures=10;return;}

}

bool test_scalar_region_index_rollback() {
  // A scalar region creates [7], then drops its payload. Recreate [7] outside
  // the region and create [9] before reading it. A stale index wrongly dedups
  // the recreated [7], then lets [9] overwrite its uncounted registry entry.
  auto base=phase({Value::from_int(7)},
      {ins_a(Opcode::PushConst,0),
       ins_ab(Opcode::CallBuiltin,static_cast<int>(BuiltinId::Singleton),1),
       ins_ab(Opcode::CallBuiltin,static_cast<int>(BuiltinId::Len),1),ins(Opcode::Return)});
  auto program=invocation(unary_segment(ValueTag::Int,std::move(base),
      constant_phase(Value::from_int(0))),0);
  program.consts={Value::from_int(0),Value::from_int(7),Value::from_int(9)};
  program.n_locals=2;
  program.code={ins_a(Opcode::PushConst,0),ins_a(Opcode::BoundedRegion,0),ins_a(Opcode::Store,1),
      ins_a(Opcode::PushConst,1),ins_ab(Opcode::CallBuiltin,static_cast<int>(BuiltinId::Singleton),1),ins_a(Opcode::Store,0),
      ins_a(Opcode::PushConst,2),ins_ab(Opcode::CallBuiltin,static_cast<int>(BuiltinId::Singleton),1),ins_a(Opcode::Store,1),
      ins_a(Opcode::Load,0),ins_a(Opcode::PushConst,0),
      ins_ab(Opcode::CallBuiltin,static_cast<int>(BuiltinId::Index),2),ins(Opcode::Return)};
  program.instruction_fuel.assign(program.code.size(),1);
  ScopedEnvironment restore_global("GAGP_GPU_GLOBAL_PAYLOAD"),restore_index("GAGP_GPU_PAYLOAD_INDEX");
  setenv("GAGP_GPU_GLOBAL_PAYLOAD","1",1);
  std::vector<CaseBindings> cases(33);
  std::vector<Value> answers(33,Value::from_int(7));
  FitnessPolicy policy;policy.list_allocation_limit=65536;
  std::vector<CpuFitnessCaseCounts> cpu_counts;
  const auto cpu=eval_fitness_cpu({program},cases,answers,1000,1000.0,64,policy,nullptr,&cpu_counts);
  for(const char* indexed:{"0","1"}) {
    setenv("GAGP_GPU_PAYLOAD_INDEX",indexed,1);
    FitnessSessionGpu session;
    if(!check(session.init(cases,answers,1000,64,1000.0,policy).ok,"scalar rollback session init"))return false;
    const auto gpu=session.eval_programs({program},true);
    if(!check(gpu.ok && gpu.fitness==cpu && cpu_counts[0].errors==0 &&
        gpu.case_counts[0][0]==33 && gpu.case_counts[0][1]==0 && gpu.case_counts[0][2]==0,
        std::string("scalar region index rollback parity index=")+indexed))return false;
  }
  return true;
}

bool test_small_int_hash_fitness() {
  ScopedEnvironment restore_hash("GAGP_GPU_SMALL_INT_HASH"),restore_fused("GAGP_GPU_FUSED_LIST_CONCAT");
  setenv("GAGP_GPU_FUSED_LIST_CONCAT","1",1);
  for(const char* enabled:{"0","1"}) {
    setenv("GAGP_GPU_SMALL_INT_HASH",enabled,1);
    if(!test_extended_list_fitness())return false;
  }
  return true;
}

// Tiny persistent lane reused by successive invocations, including errors and wrap.
__global__ void inspect_epoch_list_index(int* failures) {
  using namespace gpu_detail;
  *failures=0;
  DListPayloadEntry entries[8];Value values[8];
  std::uint64_t words[16]{};unsigned counter=0;int legacy[16]{};
  for(int mode=0;mode<2;++mode) {
    const bool epoch_mode=mode!=0;
    counter=0;
    for(int i=0;i<16;++i)words[i]=0;
    for(int invocation=0;invocation<3;++invocation) {
      DIndexedArenaPayloadState st;
      st.list_entries=entries;st.list_values=values;st.list_entry_capacity=8;
      st.kMaxListValues=8;st.list_index_capacity=16;st.list_index=legacy;
      if(epoch_mode){st.epoch_index=words;st.epoch_counter=&counter;}
      if(invocation==2) {
        // Poison with the epoch we will reuse after unsigned wrap.
        for(int i=0;i<16;++i)words[i]=(std::uint64_t(1)<<32)|8;
        counter=~0u;
      }
      d_reset_private_list_index(st);
      const auto first=d_make_list_hash_len(ValueTag::IntList,1,1);
      Value collision;
      for(unsigned hash=2;;++hash) {
        collision=d_make_list_hash_len(ValueTag::IntList,hash,1);
        if(d_private_list_bucket(collision,16)==d_private_list_bucket(first,16))break;
      }
      if(d_find_local_list_entry(st,first)!=-1){*failures=1;return;}
      values[0]=Value::from_int(17);values[1]=Value::from_int(29);
      if(!d_register_local_list(st,first,0,1) ||
         !d_register_local_list(st,collision,1,1) ||
         !d_register_local_list(st,first,1,1) ||
         d_find_local_list_entry(st,first)!=0 || d_find_local_list_entry(st,collision)!=1) {
        *failures=2;return;
      }
      d_restore_local_list_entries(st,1);
      if(d_find_local_list_entry(st,first)!=0 || d_find_local_list_entry(st,collision)!=-1 ||
         !d_register_local_list(st,collision,1,1) || d_find_local_list_entry(st,collision)!=1) {
        *failures=3;return;
      }
      Value output;ErrCode error=ErrCode::Value;DPayloadTables tables;
      const Value args[]{first,Value::from_int(10)};
      if(d_builtin_call<DPayloadFlavor::IndexedArena>(BuiltinId::Index,args,2,tables,st,output,error)) {
        *failures=4;return;
      }
      // No successful-return cleanup: next case must still ignore these entries.
    }
  }
}

bool test_epoch_list_index() {
  int* failures=nullptr;
  if(cudaMallocManaged(&failures,sizeof(int))!=cudaSuccess)return false;
  *failures=-1;inspect_epoch_list_index<<<1,1>>>(failures);
  const auto status=cudaDeviceSynchronize();
  const bool passed=check(status==cudaSuccess && *failures==0,"epoch index reset/collision/rollback/wrap");
  cudaFree(failures);return passed;
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
      restore_global("GAGP_GPU_GLOBAL_PAYLOAD"), restore_index("GAGP_GPU_PAYLOAD_INDEX"),
      restore_diagnostics("GAGP_GPU_OVERFLOW_DIAGNOSTICS");
  setenv("GAGP_GPU_OVERFLOW_DIAGNOSTICS","1",1);
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
  // floor(3*lane/33) partitions place the three cases in lanes10,21,32.
  if(!check(gpu.initial_payload_overflow_cases==std::vector<unsigned>({3,0}) &&
      gpu.initial_payload_overflow_logical_lanes==33 &&
      gpu.initial_payload_overflow_lane_masks==std::vector<unsigned>({(1u<<10)|(1u<<21),1u,0u,0u}),
      "sparse initial overflow mask not preserved across capacity retries"))return false;
  if(!check(gpu.payload_replay_programs==1 && gpu.payload_replay_launches>=2 &&
      gpu.payload_replay_cases==3*gpu.payload_replay_launches,
      "entry capacity growth must retry only the overflowing program"))return false;
  if(std::getenv("GAGP_GPU_ADAPTIVE_REPLAY") && std::string(std::getenv("GAGP_GPU_ADAPTIVE_REPLAY"))!="0" &&
      !check(gpu.payload_replay_entry_capacity==8192 && gpu.payload_replay_value_capacity==65536 &&
          gpu.payload_replay_byte_capacity==8192,"entry exhaustion must grow only entry capacity"))return false;
  for(std::size_t p=0;p<programs.size();++p)
    if(!check(cpu_counts[p].cases==3 && cpu_counts[p].errors==0 &&
        gpu.case_counts[p][0]==cpu_counts[p].cases &&
        gpu.case_counts[p][1]==cpu_counts[p].errors &&
        gpu.case_counts[p][2]==cpu_counts[p].timeouts,
        "entry growth retries retained errors or double-counted cases"))return false;
  return true;
}

bool test_payload_replay_fitness() {
  ScopedEnvironment restore_adaptive("GAGP_GPU_ADAPTIVE_REPLAY");
  setenv("GAGP_GPU_ADAPTIVE_REPLAY","0",1);
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
  ScopedEnvironment restore_index("GAGP_GPU_PAYLOAD_INDEX"),restore_global("GAGP_GPU_GLOBAL_PAYLOAD"),restore_tiles("GAGP_GPU_TILED_REPLAY"),
      restore_diagnostics("GAGP_GPU_OVERFLOW_DIAGNOSTICS");
  setenv("GAGP_GPU_OVERFLOW_DIAGNOSTICS","1",1);
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
    const unsigned overflow_cases=limit==65536 ? 513u : 0u;
    if(!check(gpu.initial_payload_overflow_cases==std::vector<unsigned>({overflow_cases,0,overflow_cases}) &&
        gpu.initial_payload_overflow_logical_lanes==lanes,
        "initial overflow counts were lost or replaced by replay"))return false;
    const int words=(lanes+31)/32;
    for(int p=0;p<3;++p) for(int w=0;w<words;++w) {
      const int bits=std::min(32,lanes-w*32);
      const unsigned expected=(p!=1 && limit==65536) ? (bits==32 ? ~0u : (1u<<bits)-1u) : 0u;
      if(!check(gpu.initial_payload_overflow_lane_masks.at(p*words+w)==expected,
          "initial overflow lane masks changed on replay or misnumbered partial warp"))return false;
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

bool test_global_window_aliases() {
  ScopedEnvironment restore_global("GAGP_GPU_GLOBAL_PAYLOAD"),restore_aliases("GAGP_GPU_GLOBAL_WINDOW_ALIASES");
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
  for(const char* global:{"0","1"}) for(const char* aliases:{"0","1"}) {
    setenv("GAGP_GPU_GLOBAL_PAYLOAD",global,1);
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
  for(int i=-257;i<=256;++i) {
    const auto value=Value::from_int(i);
    if(d_hash_concat_element(value,true)!=d_hash_value_shallow(value) ||
        d_hash_concat_element(value,false)!=d_hash_value_shallow(value)){*failure=10000+i+257;return;}
  }
  unsigned long long bits=0x8000000000000000ULL;
  for(int sample=0;sample<128;++sample) {
    // Full-width signed extremes and deterministic random patterns, including
    // Float NaN/-zero bits, must retain the original tag-dependent hash.
    for(int tag=0;tag<=static_cast<int>(ValueTag::Invalid);++tag) {
      Value value=Value::from_int(static_cast<long long>(bits));
      value.tag=static_cast<ValueTag>(tag);value.b=(sample&1)!=0;
      if(d_hash_concat_element(value,true)!=d_hash_value_shallow(value)){*failure=11000+sample;return;}
    }
    bits=sample==0?0x7fffffffffffffffULL:bits*6364136223846793005ULL+1442695040888963407ULL;
  }
  for(int small_hash=0;small_hash<2;++small_hash) for(int type=0;type<3;++type) for(int scenario=0;scenario<11;++scenario) {
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
      state.small_int_hash=small_hash!=0;
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

__global__ void inspect_concat_cache(int* failure) {
  using namespace gpu_detail;
  *failure=0;
  for(int type=0;type<3;++type) for(int scenario=0;scenario<8;++scenario) for(int logical=0;logical<2;++logical) {
    const auto tag=type==0?ValueTag::IntList:type==1?ValueTag::FloatList:ValueTag::StringList;
    Value source[4];
    for(int i=0;i<4;++i)source[i]=type==0?Value::from_int(7+i):type==1?
        Value::from_float(i*0.25):Value::from_string_hash_len(41+i,i);
    const Value a=d_make_list_hash_len(tag,101,1),b=d_make_list_hash_len(tag,102,1);
    DListPayloadEntry globals[2]={{tag,a.i,0,1},{tag,b.i,1,1}};
    DPayloadTables tables{};tables.list_entries=globals;tables.list_entry_count=2;tables.list_values=source;
    DListPayloadEntry entries[2][32]{};Value cells[2][32]{};
    DArenaPayloadState states[2];DConcatCacheEntry cache[DCONCAT_CACHE_SLOTS]{};
    for(int variant=0;variant<2;++variant) {
      auto& st=states[variant];st.list_entries=entries[variant];st.list_values=cells[variant];
      st.list_entry_capacity=32;st.kMaxListValues=32;st.logical_list_limit=logical?65536:0;
      st.fused_list_concat=true;st.concat_cache=variant?cache:nullptr;st.concat_cache_diagnostics=true;
      // Force a canonical result collision: the first registry identity must
      // win, even though the cache retains the actual newly copied backing.
      if(scenario==1 || scenario==4) {
        const auto output=d_make_list_hash_len(tag,d_hash_list_payload(source,2),2);
        d_register_local_list(st,output,-3,2);
      }
    }
    for(int call=0;call<3;++call) {
      Value outputs[2];ErrCode errors[2]={ErrCode::Value,ErrCode::Value};bool ok[2];
      for(int variant=0;variant<2;++variant) {
        auto& st=states[variant];
        if(call==1) {
          if(scenario==2)st.list_entry_capacity=st.list_entry_count;
          if(scenario==3)st.kMaxListValues=st.list_values_used;
          if(scenario==4) {
            const auto old_count=st.list_entry_count;
            d_restore_local_list_storage(st,logical?old_count:1,0);
            if(variant && d_concat_cache_slot(st,source,1,source+1,1,tag)->tag!=ValueTag::Invalid){*failure=1;return;}
          }
          if(scenario==5) {
            d_restore_local_list_storage(st,0,0);
            cells[variant][0]=source[3];st.list_values_used=1;
            d_register_local_list(st,a,0,1); // Local shadow changes resolved key.
          }
          if(scenario==6 && variant) {
            auto* entry=d_concat_cache_slot(st,source,1,source+1,1,tag);
            entry->right=source+2;entry->hash=0; // Same bucket, unequal exact key.
          }
        }
        Value args[2]={a,b};
        if(scenario==7 && call==1)args[1]=Value::from_int(3);
        if(scenario==7 && call==2)args[1]=d_make_list_hash_len(tag,999,1);
        outputs[variant]=Value::invalid();
        ok[variant]=d_builtin_call<DPayloadFlavor::ArenaMixed>(BuiltinId::Concat,args,2,tables,st,outputs[variant],errors[variant]);
      }
      bool equal=ok[0]==ok[1] && errors[0]==errors[1] && outputs[0].tag==outputs[1].tag && outputs[0].i==outputs[1].i;
      equal &= states[0].list_entry_count==states[1].list_entry_count && states[0].list_values_used==states[1].list_values_used &&
          states[0].logical_list_used==states[1].logical_list_used &&
          states[0].physical_capacity_reason==states[1].physical_capacity_reason;
      if(ok[0]) {
        const Value *left=nullptr,*right=nullptr;int nl=0,nr=0;
        const bool pl=d_lookup_list_payload(tables,states[0],outputs[0],left,nl);
        const bool pr=d_lookup_list_payload(tables,states[1],outputs[1],right,nr);
        equal &= pl==pr && nl==nr;
        if(pl && pr)for(int i=0;i<nl;++i)equal &= left[i].tag==right[i].tag && left[i].i==right[i].i && left[i].b==right[i].b;
      }
      if(!equal){*failure=10+type*100+scenario*10+call;return;}
    }
    if(scenario==0 && (states[1].concat_hits!=2 || states[1].concat_misses!=1 || states[1].concat_cells!=4)) {*failure=1000;return;}
  }
}

bool test_concat_cache() {
  int* failure=nullptr;if(cudaMallocManaged(&failure,sizeof(int))!=cudaSuccess)return false;
  *failure=-1;inspect_concat_cache<<<1,1>>>(failure);
  const bool raw=cudaDeviceSynchronize()==cudaSuccess && check(*failure==0,"concat cache raw parity failure="+std::to_string(*failure));
  cudaFree(failure);if(!raw)return false;
  ScopedEnvironment restore_cache("GAGP_GPU_CONCAT_CACHE"),restore_diag("GAGP_GPU_CONCAT_CACHE_DIAGNOSTICS"),
      restore_fused("GAGP_GPU_FUSED_LIST_CONCAT"),restore_global("GAGP_GPU_GLOBAL_PAYLOAD");
  setenv("GAGP_GPU_FUSED_LIST_CONCAT","1",1);setenv("GAGP_GPU_GLOBAL_PAYLOAD","1",1);
  setenv("GAGP_GPU_CONCAT_CACHE_DIAGNOSTICS","1",1);
  const auto a=payload::make_int_list_value({Value::from_int(7)});
  const auto b=payload::make_int_list_value({Value::from_int(9)});
  const auto answer=payload::make_int_list_value({Value::from_int(7),Value::from_int(9)});
  BytecodeProgram program;program.consts={a,b};program.n_locals=1;
  for(int i=0;i<3;++i) {
    program.code.push_back(ins_a(Opcode::PushConst,0));program.code.push_back(ins_a(Opcode::PushConst,1));
    program.code.push_back(ins_ab(Opcode::CallBuiltin,static_cast<int>(BuiltinId::Concat),2));
    program.code.push_back(ins_a(Opcode::Store,0));
  }
  program.code.push_back(ins_a(Opcode::Load,0));program.code.push_back(ins(Opcode::Return));
  std::vector<CaseBindings> cases(513);std::vector<Value> answers(513,answer);
  FitnessPolicy policy{FitnessMetric::Levenshtein};policy.list_allocation_limit=65536;
  const auto cpu=eval_fitness_cpu({program},cases,answers,1000,1000,64,policy);
  for(const char* enabled:{"0","1"}) {
    setenv("GAGP_GPU_CONCAT_CACHE",enabled,1);
    FitnessSessionGpu session;
    if(!check(session.init(cases,answers,1000,64,1000,policy).ok,"concat cache session init"))return false;
    const auto gpu=session.eval_programs({program},true);
    if(!check(gpu.ok && gpu.fitness==cpu && gpu.case_counts[0][1]==0,"concat cache CPU fitness parity"))return false;
    const std::array<unsigned long long,3> expected=enabled[0]=='1'?
        std::array<unsigned long long,3>{1026,513,2052}:std::array<unsigned long long,3>{0,0,0};
    if(!check(gpu.concat_cache_stats==expected,"concat cache per-case lifetime/diagnostics"))return false;
  }
  // Repeated cached outputs still reserve cells and must hit the same physical
  // limit, then replay every case with unchanged semantic allocation charges.
  BytecodeProgram growing;growing.n_locals=2;
  growing.consts={a,b,Value::from_int(0),Value::from_int(1),Value::from_int(5000)};
  growing.code={ins_a(Opcode::PushConst,2),ins_a(Opcode::Store,0),
      ins_a(Opcode::PushConst,0),ins_a(Opcode::PushConst,1),
      ins_ab(Opcode::CallBuiltin,static_cast<int>(BuiltinId::Concat),2),ins_a(Opcode::Store,1),
      ins_a(Opcode::Load,0),ins_a(Opcode::PushConst,3),ins(Opcode::Add),ins_a(Opcode::Store,0),
      ins_a(Opcode::Load,0),ins_a(Opcode::PushConst,4),ins(Opcode::Lt),ins_a(Opcode::JmpIfTrue,2),
      ins_a(Opcode::Load,1),ins(Opcode::Return)};
  const std::vector<CaseBindings> few_cases(3);const std::vector<Value> few_answers(3,answer);
  const auto growing_cpu=eval_fitness_cpu({growing},few_cases,few_answers,100000,1000,64,policy);
  for(const char* enabled:{"0","1"}) {
    setenv("GAGP_GPU_CONCAT_CACHE",enabled,1);
    FitnessSessionGpu session;
    if(!check(session.init(few_cases,few_answers,100000,64,1000,policy).ok,"concat cache replay init"))return false;
    const auto gpu=session.eval_programs({growing},true);
    if(!check(gpu.ok && gpu.fitness==growing_cpu && gpu.payload_replay_programs==1 &&
        gpu.payload_replay_launches==1 && gpu.case_counts[0][1]==0,"concat cache preserves overflow/replay"))return false;
    if(enabled[0]=='1' && !check(gpu.concat_cache_stats[0]>0 && gpu.concat_cache_stats[1]>=6,
        "concat replay cache must restart per case/attempt"))return false;
  }
  return true;
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

__global__ void inspect_extended_aliases(gpu_detail::DExtendedPayloadState* state,int* failure) {
  using namespace gpu_detail;
  *failure=0;
  for(int type=0;type<3;++type) for(int local=0;local<2;++local)
  for(int scenario=0;scenario<4;++scenario) for(int aliases=0;aliases<2;++aliases) {
    const int test=1+(((type*2+local)*4+scenario)*2+aliases);
    const ValueTag tag=type==0?ValueTag::IntList:type==1?ValueTag::FloatList:ValueTag::StringList;
    Value elements[6];
    for(int i=0;i<6;++i) elements[i]=type==0?Value::from_int(i-3):type==1?
        Value::from_float(i*0.25-0.5):Value::from_string_hash_len(i+100,i);
    const Value source=d_make_list_hash_len(tag,d_hash_list_payload(elements,6),6);
    const Value empty=d_make_list_hash_len(tag,d_hash_list_payload(nullptr,0),0);
    DListPayloadEntry entries[2]={{tag,empty.i,0,0},{tag,source.i,0,6}};
    DPayloadTables tables{};tables.list_entries=entries;tables.list_entry_count=2;tables.list_values=elements;
    state->list_entry_count=0;state->list_values_used=0;state->list_entry_capacity=1024;
    state->logical_list_limit=0;state->logical_list_used=0;state->physical_capacity_exceeded=false;state->physical_capacity_reason=0;
    state->alias_windows=aliases!=0;state->fused_list_concat=false;
    Value value=source;ErrCode err=ErrCode::Value;
    if(local) {
      const Value concat[2]={source,empty};
      if(!d_builtin_call<DPayloadFlavor::ExtendedMixed>(BuiltinId::Concat,concat,2,tables,*state,value,err)) {*failure=test;return;}
    }
    if(scenario==1)state->list_values_used=8192;
    if(scenario==2)state->list_entry_capacity=state->list_entry_count;
    const Value slice[3]={value,Value::from_int(1),Value::from_int(scenario==3?1:5)};
    const bool ok=d_builtin_call<DPayloadFlavor::ExtendedMixed>(BuiltinId::Slice,slice,3,tables,*state,value,err);
    const bool should_fail=scenario==2 || (scenario==1 && !aliases);
    if(ok==should_fail || state->physical_capacity_exceeded!=should_fail || state->logical_list_used!=0) {*failure=test;return;}
    if(should_fail) {if(err!=ErrCode::Value){*failure=test;return;}continue;}
    const int first_len=scenario==3?0:4;
    if(value.i!=d_make_list_hash_len(tag,d_hash_list_payload(first_len?elements+1:nullptr,first_len),first_len).i) {*failure=test;return;}
    if(scenario==3)continue;
    const Value nested[3]={value,Value::from_int(1),Value::from_int(3)};
    if(!d_builtin_call<DPayloadFlavor::ExtendedMixed>(BuiltinId::Slice,nested,3,tables,*state,value,err) ||
        value.i!=d_make_list_hash_len(tag,d_hash_list_payload(elements+2,2),2).i) {*failure=test;return;}
    const Value* resolved=nullptr;int length=0;
    if(!d_lookup_list_payload(tables,*state,value,resolved,length) || length!=2 ||
        resolved[0].i!=elements[2].i || resolved[1].i!=elements[3].i) {*failure=test;return;}
    const int expected_used=scenario==1?8192:(local?6:0)+(aliases?0:6);
    if(state->list_values_used!=expected_used) {*failure=test;return;}
  }
}

bool test_extended_aliases() {
  gpu_detail::DExtendedPayloadState* state=nullptr;int* failure=nullptr;
  if(cudaMallocManaged(&state,sizeof(*state))!=cudaSuccess)return false;
  if(cudaMallocManaged(&failure,sizeof(int))!=cudaSuccess){cudaFree(state);return false;}
  *failure=-1;inspect_extended_aliases<<<1,1>>>(state,failure);
  const auto status=cudaDeviceSynchronize();
  const bool passed=status==cudaSuccess && *failure==0;
  if(!passed)std::cerr<<"extended nested aliases failure "<<*failure<<'\n';
  cudaFree(state);cudaFree(failure);return passed;
}

bool test_adaptive_combined_reasons() {
  ScopedEnvironment restore_adaptive("GAGP_GPU_ADAPTIVE_REPLAY"),restore_tiles("GAGP_GPU_TILED_REPLAY"),
      restore_global("GAGP_GPU_GLOBAL_PAYLOAD"),restore_index("GAGP_GPU_PAYLOAD_INDEX");
  setenv("GAGP_GPU_ADAPTIVE_REPLAY","1",1);
  setenv("GAGP_GPU_PAYLOAD_INDEX","1",1);
  BytecodeProgram entries;
  entries.n_locals=2;
  entries.consts={Value::from_int(0),Value::from_int(1),Value::from_int(5000)};
  entries.code={ins_a(Opcode::PushConst,0),ins_a(Opcode::Store,0),
      ins_a(Opcode::Load,0),ins_ab(Opcode::CallBuiltin,int(BuiltinId::Singleton),1),
      ins_a(Opcode::Store,1),ins_a(Opcode::Load,0),ins_a(Opcode::PushConst,1),
      ins(Opcode::Add),ins_a(Opcode::Store,0),ins_a(Opcode::Load,0),
      ins_a(Opcode::PushConst,2),ins(Opcode::Lt),ins_a(Opcode::JmpIfTrue,2),
      ins_a(Opcode::PushConst,0),ins(Opcode::Return)};
  entries.instruction_fuel.assign(entries.code.size(),1);
  BytecodeProgram values;
  values.n_locals=2;
  values.consts={Value::from_int(0),Value::from_int(1),Value::from_int(100),
      payload::make_int_list_value(std::vector<Value>(100,Value::from_int(7)))};
  values.code={ins_a(Opcode::PushConst,0),ins_a(Opcode::Store,0),
      ins_a(Opcode::PushConst,3),ins_a(Opcode::PushConst,3),
      ins_ab(Opcode::CallBuiltin,int(BuiltinId::Concat),2),ins_a(Opcode::Store,1),
      ins_a(Opcode::Load,0),ins_a(Opcode::PushConst,1),ins(Opcode::Add),ins_a(Opcode::Store,0),
      ins_a(Opcode::Load,0),ins_a(Opcode::PushConst,2),ins(Opcode::Lt),ins_a(Opcode::JmpIfTrue,2),
      ins_a(Opcode::PushConst,0),ins(Opcode::Return)};
  values.instruction_fuel.assign(values.code.size(),1);
  BytecodeProgram unaffected;
  unaffected.consts={Value::from_int(0)};
  unaffected.code={ins_a(Opcode::PushConst,0),ins(Opcode::Return)};
  unaffected.instruction_fuel.assign(unaffected.code.size(),1);
  const std::vector<BytecodeProgram> programs{entries,unaffected,values};
  const std::vector<CaseBindings> cases(3);
  const std::vector<Value> answers(3,Value::from_int(0));
  FitnessPolicy policy{FitnessMetric::Exact}; // Unbounded logical budget exercises 16K physical value growth.
  std::vector<CpuFitnessCaseCounts> counts;
  const auto cpu=eval_fitness_cpu(programs,cases,answers,100000,1000,33,policy,nullptr,&counts);
  // First replay: one program exhausts4096 entries, another exhausts16384
  // values. OR their independent reasons and grow both, but never byte capacity.
  for(const char* global:{"0","1"}) for(const char* tiled:{"0","1"}) {
    setenv("GAGP_GPU_GLOBAL_PAYLOAD",global,1);
    setenv("GAGP_GPU_TILED_REPLAY",tiled,1);
    FitnessSessionGpu session;
    if(!check(session.init(cases,answers,100000,33,1000,policy).ok,"combined adaptive init"))return false;
    const auto gpu=session.eval_programs(programs,true);
    if(!check(gpu.ok && gpu.fitness==cpu && cpu==std::vector<double>({0,0,0}),
        "combined adaptive exact fitness")) {std::cerr<<gpu.err.message<<'\n';return false;}
    if(!check(gpu.payload_replay_programs==2 && gpu.payload_replay_launches==2 &&
        gpu.payload_replay_cases==12 && gpu.payload_replay_entry_capacity==8192 &&
        gpu.payload_replay_value_capacity==32768 && gpu.payload_replay_byte_capacity==8192,
        "combined adaptive reasons or retry work accounting"))return false;
    if(!check(gpu.initial_payload_overflow_cases==std::vector<unsigned>({3,0,3}),
        "combined adaptive overwrote initial program diagnostics"))return false;
    for(std::size_t p=0;p<programs.size();++p)
      if(!check(counts[p].cases==3 && counts[p].errors==0 &&
          gpu.case_counts[p][0]==counts[p].cases && gpu.case_counts[p][1]==counts[p].errors &&
          gpu.case_counts[p][2]==counts[p].timeouts && gpu.case_counts[p][3]==0 && gpu.case_counts[p][4]==0,
          "combined adaptive retained failures or double-counted retries"))return false;
  }
  return true;
}

bool test_adaptive_replay() {
  ScopedEnvironment restore_adaptive("GAGP_GPU_ADAPTIVE_REPLAY"),restore_tiles("GAGP_GPU_TILED_REPLAY"),
      restore_global("GAGP_GPU_GLOBAL_PAYLOAD"),restore_index("GAGP_GPU_PAYLOAD_INDEX");
  setenv("GAGP_GPU_TILED_REPLAY","1",1);setenv("GAGP_GPU_GLOBAL_PAYLOAD","0",1);
  setenv("GAGP_GPU_PAYLOAD_INDEX","1",1);
  for(int strings=0;strings<2;++strings) for(int adaptive=0;adaptive<2;++adaptive) for(unsigned limit:{0u,65536u}) {
    // 100 independent concatenations consume 20000 cells or bytes, but only
    // 100 registrations. Zero-budget list replay must grow values; a
    // declared 65536-cell budget must provision sufficient values upfront.
    // String replay grows bytes independently.
    // Keep the old string route out of this focused test: it intentionally
    // doubles entries to65536 before growing its byte storage.
    if(strings && (!adaptive || limit))continue;
    setenv("GAGP_GPU_ADAPTIVE_REPLAY",adaptive?"1":"0",1);
    std::vector<Value> elements(100,Value::from_int(7));
    Value source=strings?payload::make_string_value(std::string(100,'a')):payload::make_int_list_value(elements);
    elements.resize(200,Value::from_int(7));
    Value answer=strings?payload::make_string_value(std::string(200,'a')):payload::make_int_list_value(elements);
    BytecodeProgram growing;growing.n_locals=2;
    growing.consts={Value::from_int(0),Value::from_int(1),Value::from_int(100),source};
    growing.code={ins_a(Opcode::PushConst,0),ins_a(Opcode::Store,0),
        ins_a(Opcode::PushConst,3),ins_a(Opcode::PushConst,3),
        ins_ab(Opcode::CallBuiltin,static_cast<int>(BuiltinId::Concat),2),ins_a(Opcode::Store,1),
        ins_a(Opcode::Load,0),ins_a(Opcode::PushConst,1),ins(Opcode::Add),ins_a(Opcode::Store,0),
        ins_a(Opcode::Load,0),ins_a(Opcode::PushConst,2),ins(Opcode::Lt),ins_a(Opcode::JmpIfTrue,2),
        ins_a(Opcode::Load,1),ins(Opcode::Return)};
    growing.instruction_fuel.assign(growing.code.size(),1);
    BytecodeProgram unaffected;unaffected.consts={answer};
    unaffected.code={ins_a(Opcode::PushConst,0),ins(Opcode::Return)};
    unaffected.instruction_fuel.assign(unaffected.code.size(),1);
    const std::vector<BytecodeProgram> programs{growing,unaffected};
    const std::vector<CaseBindings> cases(3);const std::vector<Value> answers(3,answer);
    FitnessPolicy policy{FitnessMetric::Levenshtein};policy.list_allocation_limit=limit;
    std::vector<CpuFitnessCaseCounts> cpu_counts;
    const auto cpu=eval_fitness_cpu(programs,cases,answers,10000,1000,33,policy,nullptr,&cpu_counts);
    FitnessSessionGpu session;
    if(!check(session.init(cases,answers,10000,33,1000,policy).ok,"adaptive replay init"))return false;
    const auto gpu=session.eval_programs(programs,true);
    if(!check(gpu.ok && gpu.fitness==cpu && cpu==std::vector<double>({0,0}),"adaptive replay exact fitness")) {
      std::cerr<<gpu.err.message<<'\n';return false;
    }
    const unsigned launches=adaptive?(strings?3:limit?1:2):1;
    if(!check(gpu.payload_replay_programs==1 && gpu.payload_replay_launches==launches &&
        gpu.payload_replay_cases==3*launches,"adaptive replay complete-pass accounting"))return false;
    if(adaptive && !check(gpu.payload_replay_entry_capacity==4096 &&
        gpu.payload_replay_value_capacity==(limit?static_cast<int>(limit):strings?16384:32768) &&
        gpu.payload_replay_byte_capacity==(strings?32768:8192),"adaptive replay grew an unexhausted capacity"))return false;
    for(std::size_t p=0;p<programs.size();++p)
      if(!check(gpu.case_counts[p][0]==cpu_counts[p].cases && gpu.case_counts[p][1]==cpu_counts[p].errors &&
          gpu.case_counts[p][2]==cpu_counts[p].timeouts,"adaptive replay case/status parity"))return false;
  }
  setenv("GAGP_GPU_ADAPTIVE_REPLAY","1",1);
  return test_tiled_replay_entry_growth();
}

bool test_initial_payload_capacity_replay() {
  ScopedEnvironment restore_global("GAGP_GPU_GLOBAL_PAYLOAD"),
      restore_tiles("GAGP_GPU_TILED_REPLAY"),restore_adaptive("GAGP_GPU_ADAPTIVE_REPLAY");
  setenv("GAGP_GPU_GLOBAL_PAYLOAD","1",1);setenv("GAGP_GPU_TILED_REPLAY","1",1);
  setenv("GAGP_GPU_ADAPTIVE_REPLAY","1",1);
  const auto entries=[](int count) {
    BytecodeProgram p;p.n_locals=2;
    p.consts={Value::from_int(0),Value::from_int(1),Value::from_int(count),Value::from_int(7)};
    p.code={ins_a(Opcode::PushConst,0),ins_a(Opcode::Store,0),ins_a(Opcode::Load,0),
      ins_ab(Opcode::CallBuiltin,static_cast<int>(BuiltinId::Singleton),1),ins_a(Opcode::Store,1),
      ins_a(Opcode::Load,0),ins_a(Opcode::PushConst,1),ins(Opcode::Add),ins_a(Opcode::Store,0),
      ins_a(Opcode::Load,0),ins_a(Opcode::PushConst,2),ins(Opcode::Lt),ins_a(Opcode::JmpIfTrue,2),
      ins_a(Opcode::PushConst,3),ins(Opcode::Return)};
    p.instruction_fuel.assign(p.code.size(),1);return p;
  };
  BytecodeProgram cells;cells.n_locals=2;
  cells.consts={Value::from_int(0),Value::from_int(1),Value::from_int(128),Value::from_int(7),
      payload::make_int_list_value({}),payload::make_int_list_value({Value::from_int(1)})};
  cells.code={ins_a(Opcode::PushConst,0),ins_a(Opcode::Store,0),ins_a(Opcode::PushConst,4),ins_a(Opcode::Store,1),
      ins_a(Opcode::Load,1),ins_a(Opcode::PushConst,5),ins_ab(Opcode::CallBuiltin,static_cast<int>(BuiltinId::Concat),2),
      ins_a(Opcode::Store,1),ins_a(Opcode::Load,0),ins_a(Opcode::PushConst,1),ins(Opcode::Add),ins_a(Opcode::Store,0),
      ins_a(Opcode::Load,0),ins_a(Opcode::PushConst,2),ins(Opcode::Lt),ins_a(Opcode::JmpIfTrue,4),
      ins_a(Opcode::PushConst,3),ins(Opcode::Return)};
  cells.instruction_fuel.assign(cells.code.size(),1);
  BytecodeProgram plain;plain.consts={Value::from_int(7)};plain.code={ins_a(Opcode::PushConst,0),ins(Opcode::Return)};
  const std::vector<BytecodeProgram> programs{entries(1500),cells,entries(5000),plain};
  const std::vector<CaseBindings> cases(3);const std::vector<Value> answers(3,Value::from_int(7));
  FitnessPolicy policy{FitnessMetric::Exact};policy.list_allocation_limit=65536;
  std::vector<CpuFitnessCaseCounts> cpu_counts;
  const auto cpu=eval_fitness_cpu(programs,cases,answers,100000,1000,33,policy,nullptr,&cpu_counts);
  ScopedEnvironment restore_initial_tiles("GAGP_GPU_TILED_INITIAL_PAYLOAD");
  for(const char* tiled:{"0","1"}) {
    setenv("GAGP_GPU_TILED_INITIAL_PAYLOAD",tiled,1);
    FitnessSessionGpu session;if(!session.init(cases,answers,100000,33,1000,policy).ok)return false;
    const auto gpu=session.eval_programs(programs,true);
    if(!check(gpu.ok && gpu.fitness==cpu && cpu==std::vector<double>(4,0),"fixed initial capacity fitness parity"))return false;
    if(!check(gpu.payload_replay_programs==3 &&
        gpu.initial_payload_entry_capacity==1024 && gpu.initial_payload_value_capacity==8192 &&
        gpu.initial_payload_physical_blocks>0,"fixed initial capacities/replay selection"))return false;
    for(int p=0;p<4;++p)if(!check(gpu.case_counts[p][0]==cpu_counts[p].cases &&
        gpu.case_counts[p][1]==cpu_counts[p].errors && gpu.case_counts[p][2]==cpu_counts[p].timeouts,
        "fixed initial capacity case/error/timeout counts"))return false;
  }
  return true;
}

bool test_tiled_initial_payload() {
  ScopedEnvironment restore_tiles("GAGP_GPU_TILED_INITIAL_PAYLOAD"),restore_global("GAGP_GPU_GLOBAL_PAYLOAD"),
      restore_diag("GAGP_GPU_OVERFLOW_DIAGNOSTICS");
  setenv("GAGP_GPU_GLOBAL_PAYLOAD","1",1);
  setenv("GAGP_GPU_OVERFLOW_DIAGNOSTICS","1",1);
  BytecodeProgram list;list.n_locals=2;
  list.code={ins_a(Opcode::Load,0),ins_ab(Opcode::CallBuiltin,static_cast<int>(BuiltinId::Singleton),1),ins(Opcode::Return)};
  list.instruction_fuel={1,1,1};
  BytecodeProgram wrong;wrong.consts={Value::from_bool(true),Value::from_int(1)};
  wrong.code={ins_a(Opcode::PushConst,0),ins_a(Opcode::PushConst,1),ins(Opcode::Add),ins(Opcode::Return)};
  wrong.instruction_fuel={1,1,1,1};
  const std::vector<BytecodeProgram> programs{list,wrong,list};
  std::vector<CaseBindings> cases;std::vector<Value> answers;
  for(int i=0;i<1024;++i) {
    cases.push_back({{0,Value::from_int(i%3)}});
    answers.push_back(payload::make_int_list_value({Value::from_int(i%5)}));
  }
  for(bool nonempty_input:{false,true}) {
    // A single effective input, in the last case, is enough for auto. List
    // answers and list-producing programs alone must not trigger scheduling.
    if(nonempty_input)cases.back().push_back({1,payload::make_int_list_value({Value::from_int(7)})});
    for(int lanes:{33,512}) for(int fuel:{2,100}) {
      FitnessPolicy policy{FitnessMetric::Levenshtein,FitnessProximity::Discounted};
      policy.list_allocation_limit=65536;
      std::vector<CpuFitnessCaseCounts> counts;std::vector<unsigned> wrong_counts;
      const auto cpu=eval_fitness_cpu(programs,cases,answers,fuel,2.5,lanes,policy,&wrong_counts,&counts);
      FitnessEvalResult baseline;
      for(const char* tiled:{"0","1","auto"}) {
        setenv("GAGP_GPU_TILED_INITIAL_PAYLOAD",tiled,1);
        FitnessSessionGpu session;if(!session.init(cases,answers,fuel,lanes,2.5,policy).ok)return false;
        int callback_count=0;cudaError_t callback_status=cudaSuccess;
        const auto gpu=session.eval_programs(programs,true,[&]{
          ++callback_count;callback_status=cudaDeviceSynchronize();
        });
        if(!check(gpu.ok && gpu.fitness==cpu && gpu.wrong==wrong_counts && callback_count==1 &&
            callback_status==cudaSuccess && gpu.timing.initial_kernel_event_ms>0 &&
            gpu.initial_payload_tiled==(tiled[0]=='1' || (tiled[0]=='a' && nonempty_input)) && gpu.payload_replay_programs==0,
            "tiled initial full-case canonical fitness/callback parity"))return false;
        for(int p=0;p<3;++p)if(!check(gpu.case_counts[p][0]==counts[p].cases &&
            gpu.case_counts[p][1]==counts[p].errors && gpu.case_counts[p][2]==counts[p].timeouts,
            "tiled initial case/error/timeout parity"))return false;
        if(tiled[0]=='0')baseline=gpu;
        else if(!check(gpu.fitness==baseline.fitness && gpu.case_counts==baseline.case_counts &&
            gpu.initial_payload_overflow_cases==baseline.initial_payload_overflow_cases &&
            gpu.initial_payload_overflow_lane_masks==baseline.initial_payload_overflow_lane_masks,
            "tiled initial changed original-lane diagnostics"))return false;
      }
    }
  }
  return true;
}

bool test_auto_initial_payload_inputs() {
  ScopedEnvironment restore_tiles("GAGP_GPU_TILED_INITIAL_PAYLOAD"),restore_global("GAGP_GPU_GLOBAL_PAYLOAD");
  setenv("GAGP_GPU_GLOBAL_PAYLOAD","1",1);
  const auto nonempty=payload::make_int_list_value({Value::from_int(7)});
  const auto empty=payload::make_int_list_value({});
  const std::vector<std::pair<CaseBindings,bool>> fixtures{
      {{},false},
      {{{1,empty}},false},
      {{{1,nonempty},{1,Value::from_int(7)}},false},
      {{{1,nonempty},{1,empty}},false},
      {{{-1,nonempty},{64,nonempty}},false},
      {{{1,payload::make_float_list_value({Value::from_float(7)})}},false},
      {{{1,payload::make_string_list_value({payload::make_string_value("x")})}},false},
      {{{1,empty},{1,nonempty}},true},
  };
  BytecodeProgram list;list.n_locals=2;
  list.code={ins_a(Opcode::Load,0),ins_ab(Opcode::CallBuiltin,static_cast<int>(BuiltinId::Singleton),1),ins(Opcode::Return)};
  const std::vector<BytecodeProgram> programs{list};
  const std::vector<Value> answers(3,nonempty);
  FitnessPolicy policy{FitnessMetric::Levenshtein};
  for(const auto& fixture:fixtures) {
    std::vector<CaseBindings> cases(3,{{0,Value::from_int(7)}});
    cases.back().insert(cases.back().end(),fixture.first.begin(),fixture.first.end());
    std::vector<CpuFitnessCaseCounts> counts;
    const auto cpu=eval_fitness_cpu(programs,cases,answers,100,1000,33,policy,nullptr,&counts);
    for(const char* tiled:{"auto","1"}) {
      setenv("GAGP_GPU_TILED_INITIAL_PAYLOAD",tiled,1);
      FitnessSessionGpu session;if(!session.init(cases,answers,100,33,1000,policy).ok)return false;
      const auto gpu=session.eval_programs(programs,true);
      if(!check(gpu.ok && gpu.fitness==cpu && cpu==std::vector<double>{0} &&
          gpu.initial_payload_tiled==(tiled[0]=='1' || fixture.second) &&
          gpu.case_counts[0][0]==counts[0].cases && gpu.case_counts[0][1]==counts[0].errors &&
          gpu.case_counts[0][2]==counts[0].timeouts,"auto initial tiling effective binding selection"))return false;
    }
  }
  return true;
}

bool test_sorted_arena_cases() {
  ScopedEnvironment restore_sort("GAGP_GPU_SORT_ARENA_CASES"),restore_global("GAGP_GPU_GLOBAL_PAYLOAD"),
      restore_tiles("GAGP_GPU_TILED_INITIAL_PAYLOAD"),restore_replay("GAGP_GPU_TILED_REPLAY");
  setenv("GAGP_GPU_GLOBAL_PAYLOAD","1",1);
  setenv("GAGP_GPU_TILED_REPLAY","1",1);
  BytecodeProgram pass;pass.n_locals=2;pass.code={ins_a(Opcode::Load,0),ins(Opcode::Return)};
  BytecodeProgram missing;missing.n_locals=2;
  missing.code={ins_a(Opcode::Load,1),ins_ab(Opcode::CallBuiltin,static_cast<int>(BuiltinId::Singleton),1),ins(Opcode::Return)};
  BytecodeProgram growing;growing.n_locals=2;
  for(int i=0;i<7;++i) {
    growing.code.push_back(ins_a(Opcode::Load,0));growing.code.push_back(ins_a(Opcode::Load,0));
    growing.code.push_back(ins_ab(Opcode::CallBuiltin,static_cast<int>(BuiltinId::Concat),2));
    growing.code.push_back(ins_a(Opcode::Store,0));
  }
  growing.code.push_back(ins_a(Opcode::Load,0));growing.code.push_back(ins(Opcode::Return));
  const std::vector<BytecodeProgram> programs{pass,missing,growing};
  std::vector<CaseBindings> cases;std::vector<Value> answers;
  for(int i=0;i<1024;++i) {
    std::vector<Value> values((i*37)%50+1,Value::from_int(i));
    const auto input=payload::make_int_list_value(values);
    CaseBindings bindings{{0,input}};
    if(i%7)bindings.push_back({1,Value::from_int(i)});
    cases.push_back(std::move(bindings));answers.push_back(input);
  }
  FitnessPolicy policy{FitnessMetric::Levenshtein};policy.list_allocation_limit=65536;
  for(int lanes:{33,512}) {
    std::vector<CpuFitnessCaseCounts> counts;std::vector<unsigned> wrong;
    const auto cpu=eval_fitness_cpu(programs,cases,answers,1000,1000,lanes,policy,&wrong,&counts);
    for(const char* tiled:{"0","1","auto"}) for(const char* sorted:{"0","1"}) {
      setenv("GAGP_GPU_TILED_INITIAL_PAYLOAD",tiled,1);setenv("GAGP_GPU_SORT_ARENA_CASES",sorted,1);
      FitnessSessionGpu session;if(!session.init(cases,answers,1000,lanes,1000,policy).ok)return false;
      int callback_count=0;cudaError_t callback_status=cudaSuccess;
      const auto gpu=session.eval_programs(programs,true,[&]{
        ++callback_count;callback_status=cudaDeviceSynchronize();
      });
      if(!check(gpu.ok && gpu.fitness==cpu && gpu.wrong==wrong && gpu.payload_replay_programs==1 &&
          gpu.initial_payload_tiled==(tiled[0]!='0') && callback_count==1 && callback_status==cudaSuccess &&
          gpu.arena_cases_sorted==(sorted[0]=='1') && gpu.arena_case_order_bytes==(sorted[0]=='1'?4096u:0u),
          "arena sorted full-case fitness/replay/profile parity"))return false;
      for(int p=0;p<3;++p)if(!check(gpu.case_counts[p][0]==counts[p].cases &&
          gpu.case_counts[p][1]==counts[p].errors && gpu.case_counts[p][2]==counts[p].timeouts,
          "arena sorted case coverage/status mismatch"))return false;
    }
  }
  // Unproved policies must retain the ordinary case order even with opt-in.
  setenv("GAGP_GPU_SORT_ARENA_CASES","1",1);
  for(bool discounted:{false,true}) {
    auto excluded=policy;excluded.proximity=discounted?FitnessProximity::Discounted:FitnessProximity::Capped;
    FitnessSessionGpu session;if(!session.init(cases,answers,2,33,2.5,excluded).ok)return false;
    const auto gpu=session.eval_programs(programs,true);
    const auto cpu=eval_fitness_cpu(programs,cases,answers,2,2.5,33,excluded);
    if(!check(gpu.ok && gpu.fitness==cpu && !gpu.arena_cases_sorted && gpu.arena_case_order_bytes==0,
        "fractional/Discounted arena sort must be disabled"))return false;
  }
  // A local initial launch stays unsorted; its global replay can safely sort.
  setenv("GAGP_GPU_GLOBAL_PAYLOAD","0",1);
  setenv("GAGP_GPU_TILED_INITIAL_PAYLOAD","auto",1);
  ScopedEnvironment restore_index("GAGP_GPU_PAYLOAD_INDEX");
  const auto replay_cpu=eval_fitness_cpu(programs,cases,answers,1000,1000,33,policy);
  for(const char* index:{"0","1"}) {
    setenv("GAGP_GPU_PAYLOAD_INDEX",index,1);
    FitnessSessionGpu session;if(!session.init(cases,answers,1000,33,1000,policy).ok)return false;
    const auto gpu=session.eval_programs(programs,true);
    if(!check(gpu.ok && !gpu.initial_payload_tiled && gpu.arena_cases_sorted &&
        gpu.payload_replay_programs==1 && gpu.fitness==replay_cpu,
        "local initial/sorted replay parity"))return false;
  }
  return true;
}

__global__ void inspect_global_list_index(gpu_detail::DPayloadTables tables,
    const gpu_detail::DListPayloadEntry* queries, const int* expected, int count, int* failure) {
  using namespace gpu_detail;
  *failure = 0;
  auto binary = tables;
  binary.list_index = nullptr;
  binary.list_index_capacity = 0;
  for (int i = 0; i < count; ++i) {
    if (d_find_list_payload_entry(binary, queries[i].tag, queries[i].packed) != expected[i] ||
        d_find_list_payload_entry(tables, queries[i].tag, queries[i].packed) != expected[i]) {
      *failure = i + 1;
      return;
    }
  }
  if (d_find_list_payload_entry(DPayloadTables{}, ValueTag::IntList, 0) != -1)
    *failure = count + 1;
}

bool test_global_list_index() {
  using namespace gpu_detail;
  if (!check(build_global_list_index({}).empty(), "empty global table needs no index")) return false;
  std::vector<DListPayloadEntry> entries;
  // Force a probe chain that wraps at the end of the 64-slot table.
  std::int64_t candidate = 1;
  while (entries.size() < 12) {
    if ((global_list_index_hash(ValueTag::IntList, candidate) & 63) == 63)
      entries.push_back({ValueTag::IntList, candidate, static_cast<int>(entries.size()), 1});
    ++candidate;
  }
  const auto collision_key = entries.front().packed;
  entries.push_back({ValueTag::IntList, collision_key, 12345, 99}); // Later duplicate must lose.
  entries.push_back({ValueTag::FloatList, collision_key, 20, 1});
  entries.push_back({ValueTag::StringList, collision_key, 21, 1});
  entries.push_back({ValueTag::IntList, std::numeric_limits<std::int64_t>::min(), 22, 1});
  entries.push_back({ValueTag::IntList, std::numeric_limits<std::int64_t>::max(), 23, 1});
  entries.push_back({ValueTag::IntList, -1, 24, 1});
  entries.push_back({ValueTag::IntList, 0, 25, 0}); // Exact empty payload.
  const auto less = [](const auto& a, const auto& b) {
    return a.tag != b.tag ? static_cast<int>(a.tag) < static_cast<int>(b.tag) : a.packed < b.packed;
  };
  std::stable_sort(entries.begin(), entries.end(), less);
  const auto index = build_global_list_index(entries);
  if (!check(index.size() == 64, "collision test capacity")) return false;
  std::vector<DListPayloadEntry> queries = entries;
  while ((global_list_index_hash(ValueTag::IntList, candidate) & 63) != 63) ++candidate;
  queries.push_back({ValueTag::IntList, candidate, 0, 0}); // Miss through entire chain.
  queries.push_back({ValueTag::FloatList, -1, 0, 0});
  queries.push_back({ValueTag::StringList, 0, 0, 0});
  std::vector<int> expected;
  for (const auto& query : queries) {
    const auto it = std::lower_bound(entries.begin(), entries.end(), query, less);
    expected.push_back(it != entries.end() && it->tag == query.tag && it->packed == query.packed
        ? static_cast<int>(it - entries.begin()) : -1);
  }
  // One contiguous managed allocation keeps failure cleanup simple.
  unsigned char* storage = nullptr;
  const auto entry_bytes = entries.size() * sizeof(DListPayloadEntry);
  const auto query_bytes = queries.size() * sizeof(DListPayloadEntry);
  const auto bytes = entry_bytes + query_bytes + sizeof(int) * (index.size() + expected.size() + 1);
  if (cudaMallocManaged(&storage, bytes) != cudaSuccess) return false;
  auto* device_entries = reinterpret_cast<DListPayloadEntry*>(storage);
  auto* device_queries = reinterpret_cast<DListPayloadEntry*>(storage + entry_bytes);
  auto* device_index = reinterpret_cast<int*>(storage + entry_bytes + query_bytes);
  auto* device_expected = device_index + index.size();
  auto* failure = device_expected + expected.size();
  std::copy(entries.begin(), entries.end(), device_entries);
  std::copy(queries.begin(), queries.end(), device_queries);
  std::copy(index.begin(), index.end(), device_index);
  std::copy(expected.begin(), expected.end(), device_expected);
  DPayloadTables tables{};
  tables.list_entries = device_entries;
  tables.list_entry_count = static_cast<int>(entries.size());
  tables.list_index = device_index;
  tables.list_index_capacity = static_cast<int>(index.size());
  *failure = -1;
  inspect_global_list_index<<<1, 1>>>(tables, device_queries, device_expected, expected.size(), failure);
  const bool passed = cudaDeviceSynchronize() == cudaSuccess &&
      check(*failure == 0, "global list collision/first-match/signed/type/missing parity " + std::to_string(*failure));
  cudaFree(storage);
  return passed;
}

bool test_global_list_index_fitness() {
  ScopedEnvironment restore_index("GAGP_GPU_GLOBAL_LIST_INDEX"), restore_global("GAGP_GPU_GLOBAL_PAYLOAD");
  const auto captured = payload::make_int_list_value({Value::from_int(77)});
  const std::vector<CaseBindings> cases(33, {{0, captured}});
  const std::vector<Value> answers(33, Value::from_int(77));
  FitnessPolicy policy{FitnessMetric::Exact};
  policy.list_allocation_limit = 65536;
  for (const char* global : {"0", "1"}) for (const char* indexed : {"0", "1"}) {
    setenv("GAGP_GPU_GLOBAL_PAYLOAD", global, 1);
    setenv("GAGP_GPU_GLOBAL_LIST_INDEX", indexed, 1);
    FitnessSessionGpu session;
    if (!check(session.init(cases, answers, 1000, 33, 1000, policy).ok, "global index session init")) return false;
    // Reuse the session across changing constant closures/table offsets, then
    // restore the original closure. No index can survive its matching table.
    for (int generation : {0, 1, 2, 0}) {
      std::vector<Value> distractors;
      for (int i = 0; i < generation * 7; ++i)
        distractors.push_back(payload::make_int_list_value({Value::from_int(i), Value::from_int(i + 1)}));
      const auto float_list = payload::make_float_list_value({Value::from_float(77)});
      const auto string_list = payload::make_string_list_value({payload::make_string_value("77")});
      auto index_program = [](Value list) {
        BytecodeProgram program;
        program.consts = {list, Value::from_int(0)};
        program.code = {ins_a(Opcode::PushConst, 0), ins_a(Opcode::PushConst, 1),
            ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Index), 2), ins(Opcode::Return)};
        program.instruction_fuel.assign(program.code.size(), 1);
        return program;
      };
      auto from_case = index_program(captured);
      from_case.n_locals = 1;
      from_case.code[0] = ins_a(Opcode::Load, 0);
      from_case.consts.insert(from_case.consts.end(), distractors.begin(), distractors.end());
      const auto missing = Value::from_int_list_hash_len(0x0123456789abULL, 1);
      // A constant owned only by a phase changes on every evaluation too.
      const auto phase_program = index_program(payload::make_int_list_value({Value::from_int(77 + generation)}));
      const auto region_program = invocation(unary_segment(ValueTag::Int,
          phase(phase_program.consts, phase_program.code), constant_phase(Value::from_int(0))), 0);
      const std::vector<BytecodeProgram> programs{from_case, index_program(float_list),
          index_program(string_list), index_program(missing), index_program(payload::make_int_list_value({})), region_program};
      std::vector<CpuFitnessCaseCounts> counts;
      const auto cpu = eval_fitness_cpu(programs, cases, answers, 1000, 1000, 33, policy, nullptr, &counts);
      const auto gpu = session.eval_programs(programs, true);
      if (!check(gpu.ok && gpu.fitness == cpu && cpu[0] == 0 && counts[0].errors == 0, "global index cross-evaluation CPU fitness parity")) return false;
      if (!check(indexed[0] == '1' ? gpu.global_list_index_bytes > 0 &&
          (gpu.global_list_index_bytes & (gpu.global_list_index_bytes - 1)) == 0 : gpu.global_list_index_bytes == 0,
          "global index allocated byte disclosure")) return false;
      if (!check((gpu.execution_profile.find("-global-list-index") != std::string::npos) == (indexed[0] == '1'),
          "global index opt-in profile")) return false;
      for (std::size_t p = 0; p < programs.size(); ++p)
        if (!check(gpu.case_counts[p][0] == counts[p].cases && gpu.case_counts[p][1] == counts[p].errors &&
            gpu.case_counts[p][2] == counts[p].timeouts, "global index case/error/timeout parity")) return false;
    }
  }
  return true;
}

int main() {
  // Session initialization selects the least-used visible device before raw kernels.
  FitnessSessionGpu selection;
  if (!selection.init({{}}, {Value::from_int(0)}, 1, 32, 1.0).ok) return 1;
  if (!test_global_list_index() || !test_global_list_index_fitness()) return 1;
  if (!test_auto_initial_payload_inputs() || !test_sorted_arena_cases() || !test_tiled_initial_payload() || !test_initial_payload_capacity_replay() || !test_adaptive_combined_reasons() || !test_adaptive_replay() || !test_extended_aliases() || !test_fused_concat() || !test_concat_cache() || !test_small_int_hash_fitness() || !test_extended_payload() || !test_private_list_index() || !test_scalar_region_index_rollback() || !test_extended_list_fitness() || !test_payload_replay_fitness() || !test_tiled_replay_entry_growth() || !test_global_payload_program_queue() || !test_global_window_aliases() ||
      !test_subordinate_memo_coordinate_fitness() || !test_larger_memo_fitness()) return 1;
  {
    ScopedEnvironment epoch("GAGP_GPU_PAYLOAD_EPOCH_INDEX");
    setenv("GAGP_GPU_PAYLOAD_EPOCH_INDEX","1",1);
    // Existing CPU parity cases cover physical lane reuse, mixed roots, errors,
    // initial case tiles, scalar rollback, and replay with arena growth.
    if(!test_epoch_list_index() || !test_global_payload_program_queue() ||
       !test_tiled_initial_payload() || !test_scalar_region_index_rollback() ||
       !test_tiled_replay_entry_growth())return 1;
  }
  {
    ScopedEnvironment index("GAGP_GPU_GLOBAL_LIST_INDEX");
    setenv("GAGP_GPU_GLOBAL_LIST_INDEX", "1", 1);
    if (!test_concat_cache() || !test_payload_replay_fitness() || !test_scalar_region_index_rollback()) return 1;
  }
  std::cout << "gagp_test_extended_payload_gpu: OK\n";
}
