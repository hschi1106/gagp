#pragma once

#include "adapter.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <span>
#include <stdexcept>
#include "gp/fitness.h"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/grammar/identity.hpp"
#include "gagp/runtime/cpu/execution_session.hpp"
#include "gagp/runtime/payload/payload.hpp"
#include "../../runtime/payload/staging.hpp"

namespace fixed_asgp {
namespace expanded_audit_detail {
constexpr double cap = 1000;
constexpr std::uint32_t fuel = 2000000;
struct Totals {
  double case_cap = cap;
  bool list_output = false;
  std::size_t exact = 0, errors = 0, capped = 0;
  double l1 = 0;
  std::vector<Json> rows;
  void add(std::size_t index, bool correct, bool error, double loss) {
    exact += correct; errors += error; capped += loss >= case_cap; l1 += loss;
    rows.push_back(object({{"case",number(index)}, {"exact",number(correct)},
      {"runtime_error",number(error)}, {"common_loss",number(loss)}}));
  }
  Json finish() {
    const auto n = rows.size();
    return object({{"cases",number(n)}, {"exact",number(exact)},
      {"correct_count",number(exact)}, {"wrong",number(n-exact)}, {"exact_accuracy",number(double(exact)/n)},
      {"runtime_errors",number(errors)}, {"cases_at_cap",number(capped)},
      {list_output ? "exact_loss_sum" : "capped_l1_sum",number(l1)},
      {list_output ? "exact_loss_mean" : "capped_l1_mean",number(l1/n)},
      {"case_results",array(std::move(rows))}});
  }
};
inline bool execute_source(const asgp::Individual& individual,const asgp::TaskSpec& spec,
                           const asgp::Example& example,const asgp::EvalLimits& limits,
                           asgp::FitnessScratch& scratch) {
  if(spec.scheme==asgp::Scheme::kDynamicProgramming) {
    asgp::DpProgram program{&individual.phases[0],&individual.phases[1],&spec.dp,spec.carrier};
    asgp::DpInstance instance{example.elements.data(),static_cast<std::uint32_t>(example.elements.size()),example.scalars.data(),example.start,example.extent};
    const auto result=asgp::RunDp(program,instance,limits,scratch.memo,&scratch.arena);
    if(!result.ok())return false;
    scratch.produced=asgp::Project(result.structured,spec,scratch.arena);return true;
  }
  if(spec.scheme!=asgp::Scheme::kDivideConquer)throw std::runtime_error("unsupported source audit scheme");
  asgp::DcProgram program{&individual.phases[0],&individual.phases[1],&individual.phases[2],spec.carrier,spec.base_size};
  asgp::Instance instance{example.elements.data(),static_cast<std::uint32_t>(example.elements.size()),example.scalars.data()};
  const auto result=asgp::RunDc(program,instance,limits,scratch.arena);
  if(!result.ok())return false;
  scratch.produced=asgp::Project(result.value,spec,scratch.arena);return true;
}
inline Json score_asgp(const Json& program, asgp::TaskSpec spec) {
  const auto individual = decode(program);
  // The common metric is explicitly independent of native discounted fitness.
  const bool list_output = asgp::IsListType(spec.output);
  spec.metric = list_output ? asgp::Metric::kExact : asgp::Metric::kAbsolute;
  spec.proximity = asgp::Proximity::kRewarded;
  asgp::FitnessConfig config;
  config.max_case_error = cap;
  config.limits.max_steps = fuel;
  config.limits.max_recursion = 127;
  asgp::FitnessScratch scratch;
  Totals totals;
  totals.list_output = asgp::IsListType(spec.output);
  totals.case_cap = (totals.list_output || spec.output==asgp::Type::kBool) ? 1 : cap;
  for (std::size_t i=0; i<spec.test.size(); ++i) {
    const bool error=!execute_source(individual,spec,spec.test[i],config.limits,scratch);
    const auto score=error ? asgp::CaseScore{} : asgp::Compare(scratch.produced,spec.test[i].expected,spec,scratch.row);
    totals.add(i,!error && score.exact,error,error ? totals.case_cap : std::min(totals.case_cap,score.error));
  }
  auto result = totals.finish();
  result.object_v["backend"] = string("asgp_1t");
  return result;
}
inline Json score_gagp(const Json& ast, const asgp::TaskSpec& spec,
                        const gg::CompiledGrammar& grammar) {
  gagp::evo::ProgramGenome genome;
  genome.ast = gagp::cli_detail::decode_ast_json(ast);
  genome.meta = gagp::evo::build_genome_meta(genome.ast);
  gg::require_membership(grammar,genome);
  std::vector<std::string> names{"source"};
  for(std::size_t i=0;i<spec.scalars.size();++i)names.push_back("scalar"+std::to_string(i));
  if(spec.scheme==asgp::Scheme::kDynamicProgramming && spec.id!="house_robber") {
    names.insert(names.end(),{"start_a","start_b","extent_a","extent_b"});
  }
  const auto code = gagp::evo::compile_for_eval(genome,names);
  gagp::CpuExecutionSession session(code);
  Totals totals;
  totals.list_output = asgp::IsListType(spec.output);
  totals.case_cap = (totals.list_output || spec.output==asgp::Type::kBool) ? 1 : cap;
  for (std::size_t i=0; i<spec.test.size(); ++i) {
    try {
    gagp::payload::StagedPayloads payloads(8*1024*1024);
    gagp::payload::StagedPayloads::Scope scope(payloads);
    const auto& example = spec.test[i];
    std::vector<gagp::Value> elements;
    for (const auto v:example.elements) elements.push_back(gagp::Value::from_int(v));
    const auto input = gagp::payload::make_int_list_value(elements);
    std::vector<std::pair<int,gagp::Value>> inputs{{0,input}};
    for(auto v:example.scalars)inputs.emplace_back(inputs.size(),gagp::Value::from_int(v));
    if(spec.scheme==asgp::Scheme::kDynamicProgramming && spec.id!="house_robber") {
      for(auto v:{example.start.a,example.start.b,example.extent.a,example.extent.b})inputs.emplace_back(inputs.size(),gagp::Value::from_int(v));
    }
    const auto result = session.execute(inputs,fuel);
    bool error = result.is_error;
    bool exact = false;
    double loss = totals.case_cap;
    if (totals.list_output && !error) {
      std::vector<gagp::Value> produced;
      error = result.value.tag!=gagp::ValueTag::IntList ||
        !gagp::payload::lookup_list(result.value,&produced);
      exact = !error && produced.size()==example.expected.list.size();
      for (std::size_t j=0; exact && j<produced.size(); ++j)
        exact = produced[j].tag==gagp::ValueTag::Int && produced[j].i==example.expected.list[j];
      loss = exact ? 0 : 1;
    } else if (!error && spec.output==asgp::Type::kBool) {
      std::vector<gagp::Value> tuple;
      error=result.value.tag!=gagp::ValueTag::IntList || !gagp::payload::lookup_list(result.value,&tuple) || tuple.size()!=3;
      exact=!error && (tuple[0].i!=0)==example.expected.b;loss=exact?0:1;
    } else if (!error) {
      error = result.value.tag!=gagp::ValueTag::Int;
      exact = !error && result.value.i==example.expected.i;
      if (!error) loss = static_cast<double>(std::min<long double>(cap,
        std::fabs(static_cast<long double>(result.value.i)-example.expected.i)));
    }
    totals.add(i,exact,error,loss);
    } catch(const gagp::payload::PayloadBudgetExceeded&) { totals.add(i,false,true,totals.case_cap); }
  }
  auto result = totals.finish();
  result.object_v["backend"] = string("gagp_cpu_1t");
  result.object_v["grammar_admitted"] = number(1);
  result.object_v["grammar_hash"] = string(grammar.content_hash());
  return result;
}
} // namespace expanded_audit_detail

// Pure post-search audit: loads the already selected champion; never injects,
// re-ranks, simplifies or changes search programs. Optional history is a separate
// GAGP_SEARCH_HISTORY_EXPORT artifact; only gen0/middle/last are evaluated.
inline void expanded_quality(const std::string& name, const std::string& export_path,
                             const std::string& grammar_path, const std::string& output,
                             const std::string& history_path = "") {
  asgp::DataConfig config;
  config.train=1024; config.test=1024; config.seed=20261015; config.legacy_finite=false;
  auto built = asgp::BuildTask(name,config);
  if (!built) throw std::invalid_argument("unknown quality task: "+name);
  const auto& spec = *built;
  if (spec.test.size()!=1024)throw std::invalid_argument("quality audit requires 1024 test cases");
  const auto exported = read(export_path);
  if (exported.object_v.count("task") && exported.object_v.at("task").string_v!=name)
    throw std::invalid_argument("quality export task mismatch");
  const bool asgp_export = exported.object_v.count("best_program")!=0;
  std::vector<Json> encoded_cases;
  std::set<std::string> unique;
  for (const auto& example:spec.test) {
    std::vector<Json> values;
    for (auto v:example.elements) values.push_back(string(std::to_string(v)));
    auto input = array(std::move(values));
    std::vector<Json> scalars;for(auto v:example.scalars)scalars.push_back(string(std::to_string(v)));
    auto identity=object({{"input",input},{"scalars",array(scalars)},{"start",number(example.start.a)},{"start_b",number(example.start.b)}, {"extent",number(example.extent.a)}, {"extent_b",number(example.extent.b)}});
    unique.insert(gg::canonical_json(identity));
    std::vector<Json> expected_list;
    for (auto v:example.expected.list) expected_list.push_back(string(std::to_string(v)));
    encoded_cases.push_back(object({{"input",std::move(input)}, {"scalars",array(std::move(scalars))}, {"start_b",number(example.start.b)}, {"extent_b",number(example.extent.b)},
      {"summary1",string(std::to_string(example.expected.summary1))},{"summary2",string(std::to_string(example.expected.summary2))},
      {"expected",asgp::IsListType(spec.output) ? array(std::move(expected_list)) : string(std::to_string(spec.output==asgp::Type::kBool ? example.expected.b : example.expected.i))},
      {"start",number(example.start.a)}, {"extent",number(example.extent.a)}}));
  }
  auto cases = array(std::move(encoded_cases));
  const auto hash = gg::content_sha256(gg::canonical_json(cases));
  // Only GAGP artifacts require its grammar; ASGP executes original phases.
  std::shared_ptr<gg::CompiledGrammar> grammar;
  if (!asgp_export) grammar = std::make_shared<gg::CompiledGrammar>(
    gg::compile_grammar(gg::load_definition(grammar_path)));
  const auto evaluate = [&](const Json& program) {
    return asgp_export ? expanded_audit_detail::score_asgp(program,spec)
      : expanded_audit_detail::score_gagp(program,spec,*grammar);
  };
  auto final = evaluate(asgp_export ? exported.object_v.at("best_program")
    : exported.object_v.at("programs").array_v.at(0));
  std::vector<Json> history;
  if (!history_path.empty()) {
    const auto artifact = read(history_path);
    const auto& rows = artifact.object_v.at("history").array_v;
    if (!rows.empty()) for (const auto index:std::set<std::size_t>{0,rows.size()/2,rows.size()-1}) {
      const auto& row = rows[index];
      auto result = evaluate(row.object_v.at(asgp_export ? "program" : "ast"));
      result.object_v["generation"] = row.object_v.at("generation");
      history.push_back(std::move(result));
    }
  }
  write(output,object({{"format",string("expanded-heldout-quality-v1")},
    {"task",string(name)}, {"export_path",string(export_path)},
    {"history_path",string(history_path)}, {"data_seed",number(config.seed)},
    {"data_origin",string("ASGP BuildTask local task domain, test split; not official PSB")},
    {"sampling_note",string("Independent generator seed; natural duplicates and possible training overlap retained")},
    {"metric",string(asgp::IsListType(spec.output) ? "exact IntList accuracy; mismatch or execution/type error costs 1" : "exact Int accuracy and min(abs(predicted-expected),1000); execution/type error costs 1000")},
    {"fuel",number(expanded_audit_detail::fuel)}, {"case_cap",number(asgp::IsListType(spec.output) ? 1 : expanded_audit_detail::cap)},
    {"cases_sha256",string(hash)}, {"unique_inputs",number(unique.size())},
    {"cases",std::move(cases)}, {"final",std::move(final)}, {"history",array(std::move(history))}}));
}
} // namespace fixed_asgp
