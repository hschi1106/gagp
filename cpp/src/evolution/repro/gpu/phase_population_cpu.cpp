// CPU reference engine for the native phase population. A plain host
// translation unit: the shared __host__ __device__ operator bodies compile as
// ordinary C++ here, so CPU-only builds provide the same search semantics.
#ifndef __CUDACC__
#define __device__
#define __host__
#endif
#include "phase_population_cpu.hpp"
#include "phase_operators.cuh"
#include "phase_layout.hpp"
#include "parent_fitness_cache.cuh"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/genome_generation.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <set>
#include <unordered_map>

namespace gagp::evo::repro {
namespace {
using namespace phase_ops;
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) { return std::chrono::duration<double,std::milli>(Clock::now()-start).count(); }

// Host rows with the device row layout (fixed capacity per phase row).
struct HostTrees {
 std::vector<PhaseTreeNode> nodes;std::vector<int> origins;std::vector<PhaseGeneSite> sites;
 std::vector<Value> constants;std::vector<PhaseGenerationResult> lengths;
 explicit HostTrees(std::size_t rows):nodes(rows*capacity),origins(rows*capacity),sites(rows*capacity),constants(rows*capacity),lengths(rows) {}
 TreePointers pointers(){return {nodes.data(),origins.data(),sites.data(),constants.data(),lengths.data()};}
 std::size_t bytes()const{return nodes.size()*sizeof(PhaseTreeNode)+origins.size()*sizeof(int)+sites.size()*sizeof(PhaseGeneSite)+
     constants.size()*sizeof(Value)+lengths.size()*sizeof(PhaseGenerationResult);}
};
// Statically normalized tables are exact (see normalize_static_phase_weights),
// so the reference always uses them, independent of engine flags.
struct HostProfile {
 HostPhaseGrammar grammar;std::vector<PhaseProduction> productions;std::vector<PhaseNonterminal> nonterminals;
 explicit HostProfile(const HostPhaseGrammar& h):grammar(h),productions(h.productions),nonterminals(h.nonterminals) {
  normalize_static_phase_weights(grammar,nonterminals,productions);
 }
 PhaseGrammarView view()const{return {grammar.expressions.data(),int(grammar.expressions.size()),productions.data(),int(productions.size()),
  nonterminals.data(),int(nonterminals.size()),grammar.domains.data(),int(grammar.domains.size()),grammar.values.data(),int(grammar.values.size()),
  grammar.minimum.data(),grammar.depth_count,grammar.logical_depth_count,grammar.site_depth};}
};
// The CPU reference engine is single-threaded.
template<class Function> void each_row(int count,Function function) {
 for(int i=0;i<count;++i)function(i);
}
}  // namespace

