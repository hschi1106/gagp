#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>

#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/grammar/random.hpp"
#include "gagp/evolution/grammar/values.hpp"
#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/evolution/mutation.hpp"
#include "gagp/runtime/payload/payload.hpp"
#include "../../src/evolution/repro/constant_prep.hpp"

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;
namespace {
using Json = cli_detail::JsonValue;
Json json(const std::string& text) { return cli_detail::JsonParser(text, {true, 256}).parse(); }
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Action> void rejects(Action action) {
  try { action(); } catch (const std::invalid_argument&) { return; }
  throw std::runtime_error("invalid domain or nonmember was accepted");
}
const std::string strings = R"({"type":"String","sequence":{"length":[0,2],"element":{"type":"Char","values":["a","b"]}}})";
std::shared_ptr<const CompiledGrammar> compile(const std::string& domain) {
  const auto type = json(domain).object_v.at("type").string_v;
  return std::make_shared<const CompiledGrammar>(compile_grammar(parse_definition(
      R"({"format_version":"grammar-definition-v2","entry":{"nonterminal":"Value","type":")" + type +
      R"("},"search_limits":{"max_nodes":5,"max_depth":4},"execution_limits":{"fuel":10},"nonterminals":[{"id":"Value","type":")" + type +
      R"(","scope":[],"alternatives":[{"id":"value","weight":1,"expression":{"constant":)" + domain + "}}]}]}")));
}
Value value(const ProgramGenome& genome) { return genome.ast.consts.at(genome.ast.nodes.at(3).i0); }
std::string encoding(const Value& v) { return canonical_json(encode_constant(v)); }
void test_domains() {
  const auto domain = parse_constant_domain(json(strings));
  GrammarRandom rng(99);
  std::set<std::string> seen;
  for (unsigned i = 0; i < 512; ++i) {
    const auto v = sample_constant(domain, rng);
    check(constant_domain_contains(domain, v), "sample escaped String domain");
    std::string s; check(payload::lookup_string(v, &s), "missing sampled String"); seen.insert(s);
  }
  check(seen == std::set<std::string>{"", "a", "b", "aa", "ab", "ba", "bb"}, "bounded String support was narrowed");
  for (const auto& text : {"aaa", "c", "ac", "a\xff"})
    check(!constant_domain_contains(domain, payload::make_string_value(text)), "String nonmember accepted");
  const auto unicode = parse_constant_domain(json(R"({"type":"String","sequence":{"length":[1,1],"element":{"type":"Char","values":["\ud83d\ude00","\u0000"]}}})"));
  check(constant_domain_contains(unicode, payload::make_string_value("\xf0\x9f\x98\x80")), "length counted UTF-8 bytes instead of scalars");
  check(constant_domain_contains(unicode, payload::make_string_value(std::string(1, '\0'))), "NUL scalar rejected");
  check(!constant_domain_contains(unicode, payload::make_string_value("")), "minimum length ignored");
  const auto floats = parse_constant_domain(json(R"({"type":"FloatList","sequence":{"length":[1,1],"element":{"type":"Float","values":[-0.0]}}})"));
  check(constant_domain_contains(floats, payload::make_float_list_value({Value::from_float(-0.0)})), "negative zero rejected");
  check(!constant_domain_contains(floats, payload::make_float_list_value({Value::from_float(0.0)})), "signed-zero domain distinction lost");
  const auto ints = parse_constant_domain(json(R"({"type":"IntList","sequence":{"length":[0,2],"element":{"type":"Int","range":["-2","2"]}}})"));
  check(!constant_domain_contains(ints, payload::make_int_list_value({Value::from_int(3)})), "element range ignored");
  check(!constant_domain_contains(ints, payload::make_int_list_value({Value::from_int(0),Value::from_int(0),Value::from_int(0)})), "list length ignored");
  const auto lists = parse_constant_domain(json(R"({"type":"StringList","sequence":{"length":[1,1],"element":)" + strings + "}}"));
  check(!constant_domain_contains(lists, payload::make_string_list_value({payload::make_string_value("aaa")})), "nested String length ignored");
  auto fixed = json(compile(strings)->canonical_definition());
  fixed.object_v["templates"] = json(R"([{"id":"Fixed","type":"String","scope":[],"holes":[],"body":{"constant":)" + strings + "}}]");
  fixed.object_v.at("nonterminals").array_v.front().object_v.at("alternatives").array_v.front().object_v.at("expression") =
      json(R"({"template":"Fixed","holes":{}})");
  rejects([&] { (void)compile_grammar(parse_definition(canonical_json(fixed))); });
  rejects([&] { (void)decode_constant(json(strings)); });
  for (const auto& text : {
      R"({"type":"Int","sequence":{"length":[0,1],"element":{"type":"Int","values":["1"]}}})",
      R"({"type":"String","sequence":{"length":[-1,2],"element":{"type":"Char","values":["a"]}}})",
      R"({"type":"String","sequence":{"length":[0,1.5],"element":{"type":"Char","values":["a"]}}})",
      R"({"type":"String","sequence":{"length":[2,1],"element":{"type":"Char","values":["a"]}}})",
      R"({"type":"String","sequence":{"length":[0,65537],"element":{"type":"Char","values":["a"]}}})",
      R"({"type":"String","sequence":{"length":[0,1],"element":{"type":"Int","values":["1"]}}})",
      R"({"type":"String","sequence":{"length":[0,1],"element":{"type":"Char","values":[]}}})",
      R"({"type":"String","sequence":{"length":[0,1],"element":{"type":"Char","values":["a"]},"typo":0}})",
      R"({"type":"String","values":["a"],"sequence":{"length":[0,1],"element":{"type":"Char","values":["a"]}}})",
      R"({"type":"IntList","sequence":{"length":[0,1],"element":{"type":"IntList","values":[[]]}}})",
      R"({"type":"StringList","sequence":{"length":[65536,65536],"element":{"type":"String","sequence":{"length":[65536,65536],"element":{"type":"Char","values":["a"]}}}}})"})
    rejects([&] { (void)parse_constant_domain(json(text)); });
}
void test_generation_variation_and_proposals() {
  const std::vector<std::string> domains{strings,
      R"({"type":"IntList","sequence":{"length":[0,5],"element":{"type":"Int","range":["-2","2"]}}})",
      R"({"type":"FloatList","sequence":{"length":[0,5],"element":{"type":"Float","values":[-1.25,0,2.5]}}})",
      R"({"type":"FloatList","sequence":{"length":[0,5],"element":{"type":"Float","range":[-5,5]}}})",
      R"({"type":"FloatList","sequence":{"length":[0,5],"element":{"type":"Float","range":[-8,8],"quantization_scale":1000}}})",
      R"({"type":"FloatList","sequence":{"length":[0,5],"element":{"type":"Float","range":[-100,100],"sample_from":{"type":"Float","range":[-8,8],"quantization_scale":1000}}}})",
      R"({"type":"StringList","sequence":{"length":[0,5],"element":)" + strings + "}}"};
  for (const auto& text : domains) {
    const auto grammar = compile(text);
    const auto& domain = grammar->constants().front();
    for (double subtree : {0.0, 1.0}) {
      VariationContext context(grammar);
      auto parent = generate_derivation(*grammar, 42).genome;
      const auto replay = generate_derivation(*grammar, 42).genome;
      check(encoding(value(parent)) == encoding(value(replay)), "sequence seed replay changed");
      for (unsigned seed = 0; seed < 64; ++seed) {
        parent = mutate(parent, seed, context, subtree);
        require_membership(*grammar, parent);
        check(constant_domain_contains(domain, value(parent)), "sequence mutation escaped its domain");
        check(encoding(decode_constant(encode_constant(value(parent)))) == encoding(value(parent)), "concrete payload artifact changed");
      }
      check(context.counters().changed_children > 0 && context.counters().acceptance_rejections == 0,
          "sequence mutation fell back or never changed");
    }
    const auto base = repro::prepare_constant_mutation_domains(grammar);
    const auto first = repro::sample_constant_mutation_domains(base, 1, 64);
    const auto replay = repro::sample_constant_mutation_domains(base, 1, 64);
    const auto next = repro::sample_constant_mutation_domains(base, 2, 64);
    bool changed = false;
    check(base->values.empty() && first->base_domains == base, "proposals polluted run-level domain cache");
    for (std::size_t i = 0; i < first->values.size(); ++i) {
      check(constant_domain_contains(domain, first->values[i]), "GPU proposal escaped sequence domain");
      if (domain.elements && domain.elements->sampling) {
        std::vector<Value> elements;
        check(payload::lookup_list(first->values[i],&elements), "sample_from proposal lacks exact payload");
        for (const auto& element : elements)
          check(constant_domain_contains(*domain.elements->sampling,element), "sequence proposal ignored sample_from");
      }
      check(encoding(first->values[i]) == encoding(replay->values[i]), "GPU proposal seed replay changed");
      changed |= encoding(first->values[i]) != encoding(next->values[i]);
    }
    check(changed && first->values.size() == 64, "GPU proposals were frozen across preparations");
    rejects([&] { repro::sample_constant_mutation_domains(first, 2, 64); });
    auto forged = generate_derivation(*grammar, 4).genome;
    forged.derivation.reset();
    forged.ast.consts.at(forged.ast.nodes[3].i0) = domain.type == RType::String
        ? payload::make_string_value("outside") : Value::from_int(0);
    rejects([&] { require_membership(*grammar, forged); });
  }
}
} // namespace
int main() {
  try { test_domains(); test_generation_variation_and_proposals(); }
  catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
  std::cout << "sequence constants: full support, typed boundaries, replay and variation passed\n";
}
