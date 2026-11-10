#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdlib>
#include "gagp/evolution/evolve.hpp"
#include <iostream>
#include <set>
#include <stdexcept>
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/case_set.hpp"
#include "gagp/evolution/search_policy.hpp"
#include "gagp/evolution/selection.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "gagp/runtime/payload/payload.hpp"
#include "gagp/runtime/gpu/host_pack_gpu.hpp"
#include "../../src/runtime/gpu/owned_programs.hpp"
#include "../../src/runtime/gpu/shape_result_cache.hpp"
#include "../../src/evolution/repro/gpu/phase_population.hpp"
#include "../../src/evolution/repro/gpu/parent_fitness_cache.cuh"
#include "../../src/evolution/repro/gpu/phase_layout.hpp"
namespace {
using namespace gagp;using namespace gagp::evo;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct ParentCacheProbeNode {
 int kind=1,opcode=2,operand=0,arity=0,fuel=1,merge_fuel=1;
};
struct ParentCacheProbeView {
 const ParentCacheProbeNode* nodes;const int* origins;const Value* constants;
 int node_count,constant_count;
};
struct ParentCacheProbeTrees {
 ParentCacheProbeNode nodes[4];int origins[4]={0,0,0,0};Value values[4];
 int node_counts[4]={1,1,1,1},constant_counts[4]={1,1,1,1};
 __device__ ParentCacheProbeView view(int row)const {
  return {nodes+row,origins+row,values+row,node_counts[row],constant_counts[row]};
 }
};
__global__ void parent_cache_key_probe(int* failures) {
 if(blockIdx.x || threadIdx.x)return;
 namespace cache=repro::parent_cache;
 ParentCacheProbeTrees parents,query;
 for(int i=0;i<4;++i)parents.values[i]=query.values[i]=Value::from_int(i+1);
 int errors=0;
 errors+=!cache::equal_program(parents,0,query,0,1);
 errors+=cache::hash_program(parents,0,1)!=cache::hash_program(query,0,1);
#define CHECK_PARENT_FIELD(field) \
 ++query.nodes[0].field;errors+=cache::equal_program(parents,0,query,0,1);--query.nodes[0].field;
 CHECK_PARENT_FIELD(kind);CHECK_PARENT_FIELD(opcode);CHECK_PARENT_FIELD(operand);
 CHECK_PARENT_FIELD(arity);CHECK_PARENT_FIELD(fuel);CHECK_PARENT_FIELD(merge_fuel);
#undef CHECK_PARENT_FIELD
 ++query.origins[0];errors+=cache::equal_program(parents,0,query,0,1);--query.origins[0];
 query.node_counts[0]=0;errors+=cache::equal_program(parents,0,query,0,1);query.node_counts[0]=1;
 query.constant_counts[0]=0;errors+=cache::equal_program(parents,0,query,0,1);query.constant_counts[0]=1;
 query.values[0].tag=ValueTag::Bool;errors+=cache::equal_program(parents,0,query,0,1);
 query.values[0]=Value::from_int(1);query.values[0].b=true;
 errors+=cache::equal_program(parents,0,query,0,1);
 parents.values[0]=Value::from_float(0.0);query.values[0]=Value::from_float(-0.0);
 errors+=cache::equal_program(parents,0,query,0,1);
 parents.values[0]=Value::from_int(1);query.values[0]=Value::from_int(2);
 // Force every candidate hash to collide. The first candidate is wrong, the
 // second is an exact match from a different parent; hashes alone cannot admit.
 const std::uint64_t colliding[4]={0,0,0,0};int table[4]={0,1,-1,-1};
 errors+=cache::find_parent(parents,query,0,1,colliding,table,3,0)!=1;
 query.values[0]=Value::from_int(99);
 errors+=cache::find_parent(parents,query,0,1,colliding,table,3,0)!=-1;
 for(int i=0;i<4;++i)table[i]=0; // Full probe chain must safely decline.
 errors+=cache::find_parent(parents,query,0,1,colliding,table,3,0)!=-1;
 query.values[0]=parents.values[0];query.values[1]=parents.values[1];
 errors+=!cache::equal_program(parents,0,query,0,2);
 query.values[1]=Value::from_int(99);
 errors+=cache::equal_program(parents,0,query,0,2); // Inspect every phase.
 *failures=errors;
}
void check_parent_cache_keys() {
 int* device=nullptr;
 require(cudaMalloc(reinterpret_cast<void**>(&device),sizeof(int))==cudaSuccess,"parent cache probe allocation");
 parent_cache_key_probe<<<1,1>>>(device);int failures=-1;
 const auto status=cudaMemcpy(&failures,device,sizeof(int),cudaMemcpyDeviceToHost);
 const auto released=cudaFree(device);
 require(status==cudaSuccess && released==cudaSuccess && failures==0,
     "parent cache accepted hash collision, altered genotype field, signed zero or incomplete key");
}


// Every exported member is independently admitted and lowered from its AST alone.
std::vector<BytecodeProgram> independent_programs(const grammar::CompiledGrammar& grammar,
    std::vector<ProgramGenome> members,const std::vector<std::string>& names) {
 std::vector<BytecodeProgram> programs;
 for(auto& member:members) {
  member.derivation.reset();grammar::require_membership(grammar,member);
  programs.push_back(compile_for_eval(member,names));
 }
 return programs;
}
// The owner's scores equal full-case CPU scores of its independently exported members.
void require_cpu_scores(const grammar::CompiledGrammar& grammar,const repro::NativePhasePopulation& owner,
    const std::vector<std::string>& names,const std::vector<CaseBindings>& cases,const std::vector<Value>& answers,
    const FitnessEvalResult& gpu,const char* what,int fuel=10000,int blocksize=512,FitnessPolicy policy={}) {
 std::vector<unsigned> wrong;
 const auto cpu=eval_fitness_cpu(independent_programs(grammar,owner.export_population(),names),cases,answers,
     fuel,1000,blocksize,policy,&wrong);
 require(gpu.ok && cpu==gpu.fitness && (policy.metric==FitnessMetric::Legacy || wrong==gpu.wrong),what);
}
// The public entry point with the resident GPU owner: complete history, ranked
// final members and final scores equal to independent CPU evaluation.
void check_evolution_final_scores(const std::vector<EvalCase>& cases,EvolutionConfig config,
    const std::vector<ProgramGenome>& parents) {
 config.generations=4;config.seed=0x617ba;config.retain_final_population=true;config.skip_final_eval=false;
 config.phase_population=true;
 const auto result=evolve_population(cases,config,&parents);
 require(result.reproduction_profile.rfind("resident-gpu-",0)==0,"resident evolution did not use the GPU phase owner");
 require(result.history_best.size()==std::size_t(config.generations) &&
     result.final_population.size()==parents.size(),"resident evolution omitted history or final members");
 std::vector<std::string> names;for(const auto& input:config.compiled_grammar->inputs())names.push_back(input.name);
 std::vector<CaseBindings> bindings;std::vector<Value> answers;
 for(const auto& c:cases) {
  CaseBindings row;for(std::size_t j=0;j<names.size();++j)row.push_back({int(j),c.inputs.at(names[j])});
  bindings.push_back(std::move(row));answers.push_back(c.expected);
 }
 std::vector<ProgramGenome> members;for(const auto& scored:result.final_population)members.push_back(scored.genome);
 const auto cpu=eval_fitness_cpu(independent_programs(*config.compiled_grammar,members,names),bindings,answers,
     config.fuel,config.penalty,config.gpu_blocksize,config.fitness_policy);
 for(std::size_t i=0;i<cpu.size();++i) {
  require(canonicalize_fitness_for_ranking(cpu[i])==result.final_population[i].fitness,
      "resident evolution final score differs from independent CPU evaluation");
  if(i)require(result.final_population[i-1].fitness>=result.final_population[i].fitness,"final ranking reversed");
 }
}

void check_elite_evolution(const std::shared_ptr<const grammar::CompiledGrammar>& grammar,
    const std::vector<ProgramGenome>& parents,const std::vector<std::string>& names,
    const std::vector<CaseBindings>& cases,const std::vector<Value>& answers) {
 repro::NativePhasePopulation owner(grammar,parents,names);
 FitnessSessionGpu session;const FitnessPolicy policy{FitnessMetric::Exact};
 require(session.init(cases,answers,10000,512,1000,policy).ok,"elite fitness session");
 for(int generation=0;generation<=16;++generation) {
  const auto gpu=owner.evaluate(session,true);
  require_cpu_scores(*grammar,owner,names,cases,answers,gpu,"elite offspring differs from independent full CPU evaluation",10000,512,policy);
  if(generation==16)break;
  auto selection=gpu.fitness;
  if(generation%4==0)std::fill(selection.begin(),selection.end(),0.0);
  if(generation%4==1)std::fill(selection.begin(),selection.end(),-std::numeric_limits<double>::infinity());
  if(generation%4==2)for(std::size_t i=0;i<selection.size();++i)
   selection[i]=i%3==0?std::numeric_limits<double>::infinity():i%3==1?-0.:0.;
  const int elites=generation%4==3?3:generation%4==0?0:1;
  const auto seed=UINT64_C(0x691be4)+generation;const double mutation=generation%3==0?0:generation%3==1?.3:1;
  owner.reproduce(selection,2,mutation,seed,elites);
 }
}

void check_completed_fitness_reuse(const std::shared_ptr<const grammar::CompiledGrammar>& grammar,
    const std::vector<ProgramGenome>& parents,const std::vector<std::string>& names,
    const std::vector<CaseBindings>& cases,const std::vector<Value>& answers) {
 repro::NativePhasePopulation owner(grammar,parents,names);
 require(std::string(owner.profile_name()).find("-parent-fitness-cache")!=std::string::npos,"parent fitness index missing from profile");
 FitnessSessionGpu session;const FitnessPolicy policy{FitnessMetric::Exact};
 require(session.init(cases,answers,10000,512,1000,policy).ok,"completed fitness session init");
 std::size_t reused=0;
 const auto compare=[&](const FitnessEvalResult& a,const FitnessEvalResult& b) {
  require(a.ok && b.ok && a.fitness==b.fitness && a.wrong==b.wrong && a.case_counts==b.case_counts,
      "completed fitness changed score, wrong, errors, timeouts or logical cases");
 };
 // The same programs scored by the ordinary path, which has no completed-score map.
 const auto ordinary=[&](FitnessSessionGpu& target){return target.eval_programs(owner.programs(),true);};
 for(int generation=0;generation<=16;++generation) {
  const auto b=owner.evaluate(session,true);compare(ordinary(session),b);
  require_cpu_scores(*grammar,owner,names,cases,answers,b,"completed fitness differs from independent full CPU execution",10000,512,policy);
  reused+=b.fitness_reused_programs;
  const auto repeated=owner.evaluate(session,true);compare(b,repeated);
  require(repeated.fitness_reused_programs==parents.size() && repeated.physical_program_cases==0 &&
      repeated.fitness_reused_program_cases==parents.size()*cases.size(),"same-generation scores not completely reused or physical work misreported");
  const auto no_diagnostics=owner.evaluate(session,false);
  require(no_diagnostics.ok && no_diagnostics.fitness==b.fitness && no_diagnostics.wrong==b.wrong &&
      no_diagnostics.case_counts.empty(),"completed fitness changed optional diagnostics contract");
  if(generation<16) {
   const auto seed=UINT64_C(0x81a9cc)+generation;const double mutation=generation%3==0?0:generation%3==1?.3:1;
   owner.reproduce(b.fitness,2,mutation,seed,3);
  }
 }
 require(reused>0,"unchanged-offspring fixture never reused a previous-generation score");
 // Same object, same case count, different labels/policy/fuel/reduction lanes.
 auto changed_answers=answers;for(auto& answer:changed_answers)answer=Value::from_int(answer.i+101);
 const FitnessPolicy discounted{FitnessMetric::Absolute,FitnessProximity::Discounted};
 require(session.init(cases,changed_answers,10000,128,7,discounted).ok,"completed fitness reinit labels/policy");
 auto b=owner.evaluate(session,true);compare(ordinary(session),b);
 require(b.fitness_reused_programs==0,"session reinit reused scores from prior labels/policy");
 compare(b,owner.evaluate(session,true));
 for(int fuel:{0,1}) {
  require(session.init(cases,answers,fuel,512,1000,policy).ok,"completed fitness low-fuel reinit");
  b=owner.evaluate(session,true);compare(ordinary(session),b);
  require(b.fitness_reused_programs==0,"new fuel retained completed scores");
  compare(b,owner.evaluate(session,true));
 }
 // A failed init attempt also invalidates a previously ready context's cache.
 require(!session.init(cases,{},10000,512,1000,policy).ok,"invalid init accepted");
 require(session.init(cases,answers,10000,512,1000,policy).ok,"completed fitness restore context");
 b=owner.evaluate(session,true);compare(ordinary(session),b);
 require(b.fitness_reused_programs==0,"failed reinit failed to invalidate owner cache");
 // Skip an evaluation: one-generation maps cannot be followed across the gap.
 for(int i=0;i<2;++i)owner.reproduce(b.fitness,2,.3,UINT64_C(0x151529)+i,1);
 b=owner.evaluate(session,true);compare(ordinary(session),b);
 require(b.fitness_reused_programs==0,"unscored generation gap reused obsolete parent indices");
 FitnessSessionGpu other;
 require(other.init(cases,changed_answers,10000,512,1000,policy).ok,"other completed context init");
 b=owner.evaluate(other,true);compare(ordinary(other),b);
 require(b.fitness_reused_programs==0,"different session reused completed scores");
}

void check_source_crossover_gate(const std::shared_ptr<const grammar::CompiledGrammar>& grammar,
    const std::vector<ProgramGenome>& parents,const std::vector<std::string>& names,
    FitnessSessionGpu& session,const std::vector<CaseBindings>& cases,const std::vector<Value>& answers) {
 const grammar::ProjectedBudget budget{grammar->search_limits().max_nodes,grammar->search_limits().max_depth};
 for(bool uniform:{false,true}) {
  PhaseVariationPolicy base;base.site_scope=uniform?PhaseSiteScope::UniformPhase:PhaseSiteScope::Genome;
  auto gated=base;gated.crossover_probability=.7;
  repro::NativePhasePopulation reference(grammar,parents,names,budget,0,base);
  repro::NativePhasePopulation candidate(grammar,parents,names,budget,0,gated);
  require(std::string(candidate.profile_name()).find("-source-cross07")!=std::string::npos &&
      std::string(reference.profile_name()).find("-source-cross07")==std::string::npos,"crossover gate missing from profile");
  unsigned attempts=0,skips=0;
  for(int generation=0;generation<=8;++generation) {
   const auto before=reference.evaluate(session,true),on=candidate.evaluate(session,true);
   require(before.ok && on.ok,"crossover gate evaluation");
   const auto c=candidate.export_population();
   require(c.size()==parents.size(),"crossover gate lost offspring");
   for(std::size_t i=0;i<c.size();++i) {
    auto fresh=c[i];fresh.derivation.reset();const auto witness=grammar::reconstruct_derivation(*grammar,fresh);
    require(budget.accepts(witness.resources->subtree()),"crossover gate violated projected budget");
    require(on.case_counts.at(i)[0]==cases.size(),"crossover gate omitted cases");
   }
   require_cpu_scores(*grammar,candidate,names,cases,answers,on,"gated offspring GPU/CPU fitness mismatch");
   if(generation==8)break;
   const auto seed=UINT64_C(0x673219)+generation;
   const auto old=reference.reproduce(before.fitness,2,0,seed);
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
}

void check_uniform_phase_sites(const std::shared_ptr<const grammar::CompiledGrammar>& grammar,
    const std::vector<ProgramGenome>& parents,const std::vector<std::string>& names,
    FitnessSessionGpu& session,const std::vector<CaseBindings>& cases,
    const std::vector<Value>& answers) {
 for(bool counted:{false,true}) {
  PhaseVariationPolicy base;base.site_draw=counted?PhaseSiteDraw::Counted:PhaseSiteDraw::Reservoir;
  auto uniform=base;uniform.site_scope=PhaseSiteScope::UniformPhase;
  repro::NativePhasePopulation reference(grammar,parents,names,std::nullopt,0,base);
  repro::NativePhasePopulation candidate(grammar,parents,names,std::nullopt,0,uniform);
  require(std::string(candidate.profile_name()).find("-uniform-phase")!=std::string::npos &&
      std::string(reference.profile_name()).find("-uniform-phase")==std::string::npos,
      "uniform-phase search policy omitted from profile");
  bool trajectory_changed=false;
  for(int generation=0;generation<8;++generation) {
   const auto before=reference.evaluate(session,true),on=candidate.evaluate(session,true);
   require(before.ok && on.ok,"uniform-phase evaluation");
   const auto a=reference.export_population(),c=candidate.export_population();
   require(a.size()==c.size(),"uniform-phase policy lost offspring");
   for(std::size_t i=0;i<a.size();++i) {
    trajectory_changed|=ast_cache_key(a[i].ast)!=ast_cache_key(c[i].ast);
    require(on.case_counts.at(i)[0]==cases.size(),"uniform-phase policy omitted cases");
   }
   require_cpu_scores(*grammar,candidate,names,cases,answers,on,"uniform-phase offspring GPU/CPU fitness mismatch");
   const double mutation=generation%3==0?0:generation%3==1?.3:1;
   const auto seed=UINT64_C(0x89103)+generation;
   reference.reproduce(before.fitness,2,mutation,seed);
   const auto stats=candidate.reproduce(on.fitness,2,mutation,seed);
   require(stats.variation.changed_children+stats.variation.unchanged_children==parents.size(),
       "uniform-phase offspring accounting incomplete");
  }
  require(trajectory_changed,"uniform-phase fixture did not exercise changed draws");
 }
}

void check_site_depth(const std::shared_ptr<const grammar::CompiledGrammar>& grammar,
    const std::vector<ProgramGenome>& parents,const std::vector<std::string>& names,
    FitnessSessionGpu& session,const std::vector<CaseBindings>& cases,const std::vector<Value>& answers) {
 repro::NativePhasePopulation reference(grammar,parents,names);
 const auto initial=reference.site_metrics();std::vector<ProgramGenome> shallow;
 for(std::size_t i=0;i<parents.size();++i)if(initial[i][1]<=4)shallow.push_back(parents[i]);
 require(shallow.size()>=2,"site-depth fixture lacks shallow parents");
 repro::NativePhasePopulation owner(grammar,shallow,names,std::nullopt,4);
 require(std::string(owner.profile_name()).find("v4-site-depth4")!=std::string::npos,"site depth missing from profile");
 bool changed=false;
 for(int gen=0;gen<=16;++gen) {
  const auto gpu=owner.evaluate(session,true);require(gpu.ok,"site-depth fitness failed");
  const auto metrics=owner.site_metrics();const auto exported=owner.export_population();
  std::vector<BytecodeProgram> programs;
  for(std::size_t i=0;i<exported.size();++i) {
   auto member=exported[i];member.derivation.reset();
   const auto witness=grammar::reconstruct_derivation(*grammar,member);
   const auto layout=repro::import_native_phase_layout(*grammar,member);
   unsigned count=0,maximum=0;
   for(const auto& slot:layout.slots)for(const auto& c:witness.choices) {
    if(c.ast_begin<slot.begin || c.ast_end>slot.end)continue;
    ++count;unsigned depth=1;
    for(const auto& parent:witness.choices)
     if(parent.ast_begin>=slot.begin && parent.ast_end<=slot.end &&
        parent.ast_begin<=c.ast_begin && parent.ast_end>=c.ast_end && parent.ast_begin!=c.ast_begin)++depth;
    maximum=std::max(maximum,depth);
   }
   require(metrics[i][0]==count && metrics[i][1]==maximum && maximum<=4,
       "device site metrics/depth differ from independent canonical derivation");
   programs.push_back(compile_for_eval(member,names));
   changed|=ast_cache_key(member.ast)!=ast_cache_key(shallow[i].ast);
  }
  require(eval_fitness_cpu(programs,cases,answers,10000,1000,512)==gpu.fitness,"site-depth full-population CPU/GPU mismatch");
  if(gen<16)owner.reproduce(gpu.fitness,2,gen%2?1:.3,UINT64_C(0x98113)+gen,1);
 }
 require(changed,"site-depth fixture did not vary");
 bool deeper=false;for(const auto& row:initial)deeper|=row[1]>1;
 require(deeper,"site-depth fixture lacks admission boundary");
 bool declined=false;try{repro::NativePhasePopulation reject(grammar,parents,names,std::nullopt,1);}
 catch(const std::invalid_argument&){declined=true;}
 require(declined,"site-depth initial admission silently changed candidates");
 const grammar::ProjectedBudget projected{grammar->search_limits().max_nodes,4};
 declined=false;try{repro::NativePhasePopulation reject(grammar,shallow,names,projected,4);}
 catch(const std::invalid_argument&){declined=true;}
 require(declined,"site depth combined incompatible projected policy");
}


// Exports carry no derivation or certificate; each is independently admitted.
void check_export_contract(const std::shared_ptr<const grammar::CompiledGrammar>& grammar,
    const std::vector<ProgramGenome>& parents,const std::vector<std::string>& names,
    FitnessSessionGpu& session) {
 repro::NativePhasePopulation owner(grammar,parents,names);
 for(int generation=0;generation<8;++generation) {
  const auto all=owner.export_population();
  require(all.size()==parents.size(),"export population length changed");
  for(std::size_t i=0;i<all.size();++i) {
   const auto member=owner.export_member(i);
   require(!all[i].derivation && !member.derivation,"export retained a derivation cache");
   require(ast_cache_key(all[i].ast)==ast_cache_key(member.ast) && all[i].meta.program_key==member.meta.program_key,
       "bulk export differs from member export");
   grammar::require_membership(*grammar,all[i]); // independent, no supplied witness
  }
  const auto x=owner.evaluate(session,true);require(x.ok,"export contract evaluation");
  owner.reproduce(x.fitness,2,generation%2?.3:1,UINT64_C(0x16754)+generation);
 }
 auto external=owner.export_member(0);external.ast.nodes.clear();
 bool rejected=false;try{repro::NativePhasePopulation invalid(grammar,{external},names);}catch(const std::invalid_argument&){rejected=true;}
 require(rejected,"export granted authority to an externally modified AST");
}

void check_initial_packed_verification(const std::shared_ptr<const grammar::CompiledGrammar>& grammar,
    const std::vector<ProgramGenome>& parents,const std::vector<std::string>& names,
    FitnessSessionGpu& session,const std::vector<CaseBindings>& cases,const std::vector<Value>& answers) {
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
 try{repro::NativePhasePopulation bad(grammar,std::vector<ProgramGenome>(16385),names);}catch(const std::invalid_argument&){rejected=true;}
 require(rejected,"oversized population passed initial private packing");
}

void test_packed_pattern_refresh() {
 using namespace gpu_detail;
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

void test_trivial_pattern_refresh() {
 using namespace gpu_detail;
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
}

void check_private_completed_fitness(const std::shared_ptr<const grammar::CompiledGrammar>& grammar,
    const std::vector<ProgramGenome>& parents,const std::vector<std::string>& names,
    const std::vector<CaseBindings>& cases,const std::vector<Value>& answers,FitnessPolicy policy,int fuel) {
 repro::NativePhasePopulation owner(grammar,parents,names);
 FitnessSessionGpu session;require(session.init(cases,answers,fuel,32,1000,policy).ok,"private completed-score session");
 for(int generation=0;generation<=4;++generation) {
  const auto b=owner.evaluate(session,true),again=owner.evaluate(session,true);
  const auto a=session.eval_programs(owner.programs(),true);
  require(a.ok && b.ok && again.ok && a.fitness==b.fitness && a.fitness==again.fitness &&
      a.wrong==b.wrong && a.wrong==again.wrong && a.case_counts==b.case_counts && a.case_counts==again.case_counts,
      "private completed scores retained payload references or changed replay results");
  require(again.fitness_reused_programs==parents.size() && again.physical_program_cases==0 &&
      again.payload_replay_programs==0,"cached private completed scores repeated payload work");
  require_cpu_scores(*grammar,owner,names,cases,answers,b,
      "private completed scores differ from full independent CPU fitness/wrong",fuel,32,policy);
  if(generation<4)owner.reproduce(b.fitness,2,.3,UINT64_C(0x917355)+generation,1);
 }
 const auto missing=Value::from_int_list_hash_len(UINT64_C(0x7ede192d),4);std::vector<Value> unresolved;
 require(!payload::lookup_list(missing,&unresolved),"late payload fixture is already registered");
 auto missing_case=cases.front();
 for(auto& binding:missing_case)if(names[binding.idx]=="source")binding.value=missing;
 FitnessSessionGpu late;
 require(late.init({missing_case},{answers.front()},fuel,32,1000,policy).ok,"late payload session init");
 auto cold=owner.evaluate(late,true),warm=owner.evaluate(late,true);
 auto before=late.eval_programs(owner.programs(),true);
 require(before.ok && cold.ok && warm.ok && before.fitness==cold.fitness && before.wrong==cold.wrong &&
     warm.fitness_reused_programs==parents.size(),"unresolved backing did not establish complete prior scores");
 payload::register_list(missing,{Value::from_int(4),Value::from_int(-7),Value::from_int(12),Value::from_int(2)});
 auto resolved=owner.evaluate(late,true); // Must reject its old revision inside eval_programs_impl.
 auto expected=late.eval_programs(owner.programs(),true);
 require(resolved.ok && expected.ok && resolved.fitness==expected.fitness && resolved.wrong==expected.wrong &&
     resolved.case_counts==expected.case_counts && resolved.fitness_reused_programs==0,
     "newly available payload reused obsolete error/fitness observations");
 auto resolved_again=owner.evaluate(late,true);
 require(resolved_again.ok && resolved_again.fitness==expected.fitness &&
     resolved_again.fitness_reused_programs==parents.size(),"post-resolution revision was not recorded");
}

void test_private_intlist_generations(bool sequence_windows=false,bool edit_distance=false) {
 const auto grammar=std::make_shared<const grammar::CompiledGrammar>(grammar::compile_grammar(
     grammar::load_definition(sequence_windows?GAGP_REPOSITORY_ROOT "/cpp/tests/fixtures/phase_layout_measure_intlist.json":
         GAGP_REPOSITORY_ROOT "/cpp/tests/fixtures/phase_layout_intlist.json")));
 std::vector<std::string> names;for(const auto& binding:grammar->inputs())names.push_back(binding.name);
 std::vector<ProgramGenome> parents;for(int i=0;i<8;++i)parents.push_back(grammar::generate_derivation(*grammar,721+i).genome);
 for(auto& parent:parents) {
  const bool registry=std::any_of(parent.ast.consts.begin(),parent.ast.consts.end(),
      [](const Value& value){return value.tag==ValueTag::IntList;});
  require(registry==!sequence_windows,"private owner fixture registry constants changed");
  // Serialized external parents carry no public derivation or certificate.
  parent.derivation.reset();
  std::shared_ptr<const BytecodeProgram> artifact;
  parent.derivation=std::make_shared<const grammar::DerivationMetadata>(
      grammar::detail::reconstruct_cold_derivation(*grammar,parent,grammar::entry_request(*grammar),nullptr,nullptr,&artifact,&names));
  require(bool(artifact)==registry,"cold executable retention differs from the admission mode");
 }
 std::vector<CaseBindings> cases;std::vector<Value> answers;
 for(int i=0;i<1024;++i) {
  CaseBindings row;
  for(std::size_t j=0;j<names.size();++j)row.push_back({int(j),names[j]=="source"?
      payload::make_int_list_value(std::vector<Value>(sequence_windows && i%32==0?1024:i%67,Value::from_int((INT64_C(1)<<53)+i))):Value::from_int(
      names[j]=="row"?i%2:names[j]=="column"?(i/2)%2:names[j]=="rows" || names[j]=="columns"?2:i%7-3)});
  cases.push_back(row);answers.push_back(sequence_windows?row[0].value:payload::make_int_list_value({Value::from_int(i%7-3)}));
 }
 FitnessPolicy policy;policy.metric=edit_distance?FitnessMetric::Levenshtein:FitnessMetric::Exact;policy.list_allocation_limit=65536;
 const int audit_fuel=sequence_windows?100000:10000;
 FitnessSessionGpu session;require(session.init(cases,answers,audit_fuel,32,1000,policy).ok,"private list owner session");
 repro::NativePhasePopulation owner(grammar,parents,names);
 if(sequence_windows) {
  std::vector<EvalCase> evolution_cases;
  for(std::size_t i=0;i<cases.size();++i){EvalCase row;row.expected=answers[i];
   for(const auto& binding:cases[i])row.inputs[names[binding.idx]]=binding.value;
   evolution_cases.push_back(std::move(row));}
  EvolutionConfig config;config.population_size=parents.size();config.compiled_grammar=grammar;
  config.generation_request=grammar::entry_request(*grammar);config.eval_engine=EvalEngine::GPU;
  config.reproduction_backend=repro::ReproductionBackend::Gpu;config.mutation_subtree_prob=1;
  config.fuel=grammar->execution_limits().fuel;config.penalty=1000;config.gpu_blocksize=32;config.fitness_policy=policy;
  // Keep all case payload roots: evolve may prune the global registry.
  check_evolution_final_scores(evolution_cases,config,parents);
  check_private_completed_fitness(grammar,parents,names,cases,answers,policy,audit_fuel);
 }
 std::vector<CaseBindings> dense_cases(cases.begin(),cases.begin()+19);
 std::vector<Value> dense_answers(answers.begin(),answers.begin()+19);
 FitnessSessionGpu dense_session;
 require(dense_session.init(dense_cases,dense_answers,audit_fuel,512,1000,policy).ok,"fresh dense private session");
 bool changed=false,saw_content_match=false;
 for(int generation=0;generation<=32;++generation) {
  const auto gpu=owner.evaluate(session,true);
  const auto ordinary=session.eval_programs(owner.programs(),true);
  require(gpu.ok && ordinary.ok && gpu.fitness==ordinary.fitness && gpu.wrong==ordinary.wrong &&
      gpu.case_counts==ordinary.case_counts && gpu.payload_workspace_pool_peak_bytes<=gpu.payload_workspace_budget_bytes,
      "owned private phases differ from the ordinary evaluation path");
  require(!sequence_windows || (gpu.execution_profile.find("-private-window")!=std::string::npos && gpu.execution_profile.find("-private-window-declined")==std::string::npos),"sequence private driver declined");
  // The profile lists every capability, accepted or "-declined".
  const auto accepted=[&](const std::string& token){const auto& p=gpu.execution_profile;
   return p.find(token)!=std::string::npos && p.find(token+"-declined")==std::string::npos;};
  require(accepted("-trivial-phases") && accepted("-private-intlist-offset-v1"),"fresh list generation lost capability");
  const auto independent=independent_programs(*grammar,owner.export_population(),names);
  std::vector<unsigned> wrong;
  const auto cpu=eval_fitness_cpu(independent,cases,answers,audit_fuel,1000,32,policy,&wrong);
  require(gpu.fitness==cpu && gpu.wrong==wrong,"new GPU list phases differ from full-case independent CPU audit");
  if(generation%16==0)for(int low_fuel:{0,1,2,3,7,16}) {
    FitnessSessionGpu limited;
    require(limited.init({cases.front()},{answers.front()},low_fuel,32,1000,policy).ok,"private low-fuel init");
    const auto fast=owner.evaluate(limited,true);
    std::vector<unsigned> limited_wrong;
    const auto limited_cpu=eval_fitness_cpu(independent,{cases.front()},{answers.front()},low_fuel,1000,32,policy,&limited_wrong);
    require(fast.ok && fast.fitness==limited_cpu && fast.wrong==limited_wrong,"private phases changed fuel/error ordering");
  }
  const auto dense=owner.evaluate(dense_session,true),dirty_dense=owner.evaluate(dense_session,true);
  std::vector<unsigned> dense_wrong;
  const auto dense_cpu=eval_fitness_cpu(independent,dense_cases,dense_answers,audit_fuel,1000,512,policy,&dense_wrong);
  require(dense.ok && dirty_dense.ok && dense.fitness==dense_cpu && dense.wrong==dense_wrong &&
      dirty_dense.fitness==dense.fitness && dirty_dense.case_counts==dense.case_counts,
      "short case set changed fresh private generations or full CPU audit");
  for(auto value:wrong)saw_content_match|=value<1024;
  for(const auto& count:gpu.case_counts)require(count[0]==1024,"private owner omitted cases");
  if(generation==32)break;
  const auto stats=owner.reproduce(gpu.fitness,2,.3,UINT64_C(9831)+generation);
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
     policy_declined.execution_profile.find("private-intlist-offset-v1-declined")!=std::string::npos,
     "unsupported policy retained cold native phases/private profile");
 auto wrong_cases=cases;
 for(auto& binding:wrong_cases[0])if(names[binding.idx]=="base_value" || names[binding.idx]=="source")binding.value=Value::from_float(1.5);
 FitnessSessionGpu wrong_session;require(wrong_session.init(wrong_cases,answers,audit_fuel,32,1000,policy).ok,"private wrong-type session");
 owner.reproduce(owner.evaluate(session,true).fitness,2,.3,UINT64_C(123912));
 const auto declined=owner.evaluate(wrong_session,true);
 const auto ordinary=wrong_session.eval_programs(owner.programs(),true);
 require(declined.ok && ordinary.ok && declined.fitness==ordinary.fitness && declined.wrong==ordinary.wrong &&
     declined.execution_profile.find("private-intlist-offset-v1-declined")!=std::string::npos,
     "different session reused stale private capability instead of current generic fallback");
}


// Whole-program capacity is independent of the fixed per-phase allocation.
void test_whole_program_node_budget() {
 auto definition=grammar::load_definition(GAGP_REPOSITORY_ROOT "/cpp/tests/fixtures/phase_layout_memo.json");
 auto document=definition.document;
 document.object_v.at("search_limits").object_v.at("max_nodes").number_v=2048;
 document.object_v.at("search_limits").object_v.at("max_depth").number_v=24;
 document.object_v.at("templates").array_v[0].object_v.at("body").object_v.at("structured").object_v.at("plan").object_v.at("limits").object_v.at("cells").number_v=128;
 auto owner_grammar=std::make_shared<const grammar::CompiledGrammar>(grammar::compile_grammar(grammar::parse_definition(grammar::canonical_json(document))));
 auto expression=cli_detail::JsonParser(R"({"bound":"base"})").parse();
 for(int i=0;i<9;++i) {
  auto add=cli_detail::JsonParser(R"({"signature":"add(Int,Int)->Int","args":[]})").parse();
  add.object_v.at("args").array_v={expression,expression};expression=std::move(add);
 }
 const auto generate=[&](const auto& body) {
  auto exact=document;
  auto& nonterminals=exact.object_v.at("nonterminals").array_v;
  auto root=std::find_if(nonterminals.begin(),nonterminals.end(),[](const auto& nt){return nt.object_v.at("id").string_v=="Memo";});
  require(root!=nonterminals.end(),"large fixture root missing");
  root->object_v.at("alternatives").array_v[0].object_v.at("expression").object_v.at("holes").object_v.at("base")=body;
  return grammar::generate_derivation(grammar::compile_grammar(grammar::parse_definition(grammar::canonical_json(exact))),0).genome;
 };
 const auto parent=generate(expression);
 require(parent.ast.nodes.size()>1024 && parent.ast.nodes.size()<=2048,"large whole-AST fixture must cross the old limit");
 auto layout=repro::import_native_phase_layout(*owner_grammar,parent);
 require(layout.genes.size()==1 && layout.genes[0].nodes.size()==1023,"large fixture must fit one phase buffer");
 auto smaller=document;smaller.object_v.at("search_limits").object_v.at("max_nodes").number_v=1024;
 const auto bounded=grammar::compile_grammar(grammar::parse_definition(grammar::canonical_json(smaller)));
 bool declined=false;try{(void)repro::import_native_phase_layout(bounded,parent);}catch(const std::invalid_argument&){declined=true;}
 require(declined,"whole-AST node budget ceased to reject oversized input");
 auto too_large=cli_detail::JsonParser(R"({"signature":"add(Int,Int)->Int","args":[]})").parse();
 too_large.object_v.at("args").array_v={expression,cli_detail::JsonParser(R"({"bound":"base"})").parse()};
 const auto oversized=generate(too_large);declined=false;
 try{(void)repro::import_native_phase_layout(*owner_grammar,oversized);}catch(const std::invalid_argument& e){declined=std::string(e.what()).find("per-phase genotype capacity")!=std::string::npos;}
 require(declined,"per-phase node capacity ceased to reject oversized input");
 std::vector<std::string> names;for(const auto& input:owner_grammar->inputs())names.push_back(input.name);
 std::vector<CaseBindings> cases(16);std::vector<Value> answers(16,Value::from_int(0));
 for(int i=0;i<16;++i)for(std::size_t j=0;j<names.size();++j)
  cases[i].push_back({int(j),Value::from_int(names[j]=="rows" || names[j]=="columns"?4:names[j]=="base_value"?i%7:0)});
 FitnessSessionGpu session;require(session.init(cases,answers,10000,32,1000).ok,"large whole-AST GPU session");
 repro::NativePhasePopulation owner(owner_grammar,{parent,parent},names);
 for(int generation=0;generation<=4;++generation) {
  const auto gpu=owner.evaluate(session,true);require(gpu.ok,"large whole-AST GPU evaluation");
  std::vector<BytecodeProgram> programs;
  for(auto member:owner.export_population()) {
   member.derivation.reset();grammar::require_membership(*owner_grammar,member);
   require(member.ast.nodes.size()<=2048,"large whole-AST reproduction exceeded whole-program budget");
   programs.push_back(compile_for_eval(member,names));
  }
  require(eval_fitness_cpu(programs,cases,answers,10000,1000,32)==gpu.fitness,"large whole-AST CPU/GPU mismatch");
  if(generation<4)owner.reproduce(gpu.fitness,2,.3,UINT64_C(0x4119)+generation);
 }
 // Exercise the public entry point, including generic GPU preflight ordering.
 EvolutionConfig config;config.population_size=2;config.generations=2;
 config.compiled_grammar=owner_grammar;config.generation_request=grammar::entry_request(*owner_grammar);
 config.eval_engine=EvalEngine::GPU;config.reproduction_backend=repro::ReproductionBackend::Gpu;
 config.mutation_subtree_prob=1;config.fuel=10000;config.penalty=1000;config.gpu_blocksize=32;
 config.retain_final_population=true;config.skip_final_eval=false;
 std::vector<EvalCase> evolution_cases;
 for(std::size_t i=0;i<cases.size();++i){EvalCase row;row.expected=answers[i];
  for(std::size_t j=0;j<names.size();++j)row.inputs[names[j]]=cases[i][j].value;
  evolution_cases.push_back(std::move(row));}
 config.phase_population=true;
 const std::vector<ProgramGenome> parents{parent,parent};
 const auto integrated=evolve_population(evolution_cases,config,&parents);
 require(integrated.reproduction_profile.rfind("resident-gpu-",0)==0,
     "large whole-AST entry point did not use the admitted GPU phase owner");
 std::vector<BytecodeProgram> final_programs;
 for(const auto& member:integrated.final_population)final_programs.push_back(compile_for_eval(member.genome,names));
 const auto cpu=eval_fitness_cpu(final_programs,cases,answers,10000,1000,32);
 for(std::size_t i=0;i<cpu.size();++i)require(canonicalize_fitness_for_ranking(cpu[i])==integrated.final_population[i].fitness,
     "large whole-AST public evolution CPU/GPU mismatch");
}
} // namespace

int main(){try {
 test_whole_program_node_budget();
 test_packed_pattern_refresh();
 test_trivial_pattern_refresh();
 test_private_intlist_generations();
 test_private_intlist_generations(false,true);
 test_private_intlist_generations(true);
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
 config.phase_population=true;
 PhaseVariationPolicy counted;counted.site_draw=PhaseSiteDraw::Counted;config.phase_variation=counted;
 check_evolution_final_scores(evolution_cases,config,parents);
 // Per-case counts are diagnostics: the evaluator reports them only on request.
 setenv("GAGP_GPU_DIAGNOSTICS","1",1);
 const auto integrated=evolve_population(evolution_cases,config,&parents);
 unsetenv("GAGP_GPU_DIAGNOSTICS");
 require(integrated.reproduction_profile.rfind("resident-gpu-v2-counted",0)==0,"GPU genotype evolution adapter not used");
 const std::vector<EvalCase> small_cases{evolution_cases.front()};
 const grammar::ProjectedBudget budget{grammar->search_limits().max_nodes,grammar->search_limits().max_depth};
 // Projected budgets select the projected owner only on explicit request; its
 // members are independently admitted under the same budget.
 auto projected=config;projected.generations=1;projected.gpu_blocksize=32;
 projected.gpu_projected_phase_profile=true;
 projected.initial_resource_budget=budget;projected.offspring_resource_budget=budget;
 const auto projected_result=evolve_population(small_cases,projected,&parents);
 require(projected_result.reproduction_profile.rfind("resident-gpu-v3-projected",0)==0,"positive projected opt-in did not use phase owner");
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
 // The phase population is explicit: unsupported policies fail closed.
 const auto rejected_with=[&](EvolutionConfig bad,const char* needle) {
  try{(void)evolve_population(small_cases,bad,&parents);}
  catch(const std::invalid_argument& error){return std::string(error.what()).find(needle)!=std::string::npos;}
  return false;
 };
 auto unsupported_policy=projected;unsupported_policy.mutation_subtree_prob=.5;
 require(rejected_with(unsupported_policy,"default resource/variation policy"),"unsupported phase policy did not fail closed");
 auto asymmetric=projected;--asymmetric.offspring_resource_budget->max_depth;
 require(rejected_with(asymmetric,"matching initial/offspring budgets"),"asymmetric projected budgets accepted");
 auto implicit=config;implicit.generations=1;
 implicit.initial_resource_budget=budget;implicit.offspring_resource_budget=budget;
 require(rejected_with(implicit,"gpu_projected_phase_profile is requested"),"projected budgets without the projected profile accepted");
 require(integrated.timing.generations[0].elapsed_search_ms>=integrated.timing.init_population_ms+integrated.timing.gpu_eval_init_ms &&
     integrated.timing.generations[1].elapsed_search_ms>integrated.timing.generations[0].elapsed_search_ms,
     "search completion timestamps omit initialization or are not monotonic");
 require(integrated.final_population.size()==population && integrated.history_best.size()==2,"phase evolution omitted output/history");
 {
  // Offspring identical to a scored parent reuse its completed fitness.
  const auto& first=integrated.timing.generations[0].evaluation;
  const std::string counts="phase evolution incomplete cases: program_cases="+std::to_string(first.program_cases)+
      " reused="+std::to_string(first.fitness_reused_program_cases)+" expected="+std::to_string(population*1024);
  require(first.program_cases+first.fitness_reused_program_cases==population*1024,counts.c_str());
 }
 for(std::size_t i=1;i<integrated.final_population.size();++i)
   require(integrated.final_population[i-1].fitness>=integrated.final_population[i].fitness,"phase final fitness ranking reversed");
 FitnessSessionGpu session;require(session.init(cases,answers,10000,512,1000).ok,"GPU session init");
 check_parent_cache_keys();
 check_elite_evolution(grammar,parents,names,cases,answers);
 check_completed_fitness_reuse(grammar,parents,names,cases,answers);
 check_initial_packed_verification(grammar,parents,names,session,cases,answers);
 check_site_depth(grammar,parents,names,session,cases,answers);
 check_export_contract(grammar,parents,names,session);
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
 std::vector<CaseBindings> repeated_cases;std::vector<Value> repeated_answers;
 for(int i=0;i<1024;++i){repeated_cases.push_back(cases[i%8]);repeated_answers.push_back(answers[i%8]);}
 const FitnessPolicy repeated_policy{FitnessMetric::Exact};
 FitnessSessionGpu repeated_session;
 require(repeated_session.init(repeated_cases,repeated_answers,10000,512,1000,repeated_policy).ok,"repeated-case session init");
 {
  auto pack=gpu_detail::pack_programs_with_shared_case_count(owner.programs(),1024,0);
  require(gpu_detail::shape_result_program_supported(pack,pack.metas[0]),"scalar memo fixture lacks shape proof");
  auto bad=pack;
  auto& segment=bad.region_segments[bad.metas[0].region_offset];
  const auto& phase=bad.region_phases[segment.base_body_phase].program;
  bad.all_phase_code[phase.code_offset].op=gpu_detail::OP_CALL_BUILTIN;
  bad.all_phase_code[phase.code_offset].a=static_cast<int>(BuiltinId::Index);
  require(!gpu_detail::shape_result_program_supported(bad,bad.metas[0]),"element read entered shape-result cache");
  bad=pack;auto& list_memo=bad.region_segments[bad.metas[0].region_offset];
  list_memo.memoized=true;list_memo.state_types[0]=ValueTag::IntList;
  require(!gpu_detail::shape_result_program_supported(bad,bad.metas[0]),"list-identity memo entered shape-result cache");
 }
 for(int generation=0;generation<=32;++generation) {
  const auto owned=owner.evaluate(session,true); // exercise packed current generation before lazy reference materialization
  const auto gpu=session.eval_programs(owner.programs(),true);if(!gpu.ok)throw std::runtime_error("GPU phase population evaluation failed at generation "+std::to_string(generation)+": "+gpu.err.message);
  auto all_exported=owner.export_population();
  require(all_exported.size()==population,"bulk export count");
  for(int i=0;i<population;++i) {
   require(ast_cache_key(owner.export_member(i).ast)==ast_cache_key(all_exported.at(i).ast),"bulk export differs from member export");
   seen.insert(ast_cache_key(all_exported[i].ast));
   require(gpu.case_counts.at(i)[0]==1024,"not all cases evaluated");
  }
  const auto independent=independent_programs(*grammar,all_exported,names);
  require(owned.ok && owned.fitness==gpu.fitness && owned.case_counts==gpu.case_counts,"owned packing differs from independent full verification");
  const auto cpu=eval_fitness_cpu(independent,cases,answers,10000,1000,512);
  require(cpu==gpu.fitness,"resident GPU offspring differs from independently exported CPU programs");
  // The shape-result cache groups programs; logical case coverage is unchanged.
  require(owned.shape_result_programs>0 && owned.physical_program_cases<population*1024 &&
      owned.execution_profile.find("-shape-result-cache")!=std::string::npos,
      "shape-result cache inactive on the scalar memo fixture");
  if(generation%16==0)for(int low_fuel:{0,1,3,16}) {
    FitnessSessionGpu limited;
    require(limited.init(cases,answers,low_fuel,512,1000).ok,"low-fuel shape session");
    const auto a=owner.evaluate(limited,true);
    require(a.ok && a.fitness==eval_fitness_cpu(independent,cases,answers,low_fuel,1000,512),
        "shape-result cache changed original invocation fuel/error ordering");
  }
  const auto cached=owner.evaluate(repeated_session,true);
  const auto repeated_cpu=eval_fitness_cpu(independent,repeated_cases,repeated_answers,10000,1000,512,repeated_policy);
  require(cached.ok && repeated_cpu==cached.fitness && cached.physical_case_count<=8 &&
      cached.execution_profile.find("-integer-case-reuse")!=std::string::npos,
      "weighted repeated inputs differ from full-case CPU execution");
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
      owned.execution_profile.rfind("mixed",0)==0,"owner capability proof applied to different case types");
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
  for(int generation=0;generation<3;++generation) {
   const auto bucketed=mixed.evaluate(session,true);
   const auto ordinary=session.eval_programs(mixed.programs(),true);
   // Stacks up to MAX_STACK run on the direct phase executor; the 257-row
   // population still spans a complete and a partial scan tile.
   require(bucketed.ok && ordinary.ok && bucketed.fitness==ordinary.fitness && bucketed.case_counts==ordinary.case_counts,
       "mixed stack depths or partial-tile lowering changed cases, results or ordering");
   require(eval_fitness_cpu(independent_programs(*wide,mixed.export_population(),names),cases,answers,10000,1000,512)==bucketed.fitness,
       "mixed bucket offspring differs from independent CPU evaluation");
   mixed.reproduce(bucketed.fitness,2,0,91+2*generation);
  }
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
