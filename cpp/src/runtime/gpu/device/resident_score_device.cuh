#pragma once

#include "gagp/core/fitness_policy.hpp"
#include "builtins_device.cuh"

namespace gagp::gpu_detail {

// Backing dispatch occurs once per score, outside the inner distance loop.
// Admission proves all elements are Int, so comparisons retain all 64 bits.
template<bool CompactA,bool CompactB>
__device__ __forceinline__ double d_score_private_intlist_spans(const DPrivateIntListSpan& a,
    const DPrivateIntListSpan& b,double penalty,FitnessPolicy policy,bool* exact) {
  const int n=a.size,m=b.size;
  if(policy.metric==FitnessMetric::TupleSummary) {
    if(exact)*exact=false;
    if(n!=policy.summary_count+1 || m!=n || n<1 || n>3)
      return -fitness_error_ceiling(policy,penalty);
    const bool correct=a.read<CompactA>(0)==b.read<CompactB>(0);
    if(exact)*exact=correct; // Auxiliary summaries shape fitness, not correctness.
    double error=correct?0.0:1.0;
    for(int i=1;i<n;++i)
      error+=policy.summary_weight*fmin(fitness_integer_gap(a.read<CompactA>(i),b.read<CompactB>(i)),
          policy.summary_cap)/policy.summary_cap;
    return -error;
  }
  bool same=n==m;
  for(int i=0;same && i<n;++i)same=a.read<CompactA>(i)==b.read<CompactB>(i);
  if(exact)*exact=same;
  if(policy.metric==FitnessMetric::Exact)return same?0:-fitness_error_ceiling(policy,penalty);
  if(m>kFitnessMaxSequence)return -fitness_error_ceiling(policy,penalty);
  if(fitness_sequence_length_saturates(n,m,penalty,policy))return -fabs(penalty);
  unsigned row[kFitnessMaxSequence+1];
  for(int j=0;j<=m;++j)row[j]=j;
  for(int i=1;i<=n;++i) {
    const auto value=a.read<CompactA>(i-1);
    unsigned diagonal=row[0];row[0]=i;
    for(int j=1;j<=m;++j) {
      const unsigned old=row[j],best=row[j]<row[j-1]?row[j]:row[j-1];
      row[j]=value==b.read<CompactB>(j-1)?diagonal:1+(diagonal<best?diagonal:best);
      diagonal=old;
    }
  }
  const double error=row[m];
  return policy.proximity==FitnessProximity::Discounted?-error/(1+error):-fmin(error,fabs(penalty));
}

template<class State>
__device__ __noinline__ double d_score_resident(const Value& actual,const Value& expected,
    double penalty,FitnessPolicy policy,const DPayloadTables& tables,const State& state,bool* exact) {
  if(state.private_intlists && (actual.tag==ValueTag::IntList || expected.tag==ValueTag::IntList)) {
    if(exact)*exact=false;
    if(actual.tag!=ValueTag::IntList || expected.tag!=ValueTag::IntList)
      return -fitness_error_ceiling(policy,penalty);
    DPrivateIntListSpan a,b;
    if(!d_private_intlist_span(tables,state,actual,a) || !d_private_intlist_span(tables,state,expected,b))
      return -fitness_error_ceiling(policy,penalty);
    if(a.integers) {
      if(b.integers)return d_score_private_intlist_spans<true,true>(a,b,penalty,policy,exact);
      return d_score_private_intlist_spans<true,false>(a,b,penalty,policy,exact);
    }
    if(b.integers)return d_score_private_intlist_spans<false,true>(a,b,penalty,policy,exact);
    return d_score_private_intlist_spans<false,false>(a,b,penalty,policy,exact);
  }
  if(exact)*exact=fitness_case_score(actual,expected,penalty,{FitnessMetric::Exact})==0;
  if(policy.metric!=FitnessMetric::Levenshtein && policy.metric!=FitnessMetric::TupleSummary)
    return fitness_case_score(actual,expected,penalty,policy);
  if(exact)*exact=false;
  if(actual.tag!=expected.tag)return -fitness_error_ceiling(policy,penalty);
  unsigned row[kFitnessMaxSequence+1];
  if(is_typed_list(actual)) {
    const Value *a=nullptr,*b=nullptr;int n=0,m=0;
    if(!d_lookup_list_payload(tables,state,actual,a,n) || !d_lookup_list_payload(tables,state,expected,b,m))
      return -fitness_error_ceiling(policy,penalty);
    if(exact)*exact=actual.i==expected.i;
    if(exact && policy.metric==FitnessMetric::TupleSummary)
      *exact=actual.tag==ValueTag::IntList && valid_fitness_tuple(a,n,policy) &&
          valid_fitness_tuple(b,m,policy) && a[0].i==b[0].i;
    return fitness_sequence_score(a,n,b,m,penalty,policy,row);
  }
  if(actual.tag==ValueTag::String && policy.metric==FitnessMetric::Levenshtein) {
    const char *a=nullptr,*b=nullptr;int n=0,m=0;
    if(!d_lookup_string_payload(tables,state,actual,a,n) || !d_lookup_string_payload(tables,state,expected,b,m) || m>kFitnessMaxSequence)
      return -fitness_error_ceiling(policy,penalty);
    if(fitness_sequence_length_saturates(n,m,penalty,policy)) return -fabs(penalty);
    for(int j=0;j<=m;++j)row[j]=j;
    for(int i=1;i<=n;++i) {
      unsigned diagonal=row[0];row[0]=i;
      for(int j=1;j<=m;++j) {
        const unsigned old=row[j],best=row[j]<row[j-1]?row[j]:row[j-1];
        row[j]=a[i-1]==b[j-1]?diagonal:1+(diagonal<best?diagonal:best);diagonal=old;
      }
    }
    if(exact)*exact=row[m]==0;
    const double error=row[m];
    return policy.proximity==FitnessProximity::Discounted?-error/(1+error):-fmin(error,fabs(penalty));
  }
  return -fitness_error_ceiling(policy,penalty);
}

}  // namespace gagp::gpu_detail