std::vector<ProgramGenome> generate_phase_initial_population(
    const grammar::CompiledGrammar& grammar, const grammar::GenerationRequest& request,
    const std::vector<std::string>& input_names, int population, std::uint64_t seed, int site_depth) {
 phase_layout_require(population>0 && population<=16384 && site_depth>=1 && site_depth<=15,"initial population size/site depth");
 const auto layout=import_native_phase_layout(grammar,generate_random_genome(seed,grammar,request),nullptr,&input_names,true);
 const int slots=layout.slots.size();phase_layout_require(slots>0 && slots<=8,"phase count capacity");
 std::vector<std::unique_ptr<HostProfile>> profiles;
 for(const auto& slot:layout.slots)profiles.push_back(std::make_unique<HostProfile>(
     compile_phase_variation_grammar(grammar,slot.nonterminal,grammar.search_limits().max_depth,site_depth,true,&input_names)));
 const int available=int(layout.budget.max_nodes)-int(layout.fixed_nodes);
 const auto minimum=[&](int k,int logical)->long {
  const auto& g=profiles[k]->grammar;const int depth=layout.slots[k].max_depth;
  if(depth<=0 || depth>=g.depth_count || logical>=g.logical_depth_count)return 0xffffffffL;
  return g.minimum[(std::size_t(g.root_expression)*g.depth_count+depth)*g.logical_depth_count+logical];
 };
 const auto mix=[](std::uint64_t x){x+=UINT64_C(0x9e3779b97f4a7c15);x=(x^(x>>30))*UINT64_C(0xbf58476d1ce4e5b9);
  x=(x^(x>>27))*UINT64_C(0x94d049bb133111eb);return x^(x>>31);};
 std::vector<ProgramGenome> out;out.reserve(population);
 std::vector<PhaseTreeNode> nodes(capacity);std::vector<int> origins(capacity);std::vector<Value> constants(capacity);
 for(int i=0;i<population;++i) {
  const int ramp=site_depth<2?site_depth:2+i%(site_depth-1);
  std::vector<HostPhaseGene> genes(slots);int used=0;
  for(int k=0;k<slots;++k) {
   const auto& slot=layout.slots[k];const auto& profile=*profiles[k];
   const auto& nt=grammar.nonterminals()[slot.nonterminal];
   PhaseGenerationResult generated;
   // A shallower depth that cannot derive the phase moves up to the cap; each
   // retry draws a fresh seed, so the outcome is a pure function of (seed,i,k).
   for(int logical=ramp,attempt=0;logical<=site_depth && generated.status!=PhaseCompileStatus::Ok;++attempt) {
    long reserve=0;for(int j=k+1;j<slots;++j)reserve+=minimum(j,site_depth);
    const long room=std::min<long>(capacity,long(available)-used-reserve);
    phase_layout_require(room>0,"initial phase budget");
    generated=generate_phase<true,false,false>(profile.view(),profile.grammar.root_expression,int(nt.scope.size()),
        slot.max_depth,int(room),mix(seed^mix((std::uint64_t(i)*8+k)*64+attempt)),
        nodes.data(),origins.data(),constants.data(),nullptr,logical);
    if(generated.status!=PhaseCompileStatus::Ok && attempt%4==3)++logical;
    phase_layout_require(attempt<64,"initial phase generation failed");
   }
   phase_layout_require(generated.status==PhaseCompileStatus::Ok,"initial phase generation failed");
   auto& gene=genes[k];
   gene.nodes.assign(nodes.begin(),nodes.begin()+generated.nodes);
   gene.origins.assign(origins.begin(),origins.begin()+generated.nodes);
   gene.constants.assign(constants.begin(),constants.begin()+generated.constants);
   used+=generated.nodes;
  }
  out.push_back(export_native_phase_layout(grammar,layout,genes));
 }
 return out;
}

