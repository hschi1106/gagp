// The CPU reference and the GPU owner of the native phase population must
// publish identical searches: same final population ASTs, fitness, wrong
// counts and per-generation champions, across policies and engine variants.
#include <cstdlib>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include "gagp/cli/commands.hpp"
#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "../../src/evolution/repro/gpu/phase_layout.hpp"
#include "../../src/evolution/repro/gpu/phase_choice_tables.hpp"

namespace {
using namespace gagp;using namespace gagp::evo;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
struct Env {
 std::map<std::string,std::optional<std::string>> saved;
 void set(const char* name,const char* value) {
  if(!saved.count(name)){const char* old=std::getenv(name);saved[name]=old?std::optional<std::string>(old):std::nullopt;}
  if(value)setenv(name,value,1);else unsetenv(name);
 }
 ~Env(){for(const auto& [name,value]:saved)if(value)setenv(name.c_str(),value->c_str(),1);else unsetenv(name.c_str());}
};
struct Trace {
 std::vector<std::string> final_asts;std::vector<double> final_fitness;std::vector<int> final_wrong;
 std::vector<std::string> champions;std::vector<double> best,mean;std::string profile;
};
Trace trace(const EvolutionResult& r) {
 Trace t;t.profile=r.reproduction_profile;
 for(const auto& m:r.final_population){t.final_asts.push_back(cli_detail::encode_ast_json(m.genome.ast));t.final_fitness.push_back(m.fitness);t.final_wrong.push_back(m.wrong);}
 for(const auto& m:r.history_best)t.champions.push_back(cli_detail::encode_ast_json(m.genome.ast));
 t.best=r.history_best_fitness;t.mean=r.history_mean_fitness;return t;
}
void require_same(const Trace& a,const Trace& b,const std::string& label) {
 require(!a.final_asts.empty() && a.final_asts==b.final_asts,label+": final population ASTs differ");
 require(a.final_fitness==b.final_fitness && a.final_wrong==b.final_wrong,label+": final fitness/wrong differ");
 require(a.champions==b.champions && a.best==b.best && a.mean==b.mean,label+": generation champions differ");
}

// Exact threshold tables select the production the serial weighted loop selects.
void check_choice_tables(const std::shared_ptr<const grammar::CompiledGrammar>& grammar,const ProgramGenome& sample,
    const std::vector<std::string>& names) {
 const auto layout=repro::import_native_phase_layout(*grammar,sample,nullptr,&names);
 std::mt19937_64 rng(77);std::size_t checked=0;
 for(const auto& profile:layout.profiles)for(bool normalize:{false,true}) {
  auto productions=profile.productions;auto nts=profile.nonterminals;
  if(normalize)repro::normalize_static_phase_weights(profile,nts,productions);
  const auto tables=repro::build_phase_choice_tables(profile,nts,productions);
  for(std::size_t n=0;n<nts.size();++n)for(int d=0;d<profile.depth_count;++d)for(int l=0;l<profile.logical_depth_count;++l) {
   const auto& header=tables.headers[(n*profile.depth_count+d)*profile.logical_depth_count+l];
   if(header.value_count<0)continue;
   const auto& nt=nts[n];
   for(int v=0;v<header.value_count;++v) {
    const auto& set=tables.sets[header.value_offset+v];if(set.status)continue;
    const unsigned available=tables.values[header.value_offset+v];
    // Independent replay of the serial device loop for this eligible set.
    std::vector<int> eligible;double maximum=nt.normalized_weights?1:0;
    for(int i=0;i<nt.production_count;++i){const auto& p=productions[nt.production_offset+i];
     if(profile.minimum[(std::size_t(p.expression)*profile.depth_count+d)*profile.logical_depth_count+l]<=available){
      eligible.push_back(p.expression);if(!nt.normalized_weights && p.weight>maximum)maximum=p.weight;}}
    std::vector<double> terms;double total=0;
    for(int i=0;i<nt.production_count;++i){const auto& p=productions[nt.production_offset+i];
     if(profile.minimum[(std::size_t(p.expression)*profile.depth_count+d)*profile.logical_depth_count+l]<=available){
      terms.push_back(nt.normalized_weights?p.weight:p.weight/maximum);total+=terms.back();}}
    require(int(eligible.size())==set.count,"choice table eligible count");
    std::vector<std::uint64_t> draws;
    for(int k=0;k+1<set.count;++k){const auto t=tables.thresholds[set.threshold_offset+k];draws.push_back(t);if(t)draws.push_back(t-1);}
    for(int r=0;r<256;++r)draws.push_back(rng()>>11);
    for(const auto draw:draws) {
     volatile double choice=static_cast<double>(draw)*0x1.0p-53;choice=choice*total;int serial=0;
     for(std::size_t k=0;k<terms.size();++k){serial=int(k);choice=choice-terms[k];if(choice<0)break;}
     int index=0;for(int k=0;k+1<set.count;++k)index+=tables.thresholds[set.threshold_offset+k]<=draw;
     require(tables.expressions[set.choice_offset+index]==eligible[serial],"choice table disagrees with the serial draw");
     ++checked;
    }
   }
  }
 }
 require(checked>1000,"choice tables were not exercised");
}
}  // namespace

