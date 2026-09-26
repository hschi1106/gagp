#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>

#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/grammar/numeric_sampling.hpp"
#include "gagp/evolution/grammar/random.hpp"
#include "gagp/evolution/grammar/values.hpp"
#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/evolution/mutation.hpp"
#include "../../src/evolution/repro/constant_prep.hpp"

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Action> void rejects(Action action) {
  try { action(); } catch (const std::invalid_argument&) { return; }
  throw std::runtime_error("invalid numeric domain accepted");
}
cli_detail::JsonValue json(const std::string& text) { return cli_detail::JsonParser(text, {true, 256}).parse(); }
void test_intervals() {
  check(sample_float_interval(-5, 5, 0) == -5, "lower sampling endpoint");
  check(sample_float_interval(-5, 5, UINT64_C(1) << 63) == 0, "known midpoint");
  check(sample_float_interval(0, 1, UINT64_MAX) == 1 - 0x1.0p-53, "53-bit unit interval");
  check(std::signbit(sample_float_interval(-0.0, 0.0, UINT64_MAX)), "singleton signed zero");
  const double hi = std::numeric_limits<double>::max();
  for (const auto bounds : std::vector<std::pair<double,double>>{{-hi,hi}, {-5,5}, {-hi,-hi/2},
                            {hi/2,hi}, {0,std::numeric_limits<double>::denorm_min()},
                            {1,std::nextafter(1.0,2.0)}}) {
    GrammarRandom random(13);
    for (unsigned i = 0; i < 4096; ++i) {
      const double value = sample_float_interval(bounds.first,bounds.second,random.next());
      check(std::isfinite(value) && value >= bounds.first && value < bounds.second,
            "finite half-open interval sampling escaped bounds");
    }
  }
  auto domain = parse_constant_domain(json(R"({"type":"Float","range":[-5,5]})"));
  check(constant_domain_contains(domain, Value::from_float(-5)) &&
        constant_domain_contains(domain, Value::from_float(5)), "inclusive membership endpoints");
  for (double value : {std::nextafter(5.0,6.0), std::nextafter(-5.0,-6.0),
                       std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    check(!constant_domain_contains(domain,Value::from_float(value)), "nonmember admitted");
  check(!constant_domain_contains(domain,Value::from_int(0)), "range admitted wrong type");
  rejects([&] { decode_constant(json(R"({"type":"Float","range":[1,1]})")); });
  for (const auto text : {R"({"type":"Float","range":[2,1]})",
      R"({"type":"Float","range":["0","1"]})", R"({"type":"Bool","range":[0,1]})",
      R"({"type":"Float","range":[0]})", R"({"type":"Float","range":[0,1],"values":[0]})"})
    rejects([&] { parse_constant_domain(json(text)); });
  auto invalid = json(R"({"type":"Float","range":[0,1]})");
  invalid.object_v.at("range").array_v[1].number_v = std::numeric_limits<double>::infinity();
  rejects([&] { parse_constant_domain(invalid); });
}
void test_quantization() {
  auto domain = parse_constant_domain(json(R"({"type":"Float","range":[-8,8],"quantization_scale":1000})"));
  for (int index = -8000; index <= 8000; ++index) {
    check(constant_domain_contains(domain, Value::from_float(index / 1000.0)), "quantization omitted a grid value");
    check(valid_float_quantization(index / 1000.0, index / 1000.0, 1000),
          "grid endpoint was rejected due to binary multiplication rounding");
  }
  (void)parse_constant_domain(json(R"({"type":"Float","range":[1.001,1.009],"quantization_scale":1000})"));
  for (double scale : {1e-200,1000.0,1e200}) {
    for (std::int64_t offset = 0; offset < 256; ++offset) {
      const double edge = static_cast<double>((INT64_C(1) << 50) - offset) / scale;
      check(valid_float_quantization(-edge,edge,scale), "precision-boundary grid rejected");
      check(quantize_float(edge,scale) == edge && quantize_float(-edge,scale) == -edge,
            "quantization was not idempotent at its precision boundary");
    }
  }
  check(!constant_domain_contains(domain, Value::from_float(0.0001)), "off-grid Float accepted");
  check(quantize_float(0.0005,1000) == 0.001 && quantize_float(-0.0005,1000) == -0.001,
        "quantization did not round ties away from zero");
  check(std::signbit(quantize_float(-0.0001,1000)), "quantization lost negative zero");
  check(quantize_float(sample_float_interval(-8,8,UINT64_MAX),1000) == 8,
        "quantization removed upper half-cell");
  GrammarRandom random(123);
  for (unsigned i = 0; i < 8192; ++i)
    check(constant_domain_contains(domain,sample_constant(domain,random)), "quantized sample escaped membership");
  for (const auto text : {
      R"({"type":"Float","range":[-8,8],"quantization_scale":0})",
      R"({"type":"Float","range":[-8,8],"quantization_scale":-1})",
      R"({"type":"Float","range":[-8,8],"quantization_scale":"1000"})",
      R"({"type":"Int","range":["-8","8"],"quantization_scale":1000})",
      R"({"type":"Float","values":[0],"quantization_scale":1000})",
      R"({"type":"Float","range":[0.0001,8],"quantization_scale":1000})",
      R"({"type":"Float","range":[-1e20,1e20],"quantization_scale":1000})"})
    rejects([&] { parse_constant_domain(json(text)); });
}
void test_sampling_domains() {
  const auto floats = parse_constant_domain(json(R"({"type":"Float","range":[-100,100],
    "sample_from":{"type":"Float","range":[-8,8],"quantization_scale":1000}})"));
  check(constant_domain_contains(floats,Value::from_float(80.0001)), "sampling narrowed membership");
  check(!constant_domain_contains(*floats.sampling,Value::from_float(80.0001)), "sampling range ignored");
  const auto ints = parse_constant_domain(json(R"({"type":"Int","range":["-100","100"],
    "sample_from":{"type":"Int","values":["-1","1"]}})"));
  check(constant_domain_contains(ints,Value::from_int(99)), "finite sampling narrowed Int membership");
  auto definition = json(R"({"format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"I","type":"Int"},"search_limits":{"max_nodes":5,"max_depth":4},
    "execution_limits":{"fuel":10},"nonterminals":[{"id":"I","type":"Int","scope":[],
    "alternatives":[{"id":"integer","weight":1,"expression":{"constant":{"type":"Int","range":["-100","100"],
    "sample_from":{"type":"Int","values":["-1","1"]}}}}]}]})");
  auto grammar = std::make_shared<const CompiledGrammar>(compile_grammar(parse_definition(canonical_json(definition))));
  const auto prepared = repro::prepare_constant_mutation_domains(grammar);
  check(prepared->domains.front().integer_range == 0 && prepared->domains.front().value_count == 2 &&
        prepared->values.at(0).i == -1 && prepared->values.at(1).i == 1,
        "GPU descriptor used Int membership instead of its sampling subset");
  GrammarRandom random(131);
  for (unsigned i = 0; i < 1024; ++i) {
    check(constant_domain_contains(*floats.sampling,sample_constant(floats,random)), "Float escaped sample_from");
    const auto value = sample_constant(ints,random);
    check(value.i == -1 || value.i == 1, "Int escaped sample_from");
  }
  for (const auto text : {
      R"({"type":"Float","values":[0],"sample_from":{"type":"Float","values":[0]}})",
      R"({"type":"Int","range":["-1","1"],"sample_from":{"type":"Int","range":["-2","1"]}})",
      R"({"type":"Float","range":[-1,1],"sample_from":{"type":"Float","values":[2]}})",
      R"({"type":"Float","range":[-1,1],"sample_from":{"type":"Int","values":["0"]}})",
      R"({"type":"Float","range":[-1,1],"sample_from":{"type":"Float","range":[-1,1],"sample_from":{"type":"Float","values":[0]}}})",
      R"({"type":"Float","range":[-1,1],"quantization_scale":1000,"sample_from":{"type":"Float","range":[-1,1]}})",
      R"({"type":"Float","range":[-1,1],"quantization_scale":1000,"sample_from":{"type":"Float","values":[0.0001]}})"})
    rejects([&] { parse_constant_domain(json(text)); });
  (void)parse_constant_domain(json(R"({"type":"Float","range":[-1,1],"quantization_scale":1000,
    "sample_from":{"type":"Float","range":[0,0]}})"));
}
void test_generation_mutation(bool quantized, bool separated = false) {
  auto definition = json(R"({
    "format_version":"grammar-definition-v2","entry":{"nonterminal":"F","type":"Float"},
    "search_limits":{"max_nodes":5,"max_depth":4},"execution_limits":{"fuel":10},
    "nonterminals":[{"id":"F","type":"Float","scope":[],"alternatives":[
      {"id":"number","weight":1,"expression":{"constant":{"type":"Float","range":[-5,5]}}}]}]})");
  if (quantized)
    definition.object_v.at("nonterminals").array_v[0].object_v.at("alternatives").array_v[0]
        .object_v.at("expression").object_v.at("constant").object_v["quantization_scale"] = json("1000");
  if (separated) {
    auto& domain = definition.object_v.at("nonterminals").array_v[0].object_v.at("alternatives").array_v[0]
        .object_v.at("expression").object_v.at("constant");
    auto support = json(R"({"type":"Float","range":[-100,100]})");
    support.object_v["sample_from"] = domain;
    domain = std::move(support);
  }
  auto grammar = std::make_shared<const CompiledGrammar>(compile_grammar(parse_definition(canonical_json(definition))));
  const auto& domain = grammar->constants().front();
  auto prepared = repro::prepare_constant_mutation_domains(grammar);
  check(prepared->values.empty() && prepared->domains.front().float_range == 1 &&
        prepared->domains.front().float_minimum == -5 && prepared->domains.front().float_maximum == 5,
        "GPU preparation narrowed Float range to a finite table");
  check(repro::sample_constant_mutation_domains(prepared, 42, 64) == prepared,
        "scalar Float range unexpectedly needs host proposals");
  check(prepared->domains.front().float_quantization_scale == (quantized ? 1000 : 0),
        "GPU preparation lost quantization policy");
  for (double probability : {0.0, 1.0}) {
    VariationContext context(grammar);
    auto parent = generate_derivation(*grammar, 42).genome;
    if (separated) {
      parent.ast.consts.at(parent.ast.nodes.at(3).i0) = Value::from_float(42.125);
      parent.derivation.reset();
      require_membership(*grammar,parent);
    }
    std::set<double> seen;
    for (unsigned seed = 0; seed < 256; ++seed) {
      auto first = mutate(parent, seed, context, probability);
      auto replay = mutate(parent, seed, context, probability);
      const auto& value = first.ast.consts.at(first.ast.nodes.at(3).i0);
      check(value.f == replay.ast.consts.at(replay.ast.nodes.at(3).i0).f, "Float seed replay changed");
      check(constant_domain_contains(domain,value), "Float mutation escaped domain");
      if (domain.sampling)
        check(constant_domain_contains(*domain.sampling,value), "mutation ignored narrow sampler");
      first.derivation.reset();
      require_membership(*grammar, first);
      seen.insert(value.f);
      parent = first;
    }
    check(seen.size() > 200 && context.counters().acceptance_rejections == 0,
          "Float variation was narrowed or rejected");
  }
}
void test_additive_mutation() {
  for (const auto text : {
      R"({"type":"Int","values":["0"],"mutation":{"kind":"add","range":["-2","2"]}})",
      R"({"type":"Bool","values":[false,true],"mutation":{"kind":"add","range":[-1,1]}})",
      R"({"type":"Float","range":[-8,8],"quantization_scale":1000,"mutation":{"kind":"add","range":[-1,1]}})",
      R"({"type":"Float","range":[-8,8],"mutation":{"kind":"add","range":[1,-1]}})",
      R"({"type":"Float","range":[-8,8],"mutation":{"kind":"add","range":[-1,1],"gpu_grid_steps":0}})",
      R"({"type":"Float","range":[-8,8],"mutation":{"kind":"add","range":[-1,1],"gpu_grid_steps":1.5}})",
      R"({"type":"Float","range":[-8,8],"mutation":{"kind":"add","range":[-1,1],"gpu_grid_steps":4294967296}})",
      R"({"type":"Int","range":["-8","8"],"mutation":{"kind":"add","range":["-2","2"],"gpu_grid_steps":1}})",
      R"({"type":"Int","range":["-8","8"],"mutation":{"kind":"multiply","range":["-2","2"]}})",
      R"({"type":"Int","range":["-8","8"],"mutation":{"kind":"add","range":["-2","2"],"extra":1}})"})
    rejects([&] { parse_constant_domain(json(text)); });
  const std::int64_t low = INT64_MIN, high = INT64_MAX;
  for (auto previous : {low,low+1,INT64_C(-1),INT64_C(0),INT64_C(1),high-1,high})
    for (auto delta : {low,low+1,INT64_C(-2),INT64_C(0),INT64_C(2),high-1,high}) {
      const __int128 mathematical = static_cast<__int128>(previous) + delta;
      const auto expected = mathematical < low || mathematical > high ? previous : static_cast<std::int64_t>(mathematical);
      check(add_integer_in_range(previous,delta,low,high) == expected,"signed overflow in additive mutation");
    }
  check(add_integer_in_range(9,2,-10,10) == 9,"integer boundary was clamped rather than retained");
  const double maximum = std::numeric_limits<double>::max();
  check(add_float_in_range(maximum,maximum,-maximum,maximum) == maximum,"Float overflow escaped membership");
  check(add_float_in_range(-maximum,-maximum,-maximum,maximum) == -maximum,"negative Float overflow escaped membership");
  check(add_float_in_range(9,2,-10,10) == 9,"Float boundary was clamped rather than retained");
  std::set<double> grid;
  for (std::uint32_t index = 0; index <= 65535; ++index) {
    volatile double fraction = static_cast<double>(index) / 65535.0;
    volatile double scaled = fraction * 2.0;
    const double expected = scaled - 1.0;
    const auto value = sample_float_grid(-1,1,index,65535);
    check(value == expected,"GPU delta grid differs from frozen source law"); grid.insert(value);
  }
  check(grid.size() == 65536 && *grid.begin() == -1 && *grid.rbegin() == 1 && !grid.count(0),
        "GPU delta grid narrowed or lost endpoint weighting");
  for (const auto text : {
      R"({"type":"Int","range":["-100","100"],"sample_from":{"type":"Int","values":["0"]},"mutation":{"kind":"add","range":["1","2"]}})",
      R"({"type":"Float","range":[-100,100],"sample_from":{"type":"Float","range":[0,0],"quantization_scale":1000},"mutation":{"kind":"add","range":[0.25,0.75],"gpu_grid_steps":65535}})"}) {
    const auto domain_json = json(text);
    const auto type = domain_json.object_v.at("type").string_v;
    auto definition = json(R"({"format_version":"grammar-definition-v2","entry":{"nonterminal":"N","type":"Int"},
      "search_limits":{"max_nodes":5,"max_depth":4},"execution_limits":{"fuel":100},
      "nonterminals":[{"id":"N","type":"Int","scope":[],"alternatives":[
      {"id":"number","weight":1,"expression":{"constant":{"type":"Int","values":["0"]}}}]}]})");
    definition.object_v.at("entry").object_v.at("type").string_v = type;
    auto& nt = definition.object_v.at("nonterminals").array_v[0]; nt.object_v.at("type").string_v = type;
    nt.object_v.at("alternatives").array_v[0].object_v.at("expression").object_v.at("constant") = domain_json;
    auto grammar = std::make_shared<const CompiledGrammar>(compile_grammar(parse_definition(canonical_json(definition))));
    const auto prepared = repro::prepare_constant_mutation_domains(grammar);
    check(prepared->values.empty() && prepared->domains.front().mutation == ConstantMutationPolicy::Add,
          "additive preparation used sampler proposals");
    const auto& domain = grammar->constants().front();
    VariationContext context(grammar);
    auto parent = generate_derivation(*grammar,42).genome;
    for (unsigned seed = 0; seed < 256; ++seed) {
      auto child = mutate(parent,seed,context,0);
      const auto replay = mutate(parent,seed,context,0);
      const auto& before = parent.ast.consts.at(parent.ast.nodes.at(3).i0);
      const auto& after = child.ast.consts.at(child.ast.nodes.at(3).i0);
      check(canonical_json(encode_constant(after)) == canonical_json(encode_constant(replay.ast.consts.at(replay.ast.nodes.at(3).i0))),
            "additive replay changed");
      const double difference = type == "Int" ? static_cast<double>(after.i-before.i) : after.f-before.f;
      check(difference == 0 || (type == "Int" ? difference >= 1 && difference <= 2 : difference >= 0.25 && difference <= 0.75),
            "additive mutation resampled or quantized its result");
      check(constant_domain_contains(domain,after),"additive mutation escaped membership");
      child.derivation.reset(); require_membership(*grammar,child); parent = std::move(child);
    }
    const auto& final = parent.ast.consts.at(parent.ast.nodes.at(3).i0);
    check((type == "Int" ? final.i : final.f) > 99,"additive mutation never left construction support");
    check(context.counters().fallback_children == 0 && context.counters().acceptance_rejections == 0,
          "out-of-range additive proposal counted as fallback/rejection");
  }
}