struct HostPhasePopulation::Impl {
 std::shared_ptr<const grammar::CompiledGrammar> grammar;
 OperatorPolicy operators;
 std::string profile;
 NativePhaseLayout layout;int population=0,slot_count=0;
 std::vector<std::unique_ptr<HostProfile>> profiles;std::vector<DeviceSlot> slots;
 std::unique_ptr<HostTrees> current,crossed,next,scratch;
 std::vector<OperatorStats> stats;
 std::vector<BytecodeProgram> programs;
 // Completed results of the members currently held in `current`.
 Evaluation completed;bool have_completed=false;
 std::vector<int> reuse; // per member: earlier member index with an identical genotype, or -1
 // Steady state: one lowered program per batch child; members stay unlowered.
 // member_hashes indexes current members by genotype hash for exact-identity reuse.
 std::unordered_multimap<std::uint64_t,int> member_hashes;
 int batch=0;std::vector<BytecodeProgram> batch_programs;
 std::vector<HostPhaseGene> genes(int member)const {
  std::vector<HostPhaseGene> out(slot_count);
  auto trees=const_cast<HostTrees&>(*current).pointers();
  for(int k=0;k<slot_count;++k) {
   const int row=member*slot_count+k;const auto t=trees.view(row);auto& gene=out[k];
   gene.nodes.assign(t.nodes,t.nodes+t.node_count);gene.origins.assign(t.origins,t.origins+t.node_count);
   gene.constants.assign(t.constants,t.constants+t.constant_count);
  }
  return out;
 }
 // Lower every live phase with the shared phase compiler and patch the region
 // executables exactly as the GPU owner materializes them.
 // Lowers the phases of tree member `member` into `executable`; false on rejection.
 bool lower_member(TreePointers trees,int member,BytecodeProgram& executable)const {
  thread_local std::vector<gpu_detail::DInstr> code(code_capacity);thread_local std::vector<Value> values(capacity);
  bool ok=true;
  for(int k=0;k<slot_count;++k) {
   const int row=member*slot_count+k;const auto t=trees.view(row);const auto& slot=slots[k];
   const auto r=valid(trees.lengths[row])?compile_phase_tree<true,true>(t.nodes,t.node_count,t.constants,t.constant_count,slot.locals,slot.local_count,slot.type,
       code.data(),code_capacity,values.data(),capacity):PhaseCompileResult{};
   if(r.status!=PhaseCompileStatus::Ok || r.stack_bound<1 || r.stack_bound>gpu_detail::MAX_STACK){ok=false;continue;}
   if(layout.whole_program){patch_whole_program(executable,layout,code.data(),r.code_count,values.data(),r.constant_count);continue;}
   auto& program=native_phase_at(executable.bounded_region_segments[0],layout.slots[k].ordinal).program;
   program.code.clear();program.instruction_fuel.clear();program.code.reserve(r.code_count);program.instruction_fuel.reserve(r.code_count);
   for(int i=0;i<r.code_count;++i){const auto x=code[i];program.code.push_back({static_cast<Opcode>(x.op),x.a,x.b,bool(x.flags&1),bool(x.flags&2)});program.instruction_fuel.push_back(x.fuel);}
   program.consts.assign(values.begin(),values.begin()+r.constant_count);
  }
  return ok;
 }
 void refresh_programs() {
  std::vector<int> failures(population,0);
  each_row(population,[&](int p){failures[p]=!lower_member(current->pointers(),p,programs[p]);});
  for(int failed:failures)if(failed)throw std::runtime_error("CPU phase compiler rejected a constructed program");
 }
 unsigned nodes(TreePointers trees,int member,bool grammar_sites)const {
  unsigned count=grammar_sites?0:layout.fixed_nodes;
  for(int k=0;k<slot_count;++k) {
   const auto t=trees.view(member*slot_count+k);
   if(!grammar_sites){count+=t.node_count;continue;}
   for(int j=0;j<t.node_count;++j)count+=t.sites[j].nonterminal>=0;
  }
  return count;
 }
};

