#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/core/value.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/grammar/donor.hpp"
#include "../../src/evolution/grammar/variation_internal.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;

namespace {

void check(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

CompiledGrammar compile(const std::string& text) {
  return compile_grammar(parse_definition(text));
}

void rejects_member(const CompiledGrammar& grammar, const ProgramGenome& genome,
                    const std::string& message) {
  check(static_cast<bool>(verify_ast(genome.ast, {})),
        message + ": malformed fixture is not a native-valid AST");
  try {
    require_membership(grammar, genome);
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error(message);
}

std::int64_t execute_int(const CompiledGrammar& grammar,
                         const ProgramGenome& genome,
                         const std::vector<std::pair<int, Value>>& inputs = {}) {
  const auto result = execute_bytecode_cpu(
      compile_for_eval(genome), inputs, grammar.execution_limits().fuel);
  check(!result.is_error, "generated region program failed execution");
  check(result.value.tag == ValueTag::Int,
        "generated region program returned a non-Int value");
  return result.value.i;
}

void check_global_binder_ids(const AstProgram& ast) {
  std::set<int> ids;
  for (const auto& region : ast.lexical_regions) {
    for (const auto& binding : region.bindings) {
      check(binding.id >= 0, "generated lexical binder ID is negative");
      check(ids.insert(binding.id).second,
            "generated lexical binder IDs are not globally unique");
    }
  }
}

bool same_choices(const DerivationMetadata& left,
                  const DerivationMetadata& right) {
  if (left.choices.size() != right.choices.size()) return false;
  for (std::size_t i = 0; i < left.choices.size(); ++i) {
    const auto& a = left.choices[i];
    const auto& b = right.choices[i];
    if (a.nonterminal != b.nonterminal || a.production != b.production ||
        a.parent != b.parent || a.ast_begin != b.ast_begin ||
        a.ast_end != b.ast_end ||
        a.template_instance != b.template_instance || a.slot != b.slot ||
        a.enclosing_template_depth != b.enclosing_template_depth) {
      return false;
    }
  }
  return true;
}

void check_generation_contract(const CompiledGrammar& grammar,
                               const GeneratedDerivation& generated,
                               std::int64_t expected) {
  require_membership(grammar, generated.genome);
  VerifiedAst verified;
  std::vector<std::vector<int>> environments;
  const auto first = reconstruct_derivation(grammar, generated.genome,
      entry_request(grammar), &verified, &environments);
  check(environments.size() == first.choices.size(), "witness lexical sidecar lost choice alignment");
  for (std::size_t i = 0; i < first.choices.size(); ++i) {
    const auto& choice = first.choices[i];
    const auto& formal = grammar.nonterminals().at(choice.nonterminal).scope;
    check(environments[i].size() == formal.size(), "witness lexical sidecar lost formal arity");
    if (formal.empty()) continue;
    const auto& native = verified.scopes.at(verified.expression_scope_ids.at(choice.ast_begin));
    for (std::size_t j = 0; j < formal.size(); ++j) {
      const auto expected = std::make_pair(-environments[i][j] - 1, formal[j].type);
      check(environments[i][j] >= 0 &&
          std::find(native.binders.begin(), native.binders.end(), expected) != native.binders.end(),
          "witness lexical sidecar references a binder outside its physical scope");
    }
  }
  const auto second = reconstruct_derivation(grammar, generated.genome);
  check(same_choices(first, second),
        "region membership reconstruction changed its production choices");
  check(execute_int(grammar, generated.genome) == expected,
        "generated region program returned the wrong value");
  check_global_binder_ids(generated.genome.ast);
  const auto analysis = analyze_variation(grammar, generated.genome);
  for (const auto& site : analysis.sites) {
    const auto donor = generate_donor(grammar, 42, site);
    const auto projected = project_frame(grammar, donor_request(site), donor.frame, donor.genome.ast);
    check(static_cast<bool>(verify_ast(projected.ast, projected.inputs)),
        "contextual region donor projection failed native verification");
    ProgramGenome child;
    child.ast = variation_detail::splice(generated.genome.ast, site, donor.genome.ast,
        donor.payload, site.occurrence_binder_ids.front());
    require_membership(grammar, child);
    check_global_binder_ids(child.ast);
  }
}

void test_documented_custom_grammar() {
  const auto grammar = compile_grammar(load_definition(
      std::string(GAGP_REPOSITORY_ROOT) +
      "/configs/grammar_definitions/custom_integer.json"));
  bool saw_region = false;
  for (std::uint64_t seed = 0; seed < 256 && !saw_region; ++seed) {
    const auto generated = generate_derivation(grammar, seed);
    require_membership(grammar, generated.genome);
    const auto result = execute_bytecode_cpu(
        compile_for_eval(generated.genome), {{0, Value::from_int(3)}},
        grammar.execution_limits().fuel);
    check(!result.is_error && result.value.tag == ValueTag::Int,
          "documented custom grammar generated an inexecutable program");
    saw_region = !generated.genome.ast.lexical_regions.empty();
    if (saw_region) check_global_binder_ids(generated.genome.ast);
  }
  check(saw_region,
        "256 documented custom-grammar seeds did not exercise its lexical region");
}

const char* nested_scope_grammar = R"({
  "format_version":"grammar-definition-v1",
  "entry":{"nonterminal":"Main","type":"Int"},
  "search_limits":{"max_nodes":20,"max_depth":10},
  "execution_limits":{"fuel":1000},
  "nonterminals":[
    {"id":"Main","type":"Int","scope":[],"alternatives":[{"id":"nested","weight":1,
      "expression":{"signature":"let(Int,Int)->Int","args":[
        {"constant":{"type":"Int","values":["2"]}},
        {"signature":"let(Int,Int)->Int","args":[
          {"constant":{"type":"Int","values":["3"]}},
          {"ref":"Pair"}],"bind":{"1":["y"]}}],"bind":{"1":["x"]}}}]},
    {"id":"Pair","type":"Int","scope":[
      {"name":"y","type":"Int"},{"name":"x","type":"Int"}],
      "alternatives":[{"id":"sum","weight":1,
        "expression":{"signature":"add(Int,Int)->Int","args":[
          {"bound":"x"},{"bound":"y"}]}}]}
  ]
})";

void test_nested_regions_and_scope_projection() {
  const auto grammar = compile(nested_scope_grammar);
  const auto generated = generate_derivation(grammar, 17);
  check_generation_contract(grammar, generated, 5);
  check(generated.genome.ast.lexical_regions.size() == 2,
        "nested region grammar did not materialize both lexical regions");

  const int x = generated.genome.ast.lexical_regions[0].bindings[0].id;
  const int y = generated.genome.ast.lexical_regions[1].bindings[0].id;
  auto captured = generated.genome;
  const auto found = std::find_if(captured.ast.nodes.begin(), captured.ast.nodes.end(),
      [&](const AstNode& node) {
        return node.kind == NodeKind::REGION_VAR && node.i0 == x;
      });
  check(found != captured.ast.nodes.end(),
        "nested region fixture did not contain its projected x reference");
  found->i0 = y;
  rejects_member(grammar, captured,
                 "membership accepted x rebound to visible same-type y");

  auto renamed = generated.genome;
  std::map<int, int> renames;
  int next = 100;
  for (auto& region : renamed.ast.lexical_regions) {
    for (auto& binding : region.bindings) {
      renames.emplace(binding.id, next);
      binding.id = next++;
    }
  }
  for (auto& node : renamed.ast.nodes) {
    if (node.kind == NodeKind::REGION_VAR) node.i0 = renames.at(node.i0);
  }
  require_membership(grammar, renamed);
  check(execute_int(grammar, renamed) == 5,
        "alpha-renamed lexical declaration IDs changed execution");
  check_global_binder_ids(renamed.ast);

  const auto replay = generate_derivation(grammar, 17);
  check(ast_cache_key(generated.genome.ast) == ast_cache_key(replay.genome.ast) &&
            same_choices(generated.derivation, replay.derivation),
        "region generation is not deterministic for an identical seed");
}

std::string traversal_grammar(const std::string& operation, bool ranged) {
  const std::string signature = ranged
      ? operation + "(IntList,Int,Int,Int,Int,Int)->Int"
      : operation + "(IntList,Int,Int,Int)->Int";
  const std::string arguments = ranged
      ? R"([
          {"constant":{"type":"IntList","values":[["1","2","3","4"]]}},
          {"constant":{"type":"Int","values":["0"]}},
          {"constant":{"type":"Int","values":["1"]}},
          {"constant":{"type":"Int","values":["3"]}},
          {"constant":{"type":"Int","values":["0"]}},
          {"ref":"Step"}])"
      : R"([
          {"constant":{"type":"IntList","values":[["1","2","3","4"]]}},
          {"constant":{"type":"Int","values":["0"]}},
          {"constant":{"type":"Int","values":["0"]}},
          {"ref":"Step"}])";
  const std::string body_argument = ranged ? "5" : "3";
  return R"({"format_version":"grammar-definition-v1",
    "entry":{"nonterminal":"Main","type":"Int"},
    "search_limits":{"max_nodes":24,"max_depth":10},
    "execution_limits":{"fuel":10000},
    "nonterminals":[
      {"id":"Main","type":"Int","scope":[],"alternatives":[{"id":"walk","weight":1,
        "expression":{"signature":")" + signature + R"(","args":)" + arguments +
        R"(,"bind":{")" + body_argument + R"(":["element","index","accumulator"]}}}]},
      {"id":"Step","type":"Int","scope":[
        {"name":"accumulator","type":"Int"},
        {"name":"element","type":"Int"}],"alternatives":[{"id":"append_digit","weight":1,
        "expression":{"signature":"add(Int,Int)->Int","args":[
          {"signature":"mul(Int,Int)->Int","args":[
            {"bound":"accumulator"},{"constant":{"type":"Int","values":["10"]}}]},
          {"bound":"element"}]}}]}
    ]})";
}

