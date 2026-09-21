#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/cli/json.hpp"
#include "gagp/core/region_plan.hpp"
#include "gagp/evolution/grammar/compiled.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/serialization/region_plan_json.hpp"

namespace {

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;
using Json = gagp::cli_detail::JsonValue;

void check(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

Json parse(const std::string& text) {
  return gagp::cli_detail::JsonParser(text, {true, 256}).parse();
}

Json string(std::string value) {
  Json result;
  result.kind = Json::Kind::String;
  result.string_v = std::move(value);
  return result;
}

Json number(double value) {
  Json result;
  result.kind = Json::Kind::Number;
  result.number_v = value;
  return result;
}

RegionPlan plan(std::uint32_t frames = 16) {
  RegionPlan result;
  result.state_types = {ValueTag::Int};
  result.result_type = ValueTag::Int;
  result.parameter_types = {ValueTag::Int};
  RegionStateTransition previous;
  previous.kind = RegionTransitionKind::CoordinateOffset;
  previous.offset = -1;
  result.requests = {{{previous}}};
  result.limits.frames = frames;
  result.coordinate_slots = {0};
  result.coordinate_rank = {{0, 1}};
  RegionBound lower;
  lower.literal = 0;
  RegionBound upper;
  upper.literal = 10;
  result.coordinate_domains = {{lower, upper}};
  validate_region_plan(result);
  return result;
}

std::string bounded_expression(const RegionPlan& region_plan,
                               const std::string& capture) {
  const std::string encoded_plan = canonical_json(
      serialization::encode_region_plan(region_plan));
  return std::string(R"({"structured":{"family":"bounded","plan":)") +
      encoded_plan + R"(},"captures":[)" + capture + R"(],"phases":[
        {"argument":1,"bindings":[{"bank":"state","slot":0,"name":"n"}]},
        {"argument":2,"bindings":[{"bank":"parameter","slot":0,"name":"p"}]},
        {"argument":3,"bindings":[{"bank":"result","slot":0,"name":"r"}]},
        {"argument":4,"bindings":[{"bank":"state","slot":0,"name":"b"}]}
      ],"args":[
        {"input":"x"},
        {"constant":{"type":"Bool","values":[true]}},
        {"bound":"p"},
        {"bound":"r"},
        {"bound":"b"}
      ]})";
}

