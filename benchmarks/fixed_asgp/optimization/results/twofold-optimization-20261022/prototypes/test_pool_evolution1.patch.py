replace('cpp/tests/gpu/test_gpu_phase_population.cu','#include "gagp/evolution/compiler.hpp"',
    '#include "gagp/evolution/compiler.hpp"\n#include "gagp/evolution/case_set.hpp"\n#include "../../src/runtime/gpu/cuda_allocation_scope.hpp"')
replace('cpp/tests/gpu/test_gpu_phase_population.cu','void check_unchanged_fitness_reuse(',r'''void check_pool_evolution(const std::vector<EvalCase>& cases,EvolutionConfig config,
    const std::vector<ProgramGenome>& parents) {
 const char* flags[]={"GAGP_GPU_CUDA_POOL","GAGP_GPU_PHASE_STAGED_BUFFERS"};
 std::string saved[2];bool present[2];
 for(int i=0;i<2;++i){const auto* value=std::getenv(flags[i]);present[i]=value;saved[i]=value?value:"";}
 config.generations=4;config.seed=0x617ba;config.retain_final_population=true;config.skip_final_eval=false;
 for(bool staged:{false,true}) {
  setenv(flags[1],staged?"1":"0",1);setenv(flags[0],"0",1);
  const auto before=evolve_population(cases,config,&parents);
  const auto initial=gpu_detail::cuda_pool_debug_stats();setenv(flags[0],"1",1);
  const auto after=evolve_population(cases,config,&parents);
  require(before.reproduction_fallback_reason.empty() && after.reproduction_fallback_reason.empty() &&
      before.reproduction_profile.find("-cuda-pool")==std::string::npos &&
      after.reproduction_profile.find("-cuda-pool")!=std::string::npos,
      "CUDA pool did not use the strict native GPU reproduction path");
  require(before.final_population.size()==parents.size() && after.final_population.size()==parents.size() &&
      before.history_best.size()==5 && before.history_best_fitness==after.history_best_fitness &&
      before.final_population.size()==after.final_population.size() && before.history_best.size()==after.history_best.size(),
      "CUDA pool changed complete history/score coverage");
  for(std::size_t i=0;i<before.history_best.size();++i)
   require(ast_cache_key(before.history_best[i].genome.ast)==ast_cache_key(after.history_best[i].genome.ast) &&
       before.history_best[i].wrong==after.history_best[i].wrong,"CUDA pool changed selected champion trajectory");
  std::vector<BytecodeProgram> programs;const auto cs=prepare_case_set(cases);
  for(std::size_t i=0;i<before.final_population.size();++i) {
   const auto& a=before.final_population[i];const auto& b=after.final_population[i];
   require(ast_cache_key(a.genome.ast)==ast_cache_key(b.genome.ast) && a.fitness==b.fitness && a.wrong==b.wrong,
       "CUDA pool changed offspring AST, fitness or wrong");
   programs.push_back(compile_for_eval(b.genome,cs.input_names));
  }
  std::vector<unsigned> wrong;
  const auto cpu=eval_fitness_cpu(programs,cs.bindings,cs.expected_values,config.fuel,config.penalty,
      config.gpu_blocksize,config.fitness_policy,&wrong);
  for(std::size_t i=0;i<cpu.size();++i)
   require(canonicalize_fitness_for_ranking(cpu[i])==after.final_population[i].fitness &&
       (after.final_population[i].wrong<0 || wrong[i]==static_cast<unsigned>(after.final_population[i].wrong)),
       "CUDA pool result differs from independent full CPU final evaluation");
  const auto final=gpu_detail::cuda_pool_debug_stats();
  require(final.pools_created>initial.pools_created && final.allocations>initial.allocations &&
      final.pools_alive==initial.pools_alive && final.live_allocations==initial.live_allocations &&
      final.cleanup_errors==initial.cleanup_errors,"CUDA pool leaked session allocations or cleanup errors");
 }
 for(int i=0;i<2;++i)if(present[i])setenv(flags[i],saved[i].c_str(),1);else unsetenv(flags[i]);
}

void check_pool_session_lifetime(const std::vector<ProgramGenome>& parents,const std::vector<std::string>& names,
    const std::vector<CaseBindings>& cases,const std::vector<Value>& answers) {
 const auto initial=gpu_detail::cuda_pool_debug_stats();
 auto session=std::make_unique<FitnessSessionGpu>();FitnessEvalResult before;
 std::vector<BytecodeProgram> programs;for(const auto& parent:parents)programs.push_back(compile_for_eval(parent,names));
 {
  gpu_detail::CudaAllocationScope scope(true);
  require(session->init(cases,answers,10000,512,1000).ok,"escaped pool session init");
  before=session->eval_programs(programs,true);require(before.ok && scope.used(),"escaped session did not allocate in pool");
  scope.finish();
  require(gpu_detail::cuda_pool_debug_stats().live_allocations>initial.live_allocations,"scope discarded a live session's backing");
 }
 const auto again=session->eval_programs(programs,true);
 require(again.ok && again.fitness==before.fitness && again.wrong==before.wrong && again.case_counts==before.case_counts,
     "session retained after native scope lost valid GPU case/frame storage");
 session.reset();const auto final=gpu_detail::cuda_pool_debug_stats();
 require(final.pools_alive==initial.pools_alive && final.live_allocations==initial.live_allocations &&
     final.cleanup_errors==initial.cleanup_errors,"escaped session pool was not released on actual destruction");
}

void check_unchanged_fitness_reuse(''')
replace('cpp/tests/gpu/test_gpu_phase_population.cu',
    ' const auto integrated=evolve_population(evolution_cases,config,&parents);\n require(integrated.reproduction_profile=="native-gpu-phase-v2-counted"',
    ' check_pool_evolution(evolution_cases,config,parents);\n const auto integrated=evolve_population(evolution_cases,config,&parents);\n require(integrated.reproduction_profile=="native-gpu-phase-v2-counted"')
replace('cpp/tests/gpu/test_gpu_phase_population.cu',' check_unchanged_fitness_reuse(grammar,parents,names,cases,answers);',
    ' check_pool_session_lifetime(parents,names,cases,answers);\n check_unchanged_fitness_reuse(grammar,parents,names,cases,answers);')
replace('cpp/tests/gpu/test_gpu_phase_population.cu',' if(sequence_windows && staged)check_private_completed_fitness(grammar,parents,names,cases,answers,policy,audit_fuel);',
    ''' if(sequence_windows && staged) {
  std::vector<EvalCase> evolution_cases;
  for(std::size_t i=0;i<cases.size();++i){EvalCase row;row.expected=answers[i];
   for(const auto& binding:cases[i])row.inputs[names[binding.idx]]=binding.value;
   evolution_cases.push_back(std::move(row));}
  EvolutionConfig config;config.population_size=parents.size();config.compiled_grammar=grammar;
  config.generation_request=grammar::entry_request(*grammar);config.eval_engine=EvalEngine::GPU;
  config.reproduction_backend=repro::ReproductionBackend::Gpu;config.mutation_subtree_prob=1;
  config.fuel=audit_fuel;config.penalty=1000;config.gpu_blocksize=32;config.fitness_policy=policy;
  config.retain_final_population=true;
  check_pool_evolution(evolution_cases,config,parents);
  check_private_completed_fitness(grammar,parents,names,cases,answers,policy,audit_fuel);
 }''')