void test_traversal_directions_and_ranges() {
  struct Case {
    const char* operation;
    bool ranged;
    std::int64_t expected;
  };
  const std::vector<Case> cases{
      {"traverse", false, 1234},
      {"traverse_reverse", false, 4321},
      {"traverse_range", true, 23},
      {"traverse_range_reverse", true, 32},
  };
  for (const auto& test : cases) {
    const auto grammar = compile(traversal_grammar(test.operation, test.ranged));
    const auto generated = generate_derivation(grammar, 9);
    check_generation_contract(grammar, generated, test.expected);
    check(generated.genome.ast.traversal_specs.size() == 1,
          std::string(test.operation) + " did not materialize direction metadata");
    if (std::string(test.operation) == "traverse") {
      auto flipped = generated.genome;
      flipped.ast.traversal_specs[0].direction = TraversalDirection::Reverse;
      rejects_member(grammar, flipped,
                     "forward-only traversal grammar accepted a direction flip");
    }
  }
}

const char* sibling_hole_grammar = R"({
  "format_version":"grammar-definition-v1",
  "entry":{"nonterminal":"Main","type":"Int"},
  "search_limits":{"max_nodes":20,"max_depth":10},
  "execution_limits":{"fuel":1000},
  "templates":[{"id":"Sibling","type":"Int","scope":[],
    "holes":[{"id":"value","type":"Int","scope":[{"name":"x","type":"Int"}]}],
    "body":{"signature":"add(Int,Int)->Int","args":[
      {"signature":"let(Int,Int)->Int","args":[
        {"constant":{"type":"Int","values":["1"]}},{"hole":"value"}],"bind":{"1":["x"]}},
      {"signature":"let(Int,Int)->Int","args":[
        {"constant":{"type":"Int","values":["2"]}},{"hole":"value"}],"bind":{"1":["x"]}}]} }],
  "nonterminals":[
    {"id":"Main","type":"Int","scope":[],"alternatives":[{"id":"siblings","weight":1,
      "expression":{"template":"Sibling","holes":{"value":{"ref":"X"}}}}]},
    {"id":"X","type":"Int","scope":[{"name":"x","type":"Int"}],
      "alternatives":[{"id":"x","weight":1,"expression":{"bound":"x"}}]}
  ]
})";

