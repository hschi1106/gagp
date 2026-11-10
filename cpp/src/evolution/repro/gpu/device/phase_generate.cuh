#pragma once
#include "phase_compile.cuh"
#include "grammar_random.cuh"
#include <type_traits>

namespace gagp::evo::repro {
// Internal construction metadata, never an external grammar certificate.
// The current profile requires identity reference scope maps; it admits let
// binders and bounded regions (whole-program phases only) but no aliases,
// templates or coupled holes. Each position denotes a
// distinct logical site, even when two subtrees have identical contents.
struct PhaseGeneSite {
  int nonterminal = -1;
  int entry_expression = -1;
  int parent_scope = 0;
  int scope = 0;
};
// Site scope flag of a nonterminal with variation disabled: the invocation
// still counts toward site depth, but never matches a variation site.
constexpr int kPhaseFixedSite = 1 << 30;
// Private uploaded grammar tables. The host compiler admits every reachable
// production; an unsupported alternative declines the whole requested profile.
enum class PhaseExpressionKind : int { Operation, Constant, Bound, Reference, Input };
struct PhaseExpression {
  PhaseExpressionKind kind = PhaseExpressionKind::Operation;
  PhaseTreeNode node;
  int native_kind = -1;
  int depth_charge = 1;
  int target = -1;
  int children[kPhaseMaxArity]{-1,-1,-1,-1,-1,-1,-1,-1};
  int scope_count = 0;
  int scope[8]{};
  // Let: the child at this argument also sees one new binder, appended at the
  // next lexical index (identity scope maps keep index == position).
  int bind_argument = -1;
  // Region phase children see only their own n bindings (identity scope of
  // size n); -1 keeps the parent's environment.
  std::int8_t child_scope[kPhaseMaxArity]{-1,-1,-1,-1,-1,-1,-1,-1};
};
// Environment of child `i`: a phase starts afresh, a let body gains a binder.
__host__ __device__ inline int phase_child_scope(const PhaseExpression& expression, int i, int scope) {
  return expression.child_scope[i]>=0 ? expression.child_scope[i] : scope+(i==expression.bind_argument ? 1 : 0);
}
struct PhaseProduction { int expression; double weight; };
struct PhaseNonterminal { int production_offset=0, production_count=0, scope_count=0; bool normalized_weights=false, fixed=false; };
struct PhaseDomain { int value_offset=0, value_count=0; bool integer_range=false; std::int64_t minimum=0, maximum=0; };
// Optional exact production-choice tables (warp generation only). For one
// nonterminal, depth and logical budget, the eligible production set changes
// only at the sorted minimum costs. Within one set the sequential FP64 weighted
// draw is monotone in the 53-bit random value, so host-computed integer
// thresholds select exactly the production the serial loop would select.
struct PhaseChoiceHeader { int value_offset=0, value_count=-1, empty_status=0; };
struct PhaseChoiceSet { int status=0, count=0, threshold_offset=0, choice_offset=0; };
struct PhaseGrammarView {
  const PhaseExpression* expressions=nullptr; int expression_count=0;
  const PhaseProduction* productions=nullptr; int production_count=0;
  const PhaseNonterminal* nonterminals=nullptr; int nonterminal_count=0;
  const PhaseDomain* domains=nullptr; int domain_count=0;
  const Value* values=nullptr; int value_count=0;
  const unsigned* minimum=nullptr; int depth_count=0; int logical_depth_count=1; bool site_depth=false;
  const PhaseRegionInfo* regions=nullptr; int region_count=0;
  const PhaseChoiceHeader* choice_headers=nullptr; const unsigned* choice_values=nullptr;
  const PhaseChoiceSet* choice_sets=nullptr; const std::uint64_t* choice_thresholds=nullptr; const int* choice_expressions=nullptr;
};
struct PhaseGenerationResult { PhaseCompileStatus status=PhaseCompileStatus::Invalid; int nodes=0,constants=0,steps=0; };

// This constructs grammar expressions, not merely type-compatible trees. Scope
// maps and constant domains come from the admitted immutable grammar upload.
// Each lane owns its bounded output; no CPU reproduction or device allocation.
// Warp=true requires one converged warp executing identical arguments. It only
// replaces the weighted production loop with the exact choice tables above.
template<bool CompactTasks=true, bool EmitSites=false, bool Warp=false>
__host__ __device__ inline PhaseGenerationResult generate_phase(
    const PhaseGrammarView& grammar, int root_expression, int scope_count,
    int depth, int capacity, std::uint64_t seed,
    PhaseTreeNode* nodes, int* origins, Value* constants, PhaseGeneSite* sites=nullptr, int logical_depth=0) {
  PhaseGenerationResult result;
  if constexpr(EmitSites) if(!sites)return result;
  PhaseGeneSite next_site; // A reference is immediately followed by its selected production.

  if (!grammar.expressions || !grammar.minimum || !nodes || !origins || !constants ||
      root_expression<0 || root_expression>=grammar.expression_count ||
      scope_count<0 || scope_count>8 || depth<=0 || depth>=grammar.depth_count ||
      depth>64 || logical_depth<0 || logical_depth>=grammar.logical_depth_count || capacity<=0 || capacity>1024 || grammar.expression_count>131071) return result;
  const auto minimum=[&](int expression,int d,int logical)->unsigned {
    return expression<0 || expression>=grammar.expression_count || d<0 || d>=grammar.depth_count || logical<0 || logical>=grammar.logical_depth_count
        ? 0xffffffffU : grammar.minimum[(static_cast<std::size_t>(expression)*grammar.depth_count+d)*grammar.logical_depth_count+logical];
  };
  // Host grammar admission caps source expressions at65536, plus one entry
  // expression. Depth<=64 and scope<=8; eight lexical indices occupy24bits.
  // Exact field packing halves dynamically indexed local pending storage.
  constexpr unsigned unselected=131071;
  struct CompactTask {
    unsigned expression:17;
    unsigned depth:7;
    unsigned scope_count:4;
    unsigned logical:4;
    unsigned scope:24;
    CompactTask()=default;
    __host__ __device__ CompactTask(unsigned e,unsigned d,unsigned n,unsigned s,unsigned l=0):expression(e),depth(d),scope_count(n),logical(l),scope(s){}
  };
  struct ReferenceTask {
    int expression,depth; unsigned scope_count:28,logical:4; unsigned scope;
    ReferenceTask()=default;
    __host__ __device__ ReferenceTask(unsigned e,unsigned d,unsigned n,unsigned s,unsigned l=0):expression(e),depth(d),scope_count(n),logical(l),scope(s){}
  };
  static_assert(sizeof(CompactTask)==8 && sizeof(ReferenceTask)==16,"generation task layouts");
  using Task=std::conditional_t<CompactTasks,CompactTask,ReferenceTask>;
  Task pending[192]; int count=1;
  pending[0]={static_cast<unsigned>(root_expression),static_cast<unsigned>(depth),static_cast<unsigned>(scope_count),0,static_cast<unsigned>(logical_depth)};
  for(int i=0;i<scope_count;++i)pending[0].scope |= static_cast<unsigned>(i) << (3*i);
  unsigned reserved=minimum(root_expression,depth,logical_depth);
  if(reserved>static_cast<unsigned>(capacity)) {result.status=PhaseCompileStatus::Capacity;return result;}
  DGrammarRandom random(seed);
  while(count) {
    if(++result.steps>8192) {result.status=PhaseCompileStatus::Capacity;return result;}
    const auto task=pending[--count];
    const auto required=minimum(task.expression,task.depth,task.logical);
    if(required>reserved) return result;
    reserved-=required;
    const auto expression=grammar.expressions[task.expression];
    if(expression.kind==PhaseExpressionKind::Reference) {
      if(expression.target<0 || expression.target>=grammar.nonterminal_count || !grammar.nonterminals || !grammar.productions ||
          expression.scope_count<0 || expression.scope_count>8)return result;
      if constexpr(EmitSites) {
        if(next_site.nonterminal>=0 || expression.scope_count>task.scope_count)return result;
        for(int i=0;i<expression.scope_count;++i)if(expression.scope[i]!=i)return result;
        next_site={expression.target,static_cast<int>(task.expression),static_cast<int>(task.scope_count),
            expression.scope_count|(grammar.nonterminals[expression.target].fixed?kPhaseFixedSite:0)};
      }
      const auto nt=grammar.nonterminals[expression.target];
      if(nt.scope_count!=expression.scope_count || nt.production_offset<0 || nt.production_count<=0 ||
          nt.production_offset>grammar.production_count || nt.production_count>grammar.production_count-nt.production_offset)return result;
      if(grammar.site_depth && task.logical==0) {result.status=PhaseCompileStatus::Capacity;return result;}
      Task next{unselected,static_cast<unsigned>(task.depth),static_cast<unsigned>(expression.scope_count),0,
          static_cast<unsigned>(task.logical-(grammar.site_depth?1:0))};
      for(int i=0;i<next.scope_count;++i) {
        if(expression.scope[i]<0 || expression.scope[i]>=task.scope_count)return result;
        next.scope |= ((task.scope >> (3*expression.scope[i])) & 7U) << (3*i);
      }
      if(reserved>static_cast<unsigned>(capacity-result.nodes)) {result.status=PhaseCompileStatus::Capacity;return result;}
      const unsigned available=static_cast<unsigned>(capacity-result.nodes)-reserved;
#ifdef __CUDA_ARCH__
      if constexpr(Warp) if(grammar.choice_headers) {
        const unsigned full=0xffffffffU;const int lane=threadIdx.x&31;
        const auto header=grammar.choice_headers[(static_cast<std::size_t>(expression.target)*grammar.depth_count+task.depth)*grammar.logical_depth_count+next.logical];
        if(header.value_count>=0) {
          const bool fits=lane<header.value_count && grammar.choice_values[header.value_offset+lane]<=available;
          const int set_index=__popc(__ballot_sync(full,fits));
          if(!set_index){if(header.empty_status)result.status=PhaseCompileStatus::Capacity;return result;}
          const auto set=grammar.choice_sets[header.value_offset+set_index-1];
          if(set.status==1){result.status=PhaseCompileStatus::Capacity;return result;}
          if(set.status)return result;
          const std::uint64_t draw=random.next()>>11;
          const bool passed=lane<set.count-1 && grammar.choice_thresholds[set.threshold_offset+lane]<=draw;
          next.expression=grammar.choice_expressions[set.choice_offset+__popc(__ballot_sync(full,passed))];
          if(count>=192)return result;
          reserved+=minimum(next.expression,next.depth,next.logical);pending[count++]=next;continue;
        }
      }
#endif
      double maximum=nt.normalized_weights?1:0,total=0;
      if(!nt.normalized_weights)for(int i=0;i<nt.production_count;++i) {
        const auto p=grammar.productions[nt.production_offset+i];
        if(minimum(p.expression,task.depth,next.logical)<=available && p.weight>maximum) maximum=p.weight;
      }
      if(!(maximum>0)) {result.status=PhaseCompileStatus::Capacity;return result;}
      for(int i=0;i<nt.production_count;++i) {
        const auto p=grammar.productions[nt.production_offset+i];
        if(minimum(p.expression,task.depth,next.logical)<=available)total+=nt.normalized_weights?p.weight:p.weight/maximum;
      }
      double choice=static_cast<double>(random.next()>>11)*0x1.0p-53*total;
      for(int i=0;i<nt.production_count;++i) {
        const auto p=grammar.productions[nt.production_offset+i];
        if(minimum(p.expression,task.depth,next.logical)>available)continue;
        if(p.expression<0 || p.expression>=grammar.expression_count)return result;
        next.expression=p.expression; choice-=nt.normalized_weights?p.weight:p.weight/maximum; if(choice<0)break;
      }
      if(next.expression==unselected || count>=192)return result;
      reserved+=minimum(next.expression,next.depth,next.logical);pending[count++]=next;continue;
    }
    const int charge=grammar.logical_depth_count>1 && !grammar.site_depth?expression.depth_charge:0;
    if(task.depth<=0 || task.logical<charge || result.nodes>=capacity) {result.status=PhaseCompileStatus::Capacity;return result;}
    auto node=expression.node;
    if(node.arity<0 || node.arity>kPhaseMaxArity)return result;
    if(expression.kind==PhaseExpressionKind::Bound) {
      if(expression.target<0 || expression.target>=task.scope_count || node.arity!=0)return result;
      node.operand=(task.scope >> (3*expression.target)) & 7U;
    } else if(expression.kind==PhaseExpressionKind::Constant) {
      if(expression.target<0 || expression.target>=grammar.domain_count || !grammar.domains || node.arity!=0)return result;
      const auto domain=grammar.domains[expression.target]; Value value;
      if(domain.integer_range) {
        if(domain.minimum>domain.maximum)return result;
        value=Value::from_int(random.integer(domain.minimum,domain.maximum));
      } else {
        if(!grammar.values || domain.value_offset<0 || domain.value_count<=0 || domain.value_offset>grammar.value_count ||
            domain.value_count>grammar.value_count-domain.value_offset)return result;
        value=grammar.values[domain.value_offset+random.bounded(domain.value_count)];
      }
      if(value.tag==ValueTag::Invalid || value.tag==ValueTag::FallbackToken)return result;
      node.operand=result.constants;constants[result.constants++]=value;
    } else if(expression.kind!=PhaseExpressionKind::Operation && expression.kind!=PhaseExpressionKind::Input)return result;
    unsigned binder=0;
    if(expression.bind_argument>=0) {
      for(unsigned i=0;i<task.scope_count;++i) {const unsigned used=((task.scope>>(3*i))&7U)+1;if(used>binder)binder=used;}
      if(expression.bind_argument>=node.arity || task.scope_count>=8 || binder>=8)return result;
      node.operand=static_cast<int>(binder);
    }
    if constexpr(EmitSites) {sites[result.nodes]=next_site;next_site={};}
    origins[result.nodes]=task.expression;nodes[result.nodes++]=node;
    for(int i=node.arity-1;i>=0;--i) {
      if(count>=192) {result.status=PhaseCompileStatus::Capacity;return result;}
      const int child=expression.children[i];const auto need=minimum(child,task.depth-1,task.logical-charge);
      if(need>static_cast<unsigned>(capacity-result.nodes) || reserved>static_cast<unsigned>(capacity-result.nodes)-need) {
        result.status=PhaseCompileStatus::Capacity;return result;
      }
      auto next=task;next.expression=child;--next.depth;next.logical-=charge;
      if(expression.child_scope[i]>=0) {
        next.scope_count=expression.child_scope[i];next.scope=0;
        for(unsigned j=0;j<next.scope_count;++j)next.scope|=j<<(3*j);
      } else if(i==expression.bind_argument) {next.scope|=binder<<(3*task.scope_count);next.scope_count=task.scope_count+1;}
      pending[count++]=next;reserved+=need;
    }
  }
  if(reserved || !result.nodes)return result;
  result.status=PhaseCompileStatus::Ok;return result;
}
} // namespace gagp::evo::repro
