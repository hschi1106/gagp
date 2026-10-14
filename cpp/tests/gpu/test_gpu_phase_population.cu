#include <algorithm>
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
#include "gagp/runtime/gpu/host_pack_gpu.hpp"
#include "../../src/runtime/gpu/owned_programs.hpp"
#include "../../src/evolution/repro/gpu/phase_population.hpp"
#include "../../src/evolution/repro/gpu/phase_layout.hpp"
namespace {
using namespace gagp;using namespace gagp::evo;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void check_inplace_mutation(const std::shared_ptr<const grammar::CompiledGrammar>& grammar,
    const std::vector<ProgramGenome>& parents,const std::vector<std::string>& names,
    FitnessSessionGpu& session) {
 const char* setting=std::getenv("GAGP_GPU_INPLACE_MUTATION");
 const bool enabled=setting!=nullptr;const std::string saved=setting?setting:"";
 unsetenv("GAGP_GPU_INPLACE_MUTATION");repro::NativePhasePopulation reference(grammar,parents,names);
 setenv("GAGP_GPU_INPLACE_MUTATION","1",1);repro::NativePhasePopulation candidate(grammar,parents,names);
 if(enabled)setenv("GAGP_GPU_INPLACE_MUTATION",saved.c_str(),1);else unsetenv("GAGP_GPU_INPLACE_MUTATION");
 require(candidate.device_bytes()<=reference.device_bytes(),"in-place mutation increased persistent capacity");
 for(int generation=0;generation<=32;++generation) {
  const auto before=reference.evaluate(session,true),after=candidate.evaluate(session,true);
  require(before.ok && after.ok && before.fitness==after.fitness && before.case_counts==after.case_counts,
      "in-place mutation changed full-case evaluation");
  const auto a=reference.export_population(),b=candidate.export_population();
  require(a.size()==b.size(),"in-place mutation lost an offspring");
  for(std::size_t i=0;i<a.size();++i)
   require(ast_cache_key(a[i].ast)==ast_cache_key(b[i].ast),"in-place mutation changed exact genotype");
  if(generation==32)break;
  const double probability=generation%3==0?0:generation%3==1?.3:1;
  const auto x=reference.reproduce(before.fitness,2,probability,UINT64_C(0x16653)+generation);
  const auto y=candidate.reproduce(after.fitness,2,probability,UINT64_C(0x16653)+generation);
  require(x.variation.crossover_attempts==y.variation.crossover_attempts &&
      x.variation.mutation_attempts==y.variation.mutation_attempts &&
      x.variation.contract_rejections==y.variation.contract_rejections &&
      x.variation.changed_children==y.variation.changed_children &&
      x.variation.unchanged_children==y.variation.unchanged_children &&
      x.variation.fallback_children==y.variation.fallback_children,
      "in-place mutation changed operator accounting");
 }
}

void check_splice_policy(const char* flag, const std::shared_ptr<const grammar::CompiledGrammar>& grammar,
    const std::vector<ProgramGenome>& parents,const std::vector<std::string>& names,
    FitnessSessionGpu& session) {
 const char* setting=std::getenv(flag);
 const bool enabled=setting!=nullptr;const std::string saved=setting?setting:"";
 unsetenv(flag);repro::NativePhasePopulation reference(grammar,parents,names);
 setenv(flag,"1",1);repro::NativePhasePopulation candidate(grammar,parents,names);
 if(enabled)setenv(flag,saved.c_str(),1);else unsetenv(flag);
 require(candidate.device_bytes()<=reference.device_bytes(),"splice policy increased persistent capacity");
 for(int generation=0;generation<=32;++generation) {
  const auto before=reference.evaluate(session,true),after=candidate.evaluate(session,true);
  require(before.ok && after.ok && before.fitness==after.fitness && before.case_counts==after.case_counts,
      "splice policy changed full-case evaluation");
  const auto a=reference.export_population(),b=candidate.export_population();
  require(a.size()==b.size(),"splice policy lost an offspring");
  for(std::size_t i=0;i<a.size();++i)
   require(ast_cache_key(a[i].ast)==ast_cache_key(b[i].ast),"splice policy changed exact genotype");
  if(generation==32)break;
  const double probability=generation%3==0?0:generation%3==1?.3:1;
  const auto x=reference.reproduce(before.fitness,2,probability,UINT64_C(0x16653)+generation);
  const auto y=candidate.reproduce(after.fitness,2,probability,UINT64_C(0x16653)+generation);
  require(x.variation.crossover_attempts==y.variation.crossover_attempts &&
      x.variation.mutation_attempts==y.variation.mutation_attempts &&
      x.variation.contract_rejections==y.variation.contract_rejections &&
      x.variation.changed_children==y.variation.changed_children &&
      x.variation.unchanged_children==y.variation.unchanged_children &&
      x.variation.fallback_children==y.variation.fallback_children,
      "splice policy changed operator accounting");
 }
}

void check_constructive_export(const std::shared_ptr<const grammar::CompiledGrammar>& grammar,
    const std::vector<ProgramGenome>& parents,const std::vector<std::string>& names,
    FitnessSessionGpu& session) {
 const char* setting=std::getenv("GAGP_GPU_CONSTRUCTIVE_EXPORT");
 const bool enabled=setting!=nullptr;const std::string saved=setting?setting:"";
 unsetenv("GAGP_GPU_CONSTRUCTIVE_EXPORT");repro::NativePhasePopulation reference(grammar,parents,names);
 setenv("GAGP_GPU_CONSTRUCTIVE_EXPORT","1",1);repro::NativePhasePopulation candidate(grammar,parents,names);
 unsetenv("GAGP_GPU_CONSTRUCTIVE_EXPORT"); // policy belongs to the original owner
 for(int generation=0;generation<8;++generation) {
  const auto a=reference.export_population(),b=candidate.export_population();
  require(a.size()==b.size(),"constructive export population length changed");
  for(std::size_t i=0;i<a.size();++i) {
   require(a[i].derivation && !b[i].derivation,"constructive owner policy did not control optional canonical cache");
   require(ast_cache_key(a[i].ast)==ast_cache_key(b[i].ast) && a[i].meta.program_key==b[i].meta.program_key,
       "constructive export changed AST or metadata");
   require(!candidate.export_member(i).derivation,"member export unexpectedly retains canonical cache");
   grammar::require_membership(*grammar,b[i]); // independent, no supplied witness
  }
  const auto x=reference.evaluate(session,true),y=candidate.evaluate(session,true);
  require(x.ok && y.ok && x.fitness==y.fitness && x.case_counts==y.case_counts,"constructive export changed full-case GPU results");
  reference.reproduce(x.fitness,2,generation%2?.3:1,UINT64_C(0x16754)+generation);
  candidate.reproduce(y.fitness,2,generation%2?.3:1,UINT64_C(0x16754)+generation);
 }
 auto external=candidate.export_member(0);external.ast.nodes.clear();
 bool rejected=false;try{repro::NativePhasePopulation invalid(grammar,{external},names);}catch(const std::invalid_argument&){rejected=true;}
 require(rejected,"constructive export granted authority to an externally modified AST");
 if(enabled)setenv("GAGP_GPU_CONSTRUCTIVE_EXPORT",saved.c_str(),1);
}

void test_packed_pattern_refresh() {
 using namespace gpu_detail;
 setenv("GAGP_CONSTANT_PHASE","1",1);setenv("GAGP_BOUND_ADD_PHASE","1",1);setenv("GAGP_BOUND_COMPARE_PHASE","1",1);
 PackResult pack;pack.region_phases.resize(1);auto& phase=pack.region_phases[0];auto& m=phase.program;
 const auto instruction=[](unsigned char op,int a=0,unsigned fuel=1) { DInstr i;i.op=op;i.a=a;i.flags=DINSTR_HAS_A;i.fuel=fuel;return i; };
 pack.all_phase_code={instruction(OP_RETURN),instruction(OP_PUSH_CONST,0,3),instruction(OP_RETURN,0,2)};
 pack.all_phase_consts={Value::from_int(99),Value::from_int(7)};
 m.code_offset=1;m.code_len=2;m.const_offset=1;m.const_len=1;
 refresh_owned_phase_patterns(pack,0,1);
 require(m.scalar_constant_index==1 && m.scalar_constant_fuel==5,"packed constant offset/fuel recognition");
 pack.all_phase_code={instruction(OP_LOAD,0),instruction(OP_LOAD,1),instruction(OP_ADD),instruction(OP_RETURN)};
 m.code_offset=0;m.code_len=4;m.n_locals=2;
 pack.region_bindings={{RegionSlotBank::State,0,0},{RegionSlotBank::State,1,1}};phase.binding_count=2;
 refresh_owned_phase_patterns(pack,0,1);
 require(m.scalar_constant_index==-1 && m.scalar_constant_fuel==0 && m.add_left_binding==0 && m.add_right_binding==1,"constant hint survived phase growth into addition");
 pack.all_phase_code={instruction(OP_RETURN),instruction(OP_LOAD,0),instruction(OP_PUSH_CONST),instruction(OP_LE)};
 m.code_offset=1;m.code_len=3;phase.binding_count=1;
 refresh_owned_phase_patterns(pack,0,1);
 require(m.add_left_binding==-1 && m.add_right_binding==-1 && m.compare_binding==0,"add hint survived comparison replacement");
 pack.all_phase_consts[1]=Value::from_float(1.5);
 refresh_owned_phase_patterns(pack,0,1);
 require(m.compare_binding==-1,"comparison hint survived incompatible constant replacement");
 pack.all_phase_code={instruction(OP_RETURN)};m.code_offset=0;m.code_len=1;
 refresh_owned_phase_patterns(pack,0,1);
 require(m.scalar_constant_index==-1 && m.add_left_binding==-1 && m.compare_binding==-1,"phase shrink retained stale pattern metadata");
}
}
int main(){try {
 test_packed_pattern_refresh();
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
 setenv("GAGP_OWNED_PHASE_PACKED","1",1);setenv("GAGP_OWNED_PHASE_PACK","1",1);setenv("GAGP_GPU_PHASE_POPULATION","1",1);setenv("GAGP_GPU_DIAGNOSTICS","1",1);
 setenv("GAGP_REUSE_IMPORTED_EXECUTABLE","1",1);setenv("GAGP_REUSE_PHASE_GRAMMAR","1",1);setenv("GAGP_BATCH_PHASE_IMPORT","1",1);setenv("GAGP_BATCH_PHASE_EXPORT","1",1);
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
 check_inplace_mutation(grammar,parents,names,session);
 check_splice_policy("GAGP_GPU_DEFERRED_CROSSOVER_COPY",grammar,parents,names,session);
 check_splice_policy("GAGP_GPU_FUSED_SPLICE_DEPTH",grammar,parents,names,session);
 check_splice_policy("GAGP_GPU_COMPACT_COMPILE_FRAMES",grammar,parents,names,session);
 check_constructive_export(grammar,parents,names,session);
 repro::NativePhasePopulation owner(grammar,parents,names);
 {
  auto reversed_names=names;std::reverse(reversed_names.begin(),reversed_names.end());
  auto reversed_cases=cases;
  for(auto& bindings:reversed_cases)for(auto& binding:bindings)binding.idx=names.size()-1-binding.idx;
  repro::NativePhasePopulation reordered(grammar,parents,reversed_names);
  FitnessSessionGpu reordered_session;
  require(reordered_session.init(reversed_cases,answers,10000,512,1000).ok,"reordered import session");
  const auto normal=owner.evaluate(session,true), reversed=reordered.evaluate(reordered_session,true);
  require(normal.ok && reversed.ok && normal.fitness==reversed.fitness && normal.case_counts==reversed.case_counts,
      "reused admitted executable lost actual evaluator input order");
 }

 // The run owns its copies; mutating/destroying imported ASTs cannot change it.
 for(auto& p:parents){p.ast.nodes.clear();p.ast.consts.clear();}parents.clear();
 auto exported=owner.export_member(0);const auto original=ast_cache_key(exported.ast);
 exported.ast.nodes.clear();require(ast_cache_key(owner.export_member(0).ast)==original,"external export aliases private genotype");
 const auto initial_memory=owner.device_bytes();std::size_t stable_memory=0;std::set<std::string> seen;
 for(int generation=0;generation<=32;++generation) {
  const auto owned=owner.evaluate(session,true); // exercise packed current generation before lazy reference materialization
  const auto gpu=session.eval_programs(owner.programs(),true);if(!gpu.ok)throw std::runtime_error("GPU phase population evaluation failed at generation "+std::to_string(generation)+": "+gpu.err.message);
  std::vector<BytecodeProgram> independent;
  auto all_exported=owner.export_population();
  require(all_exported.size()==population,"bulk export count");
  for(int i=0;i<population;++i) {
   auto genome=owner.export_member(i);
   require(ast_cache_key(genome.ast)==ast_cache_key(all_exported.at(i).ast),"bulk export differs from member export");
   genome.derivation.reset(); // force independent membership, including reference-cache mode
   grammar::require_membership(*grammar,genome);seen.insert(ast_cache_key(genome.ast));
   independent.push_back(compile_for_eval(genome,names));
   require(gpu.case_counts.at(i)[0]==1024,"not all cases evaluated");
  }
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
  repro::NativePhasePopulation fallback_owner(grammar,owner.export_population(),names);
  const auto original=fallback_owner.evaluate(session,true);require(original.ok,"fallback owner initial eval");
  fallback_owner.reproduce(original.fitness,2,.3,UINT64_C(991));
  const auto owned=fallback_owner.evaluate(different,true); // wrong-type fallback before programs() materializes
  const auto ordinary=different.eval_programs(fallback_owner.programs(),true);
  require(ordinary.ok && owned.ok && ordinary.fitness==owned.fitness && ordinary.case_counts==owned.case_counts &&
      owned.execution_profile=="mixed","owner capability proof applied to different case types");
 }
 // A large right-nested expression forces the compact stack capability to
 // differ between two valid members. Every row must retain its original index.
 {
  auto wide_definition=definition;
  wide_definition.document.object_v.at("search_limits").object_v.at("max_nodes").number_v=128;
  wide_definition.document.object_v.at("search_limits").object_v.at("max_depth").number_v=40;
  const auto wide=std::make_shared<const grammar::CompiledGrammar>(grammar::compile_grammar(
      grammar::parse_definition(grammar::canonical_json(wide_definition.document))));
  const auto seed=grammar::generate_derivation(*wide,73).genome;
  const auto layout=repro::import_native_phase_layout(*wide,seed);
  require(layout.slots.size()==1,"mixed-stack fixture expected one evolving phase");
  int add=-1,bound=-1;
  const auto& expressions=layout.profiles[0].expressions;
  for(std::size_t i=0;i<expressions.size();++i){const auto& e=expressions[i];
   if(e.kind==repro::PhaseExpressionKind::Operation && e.node.opcode==Opcode::Add)add=i;
   if(e.kind==repro::PhaseExpressionKind::Bound)bound=i;
  }
  require(add>=0 && bound>=0,"mixed-stack fixture operations missing");
  auto make_gene=[&](int depth){repro::HostPhaseGene gene;
   const auto append=[&](int id){auto node=expressions[id].node;if(id==bound)node.operand=expressions[id].target;
    gene.nodes.push_back(node);gene.origins.push_back(id);};
   for(int i=0;i<depth;++i){append(add);append(bound);}append(bound);return gene;};
  std::vector<ProgramGenome> members;
  for(int depth:{0,20,1,21}){
   auto member=repro::export_native_phase_layout(*wide,layout,{make_gene(depth)});
   grammar::require_membership(*wide,member);members.push_back(std::move(member));
  }
  // 257 rows exercises both a complete scan tile and a partial final tile.
  const auto seeds=members;while(members.size()<257)members.push_back(seeds[members.size()%seeds.size()]);
  repro::NativePhasePopulation mixed(wide,members,names);
  unsetenv("GAGP_PHASE_CAP_BUCKETS");const auto ordinary=mixed.evaluate(session,true);
  setenv("GAGP_PHASE_CAP_BUCKETS","1",1);const auto bucketed=mixed.evaluate(session,true);
  require(bucketed.ok && bucketed.execution_profile.find("-cap-buckets")!=std::string::npos &&
      bucketed.fitness==ordinary.fitness && bucketed.case_counts==ordinary.case_counts,
      "mixed stack capability buckets changed cases, results or population ordering");
  // A fresh owner is the conservative whole-program-bound control. Both
  // refresh exactly the same compiled offspring rows under different settings.
  repro::NativePhasePopulation conservative(wide,members,names);
  unsetenv("GAGP_PER_PHASE_STACK_BOUND");
  conservative.reproduce(bucketed.fitness,2,0,91);
  setenv("GAGP_PER_PHASE_STACK_BOUND","1",1);
  mixed.reproduce(bucketed.fitness,2,0,91);
  const auto conservative_result=conservative.evaluate(session,true);
  const auto next_bucketed=mixed.evaluate(session,true);
  const auto next_ordinary=session.eval_programs(mixed.programs(),true);
  require(conservative_result.ok && conservative_result.fitness==next_bucketed.fitness &&
      conservative_result.case_counts==next_bucketed.case_counts,"per-phase compiler certificates changed mixed-stack fitness");
  require(next_bucketed.ok && next_bucketed.fitness==next_ordinary.fitness &&
      next_bucketed.case_counts==next_ordinary.case_counts,"partial-tile lowering offsets changed evaluation");
  std::vector<BytecodeProgram> independently_compiled;
  for(std::size_t i=0;i<members.size();++i){auto member=mixed.export_member(i);
    grammar::require_membership(*wide,member);independently_compiled.push_back(compile_for_eval(member,names));}
  require(eval_fitness_cpu(independently_compiled,cases,answers,10000,1000,512)==next_bucketed.fitness,
      "mixed bucket offspring differs from independent CPU evaluation");
  setenv("GAGP_GENERIC_PHASE_VM","1",1);
  mixed.reproduce(next_bucketed.fitness,2,0,93);
  const auto forced_generic=mixed.evaluate(session,true);
  const auto forced_reference=session.eval_programs(mixed.programs(),true);
  require(forced_generic.ok && forced_generic.fitness==forced_reference.fitness &&
      forced_generic.case_counts==forced_reference.case_counts,"generic VM override lost mixed-stack certificate safety");
  unsetenv("GAGP_GENERIC_PHASE_VM");unsetenv("GAGP_PER_PHASE_STACK_BOUND");
  unsetenv("GAGP_PHASE_CAP_BUCKETS");
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
