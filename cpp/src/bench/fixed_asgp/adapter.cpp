#include "adapter.hpp"

#include <algorithm>
#include <fstream>
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
Json expression(const asgp::Expr& expr, std::size_t& at, bool dp) {
  using K = asgp::NodeKind;
  const auto node = expr.at(at++);
  std::vector<Json> args;
  for (int k=0; k<asgp::Arity(node.kind); ++k) args.push_back(expression(expr, at, dp));
  switch (node.kind) {
    case K::kIntConst: return constant(node.value);
    case K::kBoolConst: return bool_constant(node.value);
    case K::kInputList: return bound("xs");
    case K::kInputSize: case K::kStateA: return bound("n");
    case K::kStateValue:
      // The fixed DP domain already guarantees n <= len(xs) <= 20.
      return call("index(IntList,Int)->Int", {bound("xs"), call("max(Int,Int)->Int", {
          call("sub(Int,Int)->Int", {bound("n"), constant(1)}), constant(0)})});
    case K::kResult:
      if (node.op > 1) throw std::runtime_error("unsupported result slot");
      return bound(dp ? (node.op ? "d2" : "d1") : (node.op ? "right" : "left"));
    case K::kHead: return call("index(IntList,Int)->Int", {args.at(0), constant(0)});
    case K::kIndex: return call("index(IntList,Int)->Int", {bound("xs"), args.at(0)});
    case K::kNot: return call("not(Bool)->Bool", args);
    case K::kIntOp: {
      const char* ops[] = {"add", "sub", "mul", "idiv0", "imod0"};
      if (node.op >= 5) throw std::runtime_error("invalid arithmetic operator");
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
    case K::kIf: return call("if(Bool,Int,Int)->Int", args);
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
  if (name != "sum_of_elements" && name != "median" && name != "house_robber")
    throw std::runtime_error("unknown fixed task " + name);
  asgp::DataConfig cfg; cfg.train=1024; cfg.test=1; cfg.seed=20260626; cfg.legacy_finite=false;
  return *asgp::BuildTask(name, cfg);
}
Json translated_root(const asgp::Individual& individual, const std::string& name) {
  const bool dp = name == "house_robber";
  Json holes=object({});
  if (!dp) holes.object_v["source"]=object({{"input", string("source")}});
  const std::vector<std::string> names = dp ? std::vector<std::string>{"solve", "transition"}
                                           : std::vector<std::string>{"solve", "divide", "combine"};
  for (std::size_t p=0; p<names.size(); ++p) {
    std::size_t at=0;
    holes.object_v[names[p]]=expression(individual.phases[p], at, dp);
    if (at != individual.phases[p].size()) throw std::runtime_error("trailing ASGP nodes");
  }
  return object({{"template", string(dp ? "Fixed.HouseRobber" : "Package.DC.IntList.Int")}, {"holes", holes}});
}
gagp::evo::ProgramGenome translate(const asgp::Individual& individual, const std::string& name,
                                  const gg::ResolvedDefinition& definition,
                                  const gg::CompiledGrammar& grammar) {
  auto doc=definition.document;
  const auto& nonterminals=doc.object_v.at("nonterminals").array_v;
  auto root=*std::find_if(nonterminals.begin(), nonterminals.end(), [](const auto& row) {
    return row.object_v.at("id").string_v == "Root";
  });
  root.object_v.at("alternatives").array_v.front().object_v["expression"]=translated_root(individual, name);
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
  asgp::Individual out;
  if (name == "house_robber") {
    auto n=expr(K::kStateA,T::kInt,0,0), v=expr(K::kStateValue,T::kInt,0,0);
    out.phases[0]=choose(lt(n,lit(1)),lit(0),v);
    auto sum=add(result(1),v);
    out.phases[1]=choose(lt(result(0),sum),sum,result(0));
  } else {
    out.phases[1]=lit(1); out.phases[2]=add(result(0),result(1));
    if (name == "sum_of_elements") out.phases[0]=index(0);
    else {
      auto a=index(0), b=index(1), c=index(2);
      out.phases[0]=choose(lt(a,b),choose(lt(b,c),b,choose(lt(a,c),c,a)),
                                   choose(lt(a,c),a,choose(lt(b,c),c,b)));
    }
  }
  return out;
}
}  // namespace fixed_asgp
