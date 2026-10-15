#include "gagp/runtime/cpu/fitness_cpu.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "gagp/core/value_semantics.hpp"
#include "gagp/runtime/payload/payload.hpp"
#include <stdexcept>
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/cpu/execution_session.hpp"
#include "../payload/staging.hpp"

namespace gagp {

namespace {

double score_resident(const Value& actual,const Value& expected,double penalty,FitnessPolicy policy, bool& exact) {
  exact=fitness_case_score(actual,expected,penalty,{FitnessMetric::Exact})==0;
  if(policy.metric!=FitnessMetric::Levenshtein && policy.metric!=FitnessMetric::TupleSummary)
    return fitness_case_score(actual,expected,penalty,policy);
  exact=false;
  std::vector<Value> a,b;
  if(actual.tag!=expected.tag)return -fitness_error_ceiling(policy,penalty);
  if(is_typed_list(actual)) {
    if(!payload::lookup_list(actual,&a) || !payload::lookup_list(expected,&b))
      return -fitness_error_ceiling(policy,penalty);
  } else if(actual.tag==ValueTag::String && policy.metric==FitnessMetric::Levenshtein) {
    std::string x,y;
    if(!payload::lookup_string(actual,&x) || !payload::lookup_string(expected,&y))
      return -fitness_error_ceiling(policy,penalty);
    for(unsigned char c:x)a.push_back(Value::from_char(c));
    for(unsigned char c:y)b.push_back(Value::from_char(c));
  } else return -fitness_error_ceiling(policy,penalty);
  exact=fitness_case_score(actual,expected,penalty,{FitnessMetric::Exact})==0;
  if(policy.metric==FitnessMetric::TupleSummary)
    exact=actual.tag==ValueTag::IntList && valid_fitness_tuple(a.data(),int(a.size()),policy) &&
        valid_fitness_tuple(b.data(),int(b.size()),policy) && a[0].i==b[0].i;
  std::vector<unsigned> row(b.size()+1);
  return fitness_sequence_score(a.data(),int(a.size()),b.data(),int(b.size()),penalty,policy,row.data());
}

double canonicalize_fitness_accumulator(double value) {
  if (!std::isfinite(value) || value == 0.0) {
    return value == 0.0 ? 0.0 : value;
  }
  int exponent = 0;
  const double mantissa = std::frexp(value, &exponent);
  constexpr int kMantissaBits = 48;
  const long long quantized_mantissa = std::llround(std::ldexp(mantissa, kMantissaBits));
  return std::ldexp(static_cast<double>(quantized_mantissa), exponent - kMantissaBits);
}

}  // namespace

std::vector<double> eval_fitness_cpu(
    const std::vector<BytecodeProgram>& programs,
    const std::vector<CaseBindings>& shared_cases,
    const std::vector<Value>& shared_answer,
    int fuel,
    double penalty,
    int reduction_lanes, FitnessPolicy policy, std::vector<unsigned>* wrong) {
  if(wrong)wrong->assign(programs.size(),0);
  if(!valid_fitness_policy(policy))throw std::invalid_argument("invalid fitness policy");
  if(policy.metric==FitnessMetric::Levenshtein || policy.metric==FitnessMetric::TupleSummary)
    for(const auto& answer:shared_answer)
      if(!is_container(answer) || Value::container_len(answer)>kFitnessMaxSequence)
        throw std::invalid_argument("sequence fitness requires a container answer of at most 1024 elements");
  if(policy.metric==FitnessMetric::TupleSummary) {
    for(const auto& answer:shared_answer) {
      std::vector<Value> tuple;
      if(answer.tag!=ValueTag::IntList || !payload::lookup_list(answer,&tuple) ||
          !valid_fitness_tuple(tuple.data(),int(tuple.size()),policy))
        throw std::invalid_argument("TupleSummary answer must contain exactly summary_count+1 Int fields");
    }
  }
  if (programs.empty() || shared_cases.empty()) {
    return {};
  }
  if (shared_answer.size() != shared_cases.size()) {
    return {};
  }

  // Case indices/values are shared across the population. Packing them per
  // program/case used to allocate the identical input vector P*C times.
  std::vector<std::vector<std::pair<int, Value>>> case_inputs(shared_cases.size());
  for (std::size_t c = 0; c < shared_cases.size(); ++c) {
    auto& inputs = case_inputs[c];
    inputs.reserve(shared_cases[c].size());
    for (const InputBinding& binding : shared_cases[c])
      inputs.emplace_back(binding.idx, binding.value);
  }
  std::vector<double> fitness(programs.size(), 0.0);
  const int lanes = std::max(1, reduction_lanes);
  for (std::size_t p = 0; p < programs.size(); ++p) {
    std::unique_ptr<CpuExecutionSession> session;
    if (!programs[p].bounded_region_segments.empty())
      session = std::make_unique<CpuExecutionSession>(programs[p]);
    payload::StagedPayloads temporary_payloads(8U * 1024U * 1024U);
    std::vector<double> partial_scores(static_cast<std::size_t>(lanes), 0.0);
    for (int lane = 0; lane < lanes; ++lane) {
      const std::size_t chunk_start =
          (shared_cases.size() * static_cast<std::size_t>(lane)) / static_cast<std::size_t>(lanes);
      const std::size_t chunk_end =
          (shared_cases.size() * static_cast<std::size_t>(lane + 1)) / static_cast<std::size_t>(lanes);
      double local_score = 0.0;
      for (std::size_t c = chunk_start; c < chunk_end; ++c) {
        const auto& inputs = case_inputs[c];
        // Evaluation temporaries live through scoring only; inputs/constants remain
        // in the committed registry. The budget also bounds exponential Concat.
        payload::StagedPayloads::Scope payload_scope(temporary_payloads);
        ExecResult out;
        try {
          out = session ? session->execute(inputs, fuel)
                        : execute_bytecode_cpu(programs[p], inputs, fuel);
        } catch (const payload::PayloadBudgetExceeded& error) {
          out.is_error = true;
          out.err = Err{ErrCode::Value, error.what()};
        }
        if (out.is_error) {
          if(wrong)++wrong->at(p);
          local_score = canonicalize_fitness_accumulator(local_score - fitness_error_ceiling(policy, penalty));
          continue;
        }

        bool exact=false;
        local_score = canonicalize_fitness_accumulator(local_score +
            score_resident(out.value, shared_answer[c], penalty, policy, exact));
        if(wrong && !exact)++wrong->at(p);
      }
      partial_scores[static_cast<std::size_t>(lane)] = local_score;
    }

    double score = 0.0;
    if(policy.metric==FitnessMetric::Legacy) {
      for (double partial_score : partial_scores)
        score = canonicalize_fitness_accumulator(score + partial_score);
    } else {
      // Match CUDA's ordered warp-then-block reduction, including each 48-bit
      // rounding point. Fractional task errors can sit on a ranking boundary.
      for(std::size_t base=0;base<partial_scores.size();base+=32) {
        double warp_score=0;
        for(std::size_t i=base;i<std::min(base+32,partial_scores.size());++i)
          warp_score=canonicalize_fitness_accumulator(warp_score+partial_scores[i]);
        score=canonicalize_fitness_accumulator(score+warp_score);
      }
    }
    fitness[p] = score;
  }

  return fitness;
}

}  // namespace gagp
