#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/repro/pack.hpp"

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;

namespace {
void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void rejects(const CompiledGrammar& grammar, const ProgramGenome& genome) {
  check(static_cast<bool>(verify_ast(genome.ast, {})), "rejection fixture must remain native-valid");
  try { require_membership(grammar, genome); }
  catch (const std::invalid_argument&) { return; }
  throw std::runtime_error("nonmember native AST was accepted");
}
cli_detail::JsonValue json(const std::string& text) {
  return cli_detail::JsonParser(text, {true, 256}).parse();
}
CompiledGrammar compile(const cli_detail::JsonValue& doc) {
  return compile_grammar(parse_definition(canonical_json(doc)));
}
std::string constant_grammar(const std::string& type, const std::string& values) {
  return R"({"format_version":"grammar-definition-v2","entry":{"nonterminal":"Value","type":")" + type +
    R"("},"search_limits":{"max_nodes":5,"max_depth":4},"execution_limits":{"fuel":100},
    "nonterminals":[{"id":"Value","type":")" + type + R"(","scope":[],"alternatives":[
    {"id":"constant","weight":1,"expression":{"constant":{"type":")" + type + R"(","values":)" + values + "}}}]}]}";
}
}  // namespace

int main() {
  try {
    const auto scalar_doc = json(R"({"format_version":"grammar-definition-v2",
      "entry":{"nonterminal":"Expr","type":"Int"},
      "search_limits":{"max_nodes":40,"max_depth":12},"execution_limits":{"fuel":1000},
      "nonterminals":[{"id":"Expr","type":"Int","scope":[],"alternatives":[
        {"id":"leaf","weight":1,"expression":{"constant":{"type":"Int","range":["-3","3"]}}},
        {"id":"sum","weight":4,"expression":{"signature":"add(Int,Int)->Int","args":[{"ref":"Expr"},{"ref":"Expr"}]}}
      ]}]})");
    const auto scalar = compile(scalar_doc);
    ProgramGenome sum;
    for (std::uint64_t seed = 0; seed < 256; ++seed) {
      auto generated = generate_derivation(scalar, seed).genome;
      require_membership(scalar, generated);
      generated.derivation.reset();
      require_membership(scalar, generated);
      if (generated.ast.nodes[3].kind == NodeKind::ADD) sum = generated;
    }
    check(!sum.ast.nodes.empty(), "scalar seeds did not exercise recursive sum");
    auto forbidden = sum;
    forbidden.ast.nodes[3].kind = NodeKind::SUB;
    rejects(scalar, forbidden);
    forbidden = sum;
    for (auto& constant : forbidden.ast.consts) constant = Value::from_int(4);
    rejects(scalar, forbidden);
    auto changed = sum;
    for (auto& constant : changed.ast.consts) constant = Value::from_int(-3);
    require_membership(scalar, changed);
    for (auto& constant : changed.ast.consts) constant = Value::from_int(3);
    require_membership(scalar, changed);
    changed.ast.names.push_back("unused_name");
    changed.ast.consts.push_back(Value::from_int(999));
    require_membership(scalar, changed);
    require_membership(scalar, repro::compact_genome_tables(changed));

    auto tight = scalar_doc;
    tight.object_v.at("search_limits").object_v.at("max_nodes").number_v = 5;
    rejects(compile(tight), sum);
    tight = scalar_doc;
    tight.object_v.at("search_limits").object_v.at("max_depth").number_v = 4;
    rejects(compile(tight), sum);

    const std::vector<std::pair<std::string, std::string>> domains{
      {"Int", "[\"-9223372036854775808\",\"9223372036854775807\"]"},
      {"Float", "[-0.0]"}, {"Bool", "[true]"}, {"Char", "[\"\\ud83d\\ude00\"]"},
      {"String", "[\"a\\u0000b\"]"}, {"IntList", "[[\"-3\",\"7\"]]"},
      {"FloatList", "[[-0.0,2.25]]"}, {"StringList", "[[\"\",\"a\\u0000b\"]]"}};
    for (const auto& domain : domains) {
      const auto grammar = compile_grammar(parse_definition(constant_grammar(domain.first, domain.second)));
      for (std::uint64_t seed = 0; seed < 16; ++seed) {
        auto genome = generate_derivation(grammar, seed).genome;
        genome.derivation.reset();
        require_membership(grammar, genome);
        if (domain.first == "Float") {
          genome.ast.consts[0] = Value::from_float(0.0);
          rejects(grammar, genome);
        }
      }
    }

    auto template_doc = scalar_doc;
    template_doc.object_v["templates"] = json(R"([
      {"id":"Double","type":"Int","scope":[],"holes":[{"id":"value","type":"Int","scope":[]}],
       "body":{"signature":"add(Int,Int)->Int","args":[{"hole":"value"},{"hole":"value"}]}},
      {"id":"Forward","type":"Int","scope":[],"holes":[{"id":"value","type":"Int","scope":[]}],
       "body":{"template":"Double","holes":{"value":{"hole":"value"}}}}
    ])");
    template_doc.object_v.at("nonterminals").array_v.push_back(json(R"({
      "id":"Main","type":"Int","scope":[],"alternatives":[{"id":"forward","weight":1,
      "expression":{"template":"Forward","holes":{"value":{"ref":"Expr"}}}}]})"));
    template_doc.object_v.at("entry").object_v.at("nonterminal").string_v = "Main";
    const auto templates = compile(template_doc);
    for (std::uint64_t seed = 0; seed < 64; ++seed) {
      auto genome = generate_derivation(templates, seed).genome;
      require_membership(templates, genome);
      genome.derivation.reset();
      require_membership(templates, genome);
    }
    // Force two one-node occurrences, then distinguish index equality from value equality.
    template_doc.object_v.at("search_limits").object_v.at("max_nodes").number_v = 7;
    const auto leaves = compile(template_doc);
    auto repeated = generate_derivation(leaves, 7).genome;
    repeated.derivation.reset();
    check(repeated.ast.nodes[4].kind == NodeKind::CONST && repeated.ast.nodes[5].kind == NodeKind::CONST,
          "repeated-hole fixture did not generate leaves");
    repeated.ast.consts = {Value::from_int(1), Value::from_int(1)};
    repeated.ast.nodes[4].i0 = 0;
    repeated.ast.nodes[5].i0 = 1;
    require_membership(leaves, repeated);
    require_membership(leaves, repro::compact_genome_tables(repeated));
    repeated.ast.consts[1] = Value::from_int(2);
    rejects(leaves, repeated);

    const auto fuel_grammar = compile(json(R"({
      "format_version":"grammar-definition-v2",
      "entry":{"nonterminal":"Main","type":"Int"},
      "search_limits":{"max_nodes":12,"max_depth":8},
      "execution_limits":{"fuel":100},
      "nonterminals":[
        {"id":"Leaf","type":"Int","scope":[],"alternatives":[
          {"id":"one","weight":1,"expression":{"constant":{"type":"Int","values":["0"]},"fuel_events":{"operation":1}}},
          {"id":"two","weight":1,"expression":{"constant":{"type":"Int","values":["0"]},"fuel_events":{"operation":2}}}]},
        {"id":"Main","type":"Int","scope":[],"alternatives":[{"id":"double","weight":1,
          "expression":{"template":"Double","holes":{"value":{"ref":"Leaf"}}}}]}],
      "templates":[{"id":"Double","type":"Int","scope":[],
        "holes":[{"id":"value","type":"Int","scope":[]}],
        "body":{"signature":"add(Int,Int)->Int","args":[{"hole":"value"},{"hole":"value"}]}}]
    })"));
    auto fuel_repeated = generate_derivation(fuel_grammar, 3).genome;
    check(fuel_repeated.ast.fuel_specs.size() == 2,
          "generation did not copy a repeated-hole fuel profile");
    require_membership(fuel_grammar, fuel_repeated);
    fuel_repeated.derivation.reset();
    require_membership(fuel_grammar, fuel_repeated);
    (void)reconstruct_derivation(fuel_grammar, fuel_repeated);
    require_membership(fuel_grammar,
                       repro::compact_genome_tables(fuel_repeated));
    auto divergent_profile = fuel_repeated;
    divergent_profile.ast.fuel_specs[1].charges[0].cost =
        divergent_profile.ast.fuel_specs[0].charges[0].cost == 1 ? 2 : 1;
    rejects(fuel_grammar, divergent_profile);
    auto missing_profile = fuel_repeated;
    missing_profile.ast.fuel_specs.erase(missing_profile.ast.fuel_specs.begin());
    rejects(fuel_grammar, missing_profile);

    const auto branch_fuel = compile(json(R"({
      "format_version":"grammar-definition-v2",
      "entry":{"nonterminal":"Expr","type":"Int"},
      "search_limits":{"max_nodes":12,"max_depth":8},
      "execution_limits":{"fuel":100},
      "nonterminals":[{"id":"Expr","type":"Int","scope":[],"alternatives":[{
        "id":"branch","weight":1,"expression":{
          "signature":"if(Bool,Int,Int)->Int",
          "fuel_events":{"branch_test":0,"branch_merge":3},
          "args":[{"constant":{"type":"Bool","values":[true]}},
                  {"constant":{"type":"Int","values":["1"]}},
                  {"constant":{"type":"Int","values":["2"]}}]}}]}]
    })"));
    auto branch = generate_derivation(branch_fuel, 0).genome;
    check(branch.ast.fuel_specs.size() == 1 &&
              branch.ast.fuel_specs[0].node_index == 3 &&
              branch.ast.fuel_specs[0].charges.size() == 2,
          "generation did not attach the authored profile to its physical owner");
    require_membership(branch_fuel, branch);
    std::reverse(branch.ast.fuel_specs[0].charges.begin(),
                 branch.ast.fuel_specs[0].charges.end());
    require_membership(branch_fuel, branch);
    branch.ast.fuel_specs[0].charges[0].cost += 1;
    rejects(branch_fuel, branch);

    auto undeclared_profile = generate_derivation(scalar, 0).genome;
    undeclared_profile.ast.fuel_specs.push_back(
        {3, {{FuelEvent::Operation, 0}}});
    rejects(scalar, undeclared_profile);

    auto aliases = scalar_doc;
    aliases.object_v.at("nonterminals").array_v[0].object_v.at("alternatives").array_v.insert(
        aliases.object_v.at("nonterminals").array_v[0].object_v.at("alternatives").array_v.begin(),
        json(R"({"id":"alias","weight":1e300,"expression":{"ref":"Expr"}})"));
    const auto alias_grammar = compile(aliases);
    require_membership(alias_grammar, sum);
    for (std::uint64_t seed = 0; seed < 32; ++seed)
      require_membership(alias_grammar, generate_derivation(alias_grammar, seed).genome);
    aliases.object_v.at("nonterminals").array_v.push_back(json(R"({
      "id":"Other","type":"Int","scope":[],"alternatives":[
        {"id":"back","weight":1,"expression":{"ref":"Expr"}},
        {"id":"exit","weight":1,"expression":{"constant":{"type":"Int","values":["0"]}}}]} )"));
    aliases.object_v.at("nonterminals").array_v[0].object_v.at("alternatives").array_v[0]
        .object_v.at("expression").object_v.at("ref").string_v = "Other";
    const auto mutual = compile(aliases);
    require_membership(mutual, sum);
    for (std::uint64_t seed = 0; seed < 32; ++seed)
      require_membership(mutual, generate_derivation(mutual, seed).genome);
    std::cout << "grammar membership passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