int main() { try {
 Env env;
 // Engine flags of the resident GPU owner (all exact variants).
 for(const char* flag:{"GAGP_OWNED_PHASE_CAPS","GAGP_VIEW_PROFILE","GAGP_TYPED_VIEW_PHASE","GAGP_DIRECT_PHASE","GAGP_DIRECT_ROOT",
     "GAGP_OWNED_PHASE_PACKED","GAGP_OWNED_PHASE_PACK","GAGP_REUSE_IMPORTED_EXECUTABLE","GAGP_REUSE_PHASE_GRAMMAR",
     "GAGP_BATCH_PHASE_IMPORT","GAGP_BATCH_PHASE_EXPORT"})env.set(flag,"1");
 // Semantic legacy flags must be absent: the policies below are explicit.
 for(const char* flag:{"GAGP_COUNTED_SITE_DRAW","GAGP_GPU_UNIFORM_PHASE_SITES","GAGP_GPU_SOURCE_CROSSOVER_GATE","GAGP_GPU_PHASE_POPULATION",
     "GAGP_GPU_WARP_VARIATION","GAGP_GPU_CHOICE_TABLES","GAGP_HOST_THREADS"})env.set(flag,nullptr);
 auto definition=grammar::load_definition(GAGP_REPOSITORY_ROOT "/cpp/tests/fixtures/phase_layout_memo.json");
 definition.document.object_v.at("templates").array_v[0].object_v.at("body").object_v.at("structured").object_v.at("plan").object_v.at("limits").object_v.at("cells").number_v=128;
 const auto compiled=std::make_shared<const grammar::CompiledGrammar>(grammar::compile_grammar(grammar::parse_definition(grammar::canonical_json(definition.document))));
 constexpr int population=32;
 std::vector<ProgramGenome> parents;for(int i=0;i<population;++i)parents.push_back(grammar::generate_derivation(*compiled,1937+i).genome);
 std::vector<std::string> names;for(const auto& input:compiled->inputs())names.push_back(input.name);
 check_choice_tables(compiled,parents.front(),names);
 std::vector<EvalCase> cases;
 for(int i=0;i<96;++i) {
  EvalCase c;c.expected=Value::from_int(i%11);
  for(const auto& name:names)c.inputs[name]=Value::from_int(name=="row"?i%4:name=="column"?(i/4)%4:name=="rows" || name=="columns"?4:i%7-3);
  cases.push_back(std::move(c));
 }
 EvolutionConfig base;base.population_size=population;base.generations=5;base.seed=11;base.elite_count=1;base.mutation_rate=0.3;
 base.compiled_grammar=compiled;base.generation_request=grammar::entry_request(*compiled);
 base.mutation_subtree_prob=1;base.fuel=compiled->execution_limits().fuel;base.penalty=1000;base.gpu_blocksize=32;
 base.phase_population=true;
 struct Policy{const char* label;PhaseVariationPolicy policy;int site_depth;int fuel_divisor=1;};
 const Policy policies[]={
  {"reservoir/genome",{},0},
  {"counted/uniform/gate",{PhaseSiteDraw::Counted,PhaseSiteScope::UniformPhase,0.7,3},0},
  {"counted/site-depth",{PhaseSiteDraw::Counted,PhaseSiteScope::Genome,0.9,8},10},
  {"counted/mutation-cap",{PhaseSiteDraw::Counted,PhaseSiteScope::Genome,1,8,3},0},
  {"lowered-fuel",{PhaseSiteDraw::Counted,PhaseSiteScope::Genome,1,8},0,64},
 };
 const auto run=[&](const Policy& p,bool gpu){
  auto cfg=base;cfg.phase_variation=p.policy;cfg.gpu_phase_site_depth=p.site_depth;cfg.fuel=base.fuel/p.fuel_divisor;
  cfg.eval_engine=gpu?EvalEngine::GPU:EvalEngine::CPU;
  cfg.reproduction_backend=gpu?repro::ReproductionBackend::Gpu:repro::ReproductionBackend::Cpu;
  return trace(evolve_population(cases,cfg,&parents));
 };
 for(const auto& p:policies) {
  const auto reference=run(p,false);
  require(reference.profile.rfind("native-cpu-phase-",0)==0,std::string(p.label)+": CPU reference owner not used");
  env.set("GAGP_HOST_THREADS","4");require_same(reference,run(p,false),std::string(p.label)+" CPU 4 threads");env.set("GAGP_HOST_THREADS",nullptr);
  // Serial GPU kernels, with and without the eager/fused/in-place engine variants.
  for(bool variants:{false,true}) {
   for(const char* flag:{"GAGP_GPU_INPLACE_MUTATION","GAGP_GPU_FUSED_DONOR_SITES","GAGP_GPU_FUSED_SPLICE_DEPTH","GAGP_GPU_DEFERRED_CROSSOVER_COPY"})
    env.set(flag,variants?"1":nullptr);
   const auto serial=run(p,true);
   require(serial.profile.rfind("native-gpu-phase-",0)==0,std::string(p.label)+": GPU owner not used");
   require_same(reference,serial,std::string(p.label)+(variants?" GPU variants":" GPU plain"));
   env.set("GAGP_GPU_WARP_VARIATION","1");
   require_same(reference,run(p,true),std::string(p.label)+" GPU warp");
   env.set("GAGP_GPU_CHOICE_TABLES","1");
   const auto tables=run(p,true);
   require(tables.profile.find("-warp-choice")!=std::string::npos,"warp choice profile suffix");
   require_same(reference,tables,std::string(p.label)+" GPU warp choice tables");
   env.set("GAGP_GPU_WARP_VARIATION",nullptr);env.set("GAGP_GPU_CHOICE_TABLES",nullptr);
  }
 }
 // Research schedules (CPU reference only): deterministic, independent of host
 // workers, distinct from the generational search, and rejected by the GPU owner.
 {
  const Policy& p=policies[1];
  const auto schedule_run=[&](const PhaseSchedulePolicy& schedule,bool gpu){
   auto cfg=base;cfg.phase_variation=p.policy;cfg.phase_schedule=schedule;
   cfg.eval_engine=gpu?EvalEngine::GPU:EvalEngine::CPU;
   cfg.reproduction_backend=gpu?repro::ReproductionBackend::Gpu:repro::ReproductionBackend::Cpu;
   return trace(evolve_population(cases,cfg,&parents));
  };
  const auto generational=run(p,false);
  require_same(generational,schedule_run(PhaseSchedulePolicy{},false),"default schedule");
  PhaseSchedulePolicy islands;islands.islands=4;islands.migration_interval=2;islands.migrants=2;
  PhaseSchedulePolicy async;async.mode=PhaseScheduleMode::Async;async.workers=8;async.child_overhead=5;
  PhaseSchedulePolicy combined=async;combined.islands=2;combined.migration_interval=1;combined.migrants=2;
  for(const auto& [label,schedule]:{std::pair{"islands",islands},std::pair{"async",async},std::pair{"async islands",combined}}) {
   const auto one=schedule_run(schedule,false);
   require(one.profile.find(schedule.mode==PhaseScheduleMode::Async?"-async-w":"-islands")!=std::string::npos,std::string(label)+" profile");
   require(one.final_asts.size()==std::size_t(population) && one.champions.size()==std::size_t(base.generations),std::string(label)+" shape");
   require(one.final_asts!=generational.final_asts,std::string(label)+" equals the generational search");
   env.set("GAGP_HOST_THREADS","4");require_same(one,schedule_run(schedule,false),std::string(label)+" CPU 4 threads");env.set("GAGP_HOST_THREADS",nullptr);
   bool gpu_rejected=false;
   try{(void)schedule_run(schedule,true);}catch(const std::invalid_argument&){gpu_rejected=true;}
   require(gpu_rejected,std::string(label)+" accepted by the GPU owner");
  }
  // The final champion never regresses: elites (islands) and the protected best (async).
  for(const auto& schedule:{islands,async,combined}) {
   const auto one=schedule_run(schedule,false);
   for(std::size_t g=1;g<one.best.size();++g)require(one.best[g]>=one.best[g-1],"schedule champion regressed");
   require(one.final_fitness.front()>=one.best.back(),"schedule final champion regressed");
  }
 }
 // Synchronous batch steady state: both owners publish identical searches.
 for(const Policy* p:{&policies[1],&policies[3]}) {
  PhaseSchedulePolicy steady;steady.mode=PhaseScheduleMode::SteadyState;steady.batch=8;
  const auto steady_run=[&](bool gpu){
   auto cfg=base;cfg.phase_variation=p->policy;cfg.phase_schedule=steady;
   cfg.eval_engine=gpu?EvalEngine::GPU:EvalEngine::CPU;
   cfg.reproduction_backend=gpu?repro::ReproductionBackend::Gpu:repro::ReproductionBackend::Cpu;
   return trace(evolve_population(cases,cfg,&parents));
  };
  const std::string label=std::string(p->label)+" steady_state";
  const auto reference=steady_run(false);
  require(reference.profile.find("-steady8")!=std::string::npos,label+" CPU profile");
  {  // An omitted batch is the population size, on both owners.
   auto whole=steady;whole.batch=0;auto explicit_whole=steady;explicit_whole.batch=population;
   const auto run_with=[&](const PhaseSchedulePolicy& s,bool gpu){auto saved=steady;steady=s;auto t=steady_run(gpu);steady=saved;return t;};
   const auto full=run_with(explicit_whole,false);
   require(full.profile.find("-steady"+std::to_string(population))!=std::string::npos,label+" whole-population profile");
   require_same(full,run_with(whole,false),label+" omitted batch CPU");
   require_same(full,run_with(whole,true),label+" omitted batch GPU");
  }
  require(reference.champions.size()==std::size_t(base.generations) && reference.final_asts.size()==std::size_t(population),label+" shape");
  require(reference.final_asts!=run(*p,false).final_asts,label+" equals the generational search");
  for(std::size_t g=1;g<reference.best.size();++g)require(reference.best[g]>=reference.best[g-1],label+" champion regressed");
  env.set("GAGP_HOST_THREADS","4");require_same(reference,steady_run(false),label+" CPU 4 threads");env.set("GAGP_HOST_THREADS",nullptr);
  for(bool variants:{false,true}) {
   for(const char* flag:{"GAGP_GPU_INPLACE_MUTATION","GAGP_GPU_FUSED_DONOR_SITES","GAGP_GPU_FUSED_SPLICE_DEPTH","GAGP_GPU_DEFERRED_CROSSOVER_COPY"})
    env.set(flag,variants?"1":nullptr);
   const auto gpu=steady_run(true);
   require(gpu.profile.rfind("native-gpu-phase-",0)==0 && gpu.profile.find("-steady8")!=std::string::npos,label+" GPU owner not used");
   require_same(reference,gpu,label+(variants?" GPU variants":" GPU plain"));
   env.set("GAGP_GPU_WARP_VARIATION","1");env.set("GAGP_GPU_CHOICE_TABLES","1");
   require_same(reference,steady_run(true),label+" GPU warp choice tables");
   env.set("GAGP_GPU_WARP_VARIATION",nullptr);env.set("GAGP_GPU_CHOICE_TABLES",nullptr);
  }
  // Staged owners stash each batch compactly; with stage storage the bank is
  // released to the evaluation workspace between reproduction and placement.
  env.set("GAGP_GPU_PHASE_STAGED_BUFFERS","1");
  require_same(reference,steady_run(true),label+" GPU staged");
  env.set("GAGP_GPU_GLOBAL_PAYLOAD","1");env.set("GAGP_GPU_PAYLOAD_WORKSPACE_MIB","256");env.set("GAGP_GPU_STAGE_STORAGE","1");
  const auto stored=steady_run(true);
  require(stored.profile.find("-stage-storage")!=std::string::npos && stored.profile.find("-declined")==std::string::npos,
      label+" stage storage not used: "+stored.profile);
  require_same(reference,stored,label+" GPU staged stage storage");
  for(const char* flag:{"GAGP_GPU_PHASE_STAGED_BUFFERS","GAGP_GPU_GLOBAL_PAYLOAD","GAGP_GPU_PAYLOAD_WORKSPACE_MIB","GAGP_GPU_STAGE_STORAGE"})
   env.set(flag,nullptr);
 }
 // A conflicting legacy semantic flag is rejected for explicit policies.
 env.set("GAGP_COUNTED_SITE_DRAW","1");
 bool rejected=false;
 try{(void)run(policies[0],false);}catch(const std::invalid_argument&){rejected=true;}
 require(rejected,"conflicting legacy site-draw flag accepted");
 std::cout<<"phase population CPU reference tests passed\n";
 return 0;
} catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;} }
