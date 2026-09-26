#include <iostream>
#include <cmath>
#include <functional>
#include <stdexcept>

#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/variation_contract.hpp"
#include "gagp/evolution/grammar/values.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/repro/pack.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/payload/payload.hpp"

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;
namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
const std::string scalar = R"({"format_version":"grammar-definition-v2",
  "entry":{"nonterminal":"Expr","type":"Int"},
  "search_limits":{"max_nodes":40,"max_depth":12},"execution_limits":{"fuel":10000},
  "nonterminals":[{"id":"Expr","type":"Int","scope":[],"alternatives":[
    {"id":"leaf","weight":1,"expression":{"constant":{"type":"Int","range":["0","3"]}}},
    {"id":"sum","weight":4,"expression":{"signature":"add(Int,Int)->Int","args":[{"ref":"Expr"},{"ref":"Expr"}]}}
  ]}]})";
std::string constant_grammar(const std::string& type, const std::string& values) {
  return R"({"format_version":"grammar-definition-v2","entry":{"nonterminal":"Value","type":")" + type +
    R"("},"search_limits":{"max_nodes":5,"max_depth":4},"execution_limits":{"fuel":100},
    "nonterminals":[{"id":"Value","type":")" + type + R"(","scope":[],"alternatives":[
    {"id":"constant","weight":1,"expression":{"constant":{"type":")" + type + R"(","values":)" + values + "}}}]}]}";
}
}  // namespace
void check_input_write() {
  const auto grammar = compile_grammar(parse_definition(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","type":"Float","category":"Program"},
    "inputs":[{"name":"x","type":"Float"}],
    "search_limits":{"max_nodes":20,"max_depth":10},"execution_limits":{"fuel":100},
    "nonterminals":[{"id":"Main","type":"Float","category":"Program","scope":[],
      "alternatives":[{"id":"write","weight":1,"expression":{
        "control":"program(Block)->Program","type":"Float","args":[{
          "control":"block_cons(Statement,Block)->Block","type":"Float","args":[{
            "control":"assign(Float)->Statement","type":"Float","input_name":"x","args":[{
              "signature":"add(Float,Float)->Float","args":[{"input":"x"},{"constant":{"type":"Float","values":[1]}}]}]}, {
            "control":"block_cons(Statement,Block)->Block","type":"Float","args":[{
              "control":"return(Float)->Statement","type":"Float","args":[{"input":"x"}]},
              {"control":"block_nil()->Block","type":"Float","args":[]}]}]}]}}]}]
  })"));
  const auto generated = generate_derivation(grammar, 7);
  (void)analyze_variation(grammar, generated.genome);
  const auto verified = verify_ast(generated.genome.ast, {{"x", RType::Float}});
  check(verified.ok, "input write produced an invalid AST");
  const auto code = compile_for_eval(generated.genome, verified.verified, {"x"});
  const std::vector<std::pair<int, Value>> inputs{{code.var2idx.at("x"), Value::from_float(6)}};
  const auto result = execute_bytecode_cpu(code, inputs, 100);
  check(!result.is_error && result.value.tag == ValueTag::Float && result.value.f == 7,
        "input write did not update subsequent reads");
  check(inputs.front().second.f == 6, "input write changed external case data");
}

