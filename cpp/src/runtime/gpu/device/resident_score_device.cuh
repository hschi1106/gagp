#pragma once

#include "gagp/core/fitness_policy.hpp"
#include "builtins_device.cuh"

namespace gagp::gpu_detail {

template<class State>
__device__ __noinline__ double d_score_resident(const Value& actual,const Value& expected,
    double penalty,FitnessPolicy policy,const DPayloadTables& tables,const State& state,bool* exact) {
  if(state.private_intlists && (actual.tag==ValueTag::IntList || expected.tag==ValueTag::IntList)) {
    if(exact)*exact=false;
    if(actual.tag!=ValueTag::IntList || expected.tag!=ValueTag::IntList)
      return -fitness_error_ceiling(policy,penalty);
    const Value *a=nullptr,*b=nullptr;int n=0,m=0;
    if(!d_lookup_list_payload(tables,state,actual,a,n) || !d_lookup_list_payload(tables,state,expected,b,m))
      return -fitness_error_ceiling(policy,penalty);
    bool same=n==m;
    for(int i=0;same && i<n;++i)same=a[i].tag==ValueTag::Int && b[i].tag==ValueTag::Int && a[i].i==b[i].i;
    if(exact)*exact=same;
    if(policy.metric==FitnessMetric::Exact)return same?0:-fitness_error_ceiling(policy,penalty);
    unsigned row[kFitnessMaxSequence+1];
    return fitness_sequence_score(a,n,b,m,penalty,policy,row);
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