HostPhasePopulation::HostPhasePopulation(std::shared_ptr<const grammar::CompiledGrammar> grammar,
    const std::vector<ProgramGenome>& input,const std::vector<std::string>& input_names,
    std::optional<grammar::ProjectedBudget> projected,int site_depth,const PhaseVariationPolicy& policy):impl_(std::make_unique<Impl>()) {
 auto& s=*impl_;s.grammar=std::move(grammar);
 validate_phase_variation_policy(policy);
 phase_layout_require(site_depth>=0 && site_depth<=15 && !(site_depth && projected),"site depth must be0..15 and cannot combine with projected resource budgets");
 phase_layout_require(s.grammar && !input.empty() && input.size()<=16384,"population/grammar capacity");
 std::set<std::string> expected_names,actual_names(input_names.begin(),input_names.end());
 for(const auto& binding:s.grammar->inputs())expected_names.insert(binding.name);
 phase_layout_require(actual_names==expected_names && actual_names.size()==input_names.size(),"input names must exactly cover grammar inputs");
 phase_layout_require(input_names.size()<=64,"input local capacity");
 s.operators.counted_sites=policy.site_draw==PhaseSiteDraw::Counted;
 s.operators.uniform_phase=policy.site_scope==PhaseSiteScope::UniformPhase;
 s.operators.crossover_probability=policy.crossover_probability;
 s.operators.crossover_attempts=policy.crossover_attempts;
 s.operators.mutation_max_nodes=policy.mutation_max_nodes;
 // Serial copies stay eager (deferred_copy=false); both schedules are exact.
 NativePhaseGrammarProfiles import_profiles(s.grammar,input_names);
 s.population=input.size();
 s.layout=import_native_phase_layout(*s.grammar,input.front(),&import_profiles,&input_names,true,projected?&*projected:nullptr);
 if(site_depth)for(std::size_t k=0;k<s.layout.slots.size();++k)
  s.layout.profiles[k]=compile_phase_variation_grammar(*s.grammar,s.layout.slots[k].nonterminal,s.grammar->search_limits().max_depth,site_depth,true,&input_names);
 s.profile=site_depth?"native-cpu-phase-v4-site-depth"+std::to_string(site_depth):s.layout.slots.front().logical_depth?"native-cpu-phase-v3-projected":
     s.operators.counted_sites?"native-cpu-phase-v2-counted":"native-cpu-phase-v1";
 s.profile+=phase_variation_profile_suffix(policy);
 if(s.operators.uniform_phase)s.profile+="-uniform-phase";
 s.slot_count=s.layout.slots.size();phase_layout_require(s.slot_count>0 && s.slot_count<=8,"phase count capacity");
 const auto key=native_phase_skeleton_key(s.layout);const std::size_t rows=input.size()*s.slot_count;
 s.current=std::make_unique<HostTrees>(rows);s.crossed=std::make_unique<HostTrees>(rows);
 s.next=std::make_unique<HostTrees>(input.size());s.scratch=std::make_unique<HostTrees>(input.size());
 for(int k=0;k<s.slot_count;++k) {
  const auto& profile=s.layout.profiles[k];const auto& layout=s.layout.slots[k];
  s.profiles.push_back(std::make_unique<HostProfile>(profile));
  const auto& nt=s.grammar->nonterminals()[layout.nonterminal];DeviceSlot slot{};
  slot.grammar=s.profiles.back()->view();slot.root=profile.root_expression;slot.scope=nt.scope.size();slot.max_depth=layout.max_depth;
  slot.logical_depth=site_depth?site_depth:layout.logical_depth;
  slot.type=phase_value_tag(nt.type);
  if(s.layout.whole_program) {
   slot.local_count=input_names.size();
   for(int j=0;j<slot.local_count;++j)for(const auto& binding:s.grammar->inputs())
    if(binding.name==input_names[j])slot.locals[j]=phase_value_tag(binding.type);
  } else {slot.local_count=slot.scope;for(int j=0;j<slot.scope;++j)slot.locals[j]=phase_value_tag(nt.scope[j].type);}
  s.slots.push_back(slot);
 }
 auto trees=s.current->pointers();
 for(std::size_t p=0;p<input.size();++p) {
  auto imported=p==0?s.layout:import_native_phase_layout(*s.grammar,input[p],&import_profiles,&input_names,false,projected?&*projected:nullptr);
  phase_layout_require(imported.slots.size()==s.layout.slots.size() && native_phase_skeleton_key(imported)==key,"mixed fixed skeleton requires native fallback");
  for(int k=0;k<s.slot_count;++k) {
   const auto& a=imported.slots[k];const auto& b=s.layout.slots[k];
   phase_layout_require(a.nonterminal==b.nonterminal && a.ordinal==b.ordinal && a.binders==b.binders && a.max_depth==b.max_depth && a.logical_depth==b.logical_depth,"mixed phase contract requires fallback");
   const int row=p*s.slot_count+k;const auto& gene=imported.genes[k];auto& l=trees.lengths[row];
   l={};l.status=PhaseCompileStatus::Ok;l.nodes=gene.nodes.size();l.constants=gene.constants.size();
   phase_layout_require(l.nodes>0 && l.nodes<=capacity && l.constants<=l.nodes,"imported phase capacity");
   const auto out=trees.output(row);
   std::copy(gene.nodes.begin(),gene.nodes.end(),out.nodes);std::copy(gene.origins.begin(),gene.origins.end(),out.origins);
   std::copy(gene.constants.begin(),gene.constants.end(),out.constants);
   const auto& slot=s.slots[k];const auto t=trees.view(row);
   phase_layout_require(annotate_phase_tree(slot.grammar,slot.root,slot.scope,t.nodes,t.origins,t.node_count,out.sites) &&
       !(slot.grammar.site_depth && phase_site_depth(t.nodes,out.sites,t.node_count)>slot.logical_depth),"import site annotation failed");
  }
  s.programs.push_back(std::move(imported.executable));
 }
 s.stats.resize(s.population);s.reuse.assign(s.population,-1);
}
HostPhasePopulation::~HostPhasePopulation()=default;

