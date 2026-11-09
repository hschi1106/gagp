// The CPU reference and the GPU owner of the native phase population must
// publish identical searches: same final population ASTs, fitness, wrong
// counts and per-generation champions, across policies and schedules.
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include "gagp/cli/program_json.hpp"
#include "gagp/core/host_threads.hpp"
#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "../../src/evolution/repro/gpu/phase_layout.hpp"
#include "../../src/evolution/repro/gpu/phase_choice_tables.hpp"

namespace {
using namespace gagp;using namespace gagp::evo;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
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
  gagp::set_host_thread_limit(4);require_same(reference,run(p,false),std::string(p.label)+" CPU 4 threads");gagp::set_host_thread_limit(1);
  // Warp-per-child GPU kernels with threshold-table draws.
  const auto gpu=run(p,true);
  require(gpu.profile.rfind("native-gpu-phase-",0)==0 && gpu.profile.find("-warp-choice")!=std::string::npos,
      std::string(p.label)+": GPU owner not used");
  require_same(reference,gpu,std::string(p.label)+" GPU");
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
  gagp::set_host_thread_limit(4);require_same(reference,steady_run(false),label+" CPU 4 threads");gagp::set_host_thread_limit(1);
  const auto gpu=steady_run(true);
  require(gpu.profile.rfind("native-gpu-phase-",0)==0 && gpu.profile.find("-steady8")!=std::string::npos,label+" GPU owner not used");
  require_same(reference,gpu,label+" GPU");
 }
 std::cout<<"phase population CPU reference tests passed\n";
 return 0;
} catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;} }
