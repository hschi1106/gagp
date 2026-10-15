#pragma once
#include "expanded_audit.hpp"

namespace fixed_asgp {
// This audit compares outputs and execution status, independently of fitness.
// Frozen parents are never replaced by witnesses or selected by these results.
inline void sampled_audit(const Json& source, const std::string& grammar_path,
                          const std::string& output) {
  auto spec=task(source.object_v.at("task").string_v);
  spec.train.clear();
  for(const auto& row:source.object_v.at("cases").array_v) {
    asgp::Example example;
    for(const auto& v:row.object_v.at("input").array_v)example.elements.push_back(std::stoll(v.string_v));
    if(row.object_v.count("scalars"))for(const auto& v:row.object_v.at("scalars").array_v)example.scalars.push_back(std::stoll(v.string_v));
    example.start.a=int(row.object_v.at("start").number_v);example.extent.a=int(row.object_v.at("extent").number_v);
    if(row.object_v.count("start_b"))example.start.b=int(row.object_v.at("start_b").number_v);
    if(row.object_v.count("extent_b"))example.extent.b=int(row.object_v.at("extent_b").number_v);
    // Expected values do not enter output parity. Score still needs a valid
    // expected shape for source metric scratch allocation.
    if(asgp::IsListType(spec.output))for(const auto& v:row.object_v.at("expected").array_v)example.expected.list.push_back(std::stoll(v.string_v));
    else if(spec.output==asgp::Type::kBool)example.expected.b=row.object_v.at("expected").number_v!=0;
    else example.expected.i=std::stoll(row.object_v.at("expected").string_v);
    spec.train.push_back(std::move(example));
  }
  if(spec.train.size()!=1024)throw std::runtime_error("sampled audit requires 1024 frozen cases");
  const auto definition=gg::load_definition(grammar_path);
  const auto grammar=gg::compile_grammar(definition);
  std::vector<std::string> names{"source"};
  for(std::size_t i=0;i<spec.scalars.size();++i)names.push_back("scalar"+std::to_string(i));
  if(spec.scheme==asgp::Scheme::kDynamicProgramming && spec.id!="house_robber")names.insert(names.end(),{"start_a","start_b","extent_a","extent_b"});
  asgp::FitnessConfig config;config.max_case_error=1000;config.limits.max_steps=2000000;config.limits.max_recursion=127;
  asgp::FitnessScratch scratch;
  std::vector<Json> rows;std::size_t mismatches=0;
  const auto& parents=source.object_v.at("population").array_v;
  const auto count=std::min<std::size_t>(8,parents.size());
  for(std::size_t p=0;p<count;++p) {
    const auto original=decode(parents[p]);
    const auto genome=translate(original,spec.id,definition,grammar);
    const auto code=gagp::evo::compile_for_eval(genome,names);
    gagp::CpuExecutionSession session(code);
    std::size_t value_mismatch=0,status_mismatch=0,source_errors=0,native_errors=0;
    for(std::size_t c=0;c<spec.train.size();++c) {
      const auto& example=spec.train[c];
      const bool source_error=!expanded_audit_detail::execute_source(original,spec,example,config.limits,scratch);
      source_errors+=source_error;
      try {
      gagp::payload::StagedPayloads payloads(8*1024*1024);
      gagp::payload::StagedPayloads::Scope scope(payloads);
      std::vector<gagp::Value> elements;for(auto v:example.elements)elements.push_back(gagp::Value::from_int(v));
      std::vector<std::pair<int,gagp::Value>> inputs{{0,gagp::payload::make_int_list_value(elements)}};
      for(auto v:example.scalars)inputs.emplace_back(inputs.size(),gagp::Value::from_int(v));
      if(spec.scheme==asgp::Scheme::kDynamicProgramming && spec.id!="house_robber")
        for(auto v:{example.start.a,example.start.b,example.extent.a,example.extent.b})inputs.emplace_back(inputs.size(),gagp::Value::from_int(v));
      const auto result=session.execute(inputs,2000000);
      native_errors+=result.is_error;
      if(source_error!=result.is_error){++status_mismatch;continue;}
      if(source_error)continue;
      bool equal=false;
      if(asgp::IsListType(spec.output) || spec.carrier==asgp::Type::kTuple) {
        std::vector<gagp::Value> actual;
        auto expected=spec.carrier==asgp::Type::kTuple ? std::vector<std::int64_t>{scratch.produced.b,scratch.produced.summary1,scratch.produced.summary2} : scratch.produced.list;
        equal=result.value.tag==gagp::ValueTag::IntList && gagp::payload::lookup_list(result.value,&actual) && actual.size()==expected.size();
        for(std::size_t i=0;equal && i<actual.size();++i)equal=actual[i].tag==gagp::ValueTag::Int && actual[i].i==expected[i];
      } else equal=result.value.tag==gagp::ValueTag::Int && result.value.i==scratch.produced.i;
      value_mismatch+=!equal;
      } catch(const gagp::payload::PayloadBudgetExceeded&) { ++native_errors;status_mismatch+=!source_error; }
    }
    mismatches+=value_mismatch+status_mismatch;
    rows.push_back(object({{"program",number(p)},{"cases",number(spec.train.size())},{"source_errors",number(source_errors)},
      {"native_errors",number(native_errors)},{"status_mismatches",number(status_mismatch)},{"value_mismatches",number(value_mismatch)}}));
  }
  write(output,object({{"task",string(spec.id)},{"sampled_programs",number(count)},{"cases_per_program",number(spec.train.size())},
    {"mismatches",number(mismatches)},{"programs",array(std::move(rows))}}));
  if(mismatches)throw std::runtime_error("sampled source/native output parity failed: "+std::to_string(mismatches));
}
}
