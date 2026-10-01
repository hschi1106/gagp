#include "phase_bank.hpp"
#include "../../evolution/grammar/executable_fragments.hpp"
#include "../../evolution/batch_workers.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/grammar/cache.hpp"
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "gagp/runtime/cpu/fitness_cpu.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <numeric>
#include <set>
#include <fstream>

namespace fixed_asgp {
namespace {
using Clock = std::chrono::steady_clock;
double ms(Clock::time_point start) {return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
std::size_t rss_kb() {
  std::ifstream file("/proc/self/status"); std::string line;
  while(std::getline(file,line)) if(line.rfind("VmRSS:",0)==0)return std::stoull(line.substr(6));
  return 0;
}
}
void fragment_probe(const std::vector<gagp::evo::ProgramGenome>& population,
    const std::vector<gagp::evo::EvalCase>& cases,
    const std::shared_ptr<const gg::CompiledGrammar>& grammar, const std::string& output) {
  using namespace gagp; using namespace gagp::evo;
  const auto call=Clock::now(); auto cs=prepare_case_set(cases);
  FitnessSessionGpu session;
  auto init=session.init(cs.bindings,cs.expected_values,2000000,512,1000);
  if(!init.ok)throw std::runtime_error(init.err.message);
  const auto setup=Clock::now();
  gg::ExecutableFragments arena(grammar,population.at(0),cs.input_names);
  std::vector<gg::ExecutableFragments::Genome> current(population.size());
  detail::BatchWorkers workers(20); std::atomic<std::size_t> next{0};
  workers.run([&]{for(;;){auto i=next.fetch_add(1);if(i>=current.size())break;current[i]=arena.import(population[i]);}});
  std::vector<Json> import_audit;
  if(const char* path=std::getenv("GAGP_FRAGMENT_IMPORT_AUDIT")) {
    const auto document=read(path);std::size_t index=0;
    for(const auto& ast:document.object_v.at("programs").array_v) {
      ProgramGenome external;external.ast=gagp::cli_detail::decode_ast_json(ast);
      try {
        auto handle=arena.import(external);
        auto direct=session.eval_programs({arena.executable(handle)},true);
        auto reference=session.eval_programs({compile_for_eval(external,cs.input_names)},true);
        if(!direct.ok || !reference.ok || direct.fitness!=reference.fitness || direct.case_counts!=reference.case_counts)
          throw std::runtime_error("imported expression evaluation mismatch");
        import_audit.push_back(object({{"index",number(index)},{"supported",number(1)},{"fitness",number(direct.fitness[0])}}));
      }catch(const std::invalid_argument& e){import_audit.push_back(object({{"index",number(index)},{"supported",number(0)},{"reason",string(e.what())}}));}
      ++index;
    }
  }
  const double setup_ms=ms(setup);
  const auto seed=std::getenv("GAGP_BM_SEED")?std::stoull(std::getenv("GAGP_BM_SEED")):0;
  gg::GrammarRandom random(seed);
  const int generations=std::getenv("GAGP_FRAGMENT_GENERATIONS")?std::stoi(std::getenv("GAGP_FRAGMENT_GENERATIONS")):4;
  if(generations<1 || generations>65536)throw std::invalid_argument("fragment generations outside [1,65536]");
  std::vector<Json> rows,initial;
  for(int generation=0;generation<generations;++generation) {
    const auto begin=Clock::now();
    std::vector<BytecodeProgram> programs;programs.reserve(current.size());
    for(const auto& g:current)programs.push_back(arena.executable(g));
    const auto fit=session.eval_programs(programs,std::getenv("GAGP_GPU_DIAGNOSTICS")!=nullptr);
    if(!fit.ok)throw std::runtime_error(fit.err.message);
    if(generation==0)for(auto f:fit.fitness)initial.push_back(number(f));
    const auto best=std::distance(fit.fitness.begin(),std::max_element(fit.fitness.begin(),fit.fitness.end()));
    const auto repro=Clock::now();
    const auto pick=[&]{auto a=random.bounded(current.size()),b=random.bounded(current.size());return fit.fitness[a]>=fit.fitness[b]?a:b;};
    struct Job{std::size_t parent,donor;std::uint64_t seed;bool mutation;};
    std::vector<Job> jobs;for(std::size_t i=1;i<current.size();++i)jobs.push_back({pick(),pick(),random.next(),random.bounded(1000000)<300000});
    std::vector<gg::ExecutableFragments::Change> children(current.size());children[0].genome=current[best];next=0;
    workers.run([&]{for(;;){auto j=next.fetch_add(1);if(j>=jobs.size())break;const auto& job=jobs[j];children[j+1]=arena.vary(current[job.parent],current[job.donor],job.seed,job.mutation);}});
    std::size_t changed=0,rejected=0; for(std::size_t i=0;i<current.size();++i){changed+=children[i].changed;rejected+=children[i].rejected;current[i]=std::move(children[i].genome);}
    // Destruction of retired fragments occurs before this timer closes.
    const double reproduction_ms=ms(repro), generation_ms=ms(begin);
    std::array<std::uint64_t,5> counts{};for(const auto& c:fit.case_counts)for(int k=0;k<5;++k)counts[k]+=c[k];
    double mean_nodes=0;for(const auto& g:current)mean_nodes+=arena.nodes(g);
    rows.push_back(object({{"generation",number(generation)},{"generation_ms",number(generation_ms)},
      {"eval_ms",number(fit.timing.total_ms)},{"pack_ms",number(fit.timing.pack_ms)},{"kernel_ms",number(fit.timing.kernel_ms)},
      {"repro_ms",number(reproduction_ms)},{"best",number(fit.fitness[best])},{"mean",number(std::accumulate(fit.fitness.begin(),fit.fitness.end(),0.)/fit.fitness.size())},
      {"changed_final_offspring",number(changed)},{"rejected",number(rejected)},{"unchanged",number(current.size()-1-changed)},
      {"live_fragments",number(arena.live_fragments())},{"host_rss_kb",number(rss_kb())},{"mean_nodes",number(mean_nodes/current.size())},
      {"cases",number(counts[0])},{"errors",number(counts[1])},{"timeouts",number(counts[2])},{"fallbacks",number(counts[3])},{"unscored",number(counts[4])}}));
  }
  const double search_ms=ms(call);
  const auto audit=Clock::now();std::vector<ProgramGenome> exported(current.size());next=0;
  workers.run([&]{for(;;){auto i=next.fetch_add(1);if(i>=current.size())break;exported[i]=arena.export_ast(current[i]);
    exported[i].derivation=std::make_shared<const gg::DerivationMetadata>(gg::reconstruct_derivation(*grammar,exported[i]));}});
  const double admission_ms=ms(audit);
  std::vector<BytecodeProgram> programs,external;
  std::set<std::string> unique;
  for(std::size_t i=0;i<current.size();++i){programs.push_back(arena.executable(current[i]));external.push_back(compile_for_eval(exported[i],cs.input_names));unique.insert(gg::runtime_cache_identity(exported[i],cs.input_names,2000000));}
  const auto final_begin=Clock::now();auto fit=session.eval_programs(programs,true);auto ref=session.eval_programs(external,true);
  if(!fit.ok || !ref.ok || fit.fitness!=ref.fitness || fit.case_counts!=ref.case_counts)throw std::runtime_error("fragment export/admission/evaluation mismatch");
  const double final_ms=ms(final_begin);
  std::vector<std::size_t> order(current.size());std::iota(order.begin(),order.end(),0);std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return fit.fitness[a]>fit.fitness[b];});
  const auto cpu_begin=Clock::now();std::vector<BytecodeProgram> top;for(std::size_t i=0;i<std::min<std::size_t>(16,order.size());++i)top.push_back(external[order[i]]);
  auto cpu=eval_fitness_cpu(top,cs.bindings,cs.expected_values,2000000,1000,512);
  std::vector<Json> top_rows,final;
  for(std::size_t i=0;i<top.size();++i)top_rows.push_back(object({{"gpu",number(fit.fitness[order[i]])},{"cpu",number(cpu[i])}}));
  for(std::size_t i=0;i<current.size();++i)final.push_back(object({{"fitness",number(fit.fitness[i])},{"nodes",number(arena.nodes(current[i]))}}));
  write(output,object({{"profile",string("owned-independent-fragments-site-variation-v1")},{"seed",number(seed)},
    {"import_audit",array(std::move(import_audit))},{"init_ms",number(setup_ms)},{"gpu_init_ms",number(init.timing.total_ms)},{"search_total_ms",number(search_ms)},
    {"generations",array(std::move(rows))},{"initial_fitness",array(std::move(initial))},{"final_population",array(std::move(final))},
    {"unique_final_genotypes",number(unique.size())},{"full_export_admission_ms",number(admission_ms)},
    {"final_and_export_eval_ms",number(final_ms)},{"top16_cpu_ms",number(ms(cpu_begin))},{"top16_cpu",array(std::move(top_rows))},
    {"total_diagnostic_call_ms",number(ms(call))}}));
}
}  // namespace fixed_asgp
