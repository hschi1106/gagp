#include "adapter.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <cstring>
#include "gagp/runtime/cpu/execution_session.hpp"
#include <stdexcept>
#include "gp/operators.h"
#include "gp/fitness.h"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/evolve.hpp"
#include "gagp/runtime/payload/payload.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/cpu/fitness_cpu.hpp"
#include "gagp/evolution/grammar/cache.hpp"
#include "gagp/runtime/gpu/fitness_gpu.hpp"

namespace {
using namespace fixed_asgp;
using Clock=std::chrono::steady_clock;
double elapsed(Clock::time_point start) {
  return std::chrono::duration<double,std::milli>(Clock::now()-start).count();
}
asgp::FitnessConfig fitness_config() {
  asgp::FitnessConfig cfg; cfg.max_case_error=1000;
  cfg.limits.max_steps=2000000; cfg.limits.max_recursion=127;
  return cfg;
}
void freeze(const std::string& name, const std::string& path) {
  auto spec=task(name);
  const auto grammar=asgp::SchemeGrammar::Build(spec.Grammar());
  asgp::Rng rng(0);
  std::vector<Json> population, cases;
  for (int i=0; i<8192; ++i)
    population.push_back(encode(asgp::Initialize(grammar,2+i%6,(i/6)%2==1,rng),grammar.count));
  for (const auto& c : spec.train) {
    std::vector<Json> values;
    for (auto v : c.elements) values.push_back(string(std::to_string(v)));
    cases.push_back(object({{"input",array(std::move(values))}, {"expected",string(std::to_string(c.expected.i))},
                            {"start",number(c.start.a)}, {"extent",number(c.extent.a)}}));
  }
  if (cases.size()!=1024) throw std::runtime_error("generator did not produce 1024 cases");
  write(path,object({{"format",string("fixed-asgp-source-v1")}, {"task",string(name)},
                    {"population_seed",number(0)}, {"data_seed",number(20260626)},
                    {"population",array(std::move(population))}, {"cases",array(std::move(cases))}}));
}
void restore_cases(const Json& source, asgp::TaskSpec& spec, std::vector<gagp::evo::EvalCase>& cases) {
  spec.train.clear(); spec.test.clear();
  for (const auto& row : source.object_v.at("cases").array_v) {
    asgp::Example example;
    std::vector<gagp::Value> values;
    for (const auto& v : row.object_v.at("input").array_v) {
      example.elements.push_back(std::stoll(v.string_v));
      values.push_back(gagp::Value::from_int(example.elements.back()));
    }
    example.expected=asgp::Output::Int(std::stoll(row.object_v.at("expected").string_v));
    example.start.a=int(row.object_v.at("start").number_v);
    example.extent.a=int(row.object_v.at("extent").number_v);
    cases.push_back({{{"source",gagp::payload::make_int_list_value(values)}},gagp::Value::from_int(example.expected.i)});
    spec.train.push_back(std::move(example));
  }
  if (cases.size()!=1024) throw std::runtime_error("fixed case count mismatch");
}
void prepare(const Json& source, const std::string& grammar_path, int count, const std::string& output) {
  const auto name=source.object_v.at("task").string_v;
  const auto definition=gg::load_definition(grammar_path);
  const auto grammar=gg::compile_grammar(definition);
  const auto& parents=source.object_v.at("population").array_v;
  if (count<=0 || count>int(parents.size())) throw std::runtime_error("invalid population prefix");
  std::vector<Json> programs;
  for (int i=0; i<count; ++i) {
    try {
      auto g=translate(decode(parents[i]),name,definition,grammar);
      programs.push_back(gagp::cli_detail::JsonParser(gagp::cli_detail::encode_ast_json(g.ast),{true,512}).parse());
    } catch (const std::exception& e) {
      throw std::runtime_error("parent " + std::to_string(i) + ": " + e.what());
    }
    if ((i+1)%256==0) std::cerr << name << " prepared " << i+1 << '/' << count << '\n';
  }
  write(output,object({{"format",string("fixed-asgp-prepared-v1")}, {"task",string(name)},
                       {"grammar_hash",string(grammar.content_hash())}, {"programs",array(std::move(programs))}}));
}
void audit(const Json& source, const std::string& grammar_path, const std::string& output) {
  auto spec=task(source.object_v.at("task").string_v);
  std::vector<gagp::evo::EvalCase> cases;
  restore_cases(source,spec,cases);
  const auto definition=gg::load_definition(grammar_path);
  const auto grammar=gg::compile_grammar(definition);
  const auto original=solution(spec.id);
  asgp::FitnessScratch scratch;
  const auto fit=asgp::Score(original,spec,spec.train,fitness_config(),scratch);
  if (fit.wrong!=0) throw std::runtime_error("known ASGP solution does not solve frozen cases");
  const auto genome=translate(original,spec.id,definition,grammar);
  const auto code=gagp::evo::compile_for_eval(genome,{"source"});
  int cpu_bad=0;
  for (const auto& c : cases) {
    const auto result=gagp::execute_bytecode_cpu(code,{{0,c.inputs.at("source")}},2000000);
    cpu_bad+=result.is_error || result.value.tag!=gagp::ValueTag::Int || result.value.i!=c.expected.i;
  }
  auto cs=gagp::evo::prepare_case_set(cases);
  gagp::FitnessSessionGpu session;
  auto init=session.init(cs.bindings,cs.expected_values,2000000,512,1000);
  if (!init.ok) throw std::runtime_error(init.err.message);
  auto gpu=session.eval_programs({code});
  if (!gpu.ok) throw std::runtime_error(gpu.err.message);
  std::set<std::string> unique;
  for (const auto& c : source.object_v.at("cases").array_v)
    unique.insert(gg::canonical_json(c.object_v.at("input")));
  write(output,object({{"task",string(spec.id)}, {"cases",number(cases.size())},
                       {"unique_inputs",number(unique.size())}, {"known_solution_cpu_mismatches",number(cpu_bad)},
                       {"known_solution_asgp_wrong",number(fit.wrong)}, {"known_solution_gpu_fitness",number(gpu.fitness[0])}}));
  // GPU payload approximations are part of the compared system, not an adapter
  // parity gate. Exact CPU output is the translation check; retain GPU evidence.
  if (cpu_bad) throw std::runtime_error("known solution CPU translation mismatches: " + std::to_string(cpu_bad));
}
Json measure_asgp(const std::vector<asgp::Individual>& parents, const asgp::TaskSpec& spec) {
  auto work=parents;
  const auto grammar=asgp::SchemeGrammar::Build(spec.Grammar());
  asgp::FitnessScratch scratch;
  asgp::VariationScratch variation;
  const auto config=fitness_config();
  asgp::Rng rng(0);
  const auto start=Clock::now();
  double best=1e300, mean=0;
  for (auto& one : work) {
    one.fitness=asgp::Score(one,spec,spec.train,config,scratch);
    best=std::min(best,one.fitness.error); mean+=one.fitness.error;
  }
  const double eval=elapsed(start);
  const auto pick=[&]() -> const asgp::Individual& {
    const auto& a=work[rng.Index(work.size())];
    const auto& b=work[rng.Index(work.size())];
    return asgp::Better(a.fitness,b.fitness) ? a : b;
  };
  std::vector<asgp::Individual> children;
  children.reserve(work.size());
  children.push_back(*std::min_element(work.begin(),work.end(),[](const auto& a,const auto& b) {return asgp::Better(a.fitness,b.fitness);}));
  std::size_t unchanged=0;
  while (children.size()<work.size()) {
    const auto& a=pick(); const auto& b=pick();
    children.push_back(asgp::CrossoverByPhase(a,b,grammar.count,.7,asgp::Reach::kOnePhase,7,rng,variation));
    asgp::MutateByPhase(children.back(),grammar,.3,asgp::Reach::kOnePhase,7,rng,variation);
  }
  const double total=elapsed(start);
  // Counts are outside timing; RNG and native variation have already completed.
  for (std::size_t i=0; i<children.size(); ++i)
    unchanged += gg::canonical_json(encode(children[i],grammar.count)) == gg::canonical_json(encode(parents[i],grammar.count));
  return object({{"generation_ms",number(total)}, {"eval_ms",number(eval)}, {"repro_ms",number(total-eval)},
                 {"best_native_fitness",number(best)}, {"mean_native_fitness",number(mean/work.size())},
                 {"offspring",number(children.size())}, {"same_slot_children",number(unchanged)}});
}
Json measure_gagp(const std::vector<gagp::evo::ProgramGenome>& population,
                  const std::vector<gagp::evo::EvalCase>& cases,
                  const std::shared_ptr<const gg::CompiledGrammar>& grammar, const std::string& mode) {
  gagp::evo::EvolutionConfig cfg;
  cfg.population_size=population.size(); cfg.generations=1; cfg.seed=0;
  cfg.eval_engine=mode=="gagp_cpu" ? gagp::evo::EvalEngine::CPU : gagp::evo::EvalEngine::GPU;
  cfg.reproduction_backend=(mode=="gpu_repro" || mode=="gpu_overlap")
      ? gagp::evo::repro::ReproductionBackend::Gpu : gagp::evo::repro::ReproductionBackend::Cpu;
  cfg.repro_overlap=mode=="gpu_overlap"; cfg.compiled_grammar=grammar;
  cfg.generation_request=gg::entry_request(*grammar);
  cfg.fuel=2000000; cfg.penalty=1000; cfg.gpu_blocksize=512;
  cfg.selection_pressure=2;
  cfg.mutation_rate=.3; cfg.mutation_subtree_prob=1;
  cfg.skip_final_eval=true; cfg.retain_final_population=false;
  const auto call_begin=std::chrono::steady_clock::now();
  const auto result=gagp::evo::evolve_population(cases,cfg,&population);
  const double evolve_wall_ms=std::chrono::duration<double,std::milli>(
      std::chrono::steady_clock::now()-call_begin).count();
  const auto& t=result.timing.generations.at(0);
  const auto& v=t.reproduction.variation;
  return object({{"execution_profile",string(t.evaluation.execution_profile)}, {"reproduction_profile",string(result.reproduction_profile)}, {"reproduction_fallback_reason",string(result.reproduction_fallback_reason)},
    {"initial_admission_ms",number(result.timing.init_population_ms)}, {"genotype_device_bytes",number(result.genotype_device_bytes)}, {"generation_ms",number(t.total_ms)}, {"eval_ms",number(t.eval_ms)}, {"repro_ms",number(t.repro_ms)},
    {"evolve_wall_ms",number(evolve_wall_ms)}, {"evolve_call_ms",number(result.timing.total_ms)}, {"gpu_init_ms",number(result.timing.gpu_eval_init_ms)},
    {"compile_ms",number(t.evaluation.cpu_compile_ms+t.evaluation.gpu_compile_ms)},
    {"gpu_eval_call_ms",number(t.evaluation.gpu_eval_call_ms)}, {"gpu_kernel_ms",number(t.evaluation.gpu_eval_kernel_ms)},
    {"gpu_pack_ms",number(t.evaluation.gpu_eval_pack_ms)}, {"gpu_upload_ms",number(t.evaluation.gpu_eval_upload_ms)},
    {"gpu_copyback_ms",number(t.evaluation.gpu_eval_copyback_ms)},
    {"repro_prepare_ms",number(t.reproduction.prepare_inputs_ms)}, {"repro_setup_ms",number(t.reproduction.setup_ms)},
    {"gpu_donor_generated",number(t.reproduction.gpu_donor_generated)}, {"gpu_donor_fallback",number(t.reproduction.gpu_donor_fallback)},
    {"gpu_donor_setup_ms",number(t.reproduction.gpu_donor_setup_ms)}, {"gpu_donor_device_bytes",number(t.reproduction.gpu_donor_device_bytes)},
    {"repro_preprocess_ms",number(t.reproduction.preprocess_ms)}, {"repro_pack_ms",number(t.reproduction.pack_ms)},
    {"repro_upload_ms",number(t.reproduction.upload_ms)}, {"repro_kernel_ms",number(t.reproduction.kernel_ms)},
    {"repro_copyback_ms",number(t.reproduction.copyback_ms)}, {"repro_decode_ms",number(t.reproduction.decode_ms)},
    {"changed_operator_outputs",number(v.changed_children)}, {"unchanged_operator_outputs",number(v.unchanged_children)},
    {"fallback_operator_outputs",number(v.fallback_children)}, {"contract_rejections",number(v.contract_rejections)},
    {"budget_rejections",number(v.budget_rejections)}, {"generation_rejections",number(v.generation_rejections)},
    {"acceptance_rejections",number(v.acceptance_rejections)},
    {"best_native_fitness",number(result.history_best_fitness.at(0))}, {"mean_native_fitness",number(result.history_mean_fitness.at(0))}});
}
// Diagnostics use the identical frozen programs/cases but are not headline
// timing runs. CPU reference, when requested, evaluates every program/case.
void snapshot(const std::vector<gagp::evo::ProgramGenome>& population,
              const std::vector<gagp::evo::EvalCase>& cases, const std::string& output) {
  auto cs=gagp::evo::prepare_case_set(cases);
  const auto begin=Clock::now();
  std::vector<gagp::BytecodeProgram> programs;
  std::size_t admission_reuses = 0;
  for (const auto& g:population) {
    if (const auto cached = gg::admitted_bytecode_for_eval(g, cs.input_names, 2000000)) {
      programs.push_back(*cached);
      ++admission_reuses;
    } else programs.push_back(gagp::evo::compile_for_eval(g,cs.input_names));
  }
  const auto compile_ms=elapsed(begin);
  gagp::FitnessSessionGpu session;
  const auto init=session.init(cs.bindings,cs.expected_values,2000000,512,1000);
  if (!init.ok) throw std::runtime_error(init.err.message);
  const auto result=session.eval_programs(programs,true);
  if (!result.ok) throw std::runtime_error(result.err.message);
  std::vector<double> cpu;
  if (std::getenv("GAGP_SNAPSHOT_CPU"))
    cpu=gagp::eval_fitness_cpu(programs,cs.bindings,cs.expected_values,2000000,1000,512);
  std::set<std::string> behaviors;
  const bool behavior_probe=std::getenv("GAGP_SNAPSHOT_BEHAVIOR")!=nullptr;
  if(behavior_probe) for(const auto& program:programs) {
    gagp::CpuExecutionSession cpu_session(program);std::string signature;
    for(std::size_t c=0;c<std::min<std::size_t>(32,cs.bindings.size());++c) {
      std::vector<std::pair<int,gagp::Value>> inputs;
      for(const auto& b:cs.bindings[c])inputs.emplace_back(b.idx,b.value);
      const auto value=cpu_session.execute(inputs,2000000);
      if(value.is_error)signature+="E"+std::to_string(static_cast<int>(value.err.code))+";";
      else {
        std::uint64_t bits=0;
        switch(value.value.tag) {
          case gagp::ValueTag::Int:case gagp::ValueTag::Char:bits=value.value.i;break;
          case gagp::ValueTag::Bool:bits=value.value.b;break;
          case gagp::ValueTag::Float:std::memcpy(&bits,&value.value.f,sizeof(bits));break;
          default:throw std::runtime_error("scalar behavior audit received non-scalar output");
        }
        signature+=std::to_string(static_cast<int>(value.value.tag))+":"+std::to_string(bits)+";";
      }
    }
    behaviors.insert(std::move(signature));
  }
  std::vector<Json> rows;
  for (std::size_t i=0;i<programs.size();++i) {
    const auto& n=result.case_counts.at(i);
    auto row=object({{"program",number(i)}, {"nodes",number(population[i].ast.nodes.size())},
      {"fitness",number(result.fitness[i])}, {"cases",number(n[0])}, {"errors",number(n[1])},
      {"timeouts",number(n[2])}, {"fallbacks",number(n[3])}, {"unscored",number(n[4])}});
    if (!cpu.empty()) row.object_v["cpu_fitness"]=number(cpu[i]);
    rows.push_back(std::move(row));
  }
  write(output,object({{"admission_compile_reuses",number(admission_reuses)}, {"compile_ms",number(compile_ms)}, {"gpu_init_ms",number(init.timing.total_ms)},
    {"eval_ms",number(result.timing.total_ms)}, {"kernel_ms",number(result.timing.kernel_ms)},
    {"execution_profile",string(result.execution_profile)}, {"diagnostic_only",number(1)},
    {"cpu_behavior_probe_cases",number(behavior_probe?std::min<std::size_t>(32,cs.bindings.size()):0)},
    {"unique_cpu_behavior_probes",number(behaviors.size())}, {"programs",array(std::move(rows))}}));
}
void search(const std::vector<gagp::evo::ProgramGenome>& population,
            const std::vector<gagp::evo::EvalCase>& cases,
            const std::shared_ptr<const gg::CompiledGrammar>& grammar, const std::string& output) {
  gagp::evo::EvolutionConfig cfg;
  cfg.population_size=population.size(); cfg.generations=std::getenv("GAGP_SEARCH_GENERATIONS") ? std::stoi(std::getenv("GAGP_SEARCH_GENERATIONS")) : 4;
  cfg.seed=std::getenv("GAGP_BM_SEED") ? std::stoull(std::getenv("GAGP_BM_SEED")) : 0;
  cfg.eval_engine=gagp::evo::EvalEngine::GPU; cfg.reproduction_backend=gagp::evo::repro::ReproductionBackend::Gpu;
  cfg.repro_overlap=true; cfg.compiled_grammar=grammar; cfg.generation_request=gg::entry_request(*grammar);
  cfg.fuel=2000000; cfg.penalty=1000; cfg.gpu_blocksize=512; cfg.selection_pressure=2;
  cfg.mutation_rate=.3; cfg.mutation_subtree_prob=1; cfg.skip_final_eval=false; cfg.retain_final_population=true;
  const auto call_begin=std::chrono::steady_clock::now();
  const auto result=gagp::evo::evolve_population(cases,cfg,&population);
  const double evolve_wall_ms=std::chrono::duration<double,std::milli>(
      std::chrono::steady_clock::now()-call_begin).count();
  std::vector<Json> rows,final;
  for (std::size_t i=0;i<result.timing.generations.size();++i) {
    const auto& t=result.timing.generations[i]; const auto& e=t.evaluation; const auto& v=t.reproduction.variation;
    rows.push_back(object({{"generation",number(i)}, {"generation_ms",number(t.total_ms)},
      {"elapsed_search_ms",number(t.elapsed_search_ms)}, {"execution_profile",string(e.execution_profile)},
      {"best",number(result.history_best_fitness[i])}, {"mean",number(result.history_mean_fitness[i])},
      {"cases",number(e.program_cases)}, {"errors",number(e.eval_errors)}, {"timeouts",number(e.eval_timeouts)},
      {"fallbacks",number(e.eval_fallbacks)}, {"unscored",number(e.eval_unscored)},
      {"gpu_donor_generated",number(t.reproduction.gpu_donor_generated)}, {"gpu_donor_fallback",number(t.reproduction.gpu_donor_fallback)},
      {"gpu_donor_setup_ms",number(t.reproduction.gpu_donor_setup_ms)}, {"gpu_donor_device_bytes",number(t.reproduction.gpu_donor_device_bytes)},
      {"changed",number(v.changed_children)}, {"unchanged",number(v.unchanged_children)},
      {"operator_fallbacks",number(v.fallback_children)}, {"rejected",number(v.acceptance_rejections)}}));
  }
  for (const auto& one:result.final_population)
    final.push_back(object({{"fitness",number(one.fitness)}, {"nodes",number(one.genome.ast.nodes.size())}}));
  if (const char* path = std::getenv("GAGP_SEARCH_EXPORT")) {
    std::vector<Json> asts;
    for (const auto& one : result.final_population)
      asts.push_back(gagp::cli_detail::JsonParser(gagp::cli_detail::encode_ast_json(one.genome.ast),{true,512}).parse());
    write(path,object({{"programs",array(std::move(asts))}}));
  }
  if (const char* path=std::getenv("GAGP_SEARCH_HISTORY_EXPORT")) {
    std::vector<Json> history;
    for(std::size_t i=0;i<result.history_best.size();++i)history.push_back(object({
      {"generation",number(i)}, {"elapsed_search_ms",number(result.timing.generations[i].elapsed_search_ms)},
      {"fitness",number(result.history_best[i].fitness)},
      {"ast",gagp::cli_detail::JsonParser(gagp::cli_detail::encode_ast_json(result.history_best[i].genome.ast),{true,512}).parse()}}));
    write(path,object({{"history",array(std::move(history))}}));
  }
  const auto audit_begin=Clock::now();
  const auto cs=gagp::evo::prepare_case_set(cases);
  std::vector<gagp::BytecodeProgram> top;
  std::set<std::string> unique;
  for(std::size_t i=0;i<result.final_population.size();++i) {
    const auto& one=result.final_population[i];
    unique.insert(gg::runtime_cache_identity(one.genome,cs.input_names,cfg.fuel));
    if(i<16 || std::getenv("GAGP_SEARCH_CPU_ALL"))top.push_back(gagp::evo::compile_for_eval(one.genome,cs.input_names));
  }
  auto cpu=gagp::eval_fitness_cpu(top,cs.bindings,cs.expected_values,cfg.fuel,cfg.penalty,512);
  std::vector<Json> top_rows;
  std::size_t audit_mismatches=0;
  for(std::size_t i=0;i<cpu.size();++i)if(gagp::evo::canonicalize_fitness_for_ranking(cpu[i])!=result.final_population[i].fitness)++audit_mismatches;
  for(std::size_t i=0;i<std::min<std::size_t>(16,cpu.size());++i)
    top_rows.push_back(object({{"gpu",number(result.final_population[i].fitness)},{"cpu",number(cpu[i])}}));
  write(output,object({{"reproduction_profile",string(result.reproduction_profile)}, {"reproduction_fallback_reason",string(result.reproduction_fallback_reason)},
    {"genotype_device_bytes",number(result.genotype_device_bytes)}, {"top16_cpu",array(std::move(top_rows))},{"cpu_audit_ms",number(elapsed(audit_begin))},{"cpu_audit_program_count",number(cpu.size())},{"cpu_audit_mismatches",number(audit_mismatches)},
    {"unique_final_genomes",number(unique.size())},{"seed",number(cfg.seed)}, {"generations",array(std::move(rows))},
    {"final_population",array(std::move(final))}, {"evolve_wall_ms",number(evolve_wall_ms)}, {"evolve_call_ms",number(result.timing.total_ms)},
    {"initial_admission_ms",number(result.timing.init_population_ms)}, {"final_eval_ms",number(result.timing.final_eval_ms)}}));
}
void measure(const Json& source, const std::string& prepared_path, const std::string& grammar_path,
             int count, const std::string& mode, const std::string& output) {
  const std::set<std::string> modes{"asgp_1t","gagp_cpu","gpu_eval","gpu_repro","gpu_overlap","snapshot","search"};
  if (!modes.count(mode)) throw std::runtime_error("unknown mode");
  auto spec=task(source.object_v.at("task").string_v);
  std::vector<gagp::evo::EvalCase> cases;
  restore_cases(source,spec,cases);
  std::vector<asgp::Individual> parents;
  for (int i=0; i<count; ++i) parents.push_back(decode(source.object_v.at("population").array_v.at(i)));
  std::shared_ptr<const gg::CompiledGrammar> grammar;
  std::vector<gagp::evo::ProgramGenome> population;
  if (mode!="asgp_1t") {
    grammar=std::make_shared<gg::CompiledGrammar>(gg::compile_grammar(gg::load_definition(grammar_path)));
    auto prepared=read(prepared_path);
    if (prepared.object_v.at("grammar_hash").string_v!=grammar->content_hash() ||
        prepared.object_v.at("task").string_v!=spec.id) throw std::runtime_error("prepared identity mismatch");
    for (int i=0; i<count; ++i) {
      gagp::evo::ProgramGenome g;
      g.ast=gagp::cli_detail::decode_ast_json(prepared.object_v.at("programs").array_v.at(i));
      g.meta=gagp::evo::build_genome_meta(g.ast);
      g.derivation=std::make_shared<gg::DerivationMetadata>(gg::reconstruct_derivation(*grammar,g));
      population.push_back(std::move(g));
    }
  }
  if (mode=="snapshot") { snapshot(population,cases,output); return; }
  if (mode=="search") { search(population,cases,grammar,output); return; }
  std::ofstream log(output);
  if (!log) throw std::runtime_error("cannot open result file");
  for (int rep=-1; rep<3; ++rep) {
    auto row=mode=="asgp_1t" ? measure_asgp(parents,spec) : measure_gagp(population,cases,grammar,mode);
    row.object_v["task"]=string(spec.id); row.object_v["mode"]=string(mode);
    row.object_v["population"]=number(count); row.object_v["cases"]=number(cases.size());
    row.object_v["rep"]=number(rep);
    log << gg::canonical_json(row) << '\n' << std::flush;
    std::cerr << spec.id << ' ' << mode << " rep=" << rep << " generation_ms="
              << row.object_v.at("generation_ms").number_v << '\n';
  }
}
}  // namespace
int main(int argc,char** argv) try {
  if (argc==4 && std::string(argv[1])=="freeze") freeze(argv[2],argv[3]);
  else if (argc==6 && std::string(argv[1])=="prepare") prepare(read(argv[2]),argv[3],std::stoi(argv[4]),argv[5]);
  else if (argc==5 && std::string(argv[1])=="audit") audit(read(argv[2]),argv[3],argv[4]);
  else if (argc==8 && std::string(argv[1])=="measure") {
    const auto begin=Clock::now();
    measure(read(argv[2]),argv[3],argv[4],std::stoi(argv[5]),argv[6],argv[7]);
    write(std::string(argv[7])+".wall.json",object({{"input_through_report_ms",number(elapsed(begin))}}));
  }
  else throw std::runtime_error("usage: freeze TASK OUT | prepare SOURCE GRAMMAR POP OUT | audit SOURCE GRAMMAR OUT | measure SOURCE PREPARED GRAMMAR POP MODE OUT");
  return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