Json document(bool two_occurrences = false) {
  const std::string first = bounded_expression(plan(), R"({"input":"x"})");
  const std::string second = bounded_expression(plan(), R"({"local":"tmp"})");
  return parse(std::string(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","type":"Int"},
    "inputs":[{"name":"x","type":"Int"},{"name":"flag","type":"Bool"}],
    "locals":[{"name":"tmp","type":"Int"}],
    "search_limits":{"max_nodes":80,"max_depth":20},
    "execution_limits":{"fuel":1000},
    "nonterminals":[{"id":"Main","type":"Int","scope":[],"alternatives":[
      {"id":"a","weight":1,"expression":)") + first +
      (two_occurrences ? std::string(R"(},
      {"id":"b","weight":1,"expression":)") + second : std::string()) +
      R"(}]}]
  })");
}

Json& expression(Json& doc, std::size_t alternative = 0) {
  return doc.object_v.at("nonterminals").array_v[0]
      .object_v.at("alternatives").array_v[alternative]
      .object_v.at("expression");
}

Json phase_template_document(Json supplied) {
  Json doc = document();
  Json body = expression(doc);
  body.object_v.at("args").array_v[2] = parse(R"({"hole":"value"})");
  Json templates = parse(R"([{
    "id":"PhaseValue","type":"Int","scope":[],
    "holes":[{"id":"value","type":"Int",
              "scope":[{"name":"p","type":"Int"}]}],
    "body":{"constant":{"type":"Int","values":["0"]}}
  }])");
  templates.array_v[0].object_v["body"] = std::move(body);
  doc.object_v["templates"] = std::move(templates);
  Json invocation = parse(
      R"({"template":"PhaseValue","holes":{"value":{"constant":{"type":"Int","values":["0"]}}}})");
  invocation.object_v.at("holes").object_v["value"] = std::move(supplied);
  expression(doc) = std::move(invocation);
  return doc;
}

CompiledGrammar compile(Json doc) {
  return compile_grammar(parse_definition(canonical_json(doc)));
}

void rejects_change(const std::function<void(Json&)>& change,
                    const std::string& message) {
  Json doc = document();
  change(doc);
  try {
    (void)compile(std::move(doc));
  } catch (const std::exception&) {
    return;
  }
  throw std::runtime_error("accepted malformed bounded grammar: " + message);
}

void test_valid_shape_and_identity() {
  const CompiledGrammar grammar = compile(document(true));
  check(grammar.structured_contracts().size() == 1,
        "identical full plans must intern as one structured contract");
  const StructuredContract& contract = grammar.structured_contracts()[0];
  check(contract.family == StructuredFamily::BoundedRegion && contract.plan &&
            contract.arguments.size() == 5 && contract.result == RType::Int,
        "bounded contract did not preserve plan-derived signature");

  std::vector<const CompiledExpression*> bounded;
  for (const CompiledExpression& candidate : grammar.expressions()) {
    if (candidate.kind == ExpressionKind::Structured &&
        grammar.structured_contracts()[candidate.target].family ==
            StructuredFamily::BoundedRegion) {
      bounded.push_back(&candidate);
    }
  }
  check(bounded.size() == 2, "both bounded occurrences must compile");
  check(bounded[0]->captures.size() == 1 &&
            bounded[0]->captures[0].kind == CompiledCaptureKind::Input &&
            bounded[0]->captures[0].target == 0,
        "input capture did not resolve to its formal input ID");
  check(bounded[1]->captures.size() == 1 &&
            bounded[1]->captures[0].kind == CompiledCaptureKind::Local &&
            bounded[1]->captures[0].target == 0,
        "local capture did not resolve to its formal local ID");
  for (const CompiledExpression* candidate : bounded) {
    check(candidate->phases.size() == 4 && candidate->regions.size() == 4,
          "all bounded phases must be retained");
    for (std::size_t i = 0; i < candidate->phases.size(); ++i) {
      check(candidate->phases[i].argument == i + 1 &&
                candidate->regions[i].argument == i + 1 &&
                candidate->phases[i].sources.size() ==
                    candidate->regions[i].bindings.size(),
            "phase source/name alignment changed");
    }
  }

  const CompiledGrammar changed = [&] {
    Json doc = document();
    expression(doc).object_v.at("structured").object_v["plan"] =
        serialization::encode_region_plan(plan(17));
    return compile(std::move(doc));
  }();
  check(changed.structured_contracts()[0].key != contract.key,
        "full canonical plan must participate in contract identity");
}

void test_bound_capture_resolution() {
  Json doc = document();
  Json bounded = expression(doc);
  bounded.object_v.at("captures").array_v[0] = parse(R"({"bound":"outer"})");
  Json wrapper = parse(R"({
    "signature":"let(Int,Int)->Int",
    "args":[{"constant":{"type":"Int","values":["4"]}},
            {"constant":{"type":"Int","values":["0"]}}],
    "bind":{"1":["outer"]}
  })");
  wrapper.object_v.at("args").array_v[1] = std::move(bounded);
  expression(doc) = std::move(wrapper);
  const CompiledGrammar grammar = compile(std::move(doc));
  for (const CompiledExpression& candidate : grammar.expressions()) {
    if (candidate.kind == ExpressionKind::Structured) {
      check(candidate.captures.size() == 1 &&
                candidate.captures[0].kind == CompiledCaptureKind::Bound &&
                candidate.captures[0].target == 0,
            "bound capture did not resolve in its enclosing lexical environment");
      return;
    }
  }
  throw std::runtime_error("bound capture fixture lost bounded expression");
}

void test_malformed_shape_and_scope() {
  rejects_change([](Json& doc) {
    expression(doc).object_v["extra"] = number(0);
  }, "unknown expression field");
  rejects_change([](Json& doc) {
    expression(doc).object_v.at("structured").object_v["extra"] = number(0);
  }, "unknown contract field");
  rejects_change([](Json& doc) {
    expression(doc).object_v.at("structured").object_v.at("plan")
        .object_v.at("requests").array_v[0].object_v.at("states").array_v[0]
        .object_v["offset"] = string("1");
  }, "invalid progress proof");
  rejects_change([](Json& doc) {
    expression(doc).object_v.at("captures").array_v.clear();
  }, "capture arity");
  rejects_change([](Json& doc) {
    auto& capture = expression(doc).object_v.at("captures").array_v[0];
    capture.object_v["local"] = string("tmp");
  }, "capture discriminant");
  rejects_change([](Json& doc) {
    expression(doc).object_v.at("captures").array_v[0] =
        parse(R"({"input":"flag"})");
  }, "capture type");
  rejects_change([](Json& doc) {
    auto& bounded = expression(doc);
    bounded.object_v.at("structured").object_v.at("plan")
        .object_v.at("parameter_types").array_v.push_back(string("Int"));
    bounded.object_v.at("captures").array_v.push_back(
        bounded.object_v.at("captures").array_v[0]);
  }, "duplicate capture");
  rejects_change([](Json& doc) {
    expression(doc).object_v.at("phases").array_v.pop_back();
  }, "missing phase");
  rejects_change([](Json& doc) {
    expression(doc).object_v.at("phases").array_v[0].object_v["argument"] =
        number(2);
  }, "noncanonical phase argument");
  rejects_change([](Json& doc) {
    expression(doc).object_v.at("phases").array_v[0].object_v["extra"] =
        number(0);
  }, "unknown phase field");
  rejects_change([](Json& doc) {
    auto& binding = expression(doc).object_v.at("phases").array_v[0]
                        .object_v.at("bindings").array_v[0];
    binding.object_v["extra"] = number(0);
  }, "unknown binding field");
  rejects_change([](Json& doc) {
    auto& binding = expression(doc).object_v.at("phases").array_v[0]
                        .object_v.at("bindings").array_v[0];
    binding.object_v["bank"] = string("local");
  }, "unknown bank");
  rejects_change([](Json& doc) {
    auto& binding = expression(doc).object_v.at("phases").array_v[0]
                        .object_v.at("bindings").array_v[0];
    binding.object_v["slot"] = number(9);
  }, "invisible slot");
  rejects_change([](Json& doc) {
    auto& bindings = expression(doc).object_v.at("phases").array_v[1]
                         .object_v.at("bindings").array_v;
    bindings.push_back(bindings[0]);
  }, "duplicate phase source and name");
  rejects_change([](Json& doc) {
    auto& bindings = expression(doc).object_v.at("phases").array_v[0]
                         .object_v.at("bindings").array_v;
    while (bindings.size() <= kBoundedRegionPhaseBindingCapacity)
      bindings.push_back(bindings.front());
  }, "phase binding capacity");
  rejects_change([](Json& doc) {
    expression(doc).object_v.at("args").array_v.pop_back();
  }, "child arity");
  rejects_change([](Json& doc) {
    expression(doc).object_v.at("args").array_v[2] = parse(R"({"bound":"n"})");
  }, "closed phase lexical scope");
}

void test_phase_graph_closure() {
  rejects_change([](Json& doc) {
    expression(doc).object_v.at("args").array_v[2] = parse(R"({"input":"x"})");
  }, "direct input in phase");

  rejects_change([](Json& doc) {
    doc.object_v.at("nonterminals").array_v.push_back(parse(R"({
      "id":"Helper","type":"Int","scope":[],"alternatives":[
        {"id":"input","weight":1,"expression":{"input":"x"}}
      ]
    })"));
    expression(doc).object_v.at("args").array_v[2] = parse(R"({"ref":"Helper"})");
  }, "indirect input through nonterminal");

  rejects_change([](Json& doc) {
    doc.object_v["templates"] = parse(R"([{
      "id":"ReadInput","type":"Int","scope":[],"holes":[],
      "body":{"input":"x"}
    }])");
    expression(doc).object_v.at("args").array_v[2] =
        parse(R"({"template":"ReadInput","holes":{}})");
  }, "indirect input through template");

  rejects_change([](Json& doc) {
    Json nested = expression(doc);
    nested.object_v.at("args").array_v[2] =
        parse(R"({"constant":{"type":"Int","values":["0"]}})");
    expression(doc).object_v.at("args").array_v[2] = std::move(nested);
  }, "nested structured phase expression");

  const CompiledGrammar safe_template = compile(phase_template_document(
      parse(R"({"constant":{"type":"Int","values":["7"]}})")));
  check(!safe_template.structured_contracts().empty(),
        "concrete safe phase template was rejected by its abstract hole body");

  for (const char* unsafe : {R"({"input":"x"})", R"({"local":"tmp"})"}) {
    try {
      (void)compile(phase_template_document(parse(unsafe)));
    } catch (const std::exception&) {
      continue;
    }
    throw std::runtime_error(
        "bounded phase accepted an unsafe concrete template-hole argument");
  }
}

}  // namespace

int main() {
  try {
    test_valid_shape_and_identity();
    test_bound_capture_resolution();
    test_malformed_shape_and_scope();
    test_phase_graph_closure();
    std::cout << "bounded structured grammar parsing passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