void test_mutation_policies() {
  rejects([&] { decode_constant(json(R"({"type":"Bool","values":[false],"mutation":"keep"})")); });
  for (const auto text : {
      R"({"type":"Bool","values":[true],"mutation":"flip"})",
      R"({"type":"Int","values":["1"],"mutation":"flip"})",
      R"({"type":"Bool","values":[true,false],"mutation":"unknown"})",
      R"({"type":"Bool","values":[true,false],"mutation":1})",
      R"({"type":"Float","range":[-1,1],"sample_from":{"type":"Float","values":[0],"mutation":"keep"}})",
      R"({"type":"IntList","sequence":{"length":[0,1],"element":{"type":"Int","values":["1"],"mutation":"keep"}}})"})
    rejects([&] { parse_constant_domain(json(text)); });
  for (const auto text : {
      R"({"type":"Bool","values":[true,false],"mutation":"flip"})",
      R"({"type":"Char","values":["a","b"],"mutation":"keep"})",
      R"({"type":"String","values":["a","b"],"mutation":"keep"})",
      R"({"type":"IntList","sequence":{"length":[0,5],"element":{"type":"Int","range":["-8","8"]}},"mutation":"keep"})",
      R"({"type":"FloatList","values":[[0],[1]],"mutation":"keep"})",
      R"({"type":"StringList","values":[["a"],["b"]],"mutation":"keep"})"}) {
    const auto domain = json(text);
    const auto type = domain.object_v.at("type").string_v;
    const auto definition = std::string(R"({"format_version":"grammar-definition-v2","entry":{"nonterminal":"C","type":")") +
        type + R"("},"search_limits":{"max_nodes":5,"max_depth":4},"execution_limits":{"fuel":10},
        "nonterminals":[{"id":"C","type":")" + type + R"(","scope":[],"alternatives":[
        {"id":"constant","weight":1,"expression":{"constant":)" + text + R"(}}]}]})";
    auto grammar = std::make_shared<const CompiledGrammar>(compile_grammar(parse_definition(definition)));
    const bool flip = type == "Bool";
    auto prepared = repro::prepare_constant_mutation_domains(grammar);
    check(prepared->domains.front().mutation == (flip ? ConstantMutationPolicy::Flip : ConstantMutationPolicy::Keep),
          "preparation lost mutation policy");
    if (!flip) check(prepared->values.empty() && !prepared->has_sequence_domains &&
        repro::sample_constant_mutation_domains(prepared, 1, 8) == prepared,
        "keep policy generated unused host proposals");
    VariationContext context(grammar);
    auto parent = generate_derivation(*grammar, 42).genome;
    for (unsigned seed = 0; seed < 64; ++seed) {
      auto child = mutate(parent,seed,context,0);
      const auto& before = parent.ast.consts.at(parent.ast.nodes.at(3).i0);
      const auto& after = child.ast.consts.at(child.ast.nodes.at(3).i0);
      check(flip ? (before.b != after.b) :
          canonical_json(encode_constant(before)) == canonical_json(encode_constant(after)),
          "constant mutation did not apply keep/flip");
      child.derivation.reset(); require_membership(*grammar,child);
      parent = std::move(child);
    }
    check(context.counters().fallback_children == 0 && context.counters().acceptance_rejections == 0 &&
          context.counters().mutation_attempts == 64 &&
          (flip ? context.counters().changed_children : context.counters().unchanged_children) == 64,
          "keep/flip was reported as fallback or resampling");
  }
}
}  // namespace
int main() {
  try {
    test_intervals(); test_quantization(); test_sampling_domains();
    test_mutation_policies();
    test_additive_mutation();
    test_generation_mutation(false); test_generation_mutation(true);
    test_generation_mutation(false,true); test_generation_mutation(true,true);
  }
  catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
  std::cout << "gagp_test_grammar_numeric_constants: OK\n";
}