HostPhasePopulation::Evaluation HostPhasePopulation::evaluate(const std::vector<CaseBindings>& cases,const std::vector<Value>& expected,
    int fuel,double penalty,int reduction_lanes,const FitnessPolicy& policy) {
 auto& s=*impl_;Evaluation result;phase_layout_require(!s.batch,"steady-state members are scored by evaluate_batch");
 result.fitness.resize(s.population);result.case_counts.resize(s.population);
 const bool wrong=policy.metric!=FitnessMetric::Legacy;if(wrong)result.wrong.resize(s.population);
 std::vector<int> pending;
 for(int i=0;i<s.population;++i) {
  const int source=s.have_completed?s.reuse[i]:-1;
  if(source<0){pending.push_back(i);continue;}
  result.fitness[i]=s.completed.fitness[source];result.case_counts[i]=s.completed.case_counts[source];
  if(wrong)result.wrong[i]=s.completed.wrong[source];++result.reused_programs;
 }
 // Fixed chunks keep each worker's evaluation independent of scheduling.
 constexpr int chunk=32;const int chunks=(int(pending.size())+chunk-1)/chunk;
 each_row(chunks,[&](int c) {
  const int begin=c*chunk,end=std::min<int>(pending.size(),begin+chunk);
  std::vector<BytecodeProgram> batch;for(int j=begin;j<end;++j)batch.push_back(s.programs[pending[j]]);
  std::vector<unsigned> batch_wrong;std::vector<CpuFitnessCaseCounts> counts;
  const auto fitness=eval_fitness_cpu(batch,cases,expected,fuel,penalty,reduction_lanes,policy,wrong?&batch_wrong:nullptr,&counts);
  for(int j=begin;j<end;++j){const int i=pending[j];result.fitness[i]=fitness[j-begin];result.case_counts[i]=counts.at(j-begin);if(wrong)result.wrong[i]=batch_wrong[j-begin];}
 });
 result.evaluated_programs=pending.size();
 s.completed=result;s.have_completed=true;
 return result;
}

ReproductionStats HostPhasePopulation::reproduce(const std::vector<double>& completed_fitness,int pressure,double probability,std::uint64_t seed,int elite_count) {
 auto& s=*impl_;const int p=s.population,k=s.slot_count,available=s.layout.budget.max_nodes-s.layout.fixed_nodes;
 phase_layout_require(elite_count>=0 && elite_count<p,"elite count");
 phase_layout_require(completed_fitness.size()==std::size_t(p) && pressure>0 && pressure<=p &&
   std::isfinite(probability) && probability>=0 && probability<=1,"selection/mutation contract");
 for(double value:completed_fitness)phase_layout_require(!std::isnan(value),"uncanonical fitness");
 ReproductionStats timing;const auto begin=Clock::now();
 const auto current=s.current->pointers(),crossed=s.crossed->pointers(),next=s.next->pointers(),scratch=s.scratch->pointers();
 const double* fitness=completed_fitness.data();
 // Same per-child bodies and draw order as the GPU kernels; children are independent.
 each_row(p,[&](int child) {
  crossover_child<false>(child,current,crossed,s.slots.data(),k,p,available,fitness,pressure,seed,s.operators,s.stats[child]);
  mutate_child<false>(child,crossed,next,scratch,s.slots.data(),k,available,probability,seed,s.operators,s.stats[child]);
 });
 // Elites: highest completed fitness, ties to the lowest source index.
 if(elite_count) {
  std::vector<int> order(p);for(int i=0;i<p;++i)order[i]=i;
  std::stable_sort(order.begin(),order.end(),[&](int a,int b){return fitness[a]>fitness[b];});
  for(int rank=0;rank<elite_count;++rank)for(int j=0;j<k;++j)copy_tree(current,order[rank]*k+j,crossed,rank*k+j);
 }
 each_row(p,[&](int child){compare_child(child,current,crossed,p,k,s.stats[child]);});
 // Exact identity with any parent also reuses that parent's completed result.
 std::unordered_multimap<std::uint64_t,int> parents;
 for(int i=0;i<p;++i)parents.emplace(parent_cache::hash_program(current,i,k),i);
 for(int child=0;child<p;++child) {
  auto& stat=s.stats[child];
  if(stat.error)throw std::runtime_error("CPU genotype variation error "+std::to_string(stat.error));
  if(stat.reusable_parent<0) {
   const auto range=parents.equal_range(parent_cache::hash_program(crossed,child,k));
   for(auto it=range.first;it!=range.second;++it)
    if(parent_cache::equal_program(current,it->second,crossed,child,k)){stat.reusable_parent=it->second;break;}
  }
  timing.variation.crossover_attempts+=stat.crossover_attempted;timing.variation.mutation_attempts+=stat.mutation;
  timing.variation.contract_rejections+=stat.crossover_rejected;
  timing.variation.changed_children+=stat.changed;timing.variation.unchanged_children+=!stat.changed;
  timing.variation.fallback_children+=stat.crossover_attempted && !stat.crossover;
  s.reuse[child]=stat.reusable_parent;
 }
 s.current.swap(s.crossed);
 // Programs belong to member slots, not lineages: rebuild every member.
 s.refresh_programs();
 timing.kernel_ms=elapsed(begin);
 return timing;
}

