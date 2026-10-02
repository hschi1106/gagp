#include <cmath>
#include <cstdlib>
#include "gagp/evolution/evolve.hpp"
#include <iostream>
#include <set>
#include <stdexcept>
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "../../src/evolution/repro/gpu/phase_population.hpp"
namespace {
using namespace gagp;using namespace gagp::evo;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
int main(){try {
 auto definition=grammar::load_definition(GAGP_REPOSITORY_ROOT "/cpp/tests/fixtures/phase_layout_memo.json");
 // Keep the original oversized fixture as a required explicit rejection.
 // The positive fixture is a separately authored GPU-capacity contract.
 const auto oversized=std::make_shared<const grammar::CompiledGrammar>(grammar::compile_grammar(definition));
 std::vector<std::string> oversized_names;for(const auto& x:oversized->inputs())oversized_names.push_back(x.name);
 bool unsupported=false;
 try {repro::NativePhasePopulation reject(oversized,{grammar::generate_derivation(*oversized,7).genome},oversized_names);}
 catch(const std::invalid_argument& e){unsupported=std::string(e.what()).find("capacity 128")!=std::string::npos;}
 require(unsupported,"oversized declared memo capacity was silently reduced or accepted");
 definition.document.object_v.at("templates").array_v[0].object_v.at("body").object_v.at("structured").object_v.at("plan").object_v.at("limits").object_v.at("cells").number_v=128;
 const auto grammar=std::make_shared<const grammar::CompiledGrammar>(grammar::compile_grammar(grammar::parse_definition(grammar::canonical_json(definition.document))));
 std::vector<ProgramGenome> parents;constexpr int population=16;
 for(int i=0;i<population;++i)parents.push_back(grammar::generate_derivation(*grammar,1937+i).genome);
 std::vector<std::string> names;for(const auto& input:grammar->inputs())names.push_back(input.name);
 std::vector<CaseBindings> cases;std::vector<Value> answers;
 for(int i=0;i<1024;++i) {
  CaseBindings row;
  for(std::size_t j=0;j<names.size();++j) {
   const int value=names[j]=="row"?i%4:names[j]=="column"?(i/4)%4:names[j]=="rows" || names[j]=="columns"?4:i%7-3;
   row.push_back({int(j),Value::from_int(value)});
  }
  cases.push_back(row);answers.push_back(Value::from_int(i%11));
 }
 bool invalid_names=false;
 try{repro::NativePhasePopulation bad(grammar,parents,{});}catch(const std::invalid_argument&){invalid_names=true;}
 require(invalid_names,"missing input names accepted");
 std::vector<EvalCase> evolution_cases;
 for(std::size_t i=0;i<cases.size();++i){EvalCase c;c.expected=answers[i];
   for(std::size_t j=0;j<names.size();++j)c.inputs[names[j]]=cases[i][j].value;
   evolution_cases.push_back(std::move(c));}
 EvolutionConfig config;config.population_size=population;config.generations=2;
 config.compiled_grammar=grammar;config.generation_request=grammar::entry_request(*grammar);
 config.eval_engine=EvalEngine::GPU;config.reproduction_backend=repro::ReproductionBackend::Gpu;
 config.mutation_subtree_prob=1;config.fuel=10000;config.penalty=1000;config.gpu_blocksize=512;
 setenv("GAGP_COUNTED_SITE_DRAW","1",1);
 setenv("GAGP_OWNED_PHASE_CAPS","1",1);setenv("GAGP_VIEW_PROFILE","1",1);
 setenv("GAGP_TYPED_VIEW_PHASE","1",1);setenv("GAGP_DIRECT_PHASE","1",1);
 setenv("GAGP_OWNED_PHASE_PACK","1",1);setenv("GAGP_GPU_PHASE_POPULATION","1",1);setenv("GAGP_GPU_DIAGNOSTICS","1",1);
 const auto integrated=evolve_population(evolution_cases,config,&parents);
 require(integrated.reproduction_profile=="native-gpu-phase-v2-counted" && integrated.reproduction_fallback_reason.empty(),"GPU genotype evolution adapter not used");
 require(integrated.timing.generations[0].elapsed_search_ms>=integrated.timing.init_population_ms+integrated.timing.gpu_eval_init_ms &&
     integrated.timing.generations[1].elapsed_search_ms>integrated.timing.generations[0].elapsed_search_ms,
     "search completion timestamps omit initialization or are not monotonic");
 require(integrated.final_population.size()==population && integrated.history_best.size()==2,"phase evolution omitted output/history");
 require(integrated.timing.generations[0].evaluation.program_cases==population*1024,"phase evolution incomplete cases");
 for(std::size_t i=1;i<integrated.final_population.size();++i)
   require(integrated.final_population[i-1].fitness>=integrated.final_population[i].fitness,"phase final fitness ranking reversed");
 config.mutation_subtree_prob=.5;config.generations=1;config.skip_final_eval=true;
 const auto fallback=evolve_population(evolution_cases,config,&parents);
 require(fallback.reproduction_profile=="native-ast" && !fallback.reproduction_fallback_reason.empty(),"unsupported policy lacks explicit fallback");
 unsetenv("GAGP_GPU_PHASE_POPULATION");unsetenv("GAGP_GPU_DIAGNOSTICS");
 FitnessSessionGpu session;require(session.init(cases,answers,10000,512,1000).ok,"GPU session init");
 repro::NativePhasePopulation owner(grammar,parents,names);
 // The run owns its copies; mutating/destroying imported ASTs cannot change it.
 for(auto& p:parents){p.ast.nodes.clear();p.ast.consts.clear();}parents.clear();
 auto exported=owner.export_member(0);const auto original=ast_cache_key(exported.ast);
 exported.ast.nodes.clear();require(ast_cache_key(owner.export_member(0).ast)==original,"external export aliases private genotype");
 const auto initial_memory=owner.device_bytes();std::size_t stable_memory=0;std::set<std::string> seen;
 for(int generation=0;generation<=32;++generation) {
  const auto gpu=session.eval_programs(owner.programs(),true);if(!gpu.ok)throw std::runtime_error("GPU phase population evaluation failed at generation "+std::to_string(generation)+": "+gpu.err.message);
  std::vector<BytecodeProgram> independent;
  for(int i=0;i<population;++i) {
   const auto genome=owner.export_member(i);grammar::require_membership(*grammar,genome);seen.insert(ast_cache_key(genome.ast));
   independent.push_back(compile_for_eval(genome,names));
   require(gpu.case_counts.at(i)[0]==1024,"not all cases evaluated");
  }
  const auto owned=owner.evaluate(session,true);
  require(owned.ok && owned.fitness==gpu.fitness && owned.case_counts==gpu.case_counts,"owned packing differs from independent full verification");
  const auto cpu=eval_fitness_cpu(independent,cases,answers,10000,1000,512);
  require(cpu==gpu.fitness,"resident GPU offspring differs from independently exported CPU programs");
  if(generation==32)break;
  const auto stats=owner.reproduce(gpu.fitness,2,.3,UINT64_C(0x16653)+generation);
  require(stats.variation.changed_children+stats.variation.unchanged_children==population,"final offspring not completely classified");
  require(stats.variation.mutation_attempts<=population,"mutation attempted count");
  if(generation==8)stable_memory=owner.device_bytes();
 }
 require(seen.size()>population,"resident search did not generate new programs");
 // Buffers may grow to a larger live code maximum, but never retain history.
 require(owner.device_bytes()<initial_memory+population*1024*128,"resident storage exceeded fixed live-capacity allowance");
 {
  auto wrong=cases;
  for(std::size_t j=0;j<names.size();++j)if(names[j]=="row")wrong[0][j].value=Value::from_float(1.5);
  FitnessSessionGpu different;require(different.init(wrong,answers,10000,512,1000).ok,"different input-type session init");
  const auto ordinary=different.eval_programs(owner.programs(),true),owned=owner.evaluate(different,true);
  require(ordinary.ok && owned.ok && ordinary.fitness==owned.fitness && ordinary.case_counts==owned.case_counts &&
      owned.execution_profile=="mixed","owner capability proof applied to different case types");
 }
 bool rejected=false;
 try{(void)owner.export_member(population);}catch(const std::invalid_argument&){rejected=true;}
 require(rejected,"out-of-range genotype export accepted");
 rejected=false;try{(void)owner.reproduce({},2,.3,0);}catch(const std::invalid_argument&){rejected=true;}
 require(rejected,"incomplete fitness accepted");
 require(owner.programs().size()==population,"rejected external arguments invalidated live owner");
 std::cout<<"generations=32 population="<<population<<" cases=1024 distinct_exported="<<seen.size()<<" bytes_initial="<<initial_memory
   <<" bytes_gen8="<<stable_memory<<" bytes_final="<<owner.device_bytes()<<" exact_cpu_fitness=true\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
