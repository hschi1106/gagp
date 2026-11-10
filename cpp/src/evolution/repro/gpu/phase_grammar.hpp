#pragma once
// Host half of the isolated CUDA grammar-construction prototype. No native
// generation path calls this yet; unsupported alternatives reject the profile.
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <vector>
#include "gagp/evolution/grammar/budget.hpp"
#include "gagp/evolution/grammar/values.hpp"
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
  int root_expression=-1,depth_count=0,logical_depth_count=1; bool site_depth=false;
  // Statements, controls or grammar locals were reached: whole programs only.
  bool statements=false;
  // A let binder was reached: gene binder operands follow lexical positions.
  bool lets=false;
};
inline bool phase_statement_category(NodeCategory category) {
  return category==NodeCategory::Program || category==NodeCategory::Block ||
      category==NodeCategory::Statement;
}
// Prove that an alternative with maximum weight is feasible whenever any
// alternative is feasible at the same depth/capacity. Only then is the dynamic
// normalizing maximum invariant; division and the first scan can move to import.
inline void normalize_static_phase_weights(const HostPhaseGrammar& host,
    std::vector<PhaseNonterminal>& nonterminals,std::vector<PhaseProduction>& productions) {
  for(auto& nt:nonterminals) {
    if(!nt.production_count)continue;
    double maximum=0;for(int i=0;i<nt.production_count;++i)
      maximum=std::max(maximum,productions.at(nt.production_offset+i).weight);
    bool proven=maximum>0;
    for(int depth=0;depth<host.depth_count*host.logical_depth_count && proven;++depth) {
      unsigned cheapest=0xffffffffu,max_weight_cheapest=0xffffffffu;
      for(int i=0;i<nt.production_count;++i){const auto& p=productions.at(nt.production_offset+i);
        const auto cost=host.minimum.at(std::size_t(p.expression)*host.depth_count*host.logical_depth_count+depth);
        cheapest=std::min(cheapest,cost);if(p.weight==maximum)max_weight_cheapest=std::min(max_weight_cheapest,cost);
      }
      proven=cheapest==max_weight_cheapest;
    }
    if(!proven)continue;
    for(int i=0;i<nt.production_count;++i)productions.at(nt.production_offset+i).weight/=maximum;
    nt.normalized_weights=true;
  }
}
// Exact runtime tag of a grammar value type; Invalid for non-value types.
inline ValueTag phase_value_tag(RType type) {
  switch(type) {
    case RType::Int:return ValueTag::Int;case RType::Float:return ValueTag::Float;
    case RType::Bool:return ValueTag::Bool;case RType::Char:return ValueTag::Char;
    case RType::String:return ValueTag::String;case RType::IntList:return ValueTag::IntList;
    case RType::FloatList:return ValueTag::FloatList;case RType::StringList:return ValueTag::StringList;
    default:return ValueTag::Invalid;
  }
}
inline Opcode phase_opcode(NodeKind kind) {
  switch(kind) {
    case NodeKind::ADD:return Opcode::Add;case NodeKind::SUB:return Opcode::Sub;
    case NodeKind::MUL:return Opcode::Mul;case NodeKind::DIV:return Opcode::Div;case NodeKind::MOD:return Opcode::Mod;
    case NodeKind::NEG:return Opcode::Neg;case NodeKind::NOT:return Opcode::Not;
    case NodeKind::LT:return Opcode::Lt;case NodeKind::LE:return Opcode::Le;
    case NodeKind::GT:return Opcode::Gt;case NodeKind::GE:return Opcode::Ge;
    case NodeKind::EQ:return Opcode::Eq;case NodeKind::NE:return Opcode::Ne;
    case NodeKind::CHECK_INT:return Opcode::CheckInt;case NodeKind::CHECK_LIST:return Opcode::CheckList;
    default:throw std::invalid_argument("GPU phase construction: unsupported primitive opcode");
  }
}
// `inputs` is the evaluator's input-local order; null means grammar order.
inline HostPhaseGrammar compile_phase_grammar(const grammar::CompiledGrammar& grammar,
    unsigned root, unsigned max_depth=32, unsigned logical_depth=0, bool site_depth=false,
    const std::vector<std::string>* inputs=nullptr) {
  using namespace grammar;
  const auto fail=[](const char* reason){throw std::invalid_argument(std::string("GPU phase construction: ")+reason);};
  if(root>=grammar.nonterminals().size() || !max_depth || max_depth>64 || max_depth>grammar.search_limits().max_depth || logical_depth>15)
    fail("invalid root/depth");
  if(grammar.expressions().size()>65536 || grammar.nonterminals().size()>4096 ||
      (grammar.expressions().size()+1)*(max_depth+1)>4*1024*1024)fail("table capacity");
  if(site_depth && !logical_depth)fail("site depth requires a positive depth budget");
  HostPhaseGrammar out;out.depth_count=max_depth+1;out.site_depth=site_depth;
  out.expressions.resize(grammar.expressions().size());out.nonterminals.resize(grammar.nonterminals().size());
  out.domains.resize(grammar.constants().size());
  std::vector<bool> seen_nt(out.nonterminals.size()),seen_expr(out.expressions.size()),seen_domain(out.domains.size());
  std::function<void(unsigned,unsigned)> visit_expression;std::function<void(unsigned)> visit_nonterminal;
  visit_nonterminal=[&](unsigned id) {
    if(seen_nt.at(id))return;seen_nt[id]=true;
    const auto& nt=grammar.nonterminals().at(id);
    const bool statement=phase_statement_category(nt.category);
    if((nt.category!=NodeCategory::Expression && !statement) || nt.scope.size()>8 || (statement && !nt.scope.empty()) ||
        phase_value_tag(nt.type)==ValueTag::Invalid)
      fail("nonterminal capability");
    out.statements|=statement;
    for(const auto& slot:nt.scope)
      if(phase_value_tag(slot.type)==ValueTag::Invalid)fail("scope type");
    // Reserve each production range before recursively visiting references.
    auto& target=out.nonterminals[id];target.scope_count=nt.scope.size();target.production_offset=out.productions.size();
    for(auto production:nt.productions) {
      const auto& p=grammar.productions().at(production);
      if(!std::isfinite(p.weight) || p.weight<=0)fail("production weight");
      out.productions.push_back({static_cast<int>(p.expression),p.weight});++target.production_count;
    }
    if(!target.production_count)fail("no production");
    for(auto production:nt.productions)visit_expression(grammar.productions().at(production).expression,nt.scope.size());
  };
  // `environment` counts the lexical binders visible to expression `id`.
  visit_expression=[&](unsigned id,unsigned environment) {
    if(seen_expr.at(id))return;seen_expr[id]=true;
    const auto& e=grammar.expressions().at(id);auto& x=out.expressions[id];
    x.depth_charge=e.resource_charge.depth;
    const bool statement=phase_statement_category(e.category);
    const bool let=e.kind==ExpressionKind::Primitive &&
        PrimitiveCatalog::standard().at(e.target).lowering_node==NodeKind::LET_REGION;
    if((e.category!=NodeCategory::Expression && !statement) || (!e.regions.empty() && !let) || phase_value_tag(e.type)==ValueTag::Invalid ||
        (statement && e.kind!=ExpressionKind::Reference && e.kind!=ExpressionKind::Control))
      fail("expression type or binder introduction");
    // A statement completes without a value; its type is the enclosing result.
    x.node.type=statement?kPhaseStatement:phase_value_tag(e.type);
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
        if(phase_value_tag(d.type)==ValueTag::Invalid || d.sampling || d.elements || d.float_range ||
            (d.integer_range && d.type!=RType::Int))fail("constant domain capability");
        domain.integer_range=d.integer_range;domain.minimum=d.minimum;domain.maximum=d.maximum;
        if(!d.integer_range) {
          domain.value_offset=out.values.size();domain.value_count=d.values.size();
          if(d.values.empty() || d.values.size()>65536 || out.values.size()+d.values.size()>1024*1024)fail("finite domain capacity");
          // Payload values are materialized once here; generation copies them.
          for(const auto& value:d.values)out.values.push_back(materialize_constant(d.type,value));
        }
      }
    } else if(e.kind==ExpressionKind::Input) {
      // A program input is a fixed evaluator local; its operand never varies.
      const auto& name=grammar.inputs().at(e.target).name;int position=e.target;
      if(inputs) {
        const auto found=std::find(inputs->begin(),inputs->end(),name);
        if(found==inputs->end())fail("input missing from evaluator order");
        position=found-inputs->begin();
      }
      if(position>=64)fail("input local capacity");
      x.kind=PhaseExpressionKind::Input;x.target=position;x.native_kind=static_cast<int>(NodeKind::VAR);
      x.node.kind=PhaseTreeKind::Local;x.node.operand=position;
    } else if(e.kind==ExpressionKind::Bound) {
      x.kind=PhaseExpressionKind::Bound;x.target=e.target;x.native_kind=static_cast<int>(NodeKind::REGION_VAR);x.node.kind=PhaseTreeKind::Bound;
      if(e.target>=environment)fail("bound reference outside its environment");
    } else if(e.kind==ExpressionKind::Primitive) {
      const auto& signature=PrimitiveCatalog::standard().at(e.target);
      if(!signature.lowering_node || (!signature.regions.empty() && !let))fail("primitive capability");
      const auto kind=*signature.lowering_node;const auto& descriptor=node_descriptor(kind);x.native_kind=static_cast<int>(kind);
      if(let) {
        // One binder for the body argument; the binder index is the
        // environment size, so it must stay within the eight lexical indices.
        if(e.regions.size()!=1 || e.regions[0].bindings.size()!=1 || e.regions[0].argument>=e.children.size() ||
            phase_value_tag(e.regions[0].bindings[0].type)==ValueTag::Invalid || !e.fuel_charges.empty() || environment>=8)
          fail("let capability");
        x.node.kind=PhaseTreeKind::Let;x.bind_argument=e.regions[0].argument;out.lets=true;
      } else if(kind==NodeKind::IF_EXPR)x.node.kind=PhaseTreeKind::If;
      else if(kind==NodeKind::AND || kind==NodeKind::OR)x.node.kind=kind==NodeKind::AND?PhaseTreeKind::And:PhaseTreeKind::Or;
      else if(descriptor.is_builtin()) {
        if(descriptor.builtin_arity!=static_cast<int>(e.children.size()))fail("builtin arity");
        x.node.kind=PhaseTreeKind::Operation;x.node.opcode=Opcode::CallBuiltin;x.node.operand=descriptor.builtin_id;
      } else {x.node.kind=PhaseTreeKind::Operation;x.node.opcode=phase_opcode(kind);}
    } else if(e.kind==ExpressionKind::Local) {
      if(e.target>=static_cast<unsigned>(kPhaseNamedLocals))fail("grammar local capacity");
      x.kind=PhaseExpressionKind::Operation;x.native_kind=static_cast<int>(NodeKind::VAR);
      x.node.kind=PhaseTreeKind::Var;x.node.operand=e.target;out.statements=true;
    } else if(e.kind==ExpressionKind::Control) {
      const auto& signature=PrimitiveCatalog::standard().control_signatures().at(e.target);
      switch(signature.lowering_node) {
        case NodeKind::PROGRAM:x.node.kind=PhaseTreeKind::Program;break;
        case NodeKind::BLOCK_CONS:x.node.kind=PhaseTreeKind::Block;break;
        case NodeKind::BLOCK_NIL:x.node.kind=PhaseTreeKind::Nil;break;
        case NodeKind::ASSIGN:x.node.kind=PhaseTreeKind::Assign;break;
        case NodeKind::RETURN:x.node.kind=PhaseTreeKind::Return;break;
        case NodeKind::IF_STMT:x.node.kind=PhaseTreeKind::If;break;
        case NodeKind::FOR_RANGE:x.node.kind=PhaseTreeKind::ForRange;break;
        default:fail("control capability");
      }
      x.kind=PhaseExpressionKind::Operation;x.native_kind=static_cast<int>(signature.lowering_node);out.statements=true;
      if(!e.fuel_charges.empty())fail("statement fuel profile capability");
      if(signature.requires_name) {
        if(e.target_input) {
          // Input targets are preset evaluator slots, encoded as -1-position.
          const auto& name=grammar.inputs().at(e.local).name;int position=e.local;
          if(inputs) {
            const auto found=std::find(inputs->begin(),inputs->end(),name);
            if(found==inputs->end())fail("input target missing from evaluator order");
            position=found-inputs->begin();
          }
          if(position>=64)fail("input local capacity");
          x.node.operand=-1-position;
        } else {
          if(e.local>=static_cast<unsigned>(kPhaseNamedLocals))fail("grammar local capacity");
          x.node.operand=e.local;
        }
      }
      // Assign/Return carry the exact stored or returned value type.
      if(x.node.kind==PhaseTreeKind::Assign || x.node.kind==PhaseTreeKind::Return)
        x.node.type=phase_value_tag(grammar.expressions().at(e.children.at(0)).type);
    } else fail("templates, repeated holes and nested regions require native fallback");
    for(const auto& charge:e.fuel_charges) {
      if(charge.event==FuelEvent::Operation || charge.event==FuelEvent::BranchTest)x.node.fuel=charge.cost;
      else if(charge.event==FuelEvent::BranchMerge)x.node.merge_fuel=charge.cost;
      else fail("fuel profile capability");
    }
    for(std::size_t i=0;i<e.children.size();++i)
      visit_expression(e.children[i],environment+(static_cast<int>(i)==x.bind_argument?1:0));
  };
  visit_nonterminal(root);
  // No zero-node alias loops: every selected production must consume a native
  // node before referring to another nonterminal. Regular recursion is allowed.
  for(const auto& p:out.productions)
    if(out.expressions[p.expression].kind==PhaseExpressionKind::Reference)fail("root aliases require native fallback");
  out.minimum.assign((out.expressions.size()+1)*out.depth_count,kNoGrammarId);
  for(std::size_t i=0;i<seen_expr.size();++i)if(seen_expr[i])
    for(unsigned d=0;d<=max_depth;++d)out.minimum[i*out.depth_count+d]=minimum_expression_nodes(grammar,i,d,{});
  PhaseExpression entry;entry.kind=PhaseExpressionKind::Reference;entry.target=root;entry.scope_count=grammar.nonterminals()[root].scope.size();
  for(int i=0;i<entry.scope_count;++i)entry.scope[i]=i;
  out.root_expression=out.expressions.size();out.expressions.push_back(entry);
  for(unsigned d=0;d<=max_depth;++d)out.minimum[out.root_expression*out.depth_count+d]=grammar.nonterminals().at(root).minimum_nodes_by_depth.at(d);
  if(logical_depth) {
    out.logical_depth_count=logical_depth+1;
    const auto size=out.expressions.size()*out.depth_count*out.logical_depth_count;
    if(size>4*1024*1024)fail("projected table capacity");
    out.minimum.assign(size,kNoGrammarId);
    // References do not consume physical or projected depth. The admitted
    // profile forbids production-root aliases, so recursion always reduces d.
    std::vector<bool> computed(size);
    std::function<unsigned(int,int,int)> cost=[&](int id,int d,int l)->unsigned {
      const auto offset=(std::size_t(id)*out.depth_count+d)*out.logical_depth_count+l;
      auto& result=out.minimum[offset];
      if(computed[offset])return result;
      computed[offset]=true;
      const auto& e=out.expressions[id];
      if(e.kind==PhaseExpressionKind::Reference) {
        if(site_depth && !l)return result;
        const auto& nt=out.nonterminals[e.target];
        for(int i=0;i<nt.production_count;++i)
          result=std::min(result,cost(out.productions[nt.production_offset+i].expression,d,l-(site_depth?1:0)));
      } else if(d>0 && l>=(site_depth?0:e.depth_charge)) {
        std::uint64_t total=1;
        for(int i=0;i<e.node.arity;++i)total+=cost(e.children[i],d-1,l-(site_depth?0:e.depth_charge));
        result=total>=kNoGrammarId?kNoGrammarId:static_cast<unsigned>(total);
      }
      return result;
    };
    for(std::size_t i=0;i<out.expressions.size();++i)if(i==std::size_t(out.root_expression) || seen_expr[i])
      for(unsigned d=0;d<=max_depth;++d)for(unsigned l=0;l<=logical_depth;++l)cost(i,d,l);
  }
  return out;
}
} // namespace gagp::evo::repro
