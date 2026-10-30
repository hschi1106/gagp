#pragma once
#include "device/phase_generate.cuh"
#include "phase_grammar.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace gagp::evo::repro {
// Host construction of the exact production-choice tables consumed by warp
// generation. The serial device draw is
//   choice = (draw * 2^-53) * total; for eligible i: choice -= term_i; stop if < 0
// with IEEE round-to-nearest FP64 (no fast-math; no expression here contracts to
// an FMA). Host doubles reproduce that sequence, and the selected index is
// monotone in the 53-bit draw, so binary search finds every switching draw.
struct PhaseChoiceTables {
 std::vector<PhaseChoiceHeader> headers;std::vector<unsigned> values;std::vector<PhaseChoiceSet> sets;
 std::vector<std::uint64_t> thresholds;std::vector<int> expressions;
};
// Index of the production chosen by the serial loop for one eligible set.
inline int serial_phase_choice(const std::vector<double>& terms,double total,std::uint64_t draw) {
 volatile double choice=static_cast<double>(draw)*0x1.0p-53;choice=choice*total;
 int chosen=0;
 for(std::size_t k=0;k<terms.size();++k){chosen=int(k);choice=choice-terms[k];if(choice<0)break;}
 return chosen;
}
// `nonterminals`/`productions` are the uploaded (possibly statically normalized)
// tables. Nonterminals with more than 32 productions or invalid expressions keep
// value_count=-1 and use the serial loop.
inline PhaseChoiceTables build_phase_choice_tables(const HostPhaseGrammar& h,
    const std::vector<PhaseNonterminal>& nonterminals,const std::vector<PhaseProduction>& productions) {
 PhaseChoiceTables t;const int depths=h.depth_count,logicals=h.logical_depth_count;
 t.headers.resize(nonterminals.size()*std::size_t(depths)*logicals);
 const auto minimum=[&](int e,int d,int l)->unsigned{
  return e<0 || e>=int(h.expressions.size()) ? 0xffffffffU : h.minimum.at((std::size_t(e)*depths+d)*logicals+l);};
 for(std::size_t n=0;n<nonterminals.size();++n) {
  const auto& nt=nonterminals[n];
  bool usable=nt.production_count>0 && nt.production_count<=32 && nt.production_offset>=0 &&
      nt.production_offset+nt.production_count<=int(productions.size());
  for(int i=0;usable && i<nt.production_count;++i){const int e=productions[nt.production_offset+i].expression;usable=e>=0 && e<int(h.expressions.size());}
  for(int d=0;d<depths;++d)for(int l=0;l<logicals;++l) {
   // No eligible production: normalized draws fall through to an invalid
   // result, unnormalized ones report capacity before drawing.
   auto& header=t.headers[(n*depths+d)*logicals+l];header.empty_status=nt.normalized_weights?0:1;
   if(!usable){header.value_count=-1;continue;}
   std::vector<unsigned> cost(nt.production_count);
   for(int i=0;i<nt.production_count;++i)cost[i]=minimum(productions[nt.production_offset+i].expression,d,l);
   std::vector<unsigned> distinct=cost;std::sort(distinct.begin(),distinct.end());
   distinct.erase(std::unique(distinct.begin(),distinct.end()),distinct.end());
   header.value_offset=int(t.values.size());header.value_count=int(distinct.size());
   for(const unsigned available:distinct) {
    t.values.push_back(available);PhaseChoiceSet set;
    std::vector<int> eligible;for(int i=0;i<nt.production_count;++i)if(cost[i]<=available)eligible.push_back(i);
    double maximum=nt.normalized_weights?1:0;
    if(!nt.normalized_weights)for(int i:eligible){const double w=productions[nt.production_offset+i].weight;if(w>maximum)maximum=w;}
    if(!(maximum>0)){set.status=1;t.sets.push_back(set);continue;}
    std::vector<double> terms;double total=0;
    for(int i:eligible){const double w=productions[nt.production_offset+i].weight;terms.push_back(nt.normalized_weights?w:w/maximum);total+=terms.back();}
    set.count=int(eligible.size());set.threshold_offset=int(t.thresholds.size());set.choice_offset=int(t.expressions.size());
    for(int k=0;k+1<set.count;++k) {
     std::uint64_t lo=0,hi=UINT64_C(1)<<53; // smallest draw selecting an index above k
     while(lo<hi){const auto mid=lo+(hi-lo)/2;if(serial_phase_choice(terms,total,mid)>k)hi=mid;else lo=mid+1;}
     t.thresholds.push_back(lo);
    }
    for(int i:eligible)t.expressions.push_back(productions[nt.production_offset+i].expression);
    t.sets.push_back(set);
   }
  }
 }
 return t;
}
} // namespace gagp::evo::repro
