replace('cpp/tests/gpu/test_gpu_phase_population.cu','void check_unchanged_fitness_reuse(',r'''void check_linear_elite_evolution(const std::shared_ptr<const grammar::CompiledGrammar>& grammar,
    const std::vector<ProgramGenome>& parents,const std::vector<std::string>& names,
    const std::vector<CaseBindings>& cases,const std::vector<Value>& answers) {
 const char* flag="GAGP_GPU_LINEAR_ELITE";const auto* prior=std::getenv(flag);
 const bool present=prior;const std::string saved=prior?prior:"";
 setenv(flag,"0",1);repro::NativePhasePopulation reference(grammar,parents,names);
 setenv(flag,"1",1);repro::NativePhasePopulation linear(grammar,parents,names);
 if(present)setenv(flag,saved.c_str(),1);else unsetenv(flag);
 require(std::string(linear.profile_name()).find("-elite-linear")!=std::string::npos &&
     std::string(reference.profile_name()).find("-elite-linear")==std::string::npos,"linear elite constructor policy was not retained");
 FitnessSessionGpu session;const FitnessPolicy policy{FitnessMetric::Exact};
 require(session.init(cases,answers,10000,512,1000,policy).ok,"linear elite fitness session");
 for(int generation=0;generation<=16;++generation) {
  const auto a=reference.evaluate(session,true),b=linear.evaluate(session,true);
  require(a.ok && b.ok && a.fitness==b.fitness && a.wrong==b.wrong && a.case_counts==b.case_counts,
      "linear elite changed GPU scores, counters or coverage");
  const auto x=reference.export_population(),y=linear.export_population();std::vector<BytecodeProgram> programs;
  require(x.size()==parents.size() && y.size()==parents.size(),"linear elite lost final members");
  for(std::size_t i=0;i<x.size();++i) {
   require(ast_cache_key(x[i].ast)==ast_cache_key(y[i].ast),"linear elite changed complete offspring AST trajectory");
   auto fresh=y[i];fresh.derivation.reset();grammar::require_membership(*grammar,fresh);
   programs.push_back(compile_for_eval(fresh,names));
  }
  std::vector<unsigned> wrong;
  const auto cpu=eval_fitness_cpu(programs,cases,answers,10000,1000,512,policy,&wrong);
  require(cpu==b.fitness && wrong==b.wrong,"linear elite offspring differs from independent full CPU evaluation");
  if(generation==16)break;
  auto selection=a.fitness;
  if(generation%4==0)std::fill(selection.begin(),selection.end(),0.0);
  if(generation%4==1)std::fill(selection.begin(),selection.end(),-std::numeric_limits<double>::infinity());
  if(generation%4==2)for(std::size_t i=0;i<selection.size();++i)
   selection[i]=i%3==0?std::numeric_limits<double>::infinity():i%3==1?-0.:0.;
  const int elites=generation%4==3?3:generation%4==0?0:1;
  const auto seed=UINT64_C(0x691be4)+generation;const double mutation=generation%3==0?0:generation%3==1?.3:1;
  reference.reproduce(selection,2,mutation,seed,elites);linear.reproduce(selection,2,mutation,seed,elites);
 }
}

void check_unchanged_fitness_reuse(''')
replace('cpp/tests/gpu/test_gpu_phase_population.cu',' check_unchanged_fitness_reuse(grammar,parents,names,cases,answers);',
    ' check_linear_elite_evolution(grammar,parents,names,cases,answers);\n check_unchanged_fitness_reuse(grammar,parents,names,cases,answers);')
