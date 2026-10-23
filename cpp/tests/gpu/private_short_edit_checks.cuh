#pragma once
#include "gagp/runtime/cpu/fitness_cpu.hpp"

bool test_private_short_edit_sessions() {
  ScopedEnvironment feature("GAGP_GPU_SHORT_EDIT_DISTANCE"),private_lists("GAGP_GPU_PRIVATE_INTLIST_OFFSETS"),
      global("GAGP_GPU_GLOBAL_PAYLOAD"),compact("GAGP_GPU_COMPACT_INTLIST_CELLS"),
      sorted("GAGP_GPU_SORT_ARENA_CASES"),workspace("GAGP_GPU_PAYLOAD_WORKSPACE_MIB");
  setenv("GAGP_GPU_PRIVATE_INTLIST_OFFSETS","1",1);setenv("GAGP_GPU_GLOBAL_PAYLOAD","1",1);
  setenv("GAGP_GPU_COMPACT_INTLIST_CELLS","1",1);setenv("GAGP_GPU_PAYLOAD_WORKSPACE_MIB","128",1);
  BytecodeProgram input;input.n_locals=1;input.code={ins_a(Opcode::Load,0),ins(Opcode::Return)};
  BytecodeProgram doubled=input;
  doubled.code={ins_a(Opcode::Load,0),ins_a(Opcode::Load,0),
      ins_ab(Opcode::CallBuiltin,int(BuiltinId::Concat),2),ins(Opcode::Return)};
  BytecodeProgram empty;empty.consts={payload::make_int_list_value({})};
  empty.code={ins_a(Opcode::PushConst,0),ins(Opcode::Return)};
  const std::vector<BytecodeProgram> programs{input,doubled,empty};
  const int lengths[]={0,1,2,31,32,33,63,64,65};
  const std::int64_t values[]={INT64_MIN,INT64_MAX,(INT64_C(1)<<53)+1,-(INT64_C(1)<<53)-1,0,7,-7};
  std::vector<CaseBindings> cases;std::vector<Value> answers;
  for(int i=0;i<1024;++i) {
    std::vector<Value> a,b;
    const int n=lengths[i%9],m=lengths[(i/9)%9];
    for(int j=0;j<n;++j)a.push_back(Value::from_int(values[(i+j)%7]));
    for(int j=0;j<m;++j)b.push_back(Value::from_int(values[(i+2*j)%7]));
    if(i%7==0)b=a;
    cases.push_back({{0,payload::make_int_list_value(a)}});answers.push_back(payload::make_int_list_value(b));
  }
  for(auto proximity:{FitnessProximity::Capped,FitnessProximity::Discounted})for(double penalty:{3.5,1000.0}) {
    FitnessPolicy policy{FitnessMetric::Levenshtein,proximity};policy.list_allocation_limit=65536;
    std::vector<unsigned> wrong;
    const auto reference=eval_fitness_cpu(programs,cases,answers,1000,penalty,512,policy,&wrong);
    for(const char* order:{"0","1"}) {
      setenv("GAGP_GPU_SORT_ARENA_CASES",order,1);
      setenv("GAGP_GPU_SHORT_EDIT_DISTANCE","0",1);
      FitnessSessionGpu control;
      if(!control.init(cases,answers,1000,512,penalty,policy).ok)return false;
      const auto ordinary=control.eval_programs(programs,true);
      setenv("GAGP_GPU_SHORT_EDIT_DISTANCE","1",1);
      FitnessSessionGpu candidate;
      if(!candidate.init(cases,answers,1000,512,penalty,policy).ok)return false;
      const auto optimized=candidate.eval_programs(programs,true);
      if(!check(ordinary.ok && optimized.ok && optimized.fitness==ordinary.fitness &&
          optimized.wrong==ordinary.wrong && optimized.case_counts==ordinary.case_counts &&
          optimized.fitness==reference && optimized.wrong==wrong &&
          optimized.execution_profile.find("-short-edit-exact")!=std::string::npos &&
          ordinary.execution_profile.find("-short-edit-exact")==std::string::npos,
          "short-edit prepared cases changed full-width score/wrong/order/capped/discounted semantics"))return false;
      auto permuted_cases=cases;auto permuted_answers=answers;
      std::reverse(permuted_cases.begin(),permuted_cases.end());
      std::reverse(permuted_answers.begin(),permuted_answers.end());
      const auto reordered=eval_fitness_cpu(programs,permuted_cases,permuted_answers,1000,penalty,512,policy,&wrong);
      if(!candidate.init(permuted_cases,permuted_answers,1000,512,penalty,policy).ok)return false;
      const auto reinitialized=candidate.eval_programs(programs,true);
      if(!check(reinitialized.ok && reinitialized.fitness==reordered && reinitialized.wrong==wrong,
          "short-edit session reinit retained stale labels or original-case indices"))return false;
    }
  }
  return true;
}
