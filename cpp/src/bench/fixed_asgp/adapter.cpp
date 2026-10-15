#include "adapter.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include "lang/expr.h"

namespace fixed_asgp {
Json string(std::string value) { Json j; j.kind=Json::Kind::String; j.string_v=std::move(value); return j; }
Json number(double value) { Json j; j.kind=Json::Kind::Number; j.number_v=value; return j; }
Json array(std::vector<Json> values) { Json j; j.kind=Json::Kind::Array; j.array_v=std::move(values); return j; }
Json object(std::initializer_list<std::pair<const std::string, Json>> values) {
  Json j; j.kind=Json::Kind::Object; j.object_v=values; return j;
}
Json read(const std::string& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot read " + path);
  std::ostringstream text; text << input.rdbuf();
  return gagp::cli_detail::JsonParser(text.str(), {true, 512}).parse();
}
void write(const std::string& path, const Json& value) {
  std::ofstream output(path);
  output << gg::canonical_json(value) << '\n';
  if (!output) throw std::runtime_error("cannot write " + path);
}
namespace {
Json boolean(bool value) { Json j; j.kind=Json::Kind::Bool; j.bool_v=value; return j; }
Json constant(std::int64_t value) {
  return object({{"constant", object({{"type", string("Int")}, {"values", array({string(std::to_string(value))})}})}});
}
Json bool_constant(bool value) {
  return object({{"constant", object({{"type", string("Bool")}, {"values", array({boolean(value)})}})}});
}
Json bound(const std::string& name) { return object({{"bound", string(name)}}); }
Json call(const std::string& signature, std::vector<Json> args) {
  return object({{"signature", string(signature)}, {"args", array(std::move(args))}});
}
Json expression(const asgp::Expr& expr, std::size_t& at, bool dp, const asgp::TaskSpec& spec, const std::string& phase, bool guarded_index) {
  using K = asgp::NodeKind;
  const auto source_node_index=at;
  const auto node = expr.at(at++);
  std::vector<Json> args;
  for (int k=0; k<asgp::Arity(node.kind); ++k) args.push_back(expression(expr, at, dp, spec, phase, guarded_index));
  switch (node.kind) {
    case K::kIntConst: case K::kCharConst: return constant(node.value);
    case K::kInputScalar: return bound("scalar" + std::to_string(node.op));
    case K::kEqChar: return call("eq(Int,Int)->Bool", args);
    case K::kIsLetter: {
      auto range=[&](int lo,int hi) { return call("if(Bool,Bool,Bool)->Bool", {call("ge(Int,Int)->Bool",{args[0],constant(lo)}),call("le(Int,Int)->Bool",{args[0],constant(hi)}),bool_constant(false)}); };
      return call("if(Bool,Bool,Bool)->Bool", {range(65,90),bool_constant(true),range(97,122)});
    }
    case K::kBoolConst: return bool_constant(node.value);
    case K::kInputList: return bound("xs");
    case K::kEmptyList:
      return object({{"constant", object({{"type", string("IntList")}, {"values", array({array({})})}})}});
    case K::kSingleton: return call("singleton(Int)->IntList", args);
    case K::kConcat: return call("concat(IntList,IntList)->IntList", args);
    case K::kInputSize: case K::kStateA: return bound(spec.id=="stock_with_cooldown" ? "source_n" : "n");
    case K::kStateB: return bound("j");
    case K::kStateValue: {
      if(spec.id=="stock_with_cooldown")return bound("source_v");
      // Subregion starts 0<=l<=r<len(xs); decrementing r stops at r<=l.
      if(spec.id=="subregion_sum")return call("index(IntList,Int)->Int",{bound("xs"),bound("j")});
      auto index=call("max(Int,Int)->Int",{call(spec.id=="house_robber" ? "sub(Int,Int)->Int" : "add(Int,Int)->Int",{bound(spec.dp.value_at==asgp::ValueAt::kStateB ? "j" : "n"),constant(spec.id=="house_robber" ? 1 : spec.dp.value_offset)}),constant(0)});
      if(spec.id!="house_robber")index=call("min(Int,Int)->Int",{call("sub(Int,Int)->Int",{call("len(IntList)->Int",{bound("xs")}),constant(1)}),index});
      return call("index(IntList,Int)->Int",{bound("xs"),index});
    }
    case K::kResult:
      if(!dp && node.op>1)throw std::runtime_error("unsupported result slot");
      return bound(dp ? "d"+std::to_string(node.op+1) : (node.op ? "right" : "left"));
    case K::kHeadIndex: return bound("lo");
    case K::kHead: return call("index(IntList,Int)->Int", {args.at(0), constant(0)});
    case K::kIndex:
      if(guarded_index)return call("if(Bool,Int,Int)->Int",{call("lt(Int,Int)->Bool",{args.at(0),constant(0)}),call("index(IntList,Int)->Int",{bound("xs"),call("len(IntList)->Int",{bound("xs")})}),call("index(IntList,Int)->Int",{bound("xs"),args.at(0)})});
      return call("index(IntList,Int)->Int", {bound("xs"), args.at(0)});
    case K::kMakeTuple:
      return call("concat(IntList,IntList)->IntList",{call("singleton(Int)->IntList",{call("if(Bool,Int,Int)->Int",{args[0],constant(1),constant(0)})}),call("concat(IntList,IntList)->IntList",{call("singleton(Int)->IntList",{args[1]}),call("singleton(Int)->IntList",{args[2]})})});
    case K::kTupleGet0:return call("ne(Int,Int)->Bool",{call("index(IntList,Int)->Int",{args[0],constant(0)}),constant(0)});
    case K::kTupleGet1:case K::kTupleGet2:return call("index(IntList,Int)->Int",{args[0],constant(node.kind==K::kTupleGet1?1:2)});
    case K::kNot: return call("not(Bool)->Bool", args);
    case K::kIntOp: {
      const char* ops[] = {"add", "sub", "mul", "idiv0", "imod0"};
      if (node.op >= 5) throw std::runtime_error("invalid arithmetic operator");
      if(guarded_index && (node.op==3 || node.op==4)) {
        const auto lhs_name="source_lhs"+std::to_string(source_node_index),rhs_name="source_rhs"+std::to_string(source_node_index);
        const auto a=bound(lhs_name),b=bound(rhs_name);
        Json result;
        if(node.op==4)result=call("if(Bool,Int,Int)->Int",{call("eq(Int,Int)->Bool",{b,constant(0)}),constant(0),call("sub(Int,Int)->Int",{a,call("mul(Int,Int)->Int",{call("idiv0(Int,Int)->Int",{a,b}),b})})});
        else {
          auto overflow=call("if(Bool,Bool,Bool)->Bool",{call("eq(Int,Int)->Bool",{a,constant(INT64_MIN)}),call("eq(Int,Int)->Bool",{b,constant(-1)}),bool_constant(false)});
          result=call("if(Bool,Int,Int)->Int",{overflow,constant(0),call("idiv0(Int,Int)->Int",{a,b})});
        }
        auto inner=call("let(Int,Int)->Int",{args[1],result});inner.object_v["bind"]=object({{"1",array({string(rhs_name)})}});
        auto outer=call("let(Int,Int)->Int",{args[0],inner});outer.object_v["bind"]=object({{"1",array({string(lhs_name)})}});
        return outer;
      }
      return call(std::string(ops[node.op])+"(Int,Int)->Int", args);
    }
    case K::kBoolOp: {
      const char* ops[] = {"lt", "eq", "gt", "le", "ge", "ne"};
      if (node.op >= 6) throw std::runtime_error("invalid comparison operator");
      return call(std::string(ops[node.op])+"(Int,Int)->Bool", args);
    }
    case K::kLogicOp:
      return node.op == 0 ? call("if(Bool,Bool,Bool)->Bool", {args[0], args[1], bool_constant(false)})
                          : call("if(Bool,Bool,Bool)->Bool", {args[0], bool_constant(true), args[1]});
    case K::kIf:
      return call(asgp::IsListType(node.type) ? "if(Bool,IntList,IntList)->IntList"
                                                  : "if(Bool,Int,Int)->Int", args);
    default: throw std::runtime_error("unmapped ASGP node " + std::to_string(int(node.kind)));
  }
}
}  // namespace

Json encode(const asgp::Individual& individual, int phases) {
  std::vector<Json> out;
  for (int p=0; p<phases; ++p) {
    std::vector<Json> nodes;
    for (const auto& n : individual.phases[p])
      nodes.push_back(array({number(int(n.kind)), number(int(n.type)), number(n.op), number(n.size), string(std::to_string(n.value))}));
    out.push_back(array(std::move(nodes)));
  }
  return array(std::move(out));
}
asgp::Individual decode(const Json& value) {
  asgp::Individual out;
  if (value.array_v.size() > out.phases.size()) throw std::runtime_error("too many phases");
  for (std::size_t p=0; p<value.array_v.size(); ++p) {
    for (const auto& row : value.array_v[p].array_v) {
      const auto& v = row.array_v;
      asgp::Node n{};
      n.kind=static_cast<asgp::NodeKind>(int(v.at(0).number_v));
      n.type=static_cast<asgp::Type>(int(v.at(1).number_v));
      n.op=static_cast<std::uint8_t>(v.at(2).number_v);
      n.size=static_cast<std::uint32_t>(v.at(3).number_v);
      n.value=std::stoll(v.at(4).string_v);
      out.phases[p].push_back(n);
    }
  }
  return out;
}
asgp::TaskSpec task(const std::string& name) {
  asgp::DataConfig cfg; cfg.train=1024; cfg.test=1; cfg.seed=20260626; cfg.legacy_finite=false;
  auto built = asgp::BuildTask(name, cfg);
  if (!built) throw std::runtime_error("unknown ASGP task " + name);
  auto spec = *built;
  // Explicit study policy; historical tasks and default ASGP scoring stay intact.
  if (std::getenv("GAGP_BM_COMMON_FITNESS") && name == "last_index_of_zero")
    spec.proximity = asgp::Proximity::kRewarded;
  if (std::getenv("GAGP_BM_COMMON_FITNESS") && name == "negative_to_zero")
    spec.metric = asgp::Metric::kExact;
  return spec;
}
Json task_contract(const std::string& name) {
  const auto spec=task(name);
  const auto grammar=asgp::SchemeGrammar::Build(spec.Grammar());
  const auto ranges=[](const auto& pool) { std::vector<Json> out; for(const auto& r:pool) out.push_back(array({string(std::to_string(r.low)),string(std::to_string(r.high))})); return array(std::move(out)); };
  std::vector<Json> phases,scalars,deps;
  for(const auto& s:spec.scalars)scalars.push_back(string(s));
  for(const auto& d:spec.dp.deps)deps.push_back(array({number(d.a),number(d.b)}));
  for(int p=0;p<grammar.count;++p) {
    std::vector<Json> productions;
    for(std::size_t i=0;i<grammar[p].size();++i) {
      const auto& prod=grammar[p][i]; std::vector<Json> children;
      for(int k=0;k<prod.arity;++k)children.push_back(string(std::string(asgp::Name(prod.child[k]))));
      productions.push_back(object({{"kind",number(int(prod.kind))},{"op",number(prod.op)},
        {"type",string(std::string(asgp::Name(prod.result)))},{"children",array(std::move(children))}}));
    }
    phases.push_back(object({{"start",string(std::string(asgp::Name(grammar[p].start())))},{"productions",array(std::move(productions))}}));
  }
  return object({{"format",string("asgp-task-contract-v1")},{"task",string(spec.id)},
    {"scheme",string(std::string(asgp::Name(spec.scheme)))},{"carrier",string(std::string(asgp::Name(spec.carrier)))},
    {"output",string(std::string(asgp::Name(spec.output)))},{"list_type",string(std::string(asgp::Name(spec.list_type)))},
    {"summary_cap",number(spec.summary_cap)},{"summary_weight",number(spec.summary_weight)},{"summaries",number(spec.summaries)},
    {"metric",number(int(spec.metric))},{"proximity",number(int(spec.proximity))},{"projection",number(int(spec.projection))},
    {"base_size",number(spec.base_size)},{"positional",number(spec.positional)}, {"sequence_input",number(spec.sequence_input)},
    {"scalars",array(std::move(scalars))},{"ints",ranges(spec.consts.ints)},{"chars",ranges(spec.consts.chars)},
    {"phases",array(std::move(phases))},{"dp_dimensions",number(spec.dp.dimensions)},
    {"dp_deps",array(std::move(deps))},{"dp_value_at",number(int(spec.dp.value_at))},{"dp_value_offset",number(spec.dp.value_offset)},
    {"prefix_window",number(spec.dp.prefix_window)},{"prefix_base_size",number(spec.dp.prefix_base_size)},
    {"source_max_phase_depth",number(7)},{"native_max_nodes",number(1024)},{"native_max_depth",number(48)}});
}

Json translated_root(const asgp::Individual& individual, const std::string& name, bool guarded_index) {
  static std::map<std::string,asgp::TaskSpec> specs;
  auto it=specs.find(name);if(it==specs.end())it=specs.emplace(name,task(name)).first;
  const auto& spec=it->second;
  const bool dp = spec.scheme == asgp::Scheme::kDynamicProgramming;
  Json holes=object({});
  if (!dp) holes.object_v["source"]=object({{"input", string("source")}});
  const std::vector<std::string> names = dp ? std::vector<std::string>{"solve", "transition"}
                                           : std::vector<std::string>{"solve", "divide", "combine"};
  for (std::size_t p=0; p<names.size(); ++p) {
    std::size_t at=0;
    holes.object_v[names[p]]=expression(individual.phases[p], at, dp, spec, p==0?"Solve":dp?"Transition":p==1?"Divide":"Combine", guarded_index);
    if (at != individual.phases[p].size()) throw std::runtime_error("trailing ASGP nodes");
  }
  return object({{"template", string(dp ? (name=="house_robber" ? "Fixed.HouseRobber" : "Fixed.DP."+name) :
      (asgp::IsListType(spec.carrier) || spec.carrier==asgp::Type::kTuple) ? "Package.DC.IntList.IntList" : "Package.DC.IntList.Int")}, {"holes", holes}});
}
gagp::evo::ProgramGenome translate(const asgp::Individual& individual, const std::string& name,
                                  const gg::ResolvedDefinition& definition,
                                  const gg::CompiledGrammar& grammar) {
  auto doc=definition.document;
  const auto& nonterminals=doc.object_v.at("nonterminals").array_v;
  auto root=*std::find_if(nonterminals.begin(), nonterminals.end(), [](const auto& row) {
    return row.object_v.at("id").string_v == "Root";
  });
  bool guarded_index=false;
  for(const auto& t:doc.object_v.at("templates").array_v)if(t.object_v.at("id").string_v=="Fixed.Solve.Index" || t.object_v.at("id").string_v=="Fixed.Solve.ProtectedDiv")guarded_index=true;
  root.object_v.at("alternatives").array_v.front().object_v["expression"]=translated_root(individual, name, guarded_index);
  doc.object_v["nonterminals"]=array({root});
  const auto exact=gg::compile_grammar(gg::parse_definition(gg::canonical_json(doc)));
  auto genome=gg::generate_derivation(exact, 0).genome;
  // Transport has 128 constant slots. Repeated source literals share a slot.
  std::vector<gagp::Value> unique;
  std::vector<int> remap;
  for (const auto v : genome.ast.consts) {
    const auto it=std::find_if(unique.begin(), unique.end(), [&](const auto u) {
      return u.tag == v.tag && (v.tag == gagp::ValueTag::Bool ? u.b == v.b : u.i == v.i);
    });
    remap.push_back(static_cast<int>(it-unique.begin()));
    if (it == unique.end()) unique.push_back(v);
  }
  for (auto& n : genome.ast.nodes)
    if (n.kind == gagp::evo::NodeKind::CONST) n.i0=remap.at(n.i0);
  genome.ast.consts=std::move(unique);
  genome.meta=gagp::evo::build_genome_meta(genome.ast);
  genome.derivation=std::make_shared<gg::DerivationMetadata>(gg::reconstruct_derivation(grammar, genome));
  return genome;
}

asgp::Individual solution(const std::string& name) {
  using K=asgp::NodeKind; using T=asgp::Type; using E=asgp::Expr;
  const auto expr=[](K kind, T type, int op, std::int64_t value, std::vector<E> args=std::vector<E>{}) {
    asgp::Node n{}; n.kind=kind; n.type=type; n.op=op; n.value=value;
    E result{n};
    for (const auto& a : args) result.insert(result.end(), a.begin(), a.end());
    result[0].size=result.size(); return result;
  };
  const auto lit=[&](int n) { return expr(K::kIntConst, T::kInt, 0, n); };
  const auto result=[&](int i) { return expr(K::kResult, T::kInt, i, 0); };
  const auto add=[&](E a, E b) { return expr(K::kIntOp, T::kInt, 0, 0, {a,b}); };
  const auto lt=[&](E a, E b) { return expr(K::kBoolOp, T::kBool, 0, 0, {a,b}); };
  const auto choose=[&](E c, E a, E b) { return expr(K::kIf, T::kInt, 0, 0, {c,a,b}); };
  const auto index=[&](int i) { return expr(K::kIndex, T::kInt, 0, 0, {lit(i)}); };
  const auto arith=[&](int op,E a,E b){return expr(K::kIntOp,T::kInt,op,0,{a,b});};
  const auto eq=[&](E a,E b){return expr(K::kBoolOp,T::kBool,1,0,{a,b});};
  const auto scalar=[&](int slot){return expr(K::kInputScalar,T::kInt,slot,0);};
  const auto chr=[&](int c){return expr(K::kCharConst,T::kChar,0,c);};
  const auto typed_if=[&](T t,E c,E a,E b){return expr(K::kIf,t,0,0,{c,a,b});};
  const auto single=[&](E e,T t){return expr(K::kSingleton,t,0,0,{e});};
  const auto concat_results=[&](T t){return expr(K::kConcat,t,0,0,{expr(K::kResult,t,0,0),expr(K::kResult,t,1,0)});};
  auto origin=expr(K::kHeadIndex,T::kInt,0,0,{expr(K::kInputList,T::kIntList,0,0)});
  asgp::Individual out;
  if(name=="stock_with_cooldown") {
    auto h=expr(K::kStateB,T::kInt,0,0),v=expr(K::kStateValue,T::kInt,0,0);
    const auto maximum=[&](E a,E b){return choose(lt(a,b),b,a);};
    out.phases[0]=lit(0);
    out.phases[1]=choose(eq(h,lit(0)),maximum(result(0),arith(1,result(1),v)),maximum(result(0),add(result(2),v)));
  } else if(name=="fibonacci" || name=="tribonacci" || name=="fibonacci_array" || name=="subregion_sum") {
    auto n=expr(K::kStateA,T::kInt,0,0);
    out.phases[0]=name=="fibonacci" ? n : name=="tribonacci" ? choose(lt(n,lit(1)),lit(0),lit(1)) : name=="subregion_sum" ? expr(K::kStateValue,T::kInt,0,0) : lit(1);
    out.phases[1]=name=="subregion_sum" ? add(result(0),expr(K::kStateValue,T::kInt,0,0)) : add(result(0),result(1));
    if(name=="tribonacci")out.phases[1]=add(out.phases[1],result(2));
  } else if (name == "house_robber") {
    auto n=expr(K::kStateA,T::kInt,0,0), v=expr(K::kStateValue,T::kInt,0,0);
    out.phases[0]=choose(lt(n,lit(1)),lit(0),v);
    auto sum=add(result(1),v);
    out.phases[1]=choose(lt(result(0),sum),sum,result(0));
  } else {
    out.phases[1]=lit(1); out.phases[2]=add(result(0),result(1));
    if (name == "negative_to_zero") {
      out.phases[1]=lit(0);
      out.phases[0]=expr(K::kSingleton,T::kIntList,0,0,{choose(lt(index(0),lit(0)),lit(0),index(0))});
      out.phases[2]=expr(K::kConcat,T::kIntList,0,0,
          {expr(K::kResult,T::kIntList,0,0),expr(K::kResult,T::kIntList,1,0)});
    } else if (name == "sum_of_elements") out.phases[0]=index(0);
    else if (name == "count_odds") {
      auto rem=expr(K::kIntOp,T::kInt,4,0,{index(0),lit(2)});
      auto odd=expr(K::kBoolOp,T::kBool,5,0,{rem,lit(0)});
      out.phases[0]=choose(odd,lit(1),lit(0));
    } else if (name == "last_index_of_zero") {
      auto zero=expr(K::kBoolOp,T::kBool,1,0,{index(0),lit(0)});
      auto origin=expr(K::kHeadIndex,T::kInt,0,0,{expr(K::kInputList,T::kIntList,0,0)});
      out.phases[0]=choose(zero,origin,lit(0));
      out.phases[1]=lit(0);
      out.phases[2]=choose(lt(result(0),result(1)),result(1),result(0));
    } else if(name=="is_sorted" || name=="all_equal") {
      const auto get=[&](int field,int slot){return expr(field==0?K::kTupleGet0:field==1?K::kTupleGet1:K::kTupleGet2,field==0?T::kBool:T::kInt,0,0,{expr(K::kResult,T::kTuple,slot,0)});};
      auto min=choose(lt(get(1,0),get(1,1)),get(1,0),get(1,1));
      auto max=choose(lt(get(2,0),get(2,1)),get(2,1),get(2,0));
      auto condition=name=="all_equal" ? eq(min,max) : expr(K::kLogicOp,T::kBool,0,0,{expr(K::kLogicOp,T::kBool,0,0,{get(0,0),get(0,1)}),expr(K::kBoolOp,T::kBool,3,0,{get(2,0),get(1,1)})});
      out.phases[0]=expr(K::kMakeTuple,T::kTuple,0,0,{expr(K::kBoolConst,T::kBool,0,1),index(0),index(0)});
      out.phases[2]=expr(K::kMakeTuple,T::kTuple,0,0,{condition,min,max});
    } else if (name == "min" || name == "smallest") {
      const auto minimum=[&](E a,E b){return choose(lt(a,b),a,b);};
      out.phases[0]=name=="min" ? index(0) : minimum(minimum(index(0),index(1)),minimum(index(2),index(3)));
      out.phases[2]=minimum(result(0),result(1));
    } else if(name=="mpi") {
      out.phases[0]=choose(lt(lit(0),index(0)),index(0),lit(0));
      out.phases[2]=choose(eq(result(0),lit(0)),result(1),choose(eq(result(1),lit(0)),result(0),choose(lt(result(0),result(1)),result(0),result(1))));
    } else if(name=="index_of_element") {
      out.phases[0]=choose(eq(index(0),scalar(0)),origin,arith(1,lit(0),lit(1)));
      out.phases[2]=choose(lt(result(0),lit(0)),result(1),result(0));
    } else if(name=="sum_of_squares") {
      out.phases[0]=arith(2,index(0),index(0));
    } else if(name=="fuel_cost") {
      out.phases[0]=arith(1,arith(3,index(0),lit(3)),choose(eq(origin,lit(0)),lit(0),lit(2)));
    } else if(name=="luhn") {
      auto doubled=arith(2,lit(2),index(0));
      auto offset=arith(1,arith(1,scalar(0),lit(1)),origin);
      out.phases[0]=choose(eq(arith(4,offset,lit(2)),lit(0)),choose(lt(lit(9),doubled),arith(1,doubled,lit(9)),doubled),index(0));
    } else if(name=="filter_odd" || name=="for_loop_index" || name=="even_squares") {
      E condition, value=index(0);
      if(name=="filter_odd")condition=expr(K::kBoolOp,T::kBool,5,0,{arith(4,value,lit(2)),lit(0)});
      else if(name=="for_loop_index")condition=eq(arith(4,arith(1,value,scalar(0)),scalar(2)),lit(0));
      else { value=arith(2,index(0),index(0));condition=expr(K::kLogicOp,T::kBool,0,0,{eq(arith(4,index(0),lit(2)),lit(0)),lt(value,scalar(0))}); }
      out.phases[0]=typed_if(T::kIntList,condition,single(value,T::kIntList),expr(K::kEmptyList,T::kIntList,0,0));
      out.phases[2]=concat_results(T::kIntList);
    } else if(name=="replace_space_with_newline" || name=="double_letters") {
      auto h=expr(K::kHead,T::kChar,0,0,{expr(K::kInputList,T::kCharList,0,0)});
      auto ceq=[&](int c){return expr(K::kEqChar,T::kBool,0,0,{h,chr(c)});};
      if(name=="replace_space_with_newline")out.phases[0]=single(typed_if(T::kChar,ceq(' '),chr('\n'),h),T::kCharList);
      else {
        auto one=single(h,T::kCharList),two=expr(K::kConcat,T::kCharList,0,0,{one,one});
        auto three=expr(K::kConcat,T::kCharList,0,0,{one,two});
        out.phases[0]=typed_if(T::kCharList,ceq('!'),three,typed_if(T::kCharList,expr(K::kIsLetter,T::kBool,0,0,{h}),two,one));
      }
      out.phases[2]=concat_results(T::kCharList);
    } else if(name=="grade") {
      auto at=index(4);
      auto ge=[&](int i){return expr(K::kBoolOp,T::kBool,4,0,{at,index(i)});};
      out.phases[0]=single(typed_if(T::kChar,ge(1),typed_if(T::kChar,ge(0),chr('A'),chr('B')),typed_if(T::kChar,ge(2),chr('C'),typed_if(T::kChar,ge(3),chr('D'),chr('F')))),T::kCharList);
      out.phases[2]=concat_results(T::kCharList);
    } else if(name=="small_or_large") {
      out.phases[1]=lit(0); // This task has no literal 1 in its source pool.
      const auto word=[&](const auto& self,const std::string& text)->E {
        if(text.size()==1)return single(chr(text[0]),T::kCharList);
        auto mid=text.size()/2;return expr(K::kConcat,T::kCharList,0,0,{self(self,text.substr(0,mid)),self(self,text.substr(mid))});
      };
      out.phases[0]=typed_if(T::kCharList,lt(index(0),lit(1000)),word(word,"small"),word(word,"large"));out.phases[2]=concat_results(T::kCharList);
    } else if(name=="syllables" || name=="scrabble_score") {
      auto h=expr(K::kHead,T::kChar,0,0,{expr(K::kInputList,T::kCharList,0,0)});
      const auto ceq=[&](char c){return expr(K::kEqChar,T::kBool,0,0,{h,chr(c)});};
      const auto any=[&](const auto& self,const std::string& text)->E {
        if(text.size()==1)return ceq(text[0]); auto mid=text.size()/2;
        return expr(K::kLogicOp,T::kBool,1,0,{self(self,text.substr(0,mid)),self(self,text.substr(mid))});
      };
      if(name=="syllables")out.phases[0]=choose(any(any,"aeiouy"),lit(1),lit(0));
      else {
        // Witness only; this is never seeded into an evolution population.
        static const std::pair<const char*,int> groups[]={{"aeilnorstuAEILNORSTU",1},{"dgDG",2},{"bcmpBCMP",3},{"fhvwyFHVWY",4},{"kK",5},{"jxJX",8},{"qzQZ",10}};
        E sum=lit(0);for(const auto& group:groups)sum=add(sum,choose(any(any,group.first),lit(group.second),lit(0)));out.phases[0]=sum;
      }
    } else if (name == "median") {
      auto a=index(0), b=index(1), c=index(2);
      out.phases[0]=choose(lt(a,b),choose(lt(b,c),b,choose(lt(a,c),c,a)),
                                   choose(lt(a,c),a,choose(lt(b,c),c,b)));
    } else throw std::runtime_error("no known solution registered for " + name);
  }
  return out;
}
}  // namespace fixed_asgp
