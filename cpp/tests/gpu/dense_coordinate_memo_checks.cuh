#pragma once

namespace dense_memo_test {

struct Environment {
  std::string previous; bool present;
  Environment():present(std::getenv("GAGP_GPU_DENSE_COORDINATE_MEMO")!=nullptr) {
    if(present)previous=std::getenv("GAGP_GPU_DENSE_COORDINATE_MEMO");
  }
  ~Environment() {
    if(present)setenv("GAGP_GPU_DENSE_COORDINATE_MEMO",previous.c_str(),1);
    else unsetenv("GAGP_GPU_DENSE_COORDINATE_MEMO");
  }
};

BytecodeProgram fixture(unsigned axes, bool capture=false, int bias=0) {
  BoundedRegionSegment segment;
  auto& plan=segment.plan;
  plan.state_types.assign(axes,ValueTag::Int);plan.result_type=ValueTag::Int;
  plan.memoized=true;plan.limits={128,128,1};
  plan.coordinate_endpoint=DomainEndpoint::Inclusive;
  std::vector<RegionPhaseBinding> states,results;
  std::vector<Instr> predicate,body,combine;
  for(unsigned axis=0;axis<axes;++axis) {
    plan.coordinate_slots.push_back(axis);plan.coordinate_rank.push_back({axis,1});
    plan.coordinate_domains.push_back({literal(-2),literal(0)});
    states.push_back({slot(RegionSlotBank::State,axis),static_cast<int>(axis)});
    results.push_back({slot(RegionSlotBank::Result,axis),static_cast<int>(axis)});
    predicate.push_back(ins_a(Opcode::Load,axis));
    if(axis)predicate.push_back(ins_ab(Opcode::CallBuiltin,static_cast<int>(BuiltinId::Min),2));
    body.push_back(ins_a(Opcode::Load,axis));
    if(axis)body.push_back(ins(Opcode::Add));
    combine.push_back(ins_a(Opcode::Load,axis));
    if(axis)combine.push_back(ins(Opcode::Add));
    RegionRequest request;
    for(unsigned state=0;state<axes;++state) {
      RegionStateTransition transition;
      transition.kind=RegionTransitionKind::CoordinateOffset;
      transition.source_state=state;transition.offset=state==axis?-1:0;
      request.states.push_back(transition);
    }
    plan.requests.push_back(std::move(request));
  }
  predicate.push_back(ins_a(Opcode::PushConst,0));predicate.push_back(ins(Opcode::Eq));
  predicate.push_back(ins(Opcode::Return));
  auto body_states=states;
  if(capture) {
    plan.parameter_types={ValueTag::Int};segment.parameter_locals={0};
    body_states.push_back({slot(RegionSlotBank::Parameter,0),static_cast<int>(axes)});
    body.push_back(ins_a(Opcode::Load,axes));body.push_back(ins(Opcode::Add));
  }
  body.push_back(ins(Opcode::Return));
  combine.push_back(ins_a(Opcode::PushConst,0));combine.push_back(ins(Opcode::Add));
  combine.push_back(ins(Opcode::Return));
  segment.boundary=constant_phase(Value::from_int(-999));
  segment.base_predicate=phase({Value::from_int(-2)},std::move(predicate),states,axes);
  segment.base_body=phase({},std::move(body),body_states,axes+(capture?1:0));
  segment.combine=phase({Value::from_int(bias)},std::move(combine),results,axes);
  for(auto* p:{&*segment.boundary,&segment.base_predicate,&segment.base_body,&segment.combine})
    p->program.instruction_fuel.assign(p->program.code.size(),1);
  BytecodeProgram program;
  program.n_locals=capture?1:0;
  for(unsigned axis=0;axis<axes;++axis) {
    program.consts.push_back(Value::from_int(0));program.code.push_back(ins_a(Opcode::PushConst,axis));
  }
  program.code.push_back(ins_a(Opcode::BoundedRegion,0));program.code.push_back(ins(Opcode::Return));
  program.instruction_fuel.assign(program.code.size(),1);
  program.bounded_region_segments.push_back(std::move(segment));
  return program;
}

bool test_domain_and_index_proof() {
  using namespace gpu_detail;
  auto packed=gpu_detail::pack_programs_with_shared_case_count({fixture(4)},1,0);
  auto segment=packed.region_segments[0];
  if(!check(dense_coordinate_memo_cells(segment)==81,"4-axis dense domain proof"))return false;
  // Different extents, permuted axes, negative and full-width signed origins.
  segment.coordinate_slots[0]=3;segment.coordinate_slots[1]=1;
  segment.coordinate_slots[2]=0;segment.coordinate_slots[3]=2;
  segment.coordinate_domains[0]={literal(INT64_MIN),literal(INT64_MIN+1)};
  segment.coordinate_domains[1]={literal(-2),literal(0)};
  segment.coordinate_domains[2]={literal(INT64_MAX-1),literal(INT64_MAX)};
  segment.coordinate_domains[3]={literal(-1),literal(1)};
  segment.limits.cells=36;
  if(!check(dense_coordinate_memo_cells(segment)==36,"signed/permuted dense domain"))return false;
  bool visited[36]{};
  for(unsigned ordinal=0;ordinal<36;++ordinal) {
    auto remainder=ordinal;DRegionFrame frame{};
    for(int axis=3;axis>=0;--axis) {
      const auto& d=segment.coordinate_domains[axis];
      const auto extent=static_cast<unsigned>(static_cast<std::uint64_t>(d.upper.literal)-
          static_cast<std::uint64_t>(d.lower.literal))+1;
      frame.state[segment.coordinate_slots[axis]]=Value::from_int(d.lower.literal+remainder%extent);
      remainder/=extent;
    }
    const auto index=dense_coordinate_memo_slot(segment,frame);
    if(!check(index<36 && !visited[index],"all coordinate axes must select distinct cells"))return false;
    visited[index]=true;
  }
  auto declined=segment;declined.limits.cells=35;
  if(!check(!dense_coordinate_memo_cells(declined),"product above declared memo capacity"))return false;
  declined=segment;declined.coordinate_slots[3]=declined.coordinate_slots[0];
  if(!check(!dense_coordinate_memo_cells(declined),"duplicate/omitted coordinate state"))return false;
  declined=segment;declined.state_types[0]=ValueTag::Bool;
  if(!check(!dense_coordinate_memo_cells(declined),"mixed state type"))return false;
  declined=segment;declined.coordinate_count=3;
  if(!check(!dense_coordinate_memo_cells(declined),"unindexed carried state"))return false;
  declined=segment;declined.coordinate_domains[0].upper.kind=RegionBoundKind::Operand;
  if(!check(!dense_coordinate_memo_cells(declined),"operand-dependent domain"))return false;
  declined=segment;declined.coordinate_domains[0]={literal(INT64_MIN),literal(INT64_MAX)};
  if(!check(!dense_coordinate_memo_cells(declined),"extent arithmetic overflow"))return false;
  declined=segment;declined.coordinate_domains[0]={literal(1),literal(0)};
  if(!check(!dense_coordinate_memo_cells(declined),"invalid literal domain"))return false;
  declined=segment;declined.memoized=false;
  if(!check(!dense_coordinate_memo_cells(declined),"nonmemoized region"))return false;
  declined=segment;declined.limits.cells=0;
  if(!check(!dense_coordinate_memo_cells(declined),"zero memo capacity"))return false;
  declined=segment;declined.state_count=declined.coordinate_count=1;
  declined.coordinate_slots[0]=0;declined.limits.cells=DMAX_REGION_MEMO;
  declined.coordinate_domains[0]={literal(INT64_MIN),literal(INT64_MIN+DMAX_REGION_MEMO-1)};
  if(!check(dense_coordinate_memo_cells(declined)==DMAX_REGION_MEMO,"maximum inclusive dense extent"))return false;
  declined.coordinate_domains[0].upper=literal(INT64_MIN+DMAX_REGION_MEMO);
  if(!check(!dense_coordinate_memo_cells(declined),"inclusive extent one beyond capacity"))return false;
  declined.coordinate_endpoint=DomainEndpoint::Exclusive;
  if(!check(dense_coordinate_memo_cells(declined)==DMAX_REGION_MEMO,"maximum exclusive dense extent"))return false;
  declined=segment;declined.coordinate_endpoint=DomainEndpoint::Exclusive;
  if(!check(dense_coordinate_memo_cells(declined)==4,"exclusive endpoint product"))return false;
  declined.coordinate_domains[0].upper=declined.coordinate_domains[0].lower;
  return check(!dense_coordinate_memo_cells(declined),"empty exclusive domain");
}

bool compare_probe(const BytecodeProgram& program,int fuel) {
  const auto ordinary=probe(program,fuel,false),dense=probe(program,fuel,true);
  const auto cpu=execute_bytecode_cpu(program,{},fuel);
  return check(ordinary.is_error==dense.is_error && ordinary.error==dense.error &&
      ordinary.tag==dense.tag && ordinary.bits==dense.bits && ordinary.fuel_left==dense.fuel_left &&
      cpu.is_error==dense.is_error && (cpu.is_error?
          std::string(err_code_name(cpu.err.code))==dense.error:encoded_bits(cpu.value)==dense.bits),
      "dense/generic/CPU output, error and exact fuel parity");
}

bool test_fuel_hits_and_fallback() {
  auto program=fixture(2);
  if(!valid(program,"dense 2D memo fixture"))return false;
  const auto ordinary=probe(program,100000,false);
  auto uncached=program;uncached.bounded_region_segments[0].plan.memoized=false;
  uncached.bounded_region_segments[0].plan.limits.cells=0;
  if(!valid(uncached,"uncached coordinate control"))return false;
  const auto no_memo=probe(uncached,100000,false);
  if(!check(!ordinary.is_error && !no_memo.is_error && ordinary.fuel_left>no_memo.fuel_left,
      "fixture must exercise real memo hits"))return false;
  const int cost=100000-ordinary.fuel_left;
  for(int fuel:{0,1,cost-1,cost,cost+1})if(!compare_probe(program,fuel))return false;
  for(unsigned axis=0;axis<2;++axis) {
    auto base=program;base.consts[axis]=Value::from_int(-2);
    if(!compare_probe(base,1000))return false;
    auto boundary=program;boundary.consts[axis]=Value::from_int(1);
    if(!compare_probe(boundary,1000))return false;
  }
  auto exclusive=program;exclusive.bounded_region_segments[0].plan.coordinate_endpoint=DomainEndpoint::Exclusive;
  for(auto& domain:exclusive.bounded_region_segments[0].plan.coordinate_domains)domain.upper=literal(1);
  if(!valid(exclusive,"dense exclusive domain") || !compare_probe(exclusive,1000))return false;
  auto small=program;small.bounded_region_segments[0].plan.limits.cells=1;
  if(!valid(small,"small memo limit") || !compare_probe(small,1000) ||
      !check(probe(small,1000,true).error=="Timeout","memo capacity remains a timeout"))return false;
  auto sparse=program;sparse.bounded_region_segments[0].plan.limits.cells=8;
  if(!valid(sparse,"dense product larger than sufficient sparse memo") || !compare_probe(sparse,1000) ||
      !check(!probe(sparse,1000,true).is_error,"sparse fallback must still complete"))return false;
  auto frames=program;frames.bounded_region_segments[0].plan.limits.frames=1;
  if(!valid(frames,"one frame") || !compare_probe(frames,1000) ||
      !check(probe(frames,1000,true).error=="Timeout","frame capacity remains a timeout"))return false;
  auto operands=program;auto& operand_plan=operands.bounded_region_segments[0].plan;
  operand_plan.bound_operand_count=1;
  operand_plan.coordinate_domains[0].upper.kind=RegionBoundKind::Operand;
  operand_plan.coordinate_domains[0].upper.operand=2;
  operands.consts.push_back(Value::from_int(0));
  operands.code.insert(operands.code.end()-2,ins_a(Opcode::PushConst,2));
  operands.instruction_fuel.assign(operands.code.size(),1);
  if(!valid(operands,"runtime operand coordinate domain") || !compare_probe(operands,1000))return false;
  auto empty=exclusive;
  empty.bounded_region_segments[0].plan.coordinate_domains[0].upper=literal(-2);
  if(!valid(empty,"empty exclusive domain") || !compare_probe(empty,1000))return false;
  // Shared validation forbids memoized carried states. A nonmemoized mixed
  // shape remains executable with every path-dependent carried value intact.
  auto carried=program;auto& segment=carried.bounded_region_segments[0];
  segment.plan.state_types.push_back(ValueTag::Int);
  for(unsigned r=0;r<2;++r) {
    RegionStateTransition transition;transition.kind=RegionTransitionKind::Expression;transition.expression=r;
    segment.plan.requests[r].states.push_back(transition);
    segment.plan.request_expression_types.push_back(ValueTag::Int);
    auto next=phase({Value::from_int(2),Value::from_int(r+1)},
        {ins_a(Opcode::Load,0),ins_a(Opcode::PushConst,0),ins(Opcode::Mul),
         ins_a(Opcode::PushConst,1),ins(Opcode::Add),ins(Opcode::Return)},
        {{slot(RegionSlotBank::State,2),0}},1);
    next.program.instruction_fuel.assign(next.program.code.size(),1);
    segment.request_expressions.push_back(std::move(next));
  }
  auto& combine=segment.combine;
  combine.bindings.push_back({slot(RegionSlotBank::State,2),2});combine.program.n_locals=3;
  combine.program.code.insert(combine.program.code.end()-1,{ins_a(Opcode::Load,2),ins(Opcode::Add)});
  combine.program.instruction_fuel.assign(combine.program.code.size(),1);
  carried.consts.push_back(Value::from_int(0));carried.code.insert(carried.code.end()-2,ins_a(Opcode::PushConst,2));
  carried.instruction_fuel.assign(carried.code.size(),1);
  if(!check(!verify_bytecode(carried).ok,"memoized omitted state must fail ordinary validation"))return false;
  segment.plan.memoized=false;segment.plan.limits.cells=0;
  if(!valid(carried,"coordinate plus varying carried state") || !compare_probe(carried,10000))return false;
  const auto packed=gpu_detail::pack_programs_with_shared_case_count({carried},1,0);
  return check(!dense_coordinate_memo_supported(packed),"carried state must decline dense key ownership");
}

bool test_fitness_cases_and_ownership() {
  Environment environment;
  std::vector<CaseBindings> cases;std::vector<Value> answers;
  for(int c=0;c<1024;++c) {
    cases.push_back(c%47==0?CaseBindings{}:CaseBindings{{0,c%53==0?Value::from_float(1.5):Value::from_int(c%11)}});
    answers.push_back(Value::from_int(c%7));
  }
  FitnessPolicy policy{FitnessMetric::Exact};
  for(int lanes:{33,512}) {
    FitnessSessionGpu session;if(!session.init(cases,answers,100000,lanes,1000,policy).ok)return false;
    for(int generation:{0,1,0}) {
      std::vector<BytecodeProgram> programs{fixture(2,true,generation*13),fixture(3,true,generation*7),fixture(4,true,generation)};
      auto switches=fixture(2,true,generation*3);
      switches.bounded_region_segments[0].plan.limits.cells=generation?128:8;
      programs.push_back(std::move(switches)); // Reuse the same backing across generic/dense invocations.
      std::vector<CpuFitnessCaseCounts> counts;std::vector<unsigned> wrong;
      const auto cpu=eval_fitness_cpu(programs,cases,answers,100000,1000,lanes,policy,&wrong,&counts);
      setenv("GAGP_GPU_DENSE_COORDINATE_MEMO","0",1);const auto generic=session.eval_programs(programs,true);
      setenv("GAGP_GPU_DENSE_COORDINATE_MEMO","1",1);const auto dense=session.eval_programs(programs,true);
      if(!check(generic.ok && dense.ok && generic.fitness==cpu && dense.fitness==cpu &&
          generic.wrong==wrong && dense.wrong==wrong && generic.case_counts==dense.case_counts &&
          dense.execution_profile.find("-dense-coordinate-memo")!=std::string::npos &&
          dense.execution_profile.find("-declined")==std::string::npos,
          "dense full-case multi-program/session/phase CPU parity"))return false;
      for(unsigned p=0;p<programs.size();++p)
        if(!check(dense.case_counts[p][0]==1024 && dense.case_counts[p][1]==counts[p].errors &&
            dense.case_counts[p][2]==counts[p].timeouts,"dense case/error/timeout accounting"))return false;
    }
  }
  // A second root invocation shares allocation but has its own logical memo.
  // The initial conservative capability declines this shape entirely.
  auto twice=fixture(2);twice.code.pop_back();
  for(unsigned axis=0;axis<2;++axis)twice.code.push_back(ins_a(Opcode::PushConst,axis));
  twice.code.push_back(ins_a(Opcode::BoundedRegion,0));twice.code.push_back(ins(Opcode::Add));
  twice.code.push_back(ins(Opcode::Return));twice.instruction_fuel.assign(twice.code.size(),1);
  if(!valid(twice,"two root region calls"))return false;
  const auto packed=gpu_detail::pack_programs_with_shared_case_count({twice},1,0);
  if(!check(!dense_coordinate_memo_supported(packed),"multiple root calls must decline initial ownership proof"))return false;
  FitnessSessionGpu session;if(!session.init({{}},{Value::from_int(0)},10000,32,1000,policy).ok)return false;
  const auto declined=session.eval_programs({twice},true);
  return check(declined.ok && declined.execution_profile.find("-dense-coordinate-memo-declined")!=std::string::npos,
      "dense decline profile diagnostic");
}

bool test_dense_coordinate_memo() {
  return test_domain_and_index_proof() && test_fuel_hits_and_fallback() && test_fitness_cases_and_ownership();
}

} // namespace dense_memo_test
