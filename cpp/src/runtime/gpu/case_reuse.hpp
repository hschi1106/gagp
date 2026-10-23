#pragma once

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gagp/core/fitness_policy.hpp"

namespace gagp::gpu_detail {

struct IntegerCaseReusePlan {
  std::vector<int> representatives;
  std::vector<unsigned> weights;
};

// Only the typed Int/Bool/view executor may consume this plan. It proves
// integral bounded scores separately; fractional policies retain ordinary
// execution. Keys contain complete effective bindings AND the expected value.
inline IntegerCaseReusePlan integer_case_reuse_plan(const std::vector<Value>& values,
    const std::vector<unsigned char>& assigned,const std::vector<Value>& answers,
    FitnessPolicy policy,double penalty) {
  IntegerCaseReusePlan out;
  const auto count=answers.size();
  if(count<128 || values.size()!=count*64 || assigned.size()!=values.size())return out;
  if(policy.metric!=FitnessMetric::Exact &&
      !(policy.metric==FitnessMetric::Absolute && policy.proximity==FitnessProximity::Capped))return out;
  const double ceiling=fitness_error_ceiling(policy,penalty);
  if(!std::isfinite(ceiling) || std::floor(ceiling)!=ceiling ||
      count*std::max(1.0,ceiling)>=1099511627776.0)return out;
  std::unordered_map<std::string,int> indices;
  const auto append=[](std::string& key,const Value& value) {
    key.push_back(static_cast<char>(value.tag));
    key.push_back(value.b?1:0);
    key.append(reinterpret_cast<const char*>(&value.i),sizeof(value.i));
  };
  for(std::size_t row=0;row<count;++row) {
    const auto& answer=answers[row];
    if(answer.tag!=ValueTag::Int && answer.tag!=ValueTag::Bool)return {};
    std::string key;
    key.append(reinterpret_cast<const char*>(assigned.data()+row*64),64);
    for(std::size_t local=0;local<64;++local)if(assigned[row*64+local]) {
      const auto& value=values[row*64+local];
      if(value.tag!=ValueTag::Int && value.tag!=ValueTag::Bool && value.tag!=ValueTag::IntList)return {};
      append(key,value);
    }
    append(key,answer);
    auto found=indices.find(key);
    if(found==indices.end()) {
      const auto index=static_cast<int>(out.representatives.size());
      indices.emplace(std::move(key),index);
      out.representatives.push_back(static_cast<int>(row));out.weights.push_back(1);
    } else ++out.weights[found->second];
  }
  // One global admission rule, independent of task names or held-out scores.
  if(out.representatives.size()>count/4)return {};
  return out;
}

struct ShapeCasePlan {
  std::vector<int> representatives;
  std::vector<int> groups;
};

// Labels deliberately do not enter this partition: each original case is
// scored against its own answer in its original canonical reduction lane.
inline ShapeCasePlan shape_case_plan(const std::vector<Value>& values,
    const std::vector<unsigned char>& assigned,std::uint64_t inputs=~std::uint64_t{0}) {
  ShapeCasePlan out;
  if(values.size()!=assigned.size() || values.size()%64 || values.size()/64<128)return out;
  const auto count=values.size()/64;
  std::unordered_map<std::string,int> indices;
  for(std::size_t row=0;row<count;++row) {
    std::string key;
    for(std::size_t local=0;local<64;++local)if(inputs & (std::uint64_t{1}<<local)) {
      key.push_back(assigned[row*64+local]?1:0);
      if(!assigned[row*64+local])continue;
      const auto& value=values[row*64+local];
      if(value.tag!=ValueTag::Int && value.tag!=ValueTag::Bool && value.tag!=ValueTag::IntList)return {};
      if(value.tag==ValueTag::IntList && value.b)return {};
      key.push_back(static_cast<char>(value.tag));
      const auto bits=value.tag==ValueTag::IntList?static_cast<std::int64_t>(Value::container_len(value)):value.i;
      if(value.tag!=ValueTag::IntList)key.push_back(value.b?1:0);
      key.append(reinterpret_cast<const char*>(&bits),sizeof(bits));
    }
    const auto it=indices.find(key);
    if(it==indices.end()) {
      const auto index=static_cast<int>(out.representatives.size());
      indices.emplace(std::move(key),index);out.representatives.push_back(static_cast<int>(row));
      out.groups.push_back(index);
      if(out.representatives.size()>std::min<std::size_t>(256,count/4))return {};
    } else out.groups.push_back(it->second);
  }
  return out;
}

} // namespace gagp::gpu_detail
