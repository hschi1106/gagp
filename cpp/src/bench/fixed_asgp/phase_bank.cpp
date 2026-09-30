#include "phase_bank.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <numeric>
#include <set>
#include <map>
#include <stdexcept>
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/evolution/grammar/cache.hpp"
#include "gagp/evolution/grammar/values.hpp"
#include "gagp/evolution/grammar/derivation_resources.hpp"
#include "gagp/evolution/repro/pack.hpp"
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "gagp/runtime/cpu/fitness_cpu.hpp"
#include "../../evolution/grammar/variation_internal.hpp"
#include "../../evolution/batch_workers.hpp"
#include <atomic>

namespace fixed_asgp {
namespace {
using namespace gagp;
using namespace gagp::evo;
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point t) { return std::chrono::duration<double,std::milli>(Clock::now()-t).count(); }
void require(bool ok, const char* reason) { if (!ok) throw std::runtime_error(std::string("phase-bank unsupported: ")+reason); }
std::vector<RegionPhase*> phases(BytecodeProgram& code) {
  require(code.bounded_region_segments.size()==1,"requires exactly one region");
  auto& s=code.bounded_region_segments[0]; std::vector<RegionPhase*> out;
  out.push_back(&s.base_predicate); out.push_back(&s.base_body);
  for (auto& p:s.preparations) out.push_back(&p);
  for (auto& p:s.request_expressions) out.push_back(&p);
  out.push_back(&s.combine); if (s.boundary) out.push_back(&*s.boundary); return out;
}
struct Member {
  gg::VariationAnalysis analysis;
  std::vector<gg::VariationSite> sites;
  std::vector<int> ordinals;
  std::vector<unsigned> phase_depths;
  BytecodeProgram code;
  std::string skeleton;
};
using Chromosome=std::vector<unsigned>;
std::string phase_identity(const RegionPhase& phase) {
  std::string key=std::to_string(phase.program.n_locals)+";";
  for(const auto& v:phase.program.consts) key+=gg::canonical_constant_encoding(v)+";";
  key+="|";
  for(const auto& i:phase.program.code) key+=std::to_string(static_cast<int>(i.op))+","+std::to_string(i.a)+","+std::to_string(i.b)+","+std::to_string(i.has_a)+","+std::to_string(i.has_b)+";";
  key+="|";for(auto fuel:phase.program.instruction_fuel)key+=std::to_string(fuel)+",";
  key+="|";for(const auto& b:phase.bindings)key+=std::to_string(static_cast<int>(b.source.bank))+","+std::to_string(b.source.slot)+","+std::to_string(b.local)+";";
  return key;
}
}
void phase_bank_probe(const std::vector<ProgramGenome>& population,
    const std::vector<EvalCase>& cases,
    const std::shared_ptr<const gg::CompiledGrammar>& grammar, const std::string& output) {
  const auto call_start=Clock::now();
  const auto cs=prepare_case_set(cases);
  FitnessSessionGpu session;
  auto init=session.init(cs.bindings,cs.expected_values,2000000,512,1000);
  if (!init.ok) throw std::runtime_error(init.err.message);
  const auto setup_begin=Clock::now();
  const auto request=gg::entry_request(*grammar);
  require(gg::resource_charges_are_local(*grammar),"requires local resource charges");
  std::vector<Member> bank(population.size());
  std::atomic<std::size_t> next{0};
  gagp::evo::detail::BatchWorkers workers(20);
  workers.run([&] {
    for (;;) {
      auto i=next.fetch_add(1); if(i>=bank.size()) break;
      auto& m=bank[i]; const auto& ast=population[i].ast;
      require(ast.bounded_region_specs.size()==1,"requires one AST region");
      m.analysis=gg::analyze_variation(*grammar,population[i],request);
      m.code=compile_for_eval(population[i],m.analysis.verified,cs.input_names);
      const auto& region=ast.bounded_region_specs[0];
      auto cursor=region.node_index+1;
      for(std::size_t j=0;j<region.plan.state_types.size()+region.plan.bound_operand_count;++j)
        cursor=m.analysis.verified.subtree_end[cursor];
      std::vector<unsigned> depths(ast.nodes.size()); std::vector<std::size_t> ends;
      for(std::size_t j=0;j<ast.nodes.size();++j) {
        while(!ends.empty() && j>=ends.back()) ends.pop_back();
        depths[j]=ends.size()+1; ends.push_back(m.analysis.verified.subtree_end[j]);
      }
      for(std::size_t ordinal=0;ordinal<region.phases.size();++ordinal) {
        const auto end=m.analysis.verified.subtree_end[cursor];
        for(const auto& site:m.analysis.sites) if(site.occurrences.size()==1 &&
            site.occurrences[0].begin==cursor && site.occurrences[0].end==end) {
          // An independent template hole is a proof boundary. Forwarded root
          // alternatives without a hole do not establish independent phases.
          if(site.slot==gg::kNoGrammarId || site.template_id==gg::kNoGrammarId) continue;
          m.sites.push_back(site); m.ordinals.push_back(ordinal); m.phase_depths.push_back(depths[cursor]); break;
        }
        cursor=end;
      }
      require(!m.sites.empty(),"no independent whole-phase variation sites");
    }
  });
  const auto dimensions=bank[0].sites.size();
  std::uint64_t max_logical=0,max_lowered=0;
  for(const auto& m:bank) {
    max_logical=std::max<std::uint64_t>(max_logical,m.analysis.witness.logical_steps);
    max_lowered=std::max<std::uint64_t>(max_lowered,m.analysis.witness.lowered_instructions);
  }
  // Conservative whole-bank bounds cover every combination, not just parents.
  require(max_logical*(dimensions+1)<=1048576 && max_lowered*(dimensions+1)<=1048576,
      "bank combinations exceed grammar execution construction limits");
  // Normalize each variable phase to the same admitted donor. Exact runtime
  // identities then prove the remaining skeleton, captures and annotations.
  next=0;
  workers.run([&] {
    for(;;) {
      auto i=next.fetch_add(1); if(i>=bank.size()) break;
      auto& m=bank[i]; require(m.sites.size()==dimensions && m.ordinals==bank[0].ordinals,"phase layouts differ");
      ProgramGenome normalized=population[i];
      for(std::size_t p=dimensions;p-->0;) {
        const auto& source=bank[0].sites[p];
        require(gg::compatible_sites(m.sites[p],source),"phase grammar contracts differ");
        // Reject shared holes/instances that would couple separate bank slots.
        for(std::size_t q=0;q<p;++q)
          require(m.sites[q].slot!=m.sites[p].slot || m.sites[q].template_id!=m.sites[p].template_id,"coupled phase holes");
        normalized.ast=gg::variation_detail::splice(normalized.ast,m.sites[p],population[0].ast,
            source.occurrences[0],source.occurrence_binder_ids[0],false);
      }
      normalized=repro::compact_genome_tables(std::move(normalized));
      m.skeleton=gg::runtime_cache_identity(normalized,cs.input_names,2000000);
    }
  });
  for(const auto& m:bank) require(m.skeleton==bank[0].skeleton,"fixed skeletons or capture mappings differ");
  std::vector<Chromosome> current(bank.size(),Chromosome(dimensions));
  std::vector<std::vector<unsigned>> canonical(dimensions,std::vector<unsigned>(bank.size()));
  std::vector<Json> unique_phase_counts;
  for(std::size_t p=0;p<dimensions;++p) {
    std::map<std::string,unsigned> identities;
    for(std::size_t i=0;i<bank.size();++i) {
      auto [it,inserted]=identities.emplace(phase_identity(*phases(bank[i].code).at(bank[0].ordinals[p])),i);
      canonical[p][i]=it->second; current[i][p]=it->second;
    }
    unique_phase_counts.push_back(number(identities.size()));
  }
  const auto materialize=[&](const Chromosome& c) {
    auto program=bank[0].code; auto targets=phases(program);
    for(std::size_t p=0;p<dimensions;++p)
      *targets.at(bank[0].ordinals[p])=*phases(bank[c[p]].code).at(bank[0].ordinals[p]);
    return program;
  };
  std::size_t fixed_nodes=population[0].ast.nodes.size();
  for(const auto& site:bank[0].sites) fixed_nodes-=site.materialized_nodes;
  const auto nodes=[&](const Chromosome& c) {
    std::size_t n=fixed_nodes;
    for(std::size_t p=0;p<dimensions;++p) n+=bank[c[p]].sites[p].materialized_nodes;
    return n;
  };
  const auto fits=[&](const Chromosome& c) {
    if(nodes(c)>request.budget.max_nodes) return false;
    for(std::size_t p=0;p<dimensions;++p) if(bank[c[p]].sites[p].materialized_depth+bank[0].phase_depths[p]-1>request.budget.max_depth ||
        !gg::donor_fits(bank[0].sites[p],bank[c[p]].sites[p])) return false;
    return true;
  };
  for(const auto& c:current) require(fits(c),"source chromosome exceeds conservative combination budget");
  const auto setup_ms=elapsed(setup_begin);
  const auto seed=std::getenv("GAGP_BM_SEED")?std::stoull(std::getenv("GAGP_BM_SEED")):0;
  gg::GrammarRandom random(seed);
  std::vector<Json> rows,initial,final;
  const int generations=std::getenv("GAGP_BANK_GENERATIONS")?std::stoi(std::getenv("GAGP_BANK_GENERATIONS")):4;
  require(generations>=1 && generations<=256,"generation count outside [1,256]");
  std::size_t changed=0,rejected=0;
  for(int generation=0;generation<generations;++generation) {
    const auto begin=Clock::now(); std::vector<BytecodeProgram> programs; programs.reserve(current.size());
    for(const auto& c:current) programs.push_back(materialize(c));
    auto fit=session.eval_programs(programs,std::getenv("GAGP_GPU_DIAGNOSTICS")!=nullptr);
    if(!fit.ok) throw std::runtime_error(fit.err.message);
    if(generation==0) for(double f:fit.fitness) initial.push_back(number(f));
    const auto best=std::distance(fit.fitness.begin(),std::max_element(fit.fitness.begin(),fit.fitness.end()));
    const auto repro_begin=Clock::now();
    const auto pick=[&] { auto a=random.bounded(current.size()),b=random.bounded(current.size()); return fit.fitness[a]>=fit.fitness[b]?a:b; };
    std::vector<Chromosome> children; children.reserve(current.size()); children.push_back(current[best]);
    changed=0;rejected=0;
    while(children.size()<current.size()) {
      const auto a=pick(),b=pick(); auto child=current[a];
      auto p=random.bounded(dimensions); child[p]=current[b][p];
      if(random.bounded(1000000)<300000) { auto slot=random.bounded(dimensions); child[slot]=canonical[slot][random.bounded(bank.size())]; }
      if(!fits(child)) { ++rejected; child=current[a]; }
      changed+=child!=current[a]; children.push_back(std::move(child));
    }
    current=std::move(children);
    const auto repro_ms=elapsed(repro_begin), generation_ms=elapsed(begin);
    std::array<std::uint64_t,5> counts{};for(const auto& c:fit.case_counts)for(int j=0;j<5;++j)counts[j]+=c[j];
    double mean_nodes=0;std::set<Chromosome> unique;for(const auto& c:current){mean_nodes+=nodes(c);unique.insert(c);}
    rows.push_back(object({{"generation",number(generation)},{"generation_ms",number(generation_ms)},
      {"first_generation_with_bank_ms",number(generation_ms+(generation==0?setup_ms:0))},
      {"eval_ms",number(fit.timing.total_ms)},{"kernel_ms",number(fit.timing.kernel_ms)},{"repro_ms",number(repro_ms)},
      {"best",number(fit.fitness[best])},{"mean",number(std::accumulate(fit.fitness.begin(),fit.fitness.end(),0.0)/fit.fitness.size())},
      {"changed",number(changed)},{"budget_rejected",number(rejected)},{"unique_chromosomes",number(unique.size())},{"mean_nodes",number(mean_nodes/current.size())},
      {"cases",number(counts[0])},{"errors",number(counts[1])},{"timeouts",number(counts[2])},{"fallbacks",number(counts[3])},{"unscored",number(counts[4])}}));
  }
  // Independent full AST admission audit of every final chromosome. This is a
  // diagnostic export, separately timed; runtime legality uses bank contracts.
  const auto audit_begin=Clock::now(); next=0; std::vector<ProgramGenome> exported(current.size());
  workers.run([&] {
    for(;;) {
      auto i=next.fetch_add(1); if(i>=current.size()) break;
      auto out=population[0];
      for(std::size_t p=dimensions;p-->0;) {
        const auto index=current[i][p]; const auto& source=bank[index].sites[p];
        out.ast=gg::variation_detail::splice(out.ast,bank[0].sites[p],population[index].ast,
            source.occurrences[0],source.occurrence_binder_ids[0],false);
      }
      out=repro::compact_genome_tables(std::move(out)); out.derivation.reset();
      out.derivation=std::make_shared<const gg::DerivationMetadata>(gg::reconstruct_derivation(*grammar,out));
      exported[i]=std::move(out);
    }
  });
  const auto audit_ms=elapsed(audit_begin);
  std::vector<BytecodeProgram> programs;for(const auto& c:current)programs.push_back(materialize(c));
  const auto final_begin=Clock::now();auto fit=session.eval_programs(programs,true);if(!fit.ok)throw std::runtime_error(fit.err.message);
  const auto final_ms=elapsed(final_begin);
  std::vector<BytecodeProgram> exported_code;for(const auto& g:exported)exported_code.push_back(compile_for_eval(g,cs.input_names));
  auto audit_fit=session.eval_programs(exported_code,true);require(audit_fit.ok,"export evaluation failed");
  require(audit_fit.fitness==fit.fitness && audit_fit.case_counts==fit.case_counts,"exported AST differs from phase handles");
  for(std::size_t i=0;i<current.size();++i){std::vector<Json> a;for(auto x:current[i])a.push_back(number(x));final.push_back(object({{"alleles",array(std::move(a))},{"fitness",number(fit.fitness[i])},{"nodes",number(nodes(current[i]))}}));}
  const auto cpu_begin=Clock::now();
  std::vector<std::size_t> order(current.size());std::iota(order.begin(),order.end(),0);
  std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return fit.fitness[a]>fit.fitness[b];});
  std::vector<BytecodeProgram> top;for(std::size_t k=0;k<std::min<std::size_t>(16,order.size());++k)top.push_back(exported_code[order[k]]);
  auto cpu=eval_fitness_cpu(top,cs.bindings,cs.expected_values,2000000,1000,512);
  std::vector<Json> top_rows;for(std::size_t k=0;k<top.size();++k)top_rows.push_back(object({{"index",number(order[k])},{"gpu",number(fit.fitness[order[k]])},{"cpu",number(cpu[k])}}));
  const auto cpu_ms=elapsed(cpu_begin);
  write(output,object({{"profile",string("finite-independent-phase-bank")},{"seed",number(seed)},{"dimensions",number(dimensions)},
    {"top16_cpu",array(std::move(top_rows))},{"top16_cpu_ms",number(cpu_ms)},{"unique_phases",array(std::move(unique_phase_counts))},{"bank_members",number(bank.size())},{"bank_init_ms",number(setup_ms)},{"gpu_init_ms",number(init.timing.total_ms)},
    {"generations",array(std::move(rows))},{"initial_fitness",array(std::move(initial))},{"final_population",array(std::move(final))},
    {"final_eval_ms",number(final_ms)},{"full_export_admission_ms",number(audit_ms)},{"export_fitness_equal",number(1)},{"total_diagnostic_call_ms",number(elapsed(call_start))}}));
}
}