void HostPhasePopulation::begin_steady(int batch) {
 auto& s=*impl_;phase_layout_require(!s.batch && batch>=1 && batch<=s.population,"steady-state batch contract");
 s.batch=batch;s.stats.assign(s.population,{});
 s.batch_programs.assign(s.programs.begin(),s.programs.begin()+batch);
 s.member_hashes.clear();
 const auto current=s.current->pointers();
 for(int i=0;i<s.population;++i)s.member_hashes.emplace(parent_cache::hash_program(current,i,s.slot_count),i);
}
std::vector<int> HostPhasePopulation::make_batch(const std::vector<double>& selection_fitness,int pressure,double probability,
    std::uint64_t seed,ReproductionStats* timing) {
 auto& s=*impl_;const int p=s.population,k=s.slot_count,b=s.batch,available=s.layout.budget.max_nodes-s.layout.fixed_nodes;
 phase_layout_require(b && selection_fitness.size()==std::size_t(p) && pressure>=1 && pressure<=p &&
   std::isfinite(probability) && probability>=0 && probability<=1,"steady-state selection/mutation contract");
 for(double value:selection_fitness)phase_layout_require(!std::isnan(value),"uncanonical fitness");
 const auto begin=Clock::now();
 const auto current=s.current->pointers(),crossed=s.crossed->pointers(),next=s.next->pointers(),scratch=s.scratch->pointers();
 // The GPU kernel bodies with `b` children drawing parents from all p members.
 each_row(b,[&](int child) {
  crossover_child<false>(child,current,crossed,s.slots.data(),k,p,available,selection_fitness.data(),pressure,seed,s.operators,s.stats[child]);
  mutate_child<false>(child,crossed,next,scratch,s.slots.data(),k,available,probability,seed,s.operators,s.stats[child]);
  compare_child(child,current,crossed,p,k,s.stats[child]);
 });
 std::vector<int> reuse(b,-1);
 for(int child=0;child<b;++child) {
  const auto& stat=s.stats[child];
  if(stat.error)throw std::runtime_error("CPU genotype variation error "+std::to_string(stat.error));
  reuse[child]=stat.reusable_parent;
  if(reuse[child]<0) {
   const auto range=s.member_hashes.equal_range(parent_cache::hash_program(crossed,child,k));
   for(auto it=range.first;it!=range.second;++it)if(parent_cache::equal_program(current,it->second,crossed,child,k)){reuse[child]=it->second;break;}
  }
  if(timing) {
   timing->variation.crossover_attempts+=stat.crossover_attempted;timing->variation.mutation_attempts+=stat.mutation;
   timing->variation.contract_rejections+=stat.crossover_rejected;
   timing->variation.changed_children+=stat.changed;timing->variation.unchanged_children+=!stat.changed;
   timing->variation.fallback_children+=stat.crossover_attempted && !stat.crossover;
  }
 }
 if(timing)timing->kernel_ms+=elapsed(begin);
 return reuse;
}
HostPhasePopulation::Evaluation HostPhasePopulation::evaluate_batch(const std::vector<int>& reuse,const std::vector<CaseBindings>& cases,
    const std::vector<Value>& expected,int fuel,double penalty,int reduction_lanes,const FitnessPolicy& policy) {
 auto& s=*impl_;const int b=s.batch;phase_layout_require(b && reuse.size()==std::size_t(b),"steady-state batch evaluation contract");
 Evaluation result;result.fitness.assign(b,0.0);result.case_counts.assign(b,{});
 const bool wrong=policy.metric!=FitnessMetric::Legacy;if(wrong)result.wrong.assign(b,0);
 std::vector<int> pending;for(int i=0;i<b;++i)if(reuse[i]<0)pending.push_back(i);
 std::vector<int> failures(pending.size(),0);
 each_row(int(pending.size()),[&](int j){failures[j]=!s.lower_member(s.crossed->pointers(),pending[j],s.batch_programs[pending[j]]);});
 for(int failed:failures)if(failed)throw std::runtime_error("CPU phase compiler rejected a constructed program");
 constexpr int chunk=32;const int chunks=(int(pending.size())+chunk-1)/chunk;
 each_row(chunks,[&](int c) {
  const int begin=c*chunk,end=std::min<int>(pending.size(),begin+chunk);
  std::vector<BytecodeProgram> programs;for(int j=begin;j<end;++j)programs.push_back(s.batch_programs[pending[j]]);
  std::vector<unsigned> batch_wrong;std::vector<CpuFitnessCaseCounts> counts;
  const auto fitness=eval_fitness_cpu(programs,cases,expected,fuel,penalty,reduction_lanes,policy,wrong?&batch_wrong:nullptr,&counts);
  for(int j=begin;j<end;++j){const int i=pending[j];result.fitness[i]=fitness[j-begin];result.case_counts[i]=counts.at(j-begin);if(wrong)result.wrong[i]=batch_wrong[j-begin];}
 });
 result.evaluated_programs=pending.size();result.reused_programs=b-pending.size();
 return result;
}
std::vector<unsigned> HostPhasePopulation::batch_nodes(bool grammar_sites)const {
 const auto& s=*impl_;phase_layout_require(s.batch,"steady-state batch nodes");
 std::vector<unsigned> counts(s.batch);
 for(int i=0;i<s.batch;++i)counts[i]=s.nodes(s.crossed->pointers(),i,grammar_sites);
 if(grammar_sites)for(auto c:counts)if(!c)throw std::runtime_error("invalid CPU grammar-site metrics");
 return counts;
}
void HostPhasePopulation::commit_batch(const std::vector<std::array<int,2>>& placements) {
 auto& s=*impl_;const int k=s.slot_count;phase_layout_require(s.batch && placements.size()<=std::size_t(s.batch),"steady-state placements");
 std::vector<bool> seen(s.population,false);
 const auto current=s.current->pointers(),crossed=s.crossed->pointers();
 for(const auto& [member,child]:placements) {
  phase_layout_require(member>=0 && member<s.population && child>=0 && child<s.batch && !seen[member],"steady-state placement");
  seen[member]=true;
  const auto range=s.member_hashes.equal_range(parent_cache::hash_program(current,member,k));
  for(auto it=range.first;it!=range.second;++it)if(it->second==member){s.member_hashes.erase(it);break;}
  for(int j=0;j<k;++j)copy_tree(crossed,child*k+j,current,member*k+j);
  s.member_hashes.emplace(parent_cache::hash_program(current,member,k),member);
 }
}

