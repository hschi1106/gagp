replace('cpp/tests/gpu/test_gpu_phase_population.cu','#include "gagp/evolution/compiler.hpp"',
    '#include "gagp/evolution/compiler.hpp"\n#include "gagp/evolution/selection.hpp"')
replace('cpp/tests/gpu/test_gpu_phase_population.cu','void check_source_crossover_gate(',r'''void check_unchanged_fitness_reuse(const std::shared_ptr<const grammar::CompiledGrammar>& grammar,
    const std::vector<ProgramGenome>& parents,const std::vector<std::string>& names,
    const std::vector<CaseBindings>& cases,const std::vector<Value>& answers) {
 const char* flag="GAGP_GPU_UNCHANGED_FITNESS_REUSE";const auto* prior=std::getenv(flag);
 const bool present=prior;const std::string saved=prior?prior:"";
 unsetenv(flag);repro::NativePhasePopulation reference(grammar,parents,names);
 setenv(flag,"1",1);repro::NativePhasePopulation cached(grammar,parents,names);
 if(present)setenv(flag,saved.c_str(),1);else unsetenv(flag); // Constructor-fixed, not a live switch.
 FitnessSessionGpu session;const FitnessPolicy policy{FitnessMetric::Exact};
 require(session.init(cases,answers,10000,512,1000,policy).ok,"completed fitness session init");
 std::size_t reused=0;
 const auto compare=[&](const FitnessEvalResult& a,const FitnessEvalResult& b) {
  require(a.ok && b.ok && a.fitness==b.fitness && a.wrong==b.wrong && a.case_counts==b.case_counts,
      "completed fitness changed score, wrong, errors, timeouts or logical cases");
 };
 for(int generation=0;generation<=16;++generation) {
  const auto a=reference.evaluate(session,true),b=cached.evaluate(session,true);compare(a,b);
  const auto x=reference.export_population(),y=cached.export_population();
  std::vector<BytecodeProgram> programs;
  for(std::size_t i=0;i<x.size();++i) {
   require(ast_cache_key(x[i].ast)==ast_cache_key(y[i].ast),"completed fitness changed offspring AST trajectory");
   auto fresh=y[i];fresh.derivation.reset();grammar::require_membership(*grammar,fresh);
   programs.push_back(compile_for_eval(fresh,names));
  }
  std::vector<unsigned> wrong;const auto cpu=eval_fitness_cpu(programs,cases,answers,10000,1000,512,policy,&wrong);
  require(cpu==b.fitness && wrong==b.wrong,"completed fitness differs from independent full CPU execution");
  reused+=b.fitness_reused_programs;
  const auto repeated=cached.evaluate(session,true);compare(a,repeated);
  require(repeated.fitness_reused_programs==parents.size() && repeated.physical_program_cases==0 &&
      repeated.fitness_reused_program_cases==parents.size()*cases.size(),"same-generation scores not completely reused or physical work misreported");
  const auto no_diagnostics=cached.evaluate(session,false);
  require(no_diagnostics.ok && no_diagnostics.fitness==a.fitness && no_diagnostics.wrong==a.wrong &&
      no_diagnostics.case_counts.empty(),"completed fitness changed optional diagnostics contract");
  if(generation<16) {
   const auto seed=UINT64_C(0x81a9cc)+generation;const double mutation=generation%3==0?0:generation%3==1?.3:1;
   reference.reproduce(a.fitness,2,mutation,seed,3);cached.reproduce(b.fitness,2,mutation,seed,3);
  }
 }
 require(reused>0,"unchanged-offspring fixture never reused a previous-generation score");
 // Same object, same case count, different labels/policy/fuel/reduction lanes.
 auto changed_answers=answers;for(auto& answer:changed_answers)answer=Value::from_int(answer.i+101);
 const FitnessPolicy discounted{FitnessMetric::Absolute,FitnessProximity::Discounted};
 require(session.init(cases,changed_answers,10000,128,7,discounted).ok,"completed fitness reinit labels/policy");
 auto a=reference.evaluate(session,true),b=cached.evaluate(session,true);compare(a,b);
 require(b.fitness_reused_programs==0,"session reinit reused scores from prior labels/policy");
 compare(a,cached.evaluate(session,true));
 for(int fuel:{0,1}) {
  require(session.init(cases,answers,fuel,512,1000,policy).ok,"completed fitness low-fuel reinit");
  a=reference.evaluate(session,true);b=cached.evaluate(session,true);compare(a,b);
  require(b.fitness_reused_programs==0,"new fuel retained completed scores");
  compare(a,cached.evaluate(session,true));
 }
 // A failed init attempt also invalidates a previously ready context's cache.
 require(!session.init(cases,{},10000,512,1000,policy).ok,"invalid init accepted");
 a=reference.evaluate(session,true);b=cached.evaluate(session,true);compare(a,b);
 require(b.fitness_reused_programs==0,"failed reinit failed to invalidate owner cache");
 require(session.init(cases,answers,10000,512,1000,policy).ok,"completed fitness restore context");
 a=reference.evaluate(session,true);b=cached.evaluate(session,true);compare(a,b);
 // Skip an evaluation: one-generation maps cannot be followed across the gap.
 for(int i=0;i<2;++i) {
  reference.reproduce(a.fitness,2,.3,UINT64_C(0x151529)+i,1);
  cached.reproduce(a.fitness,2,.3,UINT64_C(0x151529)+i,1);
 }
 a=reference.evaluate(session,true);b=cached.evaluate(session,true);compare(a,b);
 require(b.fitness_reused_programs==0,"unscored generation gap reused obsolete parent indices");
 FitnessSessionGpu other;
 require(other.init(cases,changed_answers,10000,512,1000,policy).ok,"other completed context init");
 a=reference.evaluate(other,true);b=cached.evaluate(other,true);compare(a,b);
 require(b.fitness_reused_programs==0,"different session reused completed scores");
}

void check_source_crossover_gate(''')
replace('cpp/tests/gpu/test_gpu_phase_population.cu',' check_initial_packed_verification(grammar,parents,names,session,cases,answers);',
    ' check_unchanged_fitness_reuse(grammar,parents,names,cases,answers);\n check_initial_packed_verification(grammar,parents,names,session,cases,answers);')
