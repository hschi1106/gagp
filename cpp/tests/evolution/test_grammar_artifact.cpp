#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "gagp/cli/grammar_artifact.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/grammar/identity.hpp"
#include "gagp/evolution/grammar/values.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/payload/payload.hpp"

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;
using namespace gagp::cli_detail;
namespace {
using Json = JsonValue;
void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
Json parse(const std::string& text) { return JsonParser(text, {true, 512}).parse(); }
void rejects(const std::function<void()>& action, const char* message) {
  try { action(); } catch (const std::invalid_argument&) { return; }
  throw std::runtime_error(message);
}
std::string constant_grammar(const std::string& type, const std::string& values) {
  return R"({"format_version":"grammar-definition-v1","entry":{"nonterminal":"Value","type":")" + type +
    R"("},"search_limits":{"max_nodes":5,"max_depth":4},"execution_limits":{"fuel":100},
    "nonterminals":[{"id":"Value","type":")" + type + R"(","scope":[],"alternatives":[
    {"id":"constant","weight":1,"expression":{"constant":{"type":")" + type + R"(","values":)" + values + "}}}]}]}";
}
void check_value(const Value& value, const std::string& type, const std::string& values) {
  const auto expected = parse("{\"type\":\"" + type + "\",\"values\":" + values + "}");
  check(canonical_json(encode_constant(value)) == canonical_json(expected), "constant content changed across artifact boundary");
  if (type == "Float") check(value.tag == ValueTag::Float && std::signbit(value.f), "Float signed zero lost");
  if (type == "Char") check(value.i == 0x1f600, "non-BMP Char lost");
  if (type == "String") {
    std::string text;
    check(payload::lookup_string(value, &text) && text == std::string("a\0b", 3), "embedded-NUL String lost");
  }
  if (type == "FloatList") {
    std::vector<Value> elements;
    check(payload::lookup_list(value, &elements) && elements.size() == 3 && std::signbit(elements[0].f),
          "FloatList signed zero lost");
  }
}
void test_constants_and_replay() {
  const std::vector<std::pair<std::string, std::string>> domains{
    {"Int", "[\"-9223372036854775808\"]"}, {"Int", "[\"9223372036854775807\"]"},
    {"Float", "[-0.0]"}, {"Bool", "[true]"}, {"Bool", "[false]"},
    {"Char", "[\"\\ud83d\\ude00\"]"}, {"String", "[\"a\\u0000b\"]"},
    {"IntList", "[[\"-9223372036854775808\",\"9223372036854775807\"]]"},
    {"FloatList", "[[-0.0,0.5,-2.25]]"}, {"StringList", "[[\"\",\"a\\u0000b\",\"\\ud83d\\ude00\"]]"},
    {"IntList", "[[]]"}, {"StringList", "[[]]"}};
  for (const auto& domain : domains) {
    const auto grammar = compile_grammar(parse_definition(constant_grammar(domain.first, domain.second)));
    const auto generated = generate_derivation(grammar, std::numeric_limits<std::uint64_t>::max());
    const auto artifact = encode_generated_artifact(grammar, generated);
    check(parse(artifact).object_v.at("seed").string_v == "18446744073709551615", "uint64 seed rounded or truncated");
    payload::clear();
    const auto decoded = decode_materialized_artifact(artifact);
    check(decoded.ast.consts.size() == 1, "materialized constant pool changed size");
    check_value(decoded.ast.consts.front(), domain.first, domain.second);
    const auto result = execute_bytecode_cpu(compile_for_eval(decoded), {}, 100);
    check(!result.is_error, "decoded typed constant did not execute");
    check_value(result.value, domain.first, domain.second);
    payload::clear();
    const auto replayed = replay_generated_artifact(artifact, &grammar);
    check(encode_generated_artifact(grammar, replayed) == artifact, "same-version replay not byte stable after registry clear");
    check_value(replayed.genome.ast.consts.front(), domain.first, domain.second);
    auto future = parse(artifact);
    future.object_v.at("generator_version").string_v = "future-generator";
    future.object_v.at("rng_version").string_v = "future-rng";
    future.object_v.erase("grammar");
    payload::clear();
    check_value(decode_materialized_artifact(canonical_json(future)).ast.consts.front(), domain.first, domain.second);
  }
}
void test_tampering() {
  const auto grammar = compile_grammar(parse_definition(constant_grammar("Int", "[\"7\"]")));
  const auto artifact = encode_generated_artifact(grammar, generate_derivation(grammar, 42));
  const auto original = parse(artifact);
  const auto tamper = [&](const std::function<void(Json&)>& change) {
    auto root = original;
    change(root);
    rejects([&] { replay_generated_artifact(canonical_json(root)); }, "tampered artifact accepted by replay");
  };
  for (const char* field : {"semantic_version", "generator_version", "rng_version", "grammar_hash", "input_schema_hash", "payload_seeding", "format_version"})
    tamper([&](Json& root) { root.object_v.at(field).string_v = "tampered"; });
  for (const char* seed : {"18446744073709551616", "-1", "042", "42x", ""})
    tamper([&](Json& root) { root.object_v.at("seed").string_v = seed; });
  tamper([](Json& root) { root.object_v.at("constants").array_v[0].object_v.at("values").array_v[0].string_v = "8"; });
  tamper([](Json& root) { root.object_v.at("derivation").object_v.at("nodes").array_v[3].array_v[3].number_v += 1; });
  tamper([](Json& root) { root.object_v.at("derivation").object_v.at("choices").array_v[0].array_v[4].number_v -= 1; });
  tamper([](Json& root) { root.object_v.at("derivation").object_v.at("logical_steps").number_v += 1; });
  tamper([](Json& root) { root.object_v.at("search_limits").object_v.at("max_nodes").number_v += 1; });
  tamper([](Json& root) { root.object_v.at("search_limits").object_v.at("max_depth").number_v += 1; });
  tamper([](Json& root) { root.object_v.at("execution_limits").object_v.at("fuel").number_v += 1; });
  tamper([](Json& root) { root.object_v.at("grammar").object_v.at("execution_limits").object_v.at("fuel").number_v += 1; });
  tamper([](Json& root) {
    root.object_v.at("inputs") = parse(R"([{"name":"x","type":"Int"}])");
    root.object_v.at("input_schema_hash").string_v = content_sha256(canonical_json(root.object_v.at("inputs")));
  });
  const auto other = compile_grammar(parse_definition(constant_grammar("Int", "[\"8\"]")));
  rejects([&] { replay_generated_artifact(artifact, &other); }, "required grammar mismatch accepted");
  for (double bad_index : {1e300, -1e300, 0.5}) {
    auto malformed = original;
    malformed.object_v.at("ast_shape").object_v.at("nodes").array_v[0].object_v.at("i0").number_v = bad_index;
    rejects([&] { decode_materialized_artifact(canonical_json(malformed)); }, "out-of-range or fractional shape index accepted");
  }
  auto deep = original;
  auto& nodes = deep.object_v.at("ast_shape").object_v.at("nodes").array_v;
  auto unary = nodes[3];
  unary.object_v.at("kind").number_v = static_cast<int>(NodeKind::NEG);
  unary.object_v.at("i0").number_v = 0;
  unary.object_v.at("i1").number_v = 0;
  nodes.insert(nodes.begin() + 3, 256, unary);
  rejects([&] { decode_materialized_artifact(canonical_json(deep)); }, "excessive prefix depth accepted");
  auto incompatible_semantics = original;
  incompatible_semantics.object_v.at("semantic_version").string_v = "different-runtime";
  rejects([&] { decode_materialized_artifact(canonical_json(incompatible_semantics)); }, "incompatible runtime semantics accepted");
  tamper([](Json& root) { root.object_v.at("derivation").object_v.at("lowered_instructions").number_v += 1; });
  auto bad_schema = original;
  bad_schema.object_v.at("inputs") = parse(R"([{"name":"x","type":"Int"}])");
  rejects([&] { decode_materialized_artifact(canonical_json(bad_schema)); }, "materialized decoder ignored schema hash");
  auto bad_constant = original;
  bad_constant.object_v.at("constants").array_v[0].object_v.at("values") = parse("[\"7\",\"8\"]");
  rejects([&] { decode_materialized_artifact(canonical_json(bad_constant)); }, "non-concrete constant domain accepted");
  bad_constant.object_v.at("constants").array_v.clear();
  rejects([&] { decode_materialized_artifact(canonical_json(bad_constant)); }, "dangling constant index accepted");
}
void test_input_schema() {
  auto doc = parse(constant_grammar("Int", "[\"7\"]"));
  doc.object_v["inputs"] = parse(R"([{"name":"x","type":"Int"}])");
  doc.object_v.at("nonterminals").array_v[0].object_v.at("alternatives").array_v[0]
      .object_v.at("expression") = parse(R"({"input":"x"})");
  const auto grammar = compile_grammar(parse_definition(canonical_json(doc)));
  const auto artifact = encode_generated_artifact(grammar, generate_derivation(grammar, 9));
  const auto decoded = decode_materialized_artifact(artifact);
  check(!decoded.ast.nodes.empty(), "input-backed materialized AST failed to decode");
  check(encode_generated_artifact(grammar, replay_generated_artifact(artifact)) == artifact,
        "input-backed artifact failed exact replay");
  for (const char* schema : {R"([])", R"([{"name":"x","type":"Bool"}])"}) {
    auto changed = parse(artifact);
    changed.object_v.at("inputs") = parse(schema);
    changed.object_v.at("input_schema_hash").string_v = content_sha256(canonical_json(changed.object_v.at("inputs")));
    rejects([&] { decode_materialized_artifact(canonical_json(changed)); },
            "materialized verifier accepted missing or mistyped referenced input");
  }
}
void test_forwarded_template_provenance() {
  auto doc = parse(constant_grammar("Int", "[\"7\"]"));
  doc.object_v.at("search_limits") = parse(R"({"max_nodes":20,"max_depth":10})");
  doc.object_v["templates"] = parse(R"([
    {"id":"Double","type":"Int","scope":[],"holes":[{"id":"value","type":"Int","scope":[]}],
     "body":{"signature":"add(Int,Int)->Int","args":[{"hole":"value"},{"hole":"value"}]}},
    {"id":"Forward","type":"Int","scope":[],"holes":[{"id":"value","type":"Int","scope":[]}],
     "body":{"template":"Double","holes":{"value":{"hole":"value"}}}}])");
  doc.object_v.at("nonterminals").array_v.push_back(parse(R"({
    "id":"Main","type":"Int","scope":[],"alternatives":[{"id":"forward","weight":1,
    "expression":{"template":"Forward","holes":{"value":{"ref":"Value"}}}}]})"));
  doc.object_v.at("entry").object_v.at("nonterminal").string_v = "Main";
  const auto grammar = compile_grammar(parse_definition(canonical_json(doc)));
  const auto generated = generate_derivation(grammar, 123);
  check(generated.derivation.templates.size() == 2 && generated.derivation.holes.size() == 4,
        "forwarded/copied template fixture missing provenance");
  const auto artifact = encode_generated_artifact(grammar, generated);
  payload::clear();
  const auto replayed = replay_generated_artifact(artifact, &grammar);
  check(encode_generated_artifact(grammar, replayed) == artifact, "copied/forwarded provenance did not replay exactly");
  const auto result = execute_bytecode_cpu(compile_for_eval(replayed.genome), {}, 100);
  check(!result.is_error && result.value.i == 14, "copied template changed runtime result");
  for (const char* field : {"templates", "holes"}) {
    auto changed = parse(artifact);
    changed.object_v.at("derivation").object_v.at(field).array_v[0].array_v[0].number_v += 1;
    rejects([&] { replay_generated_artifact(canonical_json(changed)); }, "tampered template provenance accepted");
  }
}
}  // namespace
int main() {
  try {
    test_constants_and_replay();
    test_tampering();
    test_input_schema();
    test_forwarded_template_provenance();
    std::cout << "grammar artifact: typed materialization, exact replay, and tamper rejection passed\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