const char* nested_payload_hole_grammar = R"({
  "format_version":"grammar-definition-v1",
  "entry":{"nonterminal":"Main","type":"Int"},
  "search_limits":{"max_nodes":30,"max_depth":12},
  "execution_limits":{"fuel":1000},
  "templates":[{"id":"Sibling","type":"Int","scope":[],
    "holes":[{"id":"value","type":"Int","scope":[{"name":"x","type":"Int"}]}],
    "body":{"signature":"add(Int,Int)->Int","args":[
      {"signature":"let(Int,Int)->Int","args":[
        {"constant":{"type":"Int","values":["1"]}},{"hole":"value"}],"bind":{"1":["x"]}},
      {"signature":"let(Int,Int)->Int","args":[
        {"constant":{"type":"Int","values":["2"]}},{"hole":"value"}],"bind":{"1":["x"]}}]} }],
  "nonterminals":[
    {"id":"Main","type":"Int","scope":[],"alternatives":[{"id":"siblings","weight":1,
      "expression":{"template":"Sibling","holes":{"value":
        {"signature":"let(Int,Int)->Int","args":[
          {"constant":{"type":"Int","values":["10"]}},
          {"signature":"add(Int,Int)->Int","args":[{"bound":"x"},{"bound":"y"}]}],
         "bind":{"1":["y"]}}}}}]}
  ]
})";