replace('cpp/tests/gpu/test_gpu_phase_population.cu','void test_private_intlist_generations(bool sequence_windows=false,bool staged=false) {',r'''void check_private_completed_fitness(const std::shared_ptr<const grammar::CompiledGrammar>& grammar,
    const std::vector<ProgramGenome>& parents,const std::vector<std::string>& names,
    const std::vector<CaseBindings>& cases,const std::vector<Value>& answers,FitnessPolicy policy,int fuel) {
 const char* flags[]={"GAGP_GPU_UNCHANGED_FITNESS_REUSE","GAGP_GPU_PHASE_STAGED_BUFFERS"};
 std::string saved[2];bool present[2];
 for(int i=0;i<2;++i){const auto* value=std::getenv(flags[i]);present[i]=value;saved[i]=value?value:"";}
 setenv(flags[1],"1",1);unsetenv(flags[0]);repro::NativePhasePopulation reference(grammar,parents,names);
 setenv(flags[0],"1",1);repro::NativePhasePopulation cached(grammar,parents,names);
 for(int i=0;i<2;++i)if(present[i])setenv(flags[i],saved[i].c_str(),1);else unsetenv(flags[i]);
 FitnessSessionGpu session;require(session.init(cases,answers,fuel,32,1000,policy).ok,"private completed-score session");
 for(int generation=0;generation<=4;++generation) {
  const auto a=reference.evaluate(session,true),b=cached.evaluate(session,true),again=cached.evaluate(session,true);
  require(a.ok && b.ok && again.ok && a.fitness==b.fitness && a.fitness==again.fitness &&
      a.wrong==b.wrong && a.wrong==again.wrong && a.case_counts==b.case_counts && a.case_counts==again.case_counts,
      "private completed scores retained payload references or changed replay results");
  require(again.fitness_reused_programs==parents.size() && again.physical_program_cases==0 &&
      again.payload_replay_programs==0 && again.payload_workspace_retained_bytes==0,
      "cached private completed scores repeated payload work or retained staged workspace");
  const auto x=reference.export_population(),y=cached.export_population();std::vector<BytecodeProgram> programs;
  for(std::size_t i=0;i<x.size();++i) {
   require(ast_cache_key(x[i].ast)==ast_cache_key(y[i].ast),"private completed-score reuse changed RNG/AST");
   auto fresh=y[i];fresh.derivation.reset();grammar::require_membership(*grammar,fresh);programs.push_back(compile_for_eval(fresh,names));
  }
  std::vector<unsigned> wrong;
  require(eval_fitness_cpu(programs,cases,answers,fuel,1000,32,policy,&wrong)==b.fitness && wrong==b.wrong,
      "private completed scores differ from full independent CPU fitness/wrong");
  if(generation<4) {
   const auto seed=UINT64_C(0x917355)+generation;
   reference.reproduce(a.fitness,2,.3,seed,1);cached.reproduce(b.fitness,2,.3,seed,1);
  }
 }
}

void test_private_intlist_generations(bool sequence_windows=false,bool staged=false) {''')
replace('cpp/tests/gpu/test_gpu_phase_population.cu',
    ' if(staged)require(owner.device_bytes()<cold_control.device_bytes(),"staged owner retained reproduction buffers at cold admission");',
    ''' if(staged)require(owner.device_bytes()<cold_control.device_bytes(),"staged owner retained reproduction buffers at cold admission");
 if(sequence_windows && staged)check_private_completed_fitness(grammar,parents,names,cases,answers,policy,audit_fuel);''')
