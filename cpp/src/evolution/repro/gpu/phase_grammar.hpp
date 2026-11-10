#pragma once
// Host half of the isolated CUDA grammar-construction prototype. No native
// generation path calls this yet; unsupported alternatives reject the profile.
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <vector>
#include "gagp/core/region_plan.hpp"
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
  // A let, traversal or bounded region was reached: Bound operands are binder
  // numbers (whole-program phases only).
  bool lets=false;
  // Bounded regions and traversals by table entry (PhaseTreeNode::operand of a
  // Region or Traverse node), with the host-only bounded region plan and phase
  // binding sources for native assembly (empty for a traversal).
  std::vector<PhaseRegionInfo> regions;
  struct RegionSource {
    unsigned expression=0;
    RegionPlan plan;
    std::vector<std::vector<RegionValueSlot>> sources;
  };
  std::vector<RegionSource> region_sources;
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
static_assert(int(PhaseFuelEvent::Result)==int(FuelEvent::Result) && int(PhaseFuelEvent::StoreSequence)==int(FuelEvent::StoreSequence),
    "phase fuel events follow the native enumeration");
// `inputs` is the evaluator's input-local order; null means grammar order.
//
// Lexical binders are numbered statically: a nonterminal of scope n is entered
// with one fixed environment (binder numbers of its n positions; the root's is
// 0..n-1), a let binds the largest visible number plus one, a traversal the
// next three, and a region phase numbers its bindings from zero. Reference,
// template and hole scope maps select from the enclosing environment. A
// grammar that enters one nonterminal under two environments, or needs a
// number above 15, declines the profile; identity-mapped grammars always
// number by lexical position.
inline HostPhaseGrammar compile_phase_grammar(const grammar::CompiledGrammar& grammar,
    unsigned root, unsigned max_depth=32, unsigned site_depth=0,
    const std::vector<std::string>* inputs=nullptr) {
  using namespace grammar;
  const auto fail=[](const char* reason){throw std::invalid_argument(std::string("GPU phase construction: ")+reason);};
  if(root>=grammar.nonterminals().size() || !max_depth || max_depth>64 || max_depth>grammar.search_limits().max_depth || site_depth>15)
    fail("invalid root/depth");
  if(grammar.expressions().size()>65536 || grammar.nonterminals().size()>4096 ||
      (grammar.expressions().size()+1)*(max_depth+1)>4*1024*1024)fail("table capacity");
  HostPhaseGrammar out;out.depth_count=max_depth+1;out.site_depth=site_depth>0;
  out.expressions.resize(grammar.expressions().size());out.nonterminals.resize(grammar.nonterminals().size());
  out.domains.resize(grammar.constants().size());
  std::vector<bool> seen_nt(out.nonterminals.size()),seen_expr(out.expressions.size()),seen_domain(out.domains.size());
  using Environment=std::vector<int>;
  std::vector<Environment> nt_environment(out.nonterminals.size()),expr_environment(out.expressions.size());
  const auto project=[&](const Environment& environment,const std::vector<std::uint32_t>& mapping) {
    if(mapping.size()>8)fail("scope capacity");
    Environment result;
    for(auto position:mapping) {
      if(position>=environment.size())fail("scope map outside its environment");
      result.push_back(environment[position]);
    }
    return result;
  };
  // Next free binder number: one above every visible binder.
  const auto next_binder=[](const Environment& environment) {
    return environment.empty()?0:*std::max_element(environment.begin(),environment.end())+1;
  };
  std::function<void(unsigned,const Environment&)> visit_expression;std::function<void(unsigned,const Environment&)> visit_nonterminal;
  visit_nonterminal=[&](unsigned id,const Environment& environment) {
    if(seen_nt.at(id)) {
      if(nt_environment[id]!=environment)fail("nonterminal entered under two lexical environments");
      return;
    }
    seen_nt[id]=true;nt_environment[id]=environment;
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
    if(environment.size()!=nt.scope.size())fail("nonterminal environment size");
    for(auto production:nt.productions)visit_expression(grammar.productions().at(production).expression,environment);
  };
  // `environment` holds the binder numbers visible to expression `id`.
  visit_expression=[&](unsigned id,const Environment& environment) {
    if(seen_expr.at(id)) {
      if(expr_environment[id]!=environment)fail("expression entered under two lexical environments");
      return;
    }
    seen_expr[id]=true;expr_environment[id]=environment;
    const auto& e=grammar.expressions().at(id);auto& x=out.expressions[id];
    if(environment.size()>15)fail("lexical environment capacity");
    const bool statement=phase_statement_category(e.category);
    const bool let=e.kind==ExpressionKind::Primitive &&
        PrimitiveCatalog::standard().at(e.target).lowering_node==NodeKind::LET_REGION;
    const bool region=e.kind==ExpressionKind::Structured;
    const bool traversal=e.kind==ExpressionKind::Primitive &&
        PrimitiveCatalog::standard().at(e.target).traversal_direction.has_value();
    const bool alias=e.kind==ExpressionKind::Template || e.kind==ExpressionKind::Hole;
    if((e.category!=NodeCategory::Expression && !statement) || (!e.regions.empty() && !let && !region && !traversal) || phase_value_tag(e.type)==ValueTag::Invalid ||
        (statement && e.kind!=ExpressionKind::Reference && e.kind!=ExpressionKind::Control && !alias))
      fail("expression type or binder introduction");
    std::vector<Environment> child_environment(e.children.size(),environment);
    // A statement completes without a value; its type is the enclosing result.
    x.node.type=statement?kPhaseStatement:phase_value_tag(e.type);
    if(e.kind==ExpressionKind::Reference) {
      const auto inner=project(environment,e.scope_mapping);
      x.kind=PhaseExpressionKind::Reference;x.target=e.target;x.scope_count=e.scope_mapping.size();
      for(std::size_t i=0;i<e.scope_mapping.size();++i)x.scope[i]=e.scope_mapping[i];
      visit_nonterminal(e.target,inner);return;
    }
    if(alias) {
      // A template instance or hole: its body or fill sees the projected scope.
      if(e.children.size()!=1 || !e.fuel_charges.empty())fail("template capability");
      if(e.kind==ExpressionKind::Template) {
        // A repeated hole couples its occurrences, which native generation
        // fills with one copied subtree. Only a fill without choices (no
        // reference, singleton constants) is generated identically each time
        // and never varied, so independent generation keeps the copies equal.
        std::function<bool(unsigned)> fixed=[&](unsigned child) {
          const auto& c=grammar.expressions().at(child);
          if(c.kind==ExpressionKind::Reference)return false;
          if(c.kind==ExpressionKind::Constant) {
            const auto& d=grammar.constants().at(c.target);
            if(d.integer_range?d.minimum!=d.maximum:d.values.size()!=1)return false;
          }
          for(auto next:c.children)if(!fixed(next))return false;
          return true;
        };
        std::vector<int> uses(grammar.templates().at(e.target).holes.size());
        std::vector<int> first(uses.size(),-1);
        std::function<void(unsigned)> count=[&](unsigned child) {
          const auto& c=grammar.expressions().at(child);
          if(c.kind==ExpressionKind::Hole && c.template_id==e.target) {
            if(c.children.size()!=1)fail("template capability");
            if(++uses.at(c.target)==1)first[c.target]=c.children[0];
            else if(!fixed(first[c.target]) || !fixed(c.children[0]))fail("repeated template hole with a varying fill");
          }
          if(c.kind!=ExpressionKind::Reference)for(auto next:c.children)count(next);
        };
        count(e.children[0]);
      }
      x.kind=PhaseExpressionKind::Alias;x.children[0]=e.children[0];x.node.arity=0;
      const auto inner=project(environment,e.scope_mapping);
      x.child_scope[0]=static_cast<std::int8_t>(inner.size());x.scope_count=inner.size();
      visit_expression(e.children[0],inner);return;
    }
    if(e.children.size()>(region || traversal?std::size_t(kPhaseMaxArity):3))fail("arity capacity");
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
      if(e.target>=environment.size())fail("bound reference outside its environment");
      x.node.operand=environment[e.target];
    } else if(e.kind==ExpressionKind::Primitive) {
      const auto& signature=PrimitiveCatalog::standard().at(e.target);
      if(!signature.lowering_node || (!signature.regions.empty() && !let && !traversal))fail("primitive capability");
      const auto kind=*signature.lowering_node;const auto& descriptor=node_descriptor(kind);x.native_kind=static_cast<int>(kind);
      if(let) {
        // One binder for the body argument, numbered above every visible one.
        const int binder=next_binder(environment);
        if(e.regions.size()!=1 || e.regions[0].bindings.size()!=1 || e.regions[0].argument>=e.children.size() ||
            phase_value_tag(e.regions[0].bindings[0].type)==ValueTag::Invalid || binder>=kPhaseBinders)
          fail("let capability");
        x.node.kind=PhaseTreeKind::Let;x.bind_argument=e.regions[0].argument;x.node.operand=binder;out.lets=true;
        child_environment[x.bind_argument].push_back(binder);
      } else if(traversal) {
        // source, start, [begin, end,] initial state, then the body, which
        // binds element, index and state numbered above every visible binder.
        const bool ranged=kind==NodeKind::TRAVERSE_RANGE;
        const int binder=next_binder(environment);const auto arity=ranged?6u:4u;
        if((kind!=NodeKind::TRAVERSE && !ranged) || e.children.size()!=arity || e.regions.size()!=1 ||
            e.regions[0].argument!=arity-1 || e.regions[0].bindings.size()!=3 || binder+2>=kPhaseBinders || out.regions.size()>=1024)
          fail("traversal capability");
        PhaseRegionInfo info;info.traversal=*signature.traversal_direction==TraversalDirection::Reverse?2:1;
        info.ranged=ranged;info.fuel_profile=!e.fuel_charges.empty();
        for(auto& cost:info.event_cost)cost=1;
        for(const auto& charge:e.fuel_charges)info.event_cost[static_cast<int>(charge.event)]=charge.cost;
        for(unsigned c=0;c<arity;++c) {
          info.operand_types[c]=phase_value_tag(grammar.expressions().at(e.children[c]).type);
          if(info.operand_types[c]==ValueTag::Invalid)fail("traversal operand type");
        }
        for(int k=0;k<3;++k) {
          info.binders[k]=binder+k;info.binding_types[0][k]=phase_value_tag(e.regions[0].bindings[k].type);
          if(info.binding_types[0][k]==ValueTag::Invalid)fail("traversal binding type");
          child_environment[arity-1].push_back(binder+k);
        }
        info.phase_type[0]=x.node.type;
        x.node.kind=PhaseTreeKind::Traverse;x.node.operand=out.regions.size();out.lets=true;
        out.regions.push_back(info);out.region_sources.push_back({id,{},{}});
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
    } else if(region) {
      // A bounded region: operands in place, then phases that see only their
      // own bindings. Phases are separate programs without regions, program
      // variables or statements (checked after the walk).
      const auto& contract=grammar.structured_contracts().at(e.target);
      if(contract.family!=StructuredFamily::BoundedRegion || !contract.plan)fail("structured capability");
      const auto& plan=*contract.plan;
      const auto operands=plan.state_types.size()+plan.bound_operand_count;
      const auto phases=e.phases.size();
      if(e.children.size()!=bounded_region_arity(plan) || operands+phases!=e.children.size() || !phases ||
          e.regions.size()!=phases || e.captures.size()!=plan.parameter_types.size() ||
          e.captures.size()>std::size_t(kPhaseMaxArity) || out.regions.size()>=1024)
        fail("bounded region capability");
      PhaseRegionInfo info;info.operands=operands;info.phases=phases;info.captures=e.captures.size();
      HostPhaseGrammar::RegionSource source{id,plan,{}};
      for(std::size_t i=0;i<operands;++i) {
        info.operand_types[i]=phase_value_tag(grammar.expressions().at(e.children[i]).type);
        if(info.operand_types[i]==ValueTag::Invalid)fail("bounded region operand type");
      }
      for(std::size_t c=0;c<e.captures.size();++c) {
        const auto& capture=e.captures[c];int target=capture.target;
        if(capture.kind==CompiledCaptureKind::Input) {
          info.capture_kind[c]=PhaseCaptureKind::Input;
          if(inputs) {
            const auto found=std::find(inputs->begin(),inputs->end(),grammar.inputs().at(capture.target).name);
            if(found==inputs->end())fail("region capture missing from evaluator order");
            target=found-inputs->begin();
          }
          if(target>=64)fail("input local capacity");
        } else if(capture.kind==CompiledCaptureKind::Local) {
          info.capture_kind[c]=PhaseCaptureKind::Local;
          if(target>=kPhaseNamedLocals)fail("grammar local capacity");
          out.statements=true;
        } else {
          info.capture_kind[c]=PhaseCaptureKind::Bound;
          if(capture.target>=environment.size())fail("region capture outside its environment");
          target=environment[capture.target];
        }
        info.capture_target[c]=target;
      }
      for(std::size_t p=0;p<phases;++p) {
        const auto& slot=e.regions[p];
        if(e.phases[p].argument!=operands+p || slot.argument!=operands+p ||
            e.phases[p].sources.size()!=slot.bindings.size() || slot.bindings.size()>=8)
          fail("bounded region phase layout");
        info.phase_bindings[p]=slot.bindings.size();
        info.phase_type[p]=bounded_region_phase_type(plan,p);
        if(info.phase_type[p]!=phase_value_tag(grammar.expressions().at(e.children[operands+p]).type))
          fail("bounded region phase type");
        for(std::size_t b=0;b<slot.bindings.size();++b) {
          info.binding_types[p][b]=phase_value_tag(slot.bindings[b].type);
          if(info.binding_types[p][b]==ValueTag::Invalid)fail("bounded region binding type");
        }
        child_environment[operands+p].clear();
        for(std::size_t b=0;b<slot.bindings.size();++b)child_environment[operands+p].push_back(static_cast<int>(b));
        source.sources.push_back(e.phases[p].sources);
      }
      x.kind=PhaseExpressionKind::Operation;x.native_kind=static_cast<int>(NodeKind::BOUNDED_REGION);
      x.node.kind=PhaseTreeKind::Region;x.node.operand=out.regions.size();out.lets=true;
      out.regions.push_back(info);out.region_sources.push_back(std::move(source));
    } else fail("expression kind requires native fallback");
    for(const auto& charge:e.fuel_charges) {
      if(traversal)break;  // traversal events are in its table entry
      // A let charges its binding Store; a region its BoundedRegion entry.
      if(let) {
        if(charge.event==FuelEvent::Bind)x.node.fuel=charge.cost;
        else fail("let fuel profile capability");
      } else if(charge.event==FuelEvent::Operation || charge.event==FuelEvent::BranchTest)x.node.fuel=charge.cost;
      else if(charge.event==FuelEvent::BranchMerge && !region)x.node.merge_fuel=charge.cost;
      else fail("fuel profile capability");
    }
    for(std::size_t i=0;i<e.children.size();++i) {
      if(child_environment[i].size()>15)fail("lexical environment capacity");
      x.child_scope[i]=static_cast<std::int8_t>(child_environment[i].size());
      visit_expression(e.children[i],child_environment[i]);
    }
  };
  {
    Environment environment;
    for(std::size_t i=0;i<grammar.nonterminals().at(root).scope.size();++i)environment.push_back(static_cast<int>(i));
    visit_nonterminal(root,environment);
  }
  // Like the native compiler, a region phase reads only its bindings: no
  // program variable, statement or nested structured region is reachable.
  for(const auto& source:out.region_sources) {
    const auto& e=grammar.expressions().at(source.expression);
    if(e.kind!=ExpressionKind::Structured)continue;
    std::vector<bool> closed(grammar.expressions().size());
    std::function<void(unsigned)> check=[&](unsigned id) {
      if(closed.at(id))return;closed[id]=true;
      const auto& c=grammar.expressions()[id];
      if(c.kind==ExpressionKind::Input || c.kind==ExpressionKind::Local || c.kind==ExpressionKind::Control ||
          c.kind==ExpressionKind::Structured || c.category!=NodeCategory::Expression ||
          (c.kind==ExpressionKind::Primitive && PrimitiveCatalog::standard().at(c.target).traversal_direction))
        fail("region phase reaches a program variable, statement, traversal or nested region");
      if(c.kind==ExpressionKind::Reference)
        for(auto production:grammar.nonterminals().at(c.target).productions)check(grammar.productions().at(production).expression);
      for(auto child:c.children)check(child);
    };
    for(std::size_t p=0;p<e.phases.size();++p)check(e.children.at(e.phases[p].argument));
  }
  // No zero-node alias loops: every selected production must consume a native
  // node before referring to another nonterminal. Regular recursion is allowed.
  // Site annotation recovers a production from its first node's origin, so the
  // node each production root resolves to must be unique.
  std::vector<bool> resolved(out.expressions.size());
  for(const auto& p:out.productions) {
    int e=p.expression;
    while(out.expressions[e].kind==PhaseExpressionKind::Alias)e=out.expressions[e].children[0];
    if(out.expressions[e].kind==PhaseExpressionKind::Reference)fail("root aliases require native fallback");
    if(resolved[e])fail("productions sharing a root node require native fallback");
    resolved[e]=true;
  }
  out.minimum.assign((out.expressions.size()+1)*out.depth_count,kNoGrammarId);
  for(std::size_t i=0;i<seen_expr.size();++i)if(seen_expr[i])
    for(unsigned d=0;d<=max_depth;++d)out.minimum[i*out.depth_count+d]=minimum_expression_nodes(grammar,i,d,{});
  // A wrapper costs exactly its child (generation reserves the child's cost).
  for(std::size_t i=0;i<seen_expr.size();++i)if(seen_expr[i] && out.expressions[i].kind==PhaseExpressionKind::Alias)
    for(unsigned d=0;d<=max_depth;++d)
      if(out.minimum[i*out.depth_count+d]!=out.minimum[std::size_t(out.expressions[i].children[0])*out.depth_count+d])
        fail("template budget differs from its body");
  PhaseExpression entry;entry.kind=PhaseExpressionKind::Reference;entry.target=root;entry.scope_count=grammar.nonterminals()[root].scope.size();
  for(int i=0;i<entry.scope_count;++i)entry.scope[i]=i;
  out.root_expression=out.expressions.size();out.expressions.push_back(entry);
  for(unsigned d=0;d<=max_depth;++d)out.minimum[out.root_expression*out.depth_count+d]=grammar.nonterminals().at(root).minimum_nodes_by_depth.at(d);
  // Lowering never runs out of its fixed tables during a search: over every
  // tree within the physical depth budget, bound the open traversals, lets,
  // loops and value ifs on one path and the temporaries they hold (a traversal
  // holds at most nine, a let or a loop one) next to the inputs and grammar
  // locals. Site depth only shortens trees, so it is ignored here.
  {
    enum {Temporaries,Walks,Lets,Loops,Ifs,Count};
    using Hold=std::array<int,Count>;
    const std::size_t depths=out.depth_count;
    std::vector<Hold> hold(out.expressions.size()*depths,Hold{});std::vector<char> done(hold.size());
    const auto feasible=[&](int id,int d){return out.minimum[std::size_t(id)*depths+d]!=kNoGrammarId;};
    const auto merge=[](Hold& into,const Hold& from){for(int i=0;i<Count;++i)into[i]=std::max(into[i],from[i]);};
    // A reference and its production root share a depth; every production
    // root resolves to a node (checked above), so the recursion terminates.
    std::function<Hold(int,int)> need=[&](int id,int d)->Hold {
      const auto key=std::size_t(id)*depths+d;
      if(done[key])return hold[key];
      const auto& e=out.expressions[id];Hold h{};
      if(e.kind==PhaseExpressionKind::Alias)h=need(e.children[0],d);
      else if(e.kind==PhaseExpressionKind::Reference) {
        const auto& nt=out.nonterminals[e.target];
        for(int i=0;i<nt.production_count;++i) {
          const int child=out.productions[nt.production_offset+i].expression;
          if(feasible(child,d))merge(h,need(child,d));
        }
      } else if(d>0) {
        for(int i=0;i<e.node.arity;++i)if(feasible(e.children[i],d-1))merge(h,need(e.children[i],d-1));
        switch(e.node.kind) {
          case PhaseTreeKind::Traverse:h[Temporaries]+=9;++h[Walks];break;
          case PhaseTreeKind::Let:++h[Temporaries];++h[Lets];break;
          case PhaseTreeKind::ForRange:++h[Temporaries];++h[Loops];break;
          case PhaseTreeKind::If:h[Ifs]+=e.node.type!=kPhaseStatement;break;
          default:break;
        }
      }
      done[key]=true;return hold[key]=h;
    };
    const auto h=need(out.root_expression,max_depth);
    const int inputs_count=static_cast<int>(inputs?inputs->size():grammar.inputs().size());
    const int named=static_cast<int>(std::min<std::size_t>(grammar.locals().size(),kPhaseNamedLocals));
    if(inputs_count+named+h[Temporaries]>gpu_detail::MAX_LOCALS || h[Walks]>kPhaseWalks || h[Lets]>kPhaseLets ||
        h[Loops]>kPhaseLoops || (h[Walks] && h[Ifs]>kPhaseIfs))
      fail("lowering capacity within the depth budget requires native fallback");
  }
  if(site_depth) {
    // Site depth: a second table dimension counts grammar invocations. Only a
    // reference consumes it; physical depth is consumed by nodes. The admitted
    // profile forbids production-root aliases, so recursion always reduces d or l.
    out.logical_depth_count=site_depth+1;
    const auto size=out.expressions.size()*out.depth_count*out.logical_depth_count;
    if(size>4*1024*1024)fail("site depth table capacity");
    out.minimum.assign(size,kNoGrammarId);
    std::vector<bool> computed(size);
    std::function<unsigned(int,int,int)> cost=[&](int id,int d,int l)->unsigned {
      const auto offset=(std::size_t(id)*out.depth_count+d)*out.logical_depth_count+l;
      auto& result=out.minimum[offset];
      if(computed[offset])return result;
      computed[offset]=true;
      const auto& e=out.expressions[id];
      if(e.kind==PhaseExpressionKind::Alias) {
        result=cost(e.children[0],d,l);
      } else if(e.kind==PhaseExpressionKind::Reference) {
        if(!l)return result;
        const auto& nt=out.nonterminals[e.target];
        for(int i=0;i<nt.production_count;++i)
          result=std::min(result,cost(out.productions[nt.production_offset+i].expression,d,l-1));
      } else if(d>0) {
        std::uint64_t total=1;
        for(int i=0;i<e.node.arity;++i)total+=cost(e.children[i],d-1,l);
        result=total>=kNoGrammarId?kNoGrammarId:static_cast<unsigned>(total);
      }
      return result;
    };
    for(std::size_t i=0;i<out.expressions.size();++i)if(i==std::size_t(out.root_expression) || seen_expr[i])
      for(unsigned d=0;d<=max_depth;++d)for(unsigned l=0;l<=site_depth;++l)cost(i,d,l);
  }
  return out;
}
} // namespace gagp::evo::repro
