#pragma once
// Host half of the isolated CUDA grammar-construction prototype. No native
// generation path calls this yet; unsupported alternatives reject the profile.
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <vector>
#include "gagp/evolution/grammar/budget.hpp"
#include "gagp/evolution/node_descriptor.hpp"
#include "device/phase_generate.cuh"

namespace gagp::evo::repro {
struct HostPhaseGrammar {
  std::vector<PhaseExpression> expressions;
  std::vector<PhaseProduction> productions;
  std::vector<PhaseNonterminal> nonterminals;
  std::vector<PhaseDomain> domains;
  std::vector<Value> values;
  std::vector<unsigned> minimum;
  int root_expression=-1,depth_count=0;
};
inline Opcode phase_opcode(NodeKind kind) {
  switch(kind) {
    case NodeKind::ADD:return Opcode::Add;case NodeKind::SUB:return Opcode::Sub;
    case NodeKind::MUL:return Opcode::Mul;case NodeKind::MOD:return Opcode::Mod;
    case NodeKind::NEG:return Opcode::Neg;case NodeKind::NOT:return Opcode::Not;
    case NodeKind::LT:return Opcode::Lt;case NodeKind::LE:return Opcode::Le;
    case NodeKind::GT:return Opcode::Gt;case NodeKind::GE:return Opcode::Ge;
    case NodeKind::EQ:return Opcode::Eq;case NodeKind::NE:return Opcode::Ne;
    case NodeKind::CHECK_INT:return Opcode::CheckInt;case NodeKind::CHECK_LIST:return Opcode::CheckList;
    default:throw std::invalid_argument("GPU phase construction: unsupported primitive opcode");
  }
}
inline HostPhaseGrammar compile_phase_grammar(const grammar::CompiledGrammar& grammar,
    unsigned root, unsigned max_depth=32) {
  using namespace grammar;
  const auto fail=[](const char* reason){throw std::invalid_argument(std::string("GPU phase construction: ")+reason);};
  if(root>=grammar.nonterminals().size() || !max_depth || max_depth>64 || max_depth>grammar.search_limits().max_depth)
    fail("invalid root/depth");
  if(grammar.expressions().size()>65536 || grammar.nonterminals().size()>4096 ||
      (grammar.expressions().size()+1)*(max_depth+1)>4*1024*1024)fail("table capacity");
  HostPhaseGrammar out;out.depth_count=max_depth+1;
  out.expressions.resize(grammar.expressions().size());out.nonterminals.resize(grammar.nonterminals().size());
  out.domains.resize(grammar.constants().size());
  std::vector<bool> seen_nt(out.nonterminals.size()),seen_expr(out.expressions.size()),seen_domain(out.domains.size());
  std::function<void(unsigned)> visit_expression,visit_nonterminal;
  visit_nonterminal=[&](unsigned id) {
    if(seen_nt.at(id))return;seen_nt[id]=true;
    const auto& nt=grammar.nonterminals().at(id);
    if(nt.category!=NodeCategory::Expression || nt.scope.size()>8 ||
        (nt.type!=RType::Int && nt.type!=RType::Bool && nt.type!=RType::IntList))fail("nonterminal capability");
    for(const auto& slot:nt.scope)
      if(slot.type!=RType::Int && slot.type!=RType::Bool && slot.type!=RType::IntList)fail("scope type");
    // Reserve each production range before recursively visiting references.
    auto& target=out.nonterminals[id];target.scope_count=nt.scope.size();target.production_offset=out.productions.size();
    for(auto production:nt.productions) {
      const auto& p=grammar.productions().at(production);
      // No subset of a language is silently compiled. Union productions are
      // inspected below even when this construction stage does not select them.
      if(p.generates(GenerationStage::Mutation)) {
        if(!std::isfinite(p.weight) || p.weight<=0)fail("production weight");
        out.productions.push_back({static_cast<int>(p.expression),p.weight});++target.production_count;
      }
    }
    if(!target.production_count)fail("no mutation production");
    for(auto production:nt.productions)visit_expression(grammar.productions().at(production).expression);
  };
  visit_expression=[&](unsigned id) {
    if(seen_expr.at(id))return;seen_expr[id]=true;
    const auto& e=grammar.expressions().at(id);auto& x=out.expressions[id];
    if(e.category!=NodeCategory::Expression || !e.regions.empty() ||
        (e.type!=RType::Int && e.type!=RType::Bool && e.type!=RType::IntList))fail("expression type or binder introduction");
    if(e.kind==ExpressionKind::Reference) {
      if(e.scope_mapping.size()>8)fail("scope capacity");
      x.kind=PhaseExpressionKind::Reference;x.target=e.target;x.scope_count=e.scope_mapping.size();
      for(std::size_t i=0;i<e.scope_mapping.size();++i)x.scope[i]=e.scope_mapping[i];
      visit_nonterminal(e.target);return;
    }
    if(e.children.size()>3)fail("arity capacity");
    x.node.arity=e.children.size();for(std::size_t i=0;i<e.children.size();++i)x.children[i]=e.children[i];
    if(e.kind==ExpressionKind::Constant) {
      x.kind=PhaseExpressionKind::Constant;x.target=e.target;x.native_kind=static_cast<int>(NodeKind::CONST);x.node.kind=PhaseTreeKind::Constant;
      if(!seen_domain.at(e.target)) {
        seen_domain[e.target]=true;const auto& d=grammar.constants().at(e.target);auto& domain=out.domains[e.target];
        if((d.type!=RType::Int && d.type!=RType::Bool) || d.sampling || d.elements || d.float_range)fail("constant domain capability");
        domain.integer_range=d.integer_range;domain.minimum=d.minimum;domain.maximum=d.maximum;
        if(!d.integer_range) {
          domain.value_offset=out.values.size();domain.value_count=d.values.size();
          if(d.values.empty() || d.values.size()>65536 || out.values.size()+d.values.size()>1024*1024)fail("finite domain capacity");
          for(const auto& value:d.values)out.values.push_back(d.type==RType::Int?Value::from_int(std::get<std::int64_t>(value)):Value::from_bool(std::get<bool>(value)));
        }
      }
    } else if(e.kind==ExpressionKind::Bound) {
      x.kind=PhaseExpressionKind::Bound;x.target=e.target;x.native_kind=static_cast<int>(NodeKind::REGION_VAR);x.node.kind=PhaseTreeKind::Local;
    } else if(e.kind==ExpressionKind::Primitive) {
      const auto& signature=PrimitiveCatalog::standard().at(e.target);
      if(!signature.lowering_node || !signature.regions.empty())fail("primitive capability");
      const auto kind=*signature.lowering_node;const auto& descriptor=node_descriptor(kind);x.native_kind=static_cast<int>(kind);
      if(kind==NodeKind::IF_EXPR)x.node.kind=PhaseTreeKind::If;
      else if(kind==NodeKind::AND || kind==NodeKind::OR)x.node.kind=kind==NodeKind::AND?PhaseTreeKind::And:PhaseTreeKind::Or;
      else if(descriptor.is_builtin()) {
        const auto builtin=static_cast<BuiltinId>(descriptor.builtin_id);
        switch(builtin) {
          case BuiltinId::IDiv0:case BuiltinId::IMod0:case BuiltinId::Min:case BuiltinId::Max:case BuiltinId::Clip:
          case BuiltinId::Len:case BuiltinId::Index:case BuiltinId::Slice:break;
          default:fail("builtin capability");
        }
        x.node.kind=PhaseTreeKind::Operation;x.node.opcode=Opcode::CallBuiltin;x.node.operand=descriptor.builtin_id;
      } else {x.node.kind=PhaseTreeKind::Operation;x.node.opcode=phase_opcode(kind);}
    } else fail("templates, repeated holes, inputs, locals and nested regions require native fallback");
    for(const auto& charge:e.fuel_charges) {
      if(charge.event==FuelEvent::Operation || charge.event==FuelEvent::BranchTest)x.node.fuel=charge.cost;
      else if(charge.event==FuelEvent::BranchMerge)x.node.merge_fuel=charge.cost;
      else fail("fuel profile capability");
    }
    for(auto child:e.children)visit_expression(child);
  };
  visit_nonterminal(root);
  // No zero-node alias loops: every selected production must consume a native
  // node before referring to another nonterminal. Regular recursion is allowed.
  for(const auto& p:out.productions)
    if(out.expressions[p.expression].kind==PhaseExpressionKind::Reference)fail("root aliases require native fallback");
  out.minimum.assign((out.expressions.size()+1)*out.depth_count,kNoGrammarId);
  const NonterminalBudgetLookup mutation_cost=[&](unsigned nt,unsigned depth) {
    const auto& costs=grammar.nonterminals().at(nt).generation_costs(GenerationStage::Mutation);
    return costs.at(depth);
  };
  for(std::size_t i=0;i<seen_expr.size();++i)if(seen_expr[i])
    for(unsigned d=0;d<=max_depth;++d)out.minimum[i*out.depth_count+d]=minimum_expression_nodes(grammar,i,d,{},mutation_cost);
  PhaseExpression entry;entry.kind=PhaseExpressionKind::Reference;entry.target=root;entry.scope_count=grammar.nonterminals()[root].scope.size();
  for(int i=0;i<entry.scope_count;++i)entry.scope[i]=i;
  out.root_expression=out.expressions.size();out.expressions.push_back(entry);
  for(unsigned d=0;d<=max_depth;++d)out.minimum[out.root_expression*out.depth_count+d]=mutation_cost(root,d);
  return out;
}
} // namespace gagp::evo::repro