void test_repeated_holes_across_sibling_regions() {
  const auto grammar = compile(sibling_hole_grammar);
  const auto generated = generate_derivation(grammar, 3);
  check_generation_contract(grammar, generated, 3);
  check(generated.derivation.holes.size() == 2,
        "sibling repeated hole did not retain both occurrences");
  const auto& first = generated.derivation.holes[0];
  const auto& second = generated.derivation.holes[1];
  check(first.template_instance == second.template_instance &&
            first.slot == second.slot,
        "sibling hole occurrences lost their shared logical identity");
  check(generated.genome.ast.nodes[first.ast_begin].kind == NodeKind::REGION_VAR &&
            generated.genome.ast.nodes[second.ast_begin].kind == NodeKind::REGION_VAR &&
            generated.genome.ast.nodes[first.ast_begin].i0 !=
                generated.genome.ast.nodes[second.ast_begin].i0,
        "sibling repeated hole did not rebind its free x reference");

  const auto payload = compile(nested_payload_hole_grammar);
  const auto nested = generate_derivation(payload, 5);
  check_generation_contract(payload, nested, 23);
  check(nested.genome.ast.lexical_regions.size() == 4,
        "copied hole payload lost an internal Let declaration");
  check_global_binder_ids(nested.genome.ast);
}