std::vector<ProgramGenome> HostPhasePopulation::export_population()const {
 std::vector<ProgramGenome> out;out.reserve(impl_->population);
 for(int i=0;i<impl_->population;++i)out.push_back(export_member(i));
 return out;
}
ProgramGenome HostPhasePopulation::export_member(std::size_t index)const {
 phase_layout_require(index<std::size_t(impl_->population),"export index");
 return export_native_phase_layout(*impl_->grammar,impl_->layout,impl_->genes(int(index)));
}
std::vector<unsigned> HostPhasePopulation::node_counts(bool grammar_sites)const {
 const auto& s=*impl_;auto trees=const_cast<HostTrees&>(*s.current).pointers();
 std::vector<unsigned> counts(s.population);
 for(int i=0;i<s.population;++i)counts[i]=s.nodes(trees,i,grammar_sites);
 if(grammar_sites)for(auto c:counts)if(!c)throw std::runtime_error("invalid CPU grammar-site metrics");
 return counts;
}
const char* HostPhasePopulation::profile_name()const{return impl_->profile.c_str();}
std::size_t HostPhasePopulation::host_bytes()const {
 const auto& s=*impl_;return s.current->bytes()+s.crossed->bytes()+s.next->bytes()+s.scratch->bytes();
}
}  // namespace gagp::evo::repro
