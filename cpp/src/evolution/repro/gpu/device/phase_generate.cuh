#pragma once
#include "phase_compile.cuh"
#include "grammar_random.cuh"

namespace gagp::evo::repro {
// Private uploaded grammar tables. The host compiler admits every reachable
// production; an unsupported alternative declines the whole requested profile.
enum class PhaseExpressionKind : int { Operation, Constant, Bound, Reference };
struct PhaseExpression {
  PhaseExpressionKind kind = PhaseExpressionKind::Operation;
  PhaseTreeNode node;
  int native_kind = -1;
  int target = -1;
  int children[3]{-1,-1,-1};
  int scope_count = 0;
  int scope[8]{};
};
struct PhaseProduction { int expression; double weight; };
struct PhaseNonterminal { int production_offset=0, production_count=0, scope_count=0; };
struct PhaseDomain { int value_offset=0, value_count=0; bool integer_range=false; std::int64_t minimum=0, maximum=0; };
struct PhaseGrammarView {
  const PhaseExpression* expressions=nullptr; int expression_count=0;
  const PhaseProduction* productions=nullptr; int production_count=0;
  const PhaseNonterminal* nonterminals=nullptr; int nonterminal_count=0;
  const PhaseDomain* domains=nullptr; int domain_count=0;
  const Value* values=nullptr; int value_count=0;
  const unsigned* minimum=nullptr; int depth_count=0;
};
struct PhaseGenerationResult { PhaseCompileStatus status=PhaseCompileStatus::Invalid; int nodes=0,constants=0,steps=0; };

// This constructs grammar expressions, not merely type-compatible trees. Scope
// maps and constant domains come from the admitted immutable grammar upload.
// Each lane owns its bounded output; no CPU reproduction or device allocation.
__device__ inline PhaseGenerationResult generate_phase(
    const PhaseGrammarView& grammar, int root_expression, int scope_count,
    int depth, int capacity, std::uint64_t seed,
    PhaseTreeNode* nodes, int* origins, Value* constants) {
  PhaseGenerationResult result;
  if (!grammar.expressions || !grammar.minimum || !nodes || !origins || !constants ||
      root_expression<0 || root_expression>=grammar.expression_count ||
      scope_count<0 || scope_count>8 || depth<=0 || depth>=grammar.depth_count ||
      depth>64 || capacity<=0 || capacity>1024) return result;
  const auto minimum=[&](int expression,int d)->unsigned {
    return expression<0 || expression>=grammar.expression_count || d<0 || d>=grammar.depth_count
        ? 0xffffffffU : grammar.minimum[static_cast<std::size_t>(expression)*grammar.depth_count+d];
  };
  struct Task { int expression,depth,scope_count; unsigned scope; };
  Task pending[192]; int count=1;
  pending[0]={root_expression,depth,scope_count,0};
  for(int i=0;i<scope_count;++i)pending[0].scope |= static_cast<unsigned>(i) << (3*i);
  unsigned reserved=minimum(root_expression,depth);
  if(reserved>static_cast<unsigned>(capacity)) {result.status=PhaseCompileStatus::Capacity;return result;}
  DGrammarRandom random(seed);
  while(count) {
    if(++result.steps>8192) {result.status=PhaseCompileStatus::Capacity;return result;}
    const auto task=pending[--count];
    const auto required=minimum(task.expression,task.depth);
    if(required>reserved) return result;
    reserved-=required;
    const auto expression=grammar.expressions[task.expression];
    if(expression.kind==PhaseExpressionKind::Reference) {
      if(expression.target<0 || expression.target>=grammar.nonterminal_count || !grammar.nonterminals || !grammar.productions ||
          expression.scope_count<0 || expression.scope_count>8)return result;
      const auto nt=grammar.nonterminals[expression.target];
      if(nt.scope_count!=expression.scope_count || nt.production_offset<0 || nt.production_count<=0 ||
          nt.production_offset>grammar.production_count || nt.production_count>grammar.production_count-nt.production_offset)return result;
      Task next{-1,task.depth,expression.scope_count,0};
      for(int i=0;i<next.scope_count;++i) {
        if(expression.scope[i]<0 || expression.scope[i]>=task.scope_count)return result;
        next.scope |= ((task.scope >> (3*expression.scope[i])) & 7U) << (3*i);
      }
      const unsigned available=static_cast<unsigned>(capacity-result.nodes)-reserved;
      double maximum=0,total=0;
      for(int i=0;i<nt.production_count;++i) {
        const auto p=grammar.productions[nt.production_offset+i];
        if(minimum(p.expression,task.depth)<=available && p.weight>maximum) maximum=p.weight;
      }
      if(!(maximum>0)) {result.status=PhaseCompileStatus::Capacity;return result;}
      for(int i=0;i<nt.production_count;++i) {
        const auto p=grammar.productions[nt.production_offset+i];
        if(minimum(p.expression,task.depth)<=available)total+=p.weight/maximum;
      }
      double choice=static_cast<double>(random.next()>>11)*0x1.0p-53*total;
      for(int i=0;i<nt.production_count;++i) {
        const auto p=grammar.productions[nt.production_offset+i];
        if(minimum(p.expression,task.depth)>available)continue;
        next.expression=p.expression; choice-=p.weight/maximum; if(choice<0)break;
      }
      if(next.expression<0 || count>=192)return result;
      reserved+=minimum(next.expression,next.depth);pending[count++]=next;continue;
    }
    if(task.depth<=0 || result.nodes>=capacity) {result.status=PhaseCompileStatus::Capacity;return result;}
    auto node=expression.node;
    if(node.arity<0 || node.arity>3)return result;
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
      if(value.tag!=ValueTag::Int && value.tag!=ValueTag::Bool)return result;
      node.operand=result.constants;constants[result.constants++]=value;
    } else if(expression.kind!=PhaseExpressionKind::Operation)return result;
    origins[result.nodes]=task.expression;nodes[result.nodes++]=node;
    for(int i=node.arity-1;i>=0;--i) {
      if(count>=192) {result.status=PhaseCompileStatus::Capacity;return result;}
      const int child=expression.children[i];const auto need=minimum(child,task.depth-1);
      if(need>static_cast<unsigned>(capacity-result.nodes) || reserved>static_cast<unsigned>(capacity-result.nodes)-need) {
        result.status=PhaseCompileStatus::Capacity;return result;
      }
      auto next=task;next.expression=child;--next.depth;pending[count++]=next;reserved+=need;
    }
  }
  if(reserved || !result.nodes)return result;
  result.status=PhaseCompileStatus::Ok;return result;
}
} // namespace gagp::evo::repro
