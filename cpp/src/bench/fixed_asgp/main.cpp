#include "adapter.hpp"
#include "search_contract.hpp"
#include "expanded_audit.hpp"
#include "sampled_audit.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <cstring>
#include <cmath>
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
gagp::FitnessPolicy task_fitness_policy(const asgp::TaskSpec& spec) {
  gagp::FitnessPolicy policy;
  if (!std::getenv("GAGP_BM_ALIGNED_FITNESS")) return policy;
  if (std::getenv("GAGP_BM_COMMON_FITNESS"))
    throw std::invalid_argument("aligned native metrics cannot use historical common-fitness override");
  switch (spec.metric) {
    case asgp::Metric::kAbsolute: policy.metric=gagp::FitnessMetric::Absolute; break;
    case asgp::Metric::kExact: policy.metric=gagp::FitnessMetric::Exact; break;
    case asgp::Metric::kLevenshtein: policy.metric=gagp::FitnessMetric::Levenshtein; break;
    case asgp::Metric::kTupleSummary: policy.metric=gagp::FitnessMetric::TupleSummary; break;
  }
  policy.proximity=spec.proximity==asgp::Proximity::kDiscounted
      ? gagp::FitnessProximity::Discounted : gagp::FitnessProximity::Capped;
  policy.summary_count=spec.summaries;
  policy.summary_weight=spec.summary_weight;
  policy.summary_cap=spec.summary_cap;
  return policy;
}
Json search_contract_json(const gagp::evo::EvolutionConfig& cfg, const std::string& profile) {
  const auto audit=fixed_asgp::audit_search_contract(cfg,profile,
      cfg.fitness_policy.metric!=gagp::FitnessMetric::Legacy,false);
  std::vector<Json> items;
  for(const auto& item:audit.items)items.push_back(object({{"id",string(item.id)},
    {"asgp",string(item.source)},{"gagp",string(item.native)},{"matched",number(item.matched)}}));
  return object({{"version",string(audit.version)},{"fully_aligned",number(audit.fully_aligned())},
    {"items",array(std::move(items))}});
}
void freeze(const std::string& name, const std::string& path) {
  auto spec=task(name);
  const auto grammar=asgp::SchemeGrammar::Build(spec.Grammar());
  asgp::Rng rng(0);
  std::vector<Json> population, cases;
  const int population_count=std::getenv("GAGP_FREEZE_POPULATION") ? std::stoi(std::getenv("GAGP_FREEZE_POPULATION")) : 8192;
  if(population_count<=0)throw std::invalid_argument("freeze population must be positive");
  for (int i=0; i<population_count; ++i)
    population.push_back(encode(asgp::Initialize(grammar,2+i%6,(i/6)%2==1,rng),grammar.count));
  for (const auto& c : spec.train) {
    std::vector<Json> values;
    for (auto v : c.elements) values.push_back(string(std::to_string(v)));
    Json expected=string(std::to_string(c.expected.i));
    if (asgp::IsListType(spec.output)) {
      std::vector<Json> items;for(auto v:c.expected.list)items.push_back(string(std::to_string(v)));
      expected=array(std::move(items));
    }
    if(spec.output==asgp::Type::kBool) expected=number(c.expected.b);
    std::vector<Json> scalars; for(auto v:c.scalars)scalars.push_back(string(std::to_string(v)));
    auto row=object({{"input",array(std::move(values))}, {"expected",expected},
                     {"scalars",array(std::move(scalars))},{"summary1",string(std::to_string(c.expected.summary1))},{"summary2",string(std::to_string(c.expected.summary2))},
                     {"start",number(c.start.a)}, {"extent",number(c.extent.a)}});
    {
      row.object_v["start_b"]=number(c.start.b);row.object_v["extent_b"]=number(c.extent.b);
    }
    cases.push_back(std::move(row));
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
    gagp::Value expected;
    if (asgp::IsListType(spec.output)) {
      std::vector<std::int64_t> native;std::vector<gagp::Value> packed;
      for(const auto& v:row.object_v.at("expected").array_v) {
        native.push_back(std::stoll(v.string_v));packed.push_back(gagp::Value::from_int(native.back()));
      }
      example.expected=asgp::Output::List(std::move(native));
      expected=gagp::payload::make_int_list_value(packed);
    } else if(spec.output==asgp::Type::kBool) {
      example.expected=asgp::Output::Bool(row.object_v.at("expected").number_v!=0);
      expected=gagp::Value::from_bool(example.expected.b);
    } else {
      example.expected=asgp::Output::Int(std::stoll(row.object_v.at("expected").string_v));
      expected=gagp::Value::from_int(example.expected.i);
    }
    example.start.a=int(row.object_v.at("start").number_v);
    example.extent.a=int(row.object_v.at("extent").number_v);
    if(row.object_v.count("start_b"))example.start.b=int(row.object_v.at("start_b").number_v);
    if(row.object_v.count("extent_b"))example.extent.b=int(row.object_v.at("extent_b").number_v);
    if(row.object_v.count("scalars"))for(const auto& v:row.object_v.at("scalars").array_v)example.scalars.push_back(std::stoll(v.string_v));
    if(example.scalars.size()!=spec.scalars.size())throw std::runtime_error("frozen scalar count mismatch");
    if(row.object_v.count("summary1"))example.expected.summary1=std::stoll(row.object_v.at("summary1").string_v);
    if(row.object_v.count("summary2"))example.expected.summary2=std::stoll(row.object_v.at("summary2").string_v);
    if(spec.carrier==asgp::Type::kTuple)expected=gagp::payload::make_int_list_value({gagp::Value::from_int(example.expected.b),gagp::Value::from_int(example.expected.summary1),gagp::Value::from_int(example.expected.summary2)});
    gagp::evo::EvalCase native{{{"source",gagp::payload::make_int_list_value(values)}},expected};
    for(std::size_t i=0;i<example.scalars.size();++i)native.inputs["scalar"+std::to_string(i)]=gagp::Value::from_int(example.scalars[i]);
    if(spec.scheme==asgp::Scheme::kDynamicProgramming && spec.id!="house_robber") {
      native.inputs["start_a"]=gagp::Value::from_int(example.start.a);native.inputs["start_b"]=gagp::Value::from_int(example.start.b);
      native.inputs["extent_a"]=gagp::Value::from_int(example.extent.a);native.inputs["extent_b"]=gagp::Value::from_int(example.extent.b);
    }
    cases.push_back(std::move(native));
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
  auto cs=gagp::evo::prepare_case_set(cases);
  const auto code=gagp::evo::compile_for_eval(genome,cs.input_names);
  int cpu_bad=0;
  for (const auto& c : cases) {
    std::vector<std::pair<int,gagp::Value>> inputs;
    for(std::size_t i=0;i<cs.input_names.size();++i)inputs.emplace_back(i,c.inputs.at(cs.input_names[i]));
    const auto result=gagp::execute_bytecode_cpu(code,inputs,2000000);
    bool equal=!result.is_error && result.value.tag==c.expected.tag;
    if(equal && c.expected.tag==gagp::ValueTag::IntList) {
      std::vector<gagp::Value> actual,wanted;
      equal=gagp::payload::lookup_list(result.value,&actual) && gagp::payload::lookup_list(c.expected,&wanted) && actual.size()==wanted.size();
      if(equal)for(std::size_t i=0;i<actual.size();++i)equal=equal && actual[i].tag==wanted[i].tag && actual[i].i==wanted[i].i;
    } else if(equal)equal=c.expected.tag==gagp::ValueTag::Bool ? result.value.b==c.expected.b : result.value.i==c.expected.i;
    cpu_bad+=!equal;
  }
  gagp::FitnessSessionGpu session;
  auto init=session.init(cs.bindings,cs.expected_values,2000000,512,1000,task_fitness_policy(spec));
  if (!init.ok) throw std::runtime_error(init.err.message);
  auto gpu=session.eval_programs({code});
  if (!gpu.ok) throw std::runtime_error(gpu.err.message);
  std::set<std::string> unique;
  for (const auto& c : source.object_v.at("cases").array_v)
    unique.insert(gg::canonical_json(c.object_v.at("input")));
  write(output,object({{"task",string(spec.id)}, {"cases",number(cases.size())},
                       {"unique_inputs",number(unique.size())}, {"known_solution_cpu_mismatches",number(cpu_bad)},
                       {"known_solution_asgp_wrong",number(fit.wrong)}, {"known_solution_gpu_fitness",number(gpu.fitness[0])}}));
  const double expected_fitness=task_fitness_policy(spec).metric==gagp::FitnessMetric::Legacy &&
      !gagp::is_numeric(cases[0].expected) ? 1024.0 : 0.0;
  if (gpu.fitness.at(0)!=expected_fitness) throw std::runtime_error("known solution GPU fitness mismatch");
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
                  const std::shared_ptr<const gg::CompiledGrammar>& grammar, const std::string& mode,
                  const asgp::TaskSpec& spec) {
  gagp::evo::EvolutionConfig cfg;
  cfg.population_size=population.size(); cfg.generations=1; cfg.seed=0;
  cfg.eval_engine=mode=="gagp_cpu" ? gagp::evo::EvalEngine::CPU : gagp::evo::EvalEngine::GPU;
  cfg.reproduction_backend=(mode=="gpu_repro" || mode=="gpu_overlap")
      ? gagp::evo::repro::ReproductionBackend::Gpu : gagp::evo::repro::ReproductionBackend::Cpu;
  cfg.repro_overlap=mode=="gpu_overlap"; cfg.compiled_grammar=grammar;
  cfg.generation_request=gg::entry_request(*grammar);
  cfg.fuel=2000000; cfg.penalty=1000; cfg.gpu_blocksize=512;
  cfg.fitness_policy=task_fitness_policy(spec);
  cfg.elite_count=std::getenv("GAGP_BM_ALIGNED_FITNESS") ? 1 : 0;
  cfg.selection_pressure=2;
  cfg.mutation_rate=.3; cfg.mutation_subtree_prob=1;
  cfg.skip_final_eval=true; cfg.retain_final_population=false;
  const auto call_begin=std::chrono::steady_clock::now();
  const auto result=gagp::evo::evolve_population(cases,cfg,&population);
  const double evolve_wall_ms=std::chrono::duration<double,std::milli>(
      std::chrono::steady_clock::now()-call_begin).count();
  const auto& t=result.timing.generations.at(0);
  const auto& v=t.reproduction.variation;
  return object({{"execution_profile",string(t.evaluation.execution_profile)}, {"search_contract",search_contract_json(cfg,result.reproduction_profile)},
    {"reproduction_profile",string(result.reproduction_profile)}, {"reproduction_fallback_reason",string(result.reproduction_fallback_reason)},
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
              const std::vector<gagp::evo::EvalCase>& cases, const std::string& output, const asgp::TaskSpec& spec) {
  const double penalty=std::getenv("GAGP_SNAPSHOT_PENALTY") ? std::stod(std::getenv("GAGP_SNAPSHOT_PENALTY")) : 1000;
  if(!std::isfinite(penalty) || penalty<0 ||
     (penalty==0 && !(std::getenv("GAGP_BM_COMMON_FITNESS") && !cases.empty() && cases[0].expected.tag==gagp::ValueTag::IntList)))
    throw std::invalid_argument("snapshot penalty must be positive (common list exact policy permits zero)");
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
  const auto init=session.init(cs.bindings,cs.expected_values,2000000,512,penalty,task_fitness_policy(spec));
  if (!init.ok) throw std::runtime_error(init.err.message);
  const auto result=session.eval_programs(programs,true);
  if (!result.ok) throw std::runtime_error(result.err.message);
  std::vector<double> cpu;
  if (std::getenv("GAGP_SNAPSHOT_CPU"))
    cpu=gagp::eval_fitness_cpu(programs,cs.bindings,cs.expected_values,2000000,penalty,512,task_fitness_policy(spec));
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
void search_asgp(const std::vector<asgp::Individual>& parents, const asgp::TaskSpec& spec,
                 const std::string& output) {
  const auto start=Clock::now();
  auto work=parents;
  const auto grammar=asgp::SchemeGrammar::Build(spec.Grammar());
  asgp::FitnessScratch scratch; asgp::VariationScratch variation;
  const auto config=fitness_config();
  const auto seed=std::getenv("GAGP_BM_SEED") ? std::stoull(std::getenv("GAGP_BM_SEED")) : 0;
  const int generations=std::getenv("GAGP_SEARCH_GENERATIONS") ? std::stoi(std::getenv("GAGP_SEARCH_GENERATIONS")) : 4;
  if(generations<=0 || work.empty())throw std::invalid_argument("invalid search dimensions");
  asgp::Rng rng(seed); std::vector<Json> rows,history;
  const double init_ms=elapsed(start);
  const auto evaluate=[&]() {
    for(auto& one:work)one.fitness=asgp::Score(one,spec,spec.train,config,scratch);
  };
  const auto best_index=[&]() {
    return std::min_element(work.begin(),work.end(),[](const auto& a,const auto& b){return asgp::Better(a.fitness,b.fitness);})-work.begin();
  };
  for(int gen=0;gen<generations;++gen) {
    const auto begin=Clock::now();evaluate();const double eval_ms=elapsed(begin);
    const auto best=best_index();double mean=0,nodes=0;
    for(const auto& one:work){mean-=one.fitness.error;nodes+=one.NodeCount();}
    // Keep the historical best available for held-out evaluation after search.
    history.push_back(object({{"generation",number(gen)}, {"fitness",number(-work[best].fitness.error)},
                              {"program",encode(work[best],grammar.count)}}));
    const auto repro_begin=Clock::now();
    const auto pick=[&]() -> const asgp::Individual& {
      const auto& a=work[rng.Index(work.size())];const auto& b=work[rng.Index(work.size())];
      return asgp::Better(a.fitness,b.fitness)?a:b;
    };
    std::vector<asgp::Individual> children;children.reserve(work.size());children.push_back(work[best]);
    while(children.size()<work.size()) {
      const auto& a=pick();const auto& b=pick();
      children.push_back(asgp::CrossoverByPhase(a,b,grammar.count,.7,asgp::Reach::kOnePhase,7,rng,variation));
      asgp::MutateByPhase(children.back(),grammar,.3,asgp::Reach::kOnePhase,7,rng,variation);
    }
    const double repro_ms=elapsed(repro_begin),total_ms=elapsed(begin),wall_ms=elapsed(start);
    rows.push_back(object({{"generation",number(gen)}, {"generation_ms",number(total_ms)},
      {"elapsed_search_ms",number(wall_ms)}, {"eval_ms",number(eval_ms)}, {"repro_ms",number(repro_ms)},
      {"best",number(-work[best].fitness.error)}, {"mean",number(mean/work.size())},
      {"best_nodes",number(work[best].NodeCount())}, {"mean_nodes",number(nodes/work.size())},
      {"best_wrong",number(work[best].fitness.wrong)}, {"case_counters_available",number(0)},
      {"expected_program_cases",number(work.size()*spec.train.size())}}));
    work=std::move(children);
    // Include parent retirement and next-generation installation in generation time.
    const double complete_ms=elapsed(begin),complete_wall_ms=elapsed(start);
    rows.back().object_v["generation_ms"]=number(complete_ms);
    rows.back().object_v["elapsed_search_ms"]=number(complete_wall_ms);
    history.back().object_v["elapsed_search_ms"]=number(complete_wall_ms);
    if(std::getenv("GAGP_SEARCH_TRACE"))std::cerr<<"generation="<<gen<<" best="<<rows.back().object_v.at("best").number_v<<" generation_ms="<<complete_ms<<" elapsed_ms="<<complete_wall_ms<<'\n';
  }
  const auto final_begin=Clock::now();evaluate();const double final_ms=elapsed(final_begin);
  const auto best=best_index();const double wall_ms=elapsed(start);
  std::vector<Json> final;std::set<std::string> unique;
  for(const auto& one:work){final.push_back(object({{"fitness",number(-one.fitness.error)},{"nodes",number(one.NodeCount())}}));unique.insert(gg::canonical_json(encode(one,grammar.count)));}
  if(const char* path=std::getenv("GAGP_SEARCH_HISTORY_EXPORT"))write(path,object({{"history",array(std::move(history))}}));
  if(const char* path=std::getenv("GAGP_SEARCH_EXPORT"))write(path,object({{"task",string(spec.id)},{"best_program",encode(work[best],grammar.count)}}));
  write(output,object({{"mode",string("asgp_1t")},{"population",number(work.size())},{"cases",number(spec.train.size())},
    {"seed",number(seed)},{"generations",array(std::move(rows))},{"final_population",array(std::move(final))},
    {"final_best",number(-work[best].fitness.error)},{"unique_final_genomes",number(unique.size())},
    {"evolve_wall_ms",number(wall_ms)},{"initial_admission_ms",number(init_ms)},{"final_eval_ms",number(final_ms)},
    {"reproduction_profile",string("asgp-fixed-native-elite1")}}));
}
void search(const std::vector<gagp::evo::ProgramGenome>& population,
            const std::vector<gagp::evo::EvalCase>& cases,
            const std::shared_ptr<const gg::CompiledGrammar>& grammar, const std::string& output,
            const std::string& mode, const asgp::TaskSpec& spec) {
  gagp::evo::EvolutionConfig cfg;
  cfg.population_size=population.size(); cfg.generations=std::getenv("GAGP_SEARCH_GENERATIONS") ? std::stoi(std::getenv("GAGP_SEARCH_GENERATIONS")) : 4;
  cfg.seed=std::getenv("GAGP_BM_SEED") ? std::stoull(std::getenv("GAGP_BM_SEED")) : 0;
  cfg.eval_engine=mode=="gagp_cpu" ? gagp::evo::EvalEngine::CPU : gagp::evo::EvalEngine::GPU;
  cfg.reproduction_backend=(mode=="gpu_repro" || mode=="gpu_overlap") ? gagp::evo::repro::ReproductionBackend::Gpu : gagp::evo::repro::ReproductionBackend::Cpu;
  cfg.repro_overlap=mode=="gpu_overlap"; cfg.compiled_grammar=grammar; cfg.generation_request=gg::entry_request(*grammar);
  cfg.fuel=2000000; cfg.penalty=std::getenv("GAGP_SEARCH_PENALTY") ? std::stod(std::getenv("GAGP_SEARCH_PENALTY")) : 1000;
  if(!std::isfinite(cfg.penalty) || cfg.penalty<0 ||
     (cfg.penalty==0 && !(std::getenv("GAGP_BM_COMMON_FITNESS") && !cases.empty() && cases[0].expected.tag==gagp::ValueTag::IntList)))
    throw std::invalid_argument("search penalty must be positive (common list exact policy permits zero)"); cfg.gpu_blocksize=512; cfg.selection_pressure=2;
  cfg.fitness_policy=task_fitness_policy(spec);
  cfg.elite_count=std::getenv("GAGP_BM_ALIGNED_FITNESS") ? 1 : 0;
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
      {"eval_ms",number(t.eval_ms)}, {"repro_ms",number(t.repro_ms)},
      {"compile_ms",number(e.cpu_compile_ms+e.gpu_compile_ms)}, {"eval_kernel_ms",number(e.gpu_eval_kernel_ms)},
      {"pack_ms",number(e.gpu_eval_pack_ms)}, {"upload_ms",number(e.gpu_eval_upload_ms)},
      {"repro_kernel_ms",number(t.reproduction.kernel_ms)}, {"repro_decode_ms",number(t.reproduction.decode_ms)},
      {"repro_prepare_ms",number(t.reproduction.prepare_inputs_ms)},
      {"best_nodes",number(result.history_best[i].genome.ast.nodes.size())},
      {"expected_program_cases",number(population.size()*cases.size())},
      {"case_counters_available",number(mode!="gagp_cpu" && std::getenv("GAGP_GPU_DIAGNOSTICS")!=nullptr)},
      {"best",number(result.history_best_fitness[i])}, {"mean",number(result.history_mean_fitness[i])},
      {"cases",number(e.program_cases)}, {"errors",number(e.eval_errors)}, {"timeouts",number(e.eval_timeouts)},
      {"fallbacks",number(e.eval_fallbacks)}, {"unscored",number(e.eval_unscored)},
      {"gpu_donor_generated",number(t.reproduction.gpu_donor_generated)}, {"gpu_donor_fallback",number(t.reproduction.gpu_donor_fallback)},
      {"gpu_donor_setup_ms",number(t.reproduction.gpu_donor_setup_ms)}, {"gpu_donor_device_bytes",number(t.reproduction.gpu_donor_device_bytes)},
      {"changed",number(v.changed_children)}, {"unchanged",number(v.unchanged_children)},
      {"operator_fallbacks",number(v.fallback_children)}, {"rejected",number(v.acceptance_rejections)},
      {"contract_rejections",number(v.contract_rejections)}, {"budget_rejections",number(v.budget_rejections)},
      {"generation_rejections",number(v.generation_rejections)}}));
  }
  for (const auto& one:result.final_population)
    final.push_back(object({{"fitness",number(one.fitness)}, {"wrong",number(one.wrong)}, {"nodes",number(one.genome.ast.nodes.size())}}));
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
  auto cpu=gagp::eval_fitness_cpu(top,cs.bindings,cs.expected_values,cfg.fuel,cfg.penalty,512,cfg.fitness_policy);
  std::vector<Json> top_rows;
  std::size_t audit_mismatches=0;
  for(std::size_t i=0;i<cpu.size();++i)if(gagp::evo::canonicalize_fitness_for_ranking(cpu[i])!=result.final_population[i].fitness)++audit_mismatches;
  for(std::size_t i=0;i<std::min<std::size_t>(16,cpu.size());++i)
    top_rows.push_back(object({{"gpu",number(result.final_population[i].fitness)},{"cpu",number(cpu[i])}}));
  write(output,object({{"mode",string(mode)}, {"case_penalty",number(cfg.penalty)}, {"elite_count",number(cfg.elite_count)}, {"fitness_metric",number(static_cast<int>(cfg.fitness_policy.metric))}, {"population",number(population.size())}, {"cases",number(cases.size())},
    {"final_best",number(result.final_population.front().fitness)},
    {"search_contract",search_contract_json(cfg,result.reproduction_profile)},
    {"reproduction_profile",string(result.reproduction_profile)}, {"reproduction_fallback_reason",string(result.reproduction_fallback_reason)},
    {"genotype_device_bytes",number(result.genotype_device_bytes)}, {"top16_cpu",array(std::move(top_rows))},{"cpu_audit_ms",number(elapsed(audit_begin))},{"cpu_audit_program_count",number(cpu.size())},{"cpu_audit_mismatches",number(audit_mismatches)},
    {"unique_final_genomes",number(unique.size())},{"seed",number(cfg.seed)}, {"generations",array(std::move(rows))},
    {"final_population",array(std::move(final))}, {"evolve_wall_ms",number(evolve_wall_ms)}, {"evolve_call_ms",number(result.timing.total_ms)},
    {"initial_admission_ms",number(result.timing.init_population_ms)}, {"gpu_init_ms",number(result.timing.gpu_eval_init_ms)},
    {"final_eval_ms",number(result.timing.final_eval_ms)}}));
}
void measure(const Json& source, const std::string& prepared_path, const std::string& grammar_path,
             int count, const std::string& mode, const std::string& output) {
  const std::set<std::string> modes{"asgp_1t","gagp_cpu","gpu_eval","gpu_repro","gpu_overlap","snapshot","asgp_snapshot","search","search_asgp_1t","search_gagp_cpu","search_gpu_eval","search_gpu_repro","search_gpu_overlap"};
  if (!modes.count(mode)) throw std::runtime_error("unknown mode");
  auto spec=task(source.object_v.at("task").string_v);
  std::vector<gagp::evo::EvalCase> cases;
  restore_cases(source,spec,cases);
  std::vector<asgp::Individual> parents;
  for (int i=0; i<count; ++i) parents.push_back(decode(source.object_v.at("population").array_v.at(i)));
  std::shared_ptr<const gg::CompiledGrammar> grammar;
  std::vector<gagp::evo::ProgramGenome> population;
  if (mode!="asgp_1t" && mode!="search_asgp_1t" && mode!="asgp_snapshot") {
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
  if (mode=="asgp_snapshot") {
    asgp::FitnessScratch scratch; const auto config=fitness_config(); std::vector<Json> rows;
    for(const auto& one:parents) {
      const auto score=asgp::Score(one,spec,spec.train,config,scratch);
      rows.push_back(object({{"fitness",number(-score.error)},{"wrong",number(score.wrong)},{"nodes",number(score.nodes)},{"cases",number(spec.train.size())}}));
    }
    write(output,object({{"programs",array(std::move(rows))}})); return;
  }
  if (mode=="search_asgp_1t") { search_asgp(parents,spec,output); return; }
  if (mode.starts_with("search_")) { search(population,cases,grammar,output,mode.substr(7),spec); return; }
  if (mode=="snapshot") { snapshot(population,cases,output,spec); return; }
  if (mode=="search") { search(population,cases,grammar,output,"gpu_overlap",spec); return; }
  std::ofstream log(output);
  if (!log) throw std::runtime_error("cannot open result file");
  for (int rep=-1; rep<3; ++rep) {
    auto row=mode=="asgp_1t" ? measure_asgp(parents,spec) : measure_gagp(population,cases,grammar,mode,spec);
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
  if (argc==4 && std::string(argv[1])=="contract") write(argv[3],task_contract(argv[2]));
  else if (argc==4 && std::string(argv[1])=="freeze") freeze(argv[2],argv[3]);
  else if ((argc==6 || argc==7) && std::string(argv[1])=="quality")
    fixed_asgp::expanded_quality(argv[2],argv[3],argv[4],argv[5],argc==7?argv[6]:"");
  else if (argc==6 && std::string(argv[1])=="prepare") prepare(read(argv[2]),argv[3],std::stoi(argv[4]),argv[5]);
  else if (argc==5 && std::string(argv[1])=="sampled-audit") sampled_audit(read(argv[2]),argv[3],argv[4]);
  else if (argc==5 && std::string(argv[1])=="audit") audit(read(argv[2]),argv[3],argv[4]);
  else if (argc==8 && std::string(argv[1])=="measure") {
    const auto begin=Clock::now();
    measure(read(argv[2]),argv[3],argv[4],std::stoi(argv[5]),argv[6],argv[7]);
    write(std::string(argv[7])+".wall.json",object({{"input_through_report_ms",number(elapsed(begin))}}));
  }
  else throw std::runtime_error("usage: quality TASK EXPORT GRAMMAR OUT [HISTORY] | freeze TASK OUT | prepare SOURCE GRAMMAR POP OUT | audit SOURCE GRAMMAR OUT | measure SOURCE PREPARED GRAMMAR POP MODE OUT");
  return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
