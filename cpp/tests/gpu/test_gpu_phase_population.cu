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
#include "gagp/runtime/payload/payload.hpp"
#include "gagp/runtime/gpu/host_pack_gpu.hpp"
#include "../../src/runtime/gpu/owned_programs.hpp"
#include "../../src/evolution/repro/gpu/phase_population.hpp"
#include "../../src/evolution/repro/gpu/phase_layout.hpp"
namespace {
using namespace gagp;using namespace gagp::evo;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void check_source_crossover_gate(const std::shared_ptr<const grammar::CompiledGrammar>& grammar,
    const std::vector<ProgramGenome>& parents,const std::vector<std::string>& names,
    FitnessSessionGpu& session,const std::vector<CaseBindings>& cases,const std::vector<Value>& answers) {
 const char* flags[]={"GAGP_GPU_SOURCE_CROSSOVER_GATE","GAGP_GPU_DEFERRED_CROSSOVER_COPY","GAGP_GPU_UNIFORM_PHASE_SITES"};
 std::string saved[3];bool present[3];
 for(int i=0;i<3;++i){const auto* value=std::getenv(flags[i]);present[i]=value;saved[i]=value?value:"";}
 const grammar::ProjectedBudget budget{grammar->search_limits().max_nodes,grammar->search_limits().max_depth};
 for(bool deferred:{false,true}) {
  if(deferred){setenv(flags[1],"1",1);setenv(flags[2],"1",1);}
  else {unsetenv(flags[1]);unsetenv(flags[2]);}
  unsetenv(flags[0]);repro::NativePhasePopulation reference(grammar,parents,names,budget);
  setenv(flags[0],"0",1);repro::NativePhasePopulation disabled(grammar,parents,names,budget);
  setenv(flags[0],"1",1);repro::NativePhasePopulation candidate(grammar,parents,names,budget);
  unsetenv(flags[0]); // Gate policy is owned by the population.
  require(std::string(reference.profile_name())==disabled.profile_name(),"zero crossover gate changed profile");
  require(std::string(candidate.profile_name()).find("-source-cross07")!=std::string::npos,"crossover gate missing from profile");
  unsigned attempts=0,skips=0;
  for(int generation=0;generation<=8;++generation) {
   const auto before=reference.evaluate(session,true),off=disabled.evaluate(session,true),on=candidate.evaluate(session,true);
   require(before.ok && off.ok && on.ok && before.fitness==off.fitness && before.case_counts==off.case_counts,"disabled crossover gate changed evaluation");
   const auto a=reference.export_population(),b=disabled.export_population(),c=candidate.export_population();
   require(a.size()==b.size() && a.size()==c.size(),"crossover gate lost offspring");
   std::vector<BytecodeProgram> programs;
   for(std::size_t i=0;i<a.size();++i) {
    require(ast_cache_key(a[i].ast)==ast_cache_key(b[i].ast),"disabled crossover gate changed RNG trajectory");
    auto fresh=c[i];fresh.derivation.reset();const auto witness=grammar::reconstruct_derivation(*grammar,fresh);
    require(budget.accepts(witness.resources->subtree()),"crossover gate violated projected budget");
    programs.push_back(compile_for_eval(fresh,names));
    require(on.case_counts.at(i)[0]==cases.size(),"crossover gate omitted cases");
   }
   require(eval_fitness_cpu(programs,cases,answers,10000,1000,512)==on.fitness,"gated offspring GPU/CPU fitness mismatch");
   if(generation==8)break;
   const auto seed=UINT64_C(0x673219)+generation;
   const auto old=reference.reproduce(before.fitness,2,0,seed);
   disabled.reproduce(off.fitness,2,0,seed);
   const auto now=candidate.reproduce(on.fitness,2,0,seed);
   require(old.variation.crossover_attempts==parents.size(),"default crossover stopped attempting every child");
   const auto tried=now.variation.crossover_attempts;
   require(tried<=parents.size() && now.variation.mutation_attempts==0,"invalid gated operator accounting");
   require(now.variation.unchanged_children>=parents.size()-tried && now.variation.changed_children<=tried,
       "skipped crossover did not clone its selected parent");
   require(now.variation.fallback_children<=tried,"skipped crossover counted as failed attempt");
   attempts+=tried;skips+=parents.size()-tried;
  }
  require(attempts>0 && skips>0,"crossover gate fixture did not cover attempts and clones");
 }
 for(int i=0;i<3;++i)if(present[i])setenv(flags[i],saved[i].c_str(),1);else unsetenv(flags[i]);
}

void check_uniform_phase_sites(const std::shared_ptr<const grammar::CompiledGrammar>& grammar,
    const std::vector<ProgramGenome>& parents,const std::vector<std::string>& names,
    FitnessSessionGpu& session,const std::vector<CaseBindings>& cases,
    const std::vector<Value>& answers) {
 const char* flag="GAGP_GPU_UNIFORM_PHASE_SITES";
 const auto* prior=std::getenv(flag);const bool had_prior=prior;const std::string saved=prior?prior:"";
 const auto* counted_prior=std::getenv("GAGP_COUNTED_SITE_DRAW");
 const bool had_counted=counted_prior;const std::string counted_saved=counted_prior?counted_prior:"";
 for(bool counted:{false,true}) {
  if(counted)setenv("GAGP_COUNTED_SITE_DRAW","1",1);else unsetenv("GAGP_COUNTED_SITE_DRAW");
  unsetenv(flag);repro::NativePhasePopulation reference(grammar,parents,names);
  setenv(flag,"0",1);repro::NativePhasePopulation disabled(grammar,parents,names);
  setenv(flag,"1",1);repro::NativePhasePopulation candidate(grammar,parents,names);
  unsetenv(flag); // Policy is captured by the owner, not reread each generation.
  require(std::string(reference.profile_name())==disabled.profile_name(),"zero flag changed profile");
  require(std::string(candidate.profile_name())==std::string(reference.profile_name())+"-uniform-phase",
      "uniform-phase search policy omitted from profile");
  bool trajectory_changed=false;
  for(int generation=0;generation<8;++generation) {
   const auto before=reference.evaluate(session,true),off=disabled.evaluate(session,true),on=candidate.evaluate(session,true);
   require(before.ok && off.ok && on.ok && before.fitness==off.fitness && before.case_counts==off.case_counts,
       "disabled uniform-phase policy changed evaluation");
   const auto a=reference.export_population(),b=disabled.export_population(),c=candidate.export_population();
   require(a.size()==b.size() && a.size()==c.size(),"uniform-phase policy lost offspring");
   std::vector<BytecodeProgram> programs;
   for(std::size_t i=0;i<a.size();++i) {
    require(ast_cache_key(a[i].ast)==ast_cache_key(b[i].ast),"disabled uniform-phase policy changed RNG trajectory");
    trajectory_changed|=ast_cache_key(a[i].ast)!=ast_cache_key(c[i].ast);
    auto fresh=c[i];fresh.derivation.reset();grammar::require_membership(*grammar,fresh);
    programs.push_back(compile_for_eval(fresh,names));
    require(on.case_counts.at(i)[0]==cases.size(),"uniform-phase policy omitted cases");
   }
   require(eval_fitness_cpu(programs,cases,answers,10000,1000,512)==on.fitness,
       "uniform-phase offspring GPU/CPU fitness mismatch");
   const double mutation=generation%3==0?0:generation%3==1?.3:1;
   const auto seed=UINT64_C(0x89103)+generation;
   reference.reproduce(before.fitness,2,mutation,seed);
   disabled.reproduce(off.fitness,2,mutation,seed);
   const auto stats=candidate.reproduce(on.fitness,2,mutation,seed);
   require(stats.variation.changed_children+stats.variation.unchanged_children==parents.size(),
       "uniform-phase offspring accounting incomplete");
  }
  require(trajectory_changed,"uniform-phase fixture did not exercise changed draws");
 }
 if(had_prior)setenv(flag,saved.c_str(),1);else unsetenv(flag);
 if(had_counted)setenv("GAGP_COUNTED_SITE_DRAW",counted_saved.c_str(),1);else unsetenv("GAGP_COUNTED_SITE_DRAW");
}

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

void check_initial_packed_verification(const std::shared_ptr<const grammar::CompiledGrammar>& grammar,
    const std::vector<ProgramGenome>& parents,const std::vector<std::string>& names,
    FitnessSessionGpu& session,const std::vector<CaseBindings>& cases,const std::vector<Value>& answers) {
 const char* flags[]={"GAGP_OWNED_PHASE_PACKED","GAGP_REUSE_IMPORTED_EXECUTABLE"};
 std::string saved[2];bool present[2];
 for(int i=0;i<2;++i){const auto* value=std::getenv(flags[i]);present[i]=value;saved[i]=value?value:"";}
 for(int packed=0;packed<2;++packed)for(int reuse=0;reuse<2;++reuse) {
  if(packed)setenv(flags[0],"1",1);else unsetenv(flags[0]);
  if(reuse)setenv(flags[1],"1",1);else unsetenv(flags[1]);
  auto external=std::vector<ProgramGenome>{parents.front(),parents.back()};
  repro::NativePhasePopulation owner(grammar,external,names);
  external.front().ast.nodes.clear(); // Cannot mutate the owner's admitted copy.
  for(int generation=0;generation<2;++generation) {
   const auto result=owner.evaluate(session,true);
   const auto ordinary=session.eval_programs(owner.programs(),true);
   const auto cpu=eval_fitness_cpu(owner.programs(),cases,answers,10000,1000,512);
   require(result.ok && ordinary.ok && result.fitness==ordinary.fitness && result.fitness==cpu &&
       result.case_counts==ordinary.case_counts,"initial private pack or subsequent lowering changed scores");
   if(!generation)owner.reproduce(result.fitness,2,1.0,9183);
  }
  bool rejected=false;
  try{repro::NativePhasePopulation bad(grammar,external,names);}catch(const std::invalid_argument&){rejected=true;}
  require(rejected,"initial private packing trusted altered external AST");
  rejected=false;
  try{repro::NativePhasePopulation bad(grammar,{},names);}catch(const std::invalid_argument&){rejected=true;}
  require(rejected,"empty population passed initial private packing");
  rejected=false;
  try{repro::NativePhasePopulation bad(grammar,std::vector<ProgramGenome>(8193),names);}catch(const std::invalid_argument&){rejected=true;}
  require(rejected,"oversized population passed initial private packing");
 }
 for(int i=0;i<2;++i)if(present[i])setenv(flags[i],saved[i].c_str(),1);else unsetenv(flags[i]);
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
void test_trivial_pattern_refresh() {
 using namespace gpu_detail;
 const char* flag="GAGP_GPU_TRIVIAL_PHASES";const bool present=std::getenv(flag)!=nullptr;
 const std::string saved=present?std::getenv(flag):"";setenv(flag,"1",1);
 PackResult pack;pack.region_phases.resize(1);auto& phase=pack.region_phases[0];auto& m=phase.program;
 pack.all_phase_code={{OP_PUSH_CONST,DINSTR_HAS_A,0,0,3},{OP_RETURN,0,0,0,2}};
 pack.all_phase_consts={Value::from_int(0),Value::from_int_list_hash_len(17,0)};
 pack.region_bindings.resize(8);
 for(int i=0;i<8;++i)pack.region_bindings[i]={RegionSlotBank::Parameter,static_cast<unsigned>(i),i};
 phase.binding_count=8;m.n_locals=8;m.code_len=2;m.const_offset=1;m.const_len=1;
 refresh_owned_phase_patterns(pack,0,1);
 require(m.trivial_constant_index==1 && m.trivial_load_binding==-1,"unused bindings blocked private constant proof");
 pack.all_phase_consts.push_back(Value::from_int_list_hash_len(23,1));m.const_offset=2;
 refresh_owned_phase_patterns(pack,0,1);
 require(m.trivial_constant_index==2,"private constant retained old generation offset");
 pack.all_phase_code[0].op=OP_LOAD;phase.binding_count=1;m.n_locals=1;
 pack.region_bindings[0]={RegionSlotBank::State,0,0};refresh_owned_phase_patterns(pack,0,1);
 require(m.trivial_constant_index==-1 && m.trivial_load_binding==0,"constant/load metadata replacement");
 pack.region_bindings[0].bank=RegionSlotBank::Parameter;refresh_owned_phase_patterns(pack,0,1);
 require(m.trivial_load_binding==-1,"Parameter load bypassed lazy capture checks");
 phase.binding_count=8;phase.binding_offset=2;m.n_locals=8;pack.region_bindings.resize(10);
 for(int j=0;j<8;++j)pack.region_bindings[2+j]={RegionSlotBank::Parameter,static_cast<unsigned>(j),(j*3)%8};
 pack.region_bindings[7].bank=RegionSlotBank::State;pack.region_bindings[4].bank=RegionSlotBank::Prepared;
 pack.all_phase_code[0].a=7;refresh_owned_phase_patterns(pack,0,1);
 require(m.trivial_load_binding==7,"permuted multi-binding Load selected wrong absolute binding");
 pack.all_phase_code[0].a=6;refresh_owned_phase_patterns(pack,0,1);
 require(m.trivial_load_binding==4,"changed selected local retained preceding generation binding");
 pack.region_bindings[4].bank=RegionSlotBank::Parameter;refresh_owned_phase_patterns(pack,0,1);
 require(m.trivial_load_binding==-1,"selected Parameter hidden among unused captures entered shortcut");
 pack.region_bindings[4].bank=RegionSlotBank::Prepared;pack.region_bindings[3].local=6;
 refresh_owned_phase_patterns(pack,0,1);
 require(m.trivial_load_binding==-1,"ambiguous selected local entered private Load shortcut");
 pack.region_bindings[3].local=3;phase.binding_count=0;refresh_owned_phase_patterns(pack,0,1);
 require(m.trivial_load_binding==-1,"missing selected binding retained shortcut");
 pack.all_phase_code[0].a=0;
 pack.all_phase_code[0].op=OP_PUSH_CONST;m.n_locals=9;refresh_owned_phase_patterns(pack,0,1);
 require(m.trivial_constant_index==-1,"private shortcut exceeded eight-local contract");
 m.n_locals=8;phase.binding_count=9;refresh_owned_phase_patterns(pack,0,1);
 require(m.trivial_constant_index==-1,"private shortcut exceeded eight-binding contract");
 phase.binding_count=8;refresh_owned_phase_patterns(pack,0,1);setenv(flag,"0",1);
 refresh_owned_phase_patterns(pack,0,1);
 require(m.trivial_constant_index==-1 && m.trivial_load_binding==-1,"disabled refresh retained private shortcut");
 if(present)setenv(flag,saved.c_str(),1);else unsetenv(flag);
}

void test_private_intlist_generations(bool sequence_windows=false,bool staged=false) {
 const char* flags[]={"GAGP_GPU_PRIVATE_INTLIST_OFFSETS","GAGP_GPU_GLOBAL_PAYLOAD","GAGP_OWNED_PHASE_PACK","GAGP_OWNED_PHASE_PACKED","GAGP_GPU_COMPACT_INTLIST_CELLS","GAGP_GPU_TRIVIAL_PHASES","GAGP_MEASURE_PHASE_CACHE","GAGP_GPU_PRIVATE_MEASURE_CACHE","GAGP_GPU_PRIVATE_GLOBAL_OFFSETS","GAGP_GPU_PRIVATE_WINDOW_EXECUTOR","GAGP_COMPACT_FRAMES","GAGP_GPU_WINDOW_LEAF_DIRECT","GAGP_GPU_PRIVATE_BOUND_PHASES","GAGP_BOUND_ADD_PHASE","GAGP_BOUND_COMPARE_PHASE","GAGP_GPU_PRIVATE_TYPED_PHASES","GAGP_GPU_PRIVATE_WARP_COPY","GAGP_COLD_PAYLOAD_PROOFS","GAGP_COLD_IMPORT_EXECUTABLES","GAGP_REUSE_IMPORT_CERTIFICATE","GAGP_REUSE_ADMISSION_COMPILE","GAGP_REUSE_IMPORTED_EXECUTABLE","GAGP_GPU_REUSE_PRIVATE_WORKSPACE"};
 struct Restore {
  const char** flags;std::string saved[23];bool present[23];
  explicit Restore(const char** f):flags(f){for(int i=0;i<23;++i){const auto* v=std::getenv(f[i]);present[i]=v;saved[i]=v?v:"";setenv(f[i],"1",1);}}
  ~Restore(){for(int i=0;i<23;++i){if(present[i])setenv(flags[i],saved[i].c_str(),1);else unsetenv(flags[i]);}}
 } restore(flags);
 if(!sequence_windows)setenv("GAGP_GPU_PRIVATE_WINDOW_EXECUTOR","0",1);
 const auto grammar=std::make_shared<const grammar::CompiledGrammar>(grammar::compile_grammar(
     grammar::load_definition(sequence_windows?GAGP_REPOSITORY_ROOT "/cpp/tests/fixtures/phase_layout_measure_intlist.json":
         GAGP_REPOSITORY_ROOT "/cpp/tests/fixtures/phase_layout_intlist.json")));
 std::vector<std::string> names;for(const auto& binding:grammar->inputs())names.push_back(binding.name);
 std::vector<ProgramGenome> parents;for(int i=0;i<8;++i)parents.push_back(grammar::generate_derivation(*grammar,721+i).genome);
 bool retained_cold_artifact=false,certified_cold_miss=false;
 for(std::size_t i=0;i<parents.size();++i) {
  auto& parent=parents[i];
  const bool registry=std::any_of(parent.ast.consts.begin(),parent.ast.consts.end(),
      [](const Value& value){return value.tag==ValueTag::IntList;});
  require(registry==!sequence_windows,"private owner fixture registry constants changed");
  // Serialized external parents carry no public derivation or certificate.
  parent.derivation.reset();
  std::shared_ptr<const BytecodeProgram> artifact;
  if(i==0 && registry) {
   // Keep one valid membership certificate created without executable retention.
   // Enabling reuse must preserve that miss and take ordinary import lowering.
   setenv("GAGP_COLD_IMPORT_EXECUTABLES","0",1);
   parent.derivation=std::make_shared<const grammar::DerivationMetadata>(
       grammar::detail::reconstruct_cold_derivation(*grammar,parent,grammar::entry_request(*grammar)));
   require(bool(parent.derivation->certificate),"cold fallback fixture lacks certificate");
   setenv("GAGP_COLD_IMPORT_EXECUTABLES","1",1);
   const auto reused=grammar::detail::reconstruct_cold_derivation(
       *grammar,parent,grammar::entry_request(*grammar),nullptr,nullptr,&artifact,&names);
   require(!artifact && reused.certificate==parent.derivation->certificate,"certified miss forced unnecessary lowering");
   certified_cold_miss=true;
  } else {
   parent.derivation=std::make_shared<const grammar::DerivationMetadata>(
       grammar::detail::reconstruct_cold_derivation(*grammar,parent,grammar::entry_request(*grammar),nullptr,nullptr,&artifact,&names));
   require(bool(artifact)==registry,"cold executable retention differs from actual constant storage");
  }
  retained_cold_artifact|=bool(artifact);
 }
 require(retained_cold_artifact==!sequence_windows && certified_cold_miss==!sequence_windows,
     "private owner fixture must cover registry reuse/miss and scalar ordinary lowering");
 std::vector<CaseBindings> cases;std::vector<Value> answers;
 for(int i=0;i<1024;++i) {
  CaseBindings row;
  for(std::size_t j=0;j<names.size();++j)row.push_back({int(j),names[j]=="source"?
      payload::make_int_list_value(std::vector<Value>(sequence_windows && i%32==0?1024:i%67,Value::from_int((INT64_C(1)<<53)+i))):Value::from_int(
      names[j]=="row"?i%2:names[j]=="column"?(i/2)%2:names[j]=="rows" || names[j]=="columns"?2:i%7-3)});
  cases.push_back(row);answers.push_back(sequence_windows?row[0].value:payload::make_int_list_value({Value::from_int(i%7-3)}));
 }
 FitnessPolicy policy;policy.metric=FitnessMetric::Exact;policy.list_allocation_limit=65536;
 const int audit_fuel=sequence_windows?100000:10000;
 FitnessSessionGpu session;require(session.init(cases,answers,audit_fuel,32,1000,policy).ok,"private list owner session");
 const auto* stage_prior=std::getenv("GAGP_GPU_PHASE_STAGED_BUFFERS");
 const bool stage_present=stage_prior;const std::string stage_saved=stage_prior?stage_prior:"";
 setenv("GAGP_GPU_PHASE_STAGED_BUFFERS","0",1);
 setenv("GAGP_COLD_IMPORT_EXECUTABLES","0",1);
 repro::NativePhasePopulation cold_control(grammar,parents,names);
 setenv("GAGP_GPU_PHASE_STAGED_BUFFERS",staged?"1":"0",1);
 setenv("GAGP_COLD_IMPORT_EXECUTABLES","1",1);
 repro::NativePhasePopulation owner(grammar,parents,names);
 if(stage_present)setenv("GAGP_GPU_PHASE_STAGED_BUFFERS",stage_saved.c_str(),1);else unsetenv("GAGP_GPU_PHASE_STAGED_BUFFERS");
 if(staged)require(owner.device_bytes()<cold_control.device_bytes(),"staged owner retained reproduction buffers at cold admission");
 bool changed=false,saw_content_match=false;
 for(int generation=0;generation<=32;++generation) {
  setenv("GAGP_GPU_REUSE_PRIVATE_WORKSPACE","0",1);
  setenv("GAGP_GPU_PRIVATE_WARP_COPY","0",1);
  setenv("GAGP_GPU_PRIVATE_TYPED_PHASES","0",1);
  setenv("GAGP_GPU_PRIVATE_GLOBAL_OFFSETS","0",1);
  setenv("GAGP_GPU_PRIVATE_BOUND_PHASES","0",1);
  setenv("GAGP_GPU_PRIVATE_MEASURE_CACHE","0",1);
  setenv("GAGP_GPU_PRIVATE_WINDOW_EXECUTOR","0",1);
  const auto uncached=owner.evaluate(session,true);
  setenv("GAGP_GPU_PRIVATE_MEASURE_CACHE","1",1);
  setenv("GAGP_GPU_PRIVATE_WINDOW_EXECUTOR",sequence_windows?"1":"0",1);
  setenv("GAGP_GPU_COMPACT_INTLIST_CELLS","0",1);
  setenv("GAGP_GPU_PRIVATE_WARP_COPY","1",1);
  const auto boxed=owner.evaluate(session,true);
  setenv("GAGP_GPU_PRIVATE_WARP_COPY","0",1);
  setenv("GAGP_GPU_COMPACT_INTLIST_CELLS","1",1);
  const auto no_bound=owner.evaluate(session,true);
  setenv("GAGP_GPU_PRIVATE_BOUND_PHASES","1",1);
  const auto canonical=owner.evaluate(session,true);
  setenv("GAGP_GPU_PRIVATE_GLOBAL_OFFSETS","1",1);
  const auto untyped=owner.evaluate(session,true);
  setenv("GAGP_GPU_PRIVATE_TYPED_PHASES","1",1);
  const auto no_warp=owner.evaluate(session,true);
  setenv("GAGP_GPU_PRIVATE_WARP_COPY","1",1);
  const auto unpooled=owner.evaluate(session,true);
  setenv("GAGP_GPU_REUSE_PRIVATE_WORKSPACE","1",1);
  const auto gpu=owner.evaluate(session,true);
  const auto dirty=owner.evaluate(session,true);
  require(unpooled.ok && unpooled.fitness==gpu.fitness && unpooled.wrong==gpu.wrong && unpooled.case_counts==gpu.case_counts &&
      dirty.ok && dirty.fitness==gpu.fitness && dirty.wrong==gpu.wrong && dirty.case_counts==gpu.case_counts &&
      (staged?gpu.payload_workspace_retained_bytes==0:gpu.payload_workspace_retained_bytes>0) && gpu.payload_workspace_pool_peak_bytes<=gpu.payload_workspace_budget_bytes &&
      dirty.payload_workspace_pool_peak_bytes<=dirty.payload_workspace_budget_bytes,
      "fresh-generation pooled cells/frames/memo differ from ordinary execution");
  const auto cold_off=cold_control.evaluate(session,true);
  require(cold_off.ok && cold_off.fitness==gpu.fitness && cold_off.wrong==gpu.wrong && cold_off.case_counts==gpu.case_counts,
      "cold executable reuse changed fresh-generation full-case fitness");
  require(no_warp.ok && no_warp.fitness==gpu.fitness && no_warp.wrong==gpu.wrong &&
      no_warp.case_counts==gpu.case_counts && no_warp.payload_replay_programs==gpu.payload_replay_programs &&
      no_warp.payload_replay_value_capacity==gpu.payload_replay_value_capacity && no_warp.execution_profile.find("-private-warp-copy")==std::string::npos &&
      gpu.execution_profile.find("-private-warp-copy")!=std::string::npos &&
      boxed.execution_profile.find("-private-warp-copy-declined")!=std::string::npos &&
      untyped.ok && untyped.fitness==gpu.fitness && untyped.wrong==gpu.wrong &&
      untyped.case_counts==gpu.case_counts && untyped.execution_profile.find("-private-typed-phases")==std::string::npos &&
      gpu.execution_profile.find("-private-typed-phases")!=std::string::npos &&
      no_bound.ok && no_bound.fitness==gpu.fitness && no_bound.wrong==gpu.wrong &&
      no_bound.case_counts==gpu.case_counts && no_bound.execution_profile.find("-private-bound-phases")==std::string::npos &&
      gpu.execution_profile.find("-private-bound-phases")!=std::string::npos &&
      uncached.ok && boxed.ok && gpu.ok && uncached.fitness==gpu.fitness && uncached.wrong==gpu.wrong &&
      uncached.case_counts==gpu.case_counts && gpu.execution_profile.find("-private-measure-cache")!=std::string::npos &&
      boxed.fitness==gpu.fitness && boxed.wrong==gpu.wrong,"cached/uncached fresh private phases differ");
  require(canonical.ok && gpu.ok && canonical.fitness==gpu.fitness && canonical.wrong==gpu.wrong &&
      canonical.case_counts==gpu.case_counts && gpu.execution_profile.find("-private-global-offsets")!=std::string::npos,
      "global descriptor staging changed fresh GPU phases");
  require(boxed.ok && gpu.ok && boxed.fitness==gpu.fitness && boxed.wrong==gpu.wrong &&
      boxed.case_counts==gpu.case_counts && boxed.execution_profile.find("-compact-intlist-cells")==std::string::npos &&
      gpu.execution_profile.find("-compact-intlist-cells")!=std::string::npos,
      "private bound/boxed/compact phases differ across fresh GPU generations");
  require(!sequence_windows || (gpu.execution_profile.find("-private-window")!=std::string::npos && gpu.execution_profile.find("-private-window-declined")==std::string::npos),"sequence private driver declined");
  require(gpu.ok && gpu.execution_profile.find("-trivial-phases")!=std::string::npos && gpu.execution_profile.find("private-intlist-offset-v1")!=std::string::npos &&
      gpu.execution_profile.find("-declined")==std::string::npos,"fresh list generation lost capability");
  std::vector<BytecodeProgram> independent;
  for(auto genome:owner.export_population()) {
   genome.derivation.reset();grammar::require_membership(*grammar,genome);
   independent.push_back(compile_for_eval(genome,names));
  }
  std::vector<unsigned> wrong;
  const auto cpu=eval_fitness_cpu(independent,cases,answers,audit_fuel,1000,32,policy,&wrong);
  require(gpu.fitness==cpu && gpu.wrong==wrong,"new GPU list phases differ from full-case independent CPU audit");
  for(auto value:wrong)saw_content_match|=value<1024;
  for(const auto& count:gpu.case_counts)require(count[0]==1024,"private owner omitted cases");
  if(generation==32)break;
  const auto stats=owner.reproduce(gpu.fitness,2,.3,UINT64_C(9831)+generation);
  cold_control.reproduce(gpu.fitness,2,.3,UINT64_C(9831)+generation);
  changed|=stats.variation.changed_children!=0;
 }
 require(changed && saw_content_match,"private list owner did not generate new phases/content matches");
 // Same input types but an unsupported policy must use ordinary current phases.
 auto unsupported_policy=policy;unsupported_policy.list_allocation_limit=0;
 FitnessSessionGpu unsupported_session;
 require(unsupported_session.init(cases,answers,audit_fuel,32,1000,unsupported_policy).ok,"private unsupported policy init");
 owner.reproduce(owner.evaluate(session,true).fitness,2,.3,UINT64_C(17913));
 const auto policy_declined=owner.evaluate(unsupported_session,true);
 const auto policy_ordinary=unsupported_session.eval_programs(owner.programs(),true);
 require(policy_declined.ok && policy_ordinary.ok && policy_declined.fitness==policy_ordinary.fitness &&
     policy_declined.wrong==policy_ordinary.wrong &&
     policy_declined.execution_profile.find("private-intlist-offset-v1-declined")!=std::string::npos &&
     policy_declined.execution_profile.find("compact-intlist-cells-declined")!=std::string::npos &&
     policy_declined.execution_profile.find("-trivial-phases-declined")!=std::string::npos &&
     policy_declined.execution_profile.find("-private-bound-phases-declined")!=std::string::npos &&
     policy_declined.execution_profile.find("-private-typed-phases-declined")!=std::string::npos &&
     policy_declined.execution_profile.find("-private-warp-copy-declined")!=std::string::npos,
     "unsupported policy retained cold native phases/private profile");
 auto wrong_cases=cases;
 for(auto& binding:wrong_cases[0])if(names[binding.idx]=="base_value" || names[binding.idx]=="source")binding.value=Value::from_float(1.5);
 FitnessSessionGpu wrong_session;require(wrong_session.init(wrong_cases,answers,audit_fuel,32,1000,policy).ok,"private wrong-type session");
 owner.reproduce(owner.evaluate(session,true).fitness,2,.3,UINT64_C(123912));
 const auto declined=owner.evaluate(wrong_session,true);
 const auto ordinary=wrong_session.eval_programs(owner.programs(),true);
 require(declined.ok && ordinary.ok && declined.fitness==ordinary.fitness && declined.wrong==ordinary.wrong &&
     declined.execution_profile.find("private-intlist-offset-v1-declined")!=std::string::npos &&
     declined.execution_profile.find("compact-intlist-cells-declined")!=std::string::npos &&
     declined.execution_profile.find("-trivial-phases-declined")!=std::string::npos &&
     declined.execution_profile.find("-private-bound-phases-declined")!=std::string::npos &&
     declined.execution_profile.find("-private-typed-phases-declined")!=std::string::npos &&
     declined.execution_profile.find("-private-warp-copy-declined")!=std::string::npos,
     "different session reused stale private capability instead of current generic fallback");
}

int main(){try {
 test_packed_pattern_refresh();
 test_trivial_pattern_refresh();
 test_private_intlist_generations();
 test_private_intlist_generations(true);
 test_private_intlist_generations(false,true);
 test_private_intlist_generations(true,true);
 auto definition=grammar::load_definition(GAGP_REPOSITORY_ROOT "/cpp/tests/fixtures/phase_layout_memo.json");
 // Keep the original oversized fixture as a required explicit rejection.
 // The positive fixture is a separately authored GPU-capacity contract.
 const auto oversized=std::make_shared<const grammar::CompiledGrammar>(grammar::compile_grammar(definition));
 std::vector<std::string> oversized_names;for(const auto& x:oversized->inputs())oversized_names.push_back(x.name);
 bool unsupported=false;
 try {repro::NativePhasePopulation reject(oversized,{grammar::generate_derivation(*oversized,7).genome},oversized_names);}
 catch(const std::invalid_argument& e){unsupported=std::string(e.what()).find("capacity 256")!=std::string::npos;}
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
 // Existing projected-budget callers must retain generic GPU fallback unless
 // they explicitly request the strict projected phase profile.
 auto legacy=config;legacy.generations=1;legacy.gpu_blocksize=32;
 legacy.initial_resource_budget=grammar::ProjectedBudget{grammar->search_limits().max_nodes,grammar->search_limits().max_depth};
 legacy.offspring_resource_budget=legacy.initial_resource_budget;
 legacy.mutation_subtree_prob=.5; // Unsupported phase policy, valid generic policy.
 const std::vector<EvalCase> small_cases{evolution_cases.front()};
 // Positive strict opt-in must select the projected owner and publish members
 // independently admitted under the same budget, not merely reject bad flags.
 auto projected=config;projected.generations=1;projected.gpu_blocksize=32;
 projected.gpu_projected_phase_profile=true;
 projected.initial_resource_budget=legacy.initial_resource_budget;
 projected.offspring_resource_budget=legacy.offspring_resource_budget;
 const auto projected_result=evolve_population(small_cases,projected,&parents);
 require(projected_result.reproduction_profile=="native-gpu-phase-v3-projected" &&
     projected_result.reproduction_fallback_reason.empty(),"positive projected opt-in did not use phase owner");
 std::vector<BytecodeProgram> projected_programs;
 for(const auto& scored:projected_result.final_population) {
  auto fresh=scored.genome;fresh.derivation.reset();
  const auto witness=grammar::reconstruct_derivation(*grammar,fresh);
  require(projected.offspring_resource_budget->accepts(witness.resources->subtree()),"projected offspring exceeds canonical budget");
  projected_programs.push_back(compile_for_eval(fresh,names));
 }
 const auto projected_cpu=eval_fitness_cpu(projected_programs,{cases.front()},{answers.front()},10000,1000,32);
 for(std::size_t i=0;i<projected_cpu.size();++i)
  require(canonicalize_fitness_for_ranking(projected_cpu[i])==projected_result.final_population[i].fitness,
      "positive projected opt-in offspring CPU fitness mismatch");
 const auto generic=evolve_population(small_cases,legacy,&parents);
 require(generic.reproduction_profile=="native-ast" && !generic.reproduction_fallback_reason.empty(),
     "legacy projected budgets no longer fall back to generic GPU reproduction");
 auto strict=legacy;strict.gpu_projected_phase_profile=true;
 bool strict_rejected=false;
 try{(void)evolve_population(small_cases,strict,&parents);}catch(const std::invalid_argument& error){
   strict_rejected=std::string(error.what()).find("default resource/variation policy")!=std::string::npos;}
 require(strict_rejected,"explicit projected phase profile silently fell back");
 legacy.mutation_subtree_prob=1;
 --legacy.offspring_resource_budget->max_depth;
 const auto asymmetric=evolve_population(small_cases,legacy,&parents);
 require(asymmetric.reproduction_profile=="native-ast" && !asymmetric.reproduction_fallback_reason.empty(),
     "legacy asymmetric resource budgets no longer retain generic GPU controls");
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
 check_initial_packed_verification(grammar,parents,names,session,cases,answers);
 check_inplace_mutation(grammar,parents,names,session);
 setenv("GAGP_GPU_PHASE_STAGED_BUFFERS","1",1);
 check_inplace_mutation(grammar,parents,names,session);
 unsetenv("GAGP_GPU_PHASE_STAGED_BUFFERS");
 check_splice_policy("GAGP_GPU_PHASE_STAGED_BUFFERS",grammar,parents,names,session);
 check_splice_policy("GAGP_GPU_DEFERRED_CROSSOVER_COPY",grammar,parents,names,session);
 check_splice_policy("GAGP_GPU_FUSED_SPLICE_DEPTH",grammar,parents,names,session);
 check_splice_policy("GAGP_GPU_COMPACT_COMPILE_FRAMES",grammar,parents,names,session);
 check_constructive_export(grammar,parents,names,session);
 check_uniform_phase_sites(grammar,parents,names,session,cases,answers);
 check_source_crossover_gate(grammar,parents,names,session,cases,answers);
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
 {
  std::vector<double> elite_scores(population,-1);elite_scores[3]=0;
  const auto before=ast_cache_key(owner.export_member(3).ast);
  owner.reproduce(elite_scores,2,1.0,812,1);
  require(ast_cache_key(owner.export_member(0).ast)==before,"GPU elite was mutated or lost");
 }
 rejected=false;try{(void)owner.reproduce({},2,.3,0);}catch(const std::invalid_argument&){rejected=true;}
 require(rejected,"incomplete fitness accepted");
 require(owner.programs().size()==population,"rejected external arguments invalidated live owner");
 std::cout<<"generations=32 population="<<population<<" cases=1024 distinct_exported="<<seen.size()<<" bytes_initial="<<initial_memory
   <<" bytes_gen8="<<stable_memory<<" bytes_final="<<owner.device_bytes()<<" exact_cpu_fitness=true\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