int main() {
  try {
    check_input_write();
    const auto grammar = compile_grammar(parse_definition(scalar));
    auto owned = generate_derivation(grammar, 7);
    check(owned.genome.derivation != nullptr, "generated genome lost derivation ownership");
    check(owned.genome.derivation->grammar_hash == grammar.content_hash() &&
          owned.genome.derivation->seed == 7 &&
          owned.genome.derivation->nodes.size() == owned.genome.ast.nodes.size(),
          "owned derivation does not describe generated genome");
    const auto clone = owned.genome;
    check(clone.derivation == owned.genome.derivation, "normal genome clone did not share immutable provenance");
    owned.derivation.seed = 99;
    check(clone.derivation->seed == 7, "mutable result metadata changed immutable genome provenance");
    auto padded = clone;
    padded.ast.names.push_back("unused_compaction_name");
    padded.ast.consts.push_back(Value::from_int(123456789));
    const auto compacted = repro::compact_genome_tables(padded);
    check(compacted.derivation == clone.derivation, "table compaction dropped immutable provenance");
    check(compacted.ast.names.size() < padded.ast.names.size() &&
          compacted.ast.consts.size() < padded.ast.consts.size(), "compaction fixture did not remove unused tables");
    check(compacted.ast.nodes.size() == clone.ast.nodes.size(), "compaction changed provenance node count");
    for (std::size_t i = 0; i < clone.ast.nodes.size(); ++i) {
      const auto& before = clone.ast.nodes[i];
      const auto& after = compacted.ast.nodes[i];
      check(before.kind == after.kind && before.i0 == after.i0 && before.i1 == after.i1,
            "table compaction reordered generated prefix nodes");
    }
    const auto program_grammar = compile_grammar(parse_definition(R"({
      "format_version":"grammar-definition-v2","entry":{"nonterminal":"Main","category":"Program","type":"Int"},
      "search_limits":{"max_nodes":10,"max_depth":8},"execution_limits":{"fuel":100},"locals":[{"name":"i","type":"Int"}],
      "nonterminals":[{"id":"Main","category":"Program","type":"Int","scope":[],"alternatives":[
        {"id":"loop","weight":1,"expression":{"control":"program(Block)->Program","type":"Int","args":[
          {"control":"block_cons(Statement,Block)->Block","type":"Int","args":[
            {"control":"for_range(Int,Block)->Statement","type":"Int","name":"i","args":[
              {"constant":{"type":"Int","values":["2"]}},{"control":"block_nil()->Block","type":"Int","args":[]}]},
            {"control":"block_cons(Statement,Block)->Block","type":"Int","args":[
              {"control":"return(Int)->Statement","type":"Int","args":[{"constant":{"type":"Int","values":["7"]}}]},
              {"control":"block_nil()->Block","type":"Int","args":[]}]}]}]}}]}]
    })"));
    const auto program = generate_derivation(program_grammar, 0);
    check(program.genome.ast.nodes.size() == 9 && program.derivation.derived_nodes == 9,
          "Program entry received a duplicate envelope");
    const auto program_result = execute_bytecode_cpu(compile_for_eval(program.genome), {}, 100);
    check(!program_result.is_error && program_result.value.tag == ValueTag::Int && program_result.value.i == 7,
          "control-category materialization did not preserve execution");
    auto alias_doc = cli_detail::JsonParser(scalar, {true, 256}).parse();
    auto& alternatives = alias_doc.object_v.at("nonterminals").array_v[0].object_v.at("alternatives").array_v;
    alternatives[1] = cli_detail::JsonParser(R"({"id":"alias","weight":1e300,"expression":{"ref":"Expr"}})", {true, 256}).parse();
    const auto alias_grammar = compile_grammar(parse_definition(canonical_json(alias_doc)));
    for (std::uint64_t seed = 0; seed < 64; ++seed) {
      const auto generated = generate_derivation(alias_grammar, seed);
      check(generated.genome.ast.nodes.size() == 5 && generated.derivation.choices.size() == 1,
            "redundant alias cycle prevented a productive derivation");
    }
    for (std::uint64_t seed = 0; seed < 512; ++seed) {
      const auto generated = generate_derivation(grammar, seed);
      const auto& ast = generated.genome.ast;
      check(ast.nodes.size() <= 40 && ast.nodes.size() >= 5, "materialized AST exceeds search budget");
      std::size_t cursor = 0;
      std::function<unsigned()> depth = [&]() {
        const auto arity = node_descriptor(ast.nodes.at(cursor++).kind).prefix_arity;
        unsigned result = 1;
        for (int i = 0; i < arity; ++i) result = std::max(result, 1 + depth());
        return result;
      };
      check(depth() <= 12 && cursor == ast.nodes.size(), "materialized depth exceeds search budget");
      check(generated.derivation.execution_limits.fuel == 10000 && generated.derivation.search_limits.max_nodes == 40,
            "derivation lost separate execution/search limits");
      check(generated.derivation.nodes.size() == ast.nodes.size(), "provenance lost node alignment");
      for (std::size_t i = 0; i < generated.derivation.choices.size(); ++i) {
        const auto& choice = generated.derivation.choices[i];
        check(choice.ast_begin < choice.ast_end && choice.ast_end <= ast.nodes.size(), "invalid derivation span");
        check(choice.parent == kNoGrammarId || choice.parent < i, "derivation parent does not precede child");
      }
      check(generated.derivation.derived_nodes + 4 == ast.nodes.size(), "derived/envelope node accounting");
      std::int64_t expected = 0;
      for (const auto& value : ast.consts) { check(value.i >= 0 && value.i <= 3, "constant escaped domain"); expected += value.i; }
      for (std::size_t i = 3; i + 1 < ast.nodes.size(); ++i) {
        check(ast.nodes[i].kind == NodeKind::CONST || ast.nodes[i].kind == NodeKind::ADD, "generation enabled excluded operation");
        check(generated.derivation.nodes[i].production < grammar.productions().size(), "missing production provenance");
      }
      const auto bytecode = compile_for_eval(generated.genome);
      check(generated.derivation.lowered_instructions == bytecode.code.size() &&
            generated.derivation.semantic_version == kGrammarSemanticVersion,
            "lowered size or semantic identity missing from derivation");
      const auto result = execute_bytecode_cpu(bytecode, {}, grammar.execution_limits().fuel);
      check(!result.is_error && result.value.tag == ValueTag::Int && result.value.i == expected, "generated AST/runtime semantics mismatch");
      const auto again = generate_derivation(grammar, seed);
      check(ast_cache_key(ast) == ast_cache_key(again.genome.ast), "same-version seed replay changed");
    }
    auto template_doc = cli_detail::JsonParser(scalar, {true, 256}).parse();
    template_doc.object_v["templates"] = cli_detail::JsonParser(R"([
      {"id":"Double","type":"Int","scope":[],"holes":[{"id":"value","type":"Int","scope":[]}],
       "body":{"signature":"add(Int,Int)->Int","args":[{"hole":"value"},{"hole":"value"}]}},
      {"id":"Forward","type":"Int","scope":[],"holes":[{"id":"value","type":"Int","scope":[]}],
       "body":{"template":"Double","holes":{"value":{"hole":"value"}}}},
      {"id":"Unused","type":"Int","scope":[],"holes":[],
       "body":{"signature":"let(Int,Int)->Int","args":[{"constant":{"type":"Int","values":["0"]}},{"bound":"x"}],"bind":{"1":["x"]}}}
    ])", {true, 256}).parse();
    template_doc.object_v.at("nonterminals").array_v.push_back(cli_detail::JsonParser(R"({
      "id":"Main","type":"Int","scope":[],"alternatives":[{"id":"forward","weight":1,
      "expression":{"template":"Forward","holes":{"value":{"ref":"Expr"}}}}]})", {true, 256}).parse());
    template_doc.object_v.at("entry").object_v.at("nonterminal").string_v = "Main";
    const auto templates = compile_grammar(parse_definition(canonical_json(template_doc)));
    for (std::uint64_t seed = 0; seed < 256; ++seed) {
      const auto generated = generate_derivation(templates, seed);
      check(generated.genome.ast.nodes.size() <= 40 && generated.derivation.templates.size() == 2,
            "template materialization lost budget or instance identity");
      check(generated.derivation.holes.size() == 4, "forwarded/repeated hole occurrence metadata missing");
      check(generated.derivation.nodes[3].fixed && generated.derivation.nodes[3].template_instance != kNoGrammarId,
            "fixed template skeleton lost provenance");
      for (std::size_t index = 0; index < generated.derivation.choices.size(); ++index) {
        const auto& choice = generated.derivation.choices[index];
        check(choice.ast_begin < choice.ast_end && choice.ast_end <= generated.genome.ast.nodes.size(), "copied choice span invalid");
        if (choice.parent != kNoGrammarId) {
          check(choice.parent < index, "copied derivation parent points forward");
          const auto& parent = generated.derivation.choices[choice.parent];
          check(parent.ast_begin <= choice.ast_begin && parent.ast_end >= choice.ast_end, "copied derivation escaped parent span");
        }
      }
      const auto& first = generated.derivation.holes[0];
      const auto& second = generated.derivation.holes[2];
      check(first.template_instance == second.template_instance && first.slot == second.slot &&
            first.ast_end - first.ast_begin == second.ast_end - second.ast_begin, "repeated hole has different logical identity or size");
      for (std::uint32_t offset = 0; offset < first.ast_end - first.ast_begin; ++offset) {
        const auto& a = generated.genome.ast.nodes[first.ast_begin + offset];
        const auto& b = generated.genome.ast.nodes[second.ast_begin + offset];
        check(a.kind == b.kind && a.i0 == b.i0 && a.i1 == b.i1, "repeated hole was resampled");
        check(generated.derivation.nodes[first.ast_begin + offset].logical_instance ==
              generated.derivation.nodes[second.ast_begin + offset].logical_instance, "logical node identity lost on copy");
      }
      const auto result = execute_bytecode_cpu(compile_for_eval(generated.genome), {}, 10000);
      check(!result.is_error && result.value.i % 2 == 0, "duplicated template hole did not execute as a shared choice");
      check(ast_cache_key(generated.genome.ast) == ast_cache_key(generate_derivation(templates, seed).genome.ast),
            "template generation replay changed");
    }

    alias_doc.object_v["templates"] = cli_detail::JsonParser(R"([{
      "id":"Identity","type":"Int","scope":[],"holes":[{"id":"value","type":"Int","scope":[]}],
      "body":{"hole":"value"}}])", {true, 256}).parse();
    alias_doc.object_v.at("nonterminals").array_v[0].object_v.at("alternatives").array_v[1]
        .object_v.at("expression") = cli_detail::JsonParser(R"({"template":"Identity","holes":{"value":{"ref":"Expr"}}})", {true, 256}).parse();
    const auto wrapped_alias = compile_grammar(parse_definition(canonical_json(alias_doc)));
    for (std::uint64_t seed = 0; seed < 64; ++seed)
      check(generate_derivation(wrapped_alias, seed).genome.ast.nodes.size() == 5,
            "template wrapper hid a nonproductive alias path");

    auto asymmetric = cli_detail::JsonParser(R"({"format_version":"grammar-definition-v2",
      "entry":{"nonterminal":"Main","type":"Int"},"search_limits":{"max_nodes":14,"max_depth":7},"execution_limits":{"fuel":1000},
      "templates":[{"id":"Asymmetric","type":"Int","scope":[],"holes":[{"id":"x","type":"Int","scope":[]}],
        "body":{"signature":"add(Int,Int)->Int","args":[{"hole":"x"},{"signature":"neg(Int)->Int","args":[{"hole":"x"}]}]}}],
      "nonterminals":[
        {"id":"Choice","type":"Int","scope":[],"alternatives":[
          {"id":"deep","weight":10,"expression":{"signature":"neg(Int)->Int","args":[{"signature":"neg(Int)->Int","args":[{"constant":{"type":"Int","values":["2"]}}]}]}},
          {"id":"wide","weight":1,"expression":{"signature":"if(Bool,Int,Int)->Int","args":[{"constant":{"type":"Bool","values":[true]}},{"constant":{"type":"Int","values":["2"]}},{"constant":{"type":"Int","values":["2"]}}]}}]},
        {"id":"Main","type":"Int","scope":[],"alternatives":[{"id":"shared","weight":1,"expression":{"template":"Asymmetric","holes":{"x":{"ref":"Choice"}}}}]}
      ]})", {true, 256}).parse();
    const auto asymmetric_grammar = compile_grammar(parse_definition(canonical_json(asymmetric)));
    check(asymmetric_grammar.nonterminals()[asymmetric_grammar.entry()].minimum_nodes_by_depth[4] == 10,
          "shared hole minimum combined incompatible per-occurrence choices");
    for (std::uint64_t seed = 0; seed < 64; ++seed) {
      const auto generated = generate_derivation(asymmetric_grammar, seed);
      check(generated.genome.ast.nodes.size() == 14, "shared hole did not honor tightest depth occurrence");
      const auto result = execute_bytecode_cpu(compile_for_eval(generated.genome), {}, 1000);
      check(!result.is_error && result.value.i == 0, "asymmetric hole copies chose different derivations");
    }
    asymmetric.object_v.at("search_limits").object_v.at("max_nodes").number_v = 13;
    bool rejected = false;
    try { generate_derivation(compile_grammar(parse_definition(canonical_json(asymmetric))), 0); }
    catch (const std::invalid_argument& error) { rejected = std::string(error.what()).find("budget") != std::string::npos; }
    check(rejected, "impossible shared-hole budget was accepted");

    const std::vector<std::pair<std::string, std::string>> domains{
      {"Int", "[\"9223372036854775807\"]"}, {"Float", "[-0.0]"}, {"Bool", "[true]"},
      {"Char", "[\"\\ud83d\\ude00\"]"}, {"String", "[\"a\\u0000b\"]"},
      {"IntList", "[[\"-9223372036854775808\",\"9223372036854775807\"]]"},
      {"FloatList", "[[0.5,-2.25]]"}, {"StringList", "[[\"\",\"a\\u0000b\"]]"}};
    for (const auto& domain : domains) {
      const auto typed = compile_grammar(parse_definition(constant_grammar(domain.first, domain.second)));
      const auto generated = generate_derivation(typed, 7);
      const auto result = execute_bytecode_cpu(compile_for_eval(generated.genome), {}, 100);
      check(!result.is_error, "typed constant generation did not execute");
      const auto value = generated.genome.ast.consts.front();
      check(result.value.tag == value.tag, "typed constant tag changed");
      if (value.tag == ValueTag::Float)
        check(result.value.f == value.f && std::signbit(result.value.f) == std::signbit(value.f), "Float constant bits changed");
      else if (value.tag == ValueTag::Bool) check(result.value.b == value.b, "Bool constant changed");
      else check(result.value.i == value.i, "typed constant bits changed");
      if (domain.first == "String") {
        std::string decoded;
        check(payload::lookup_string(value, &decoded) && decoded == std::string("a\0b", 3), "String payload lost exact bytes");
      }
      if (domain.first == "StringList") {
        std::vector<Value> decoded;
        check(payload::lookup_list(value, &decoded) && decoded.size() == 2, "StringList payload missing");
        std::string item;
        check(payload::lookup_string(decoded[1], &item) && item == std::string("a\0b", 3), "nested String payload missing");
      }
      const auto serialized = canonical_json(encode_constant(value));
      payload::clear();
      const auto restored = decode_constant(cli_detail::JsonParser(serialized, {true, 256}).parse());
      check(canonical_json(encode_constant(restored)) == serialized, "constant artifact changed after registry reset");
    }
    bool missing_payload = false;
    try { encode_constant(Value::from_string_hash_len(12345, 5)); }
    catch (const std::invalid_argument&) { missing_payload = true; }
    check(missing_payload, "opaque payload was serialized as an exact constant");
    std::cout << "grammar materialization: 512 scalar seeds and all eight typed domains passed\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