const char* forwarded_scope_grammar = R"({
  "format_version":"grammar-definition-v1",
  "entry":{"nonterminal":"Main","type":"Int"},
  "search_limits":{"max_nodes":20,"max_depth":10},
  "execution_limits":{"fuel":1000},
  "templates":[
    {"id":"Inner","type":"Int","scope":[{"name":"x","type":"Int"}],
      "holes":[{"id":"inner","type":"Int","scope":[{"name":"x","type":"Int"}]}],
      "body":{"signature":"add(Int,Int)->Int","args":[{"hole":"inner"},{"bound":"x"}]}},
    {"id":"Outer","type":"Int","scope":[{"name":"x","type":"Int"}],
      "holes":[{"id":"outer","type":"Int","scope":[{"name":"x","type":"Int"}]}],
      "body":{"template":"Inner","holes":{"inner":{"hole":"outer"}}}}
  ],
  "nonterminals":[
    {"id":"Main","type":"Int","scope":[],"alternatives":[{"id":"let","weight":1,
      "expression":{"signature":"let(Int,Int)->Int","args":[
        {"constant":{"type":"Int","values":["4"]}},
        {"template":"Outer","holes":{"outer":{"ref":"X"}}}],"bind":{"1":["x"]}}}]},
    {"id":"X","type":"Int","scope":[{"name":"x","type":"Int"}],
      "alternatives":[{"id":"x","weight":1,"expression":{"bound":"x"}}]}
  ]
})";

void test_forwarded_nonempty_scope_hole() {
  const auto grammar = compile(forwarded_scope_grammar);
  const auto generated = generate_derivation(grammar, 11);
  check_generation_contract(grammar, generated, 8);
  check(generated.derivation.templates.size() == 2 &&
            generated.derivation.holes.size() == 2,
        "forwarded scoped hole lost template or occurrence provenance");
}

const char* divergent_constant_grammar = R"({
  "format_version":"grammar-definition-v1",
  "entry":{"nonterminal":"Main","type":"Int"},
  "search_limits":{"max_nodes":10,"max_depth":7},
  "execution_limits":{"fuel":100},
  "templates":[{"id":"Double","type":"Int","scope":[],
    "holes":[{"id":"value","type":"Int","scope":[]}],
    "body":{"signature":"add(Int,Int)->Int","args":[{"hole":"value"},{"hole":"value"}]}}],
  "nonterminals":[
    {"id":"Choice","type":"Int","scope":[],"alternatives":[
      {"id":"one","weight":1,"expression":{"constant":{"type":"Int","values":["1"]}}},
      {"id":"two","weight":1,"expression":{"constant":{"type":"Int","values":["2"]}}}]},
    {"id":"Main","type":"Int","scope":[],"alternatives":[{"id":"double","weight":1,
      "expression":{"template":"Double","holes":{"value":{"ref":"Choice"}}}}]}
  ]
})";

void test_repeated_hole_divergence() {
  const auto grammar = compile(divergent_constant_grammar);
  const auto generated = generate_derivation(grammar, 13);
  check(generated.derivation.holes.size() == 2,
        "constant repeated-hole fixture lost an occurrence");
  auto divergent = generated.genome;
  const auto& left = generated.derivation.holes[0];
  const auto& right = generated.derivation.holes[1];
  check(left.ast_end - left.ast_begin == 1 && right.ast_end - right.ast_begin == 1 &&
            divergent.ast.nodes[right.ast_begin].kind == NodeKind::CONST,
        "constant repeated-hole fixture did not materialize singleton leaves");
  const auto original = divergent.ast.consts.at(
      divergent.ast.nodes[left.ast_begin].i0).i;
  divergent.ast.consts.push_back(Value::from_int(original == 1 ? 2 : 1));
  divergent.ast.nodes[right.ast_begin].i0 =
      static_cast<int>(divergent.ast.consts.size() - 1);
  rejects_member(grammar, divergent,
                 "membership accepted divergent but individually admissible hole copies");
}

}  // namespace

int main() {
  try {
    test_documented_custom_grammar();
    test_nested_regions_and_scope_projection();
    test_traversal_directions_and_ranges();
    test_repeated_holes_across_sibling_regions();
    test_forwarded_nonempty_scope_hole();
    test_repeated_hole_divergence();
    std::cout << "gagp_test_grammar_regions: OK\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
