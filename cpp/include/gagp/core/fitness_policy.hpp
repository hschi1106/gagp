#pragma once
#include "gagp/core/value_semantics.hpp"
namespace gagp {
enum class FitnessMetric { Legacy, Absolute, Exact, Levenshtein, TupleSummary };
enum class FitnessProximity { Capped, Discounted };
struct FitnessPolicy {
  FitnessMetric metric = FitnessMetric::Legacy;
  FitnessProximity proximity = FitnessProximity::Capped;
  int summary_count = 0;
  double summary_weight = 0.1;
  double summary_cap = 100.0;
};
inline bool valid_fitness_policy(FitnessPolicy p) {
  return (p.metric==FitnessMetric::Legacy || p.metric==FitnessMetric::Absolute ||
      p.metric==FitnessMetric::Exact || p.metric==FitnessMetric::Levenshtein || p.metric==FitnessMetric::TupleSummary) &&
      (p.proximity==FitnessProximity::Capped || p.proximity==FitnessProximity::Discounted) &&
      p.summary_count>=0 && p.summary_count<=2 && std::isfinite(p.summary_weight) && p.summary_weight>=0 &&
      std::isfinite(p.summary_cap) && p.summary_cap>0;
}
#ifdef __CUDACC__
#define GAGP_FIT_HD __host__ __device__
#else
#define GAGP_FIT_HD
#endif
GAGP_FIT_HD inline double fitness_error_ceiling(FitnessPolicy policy, double penalty) {
  if (policy.metric == FitnessMetric::TupleSummary) return 1.0 + policy.summary_count * policy.summary_weight;
  return policy.metric == FitnessMetric::Exact ||
      (policy.metric != FitnessMetric::Legacy && policy.proximity == FitnessProximity::Discounted)
      ? 1.0 : fabs(penalty);
}
GAGP_FIT_HD inline double fitness_integer_gap(std::int64_t a,std::int64_t b) {
  return double(a>=b ? std::uint64_t(a)-std::uint64_t(b) : std::uint64_t(b)-std::uint64_t(a));
}
GAGP_FIT_HD inline double fitness_case_score(const Value& actual, const Value& expected,
                                            double penalty, FitnessPolicy policy) {
  double score = 0;
  if (policy.metric == FitnessMetric::Legacy) {
    vm_semantics::fitness_score_for_values(actual, expected, penalty, score);
    return score;
  }
  double a=0,b=0; bool floating=false;
  const bool numeric = vm_semantics::to_numeric_pair_for_fitness(actual,expected,a,b,floating);
  if (policy.metric == FitnessMetric::Exact) {
    if(actual.tag==ValueTag::Int && expected.tag==ValueTag::Int) return actual.i==expected.i?0.0:-1.0;
    if (numeric) return std::isfinite(a) && std::isfinite(b) && a == b ? 0.0 : -1.0;
    if (actual.tag != expected.tag || actual.tag == ValueTag::Invalid ||
        actual.tag == ValueTag::FallbackToken) return -1.0;
    return (actual.tag == ValueTag::Bool ? actual.b == expected.b : actual.i == expected.i) ? 0.0 : -1.0;
  }
  if (!numeric || !std::isfinite(a) || !std::isfinite(b))
    return -fitness_error_ceiling(policy,penalty);
  const double error = actual.tag==ValueTag::Int && expected.tag==ValueTag::Int
      ? fitness_integer_gap(actual.i,expected.i) : fabs(a-b);
  if (!std::isfinite(error)) return -fitness_error_ceiling(policy,penalty);
  return policy.proximity == FitnessProximity::Discounted ? -error/(1.0+error) : -fmin(error,fabs(penalty));
}
// Callers resolve payloads while their invocation owns them. The expected side
// bounds the row; no allocation or payload-token approximation occurs on device.
constexpr int kFitnessMaxSequence = 1024;
GAGP_FIT_HD inline bool valid_fitness_tuple(const Value* values,int count,FitnessPolicy policy) {
  if(!values || count!=policy.summary_count+1 || count<1 || count>3)return false;
  for(int i=0;i<count;++i)if(values[i].tag!=ValueTag::Int)return false;
  return true;
}
GAGP_FIT_HD inline double fitness_sequence_score(const Value* actual, int n,
    const Value* expected, int m, double penalty, FitnessPolicy policy, unsigned* row) {
  if (n < 0 || m < 0 || m > kFitnessMaxSequence)
    return -fitness_error_ceiling(policy,penalty);
  if (policy.metric == FitnessMetric::TupleSummary) {
    if (!valid_fitness_tuple(actual,n,policy) || !valid_fitness_tuple(expected,m,policy))
      return -fitness_error_ceiling(policy,penalty);
    double error = actual[0].i == expected[0].i ? 0.0 : 1.0;
    for (int i=1;i<n;++i) {
      if(actual[i].tag!=ValueTag::Int || expected[i].tag!=ValueTag::Int)
        return -fitness_error_ceiling(policy,penalty);
      error += policy.summary_weight * fmin(fitness_integer_gap(actual[i].i,expected[i].i),policy.summary_cap)/policy.summary_cap;
    }
    return -error;
  }
  for(int j=0;j<=m;++j)row[j]=j;
  const FitnessPolicy exact{FitnessMetric::Exact};
  for(int i=1;i<=n;++i) {
    unsigned diagonal=row[0];row[0]=i;
    for(int j=1;j<=m;++j) {
      const unsigned old=row[j];
      const unsigned best=row[j]<row[j-1]?row[j]:row[j-1];
      row[j]=fitness_case_score(actual[i-1],expected[j-1],penalty,exact)==0 ? diagonal
          : 1+(diagonal<best?diagonal:best);
      diagonal=old;
    }
  }
  const double error=row[m];
  return policy.proximity==FitnessProximity::Discounted ? -error/(1+error) : -fmin(error,fabs(penalty));
}
#undef GAGP_FIT_HD
} // namespace gagp
