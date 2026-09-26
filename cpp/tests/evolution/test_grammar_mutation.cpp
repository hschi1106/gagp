#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/evolution/mutation.hpp"

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;

namespace {

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

std::shared_ptr<const CompiledGrammar> compile(const std::string& text) {
  return std::make_shared<const CompiledGrammar>(
      compile_grammar(parse_definition(text)));
}

std::string constant_grammar(const std::string& type,
    const std::string& domain) {
  return R"({"format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Value","type":")" + type +
    R"("},"search_limits":{"max_nodes":5,"max_depth":4},
    "execution_limits":{"fuel":100},"nonterminals":[{
      "id":"Value","type":")" + type +
    R"(","scope":[],"alternatives":[{"id":"constant","weight":1,
      "expression":{"constant":{"type":")" + type + R"(",)" + domain +
    R"(}}}]}]})";
}

void require_valid_child(const CompiledGrammar& grammar,
    const ProgramGenome& child) {
  std::vector<InputSpec> inputs;
  for (const auto& input : grammar.inputs())
    inputs.push_back({input.name, input.type});
  const auto verified = verify_ast(child.ast, inputs);
  check(verified &&
        verified.verified.return_type ==
            grammar.nonterminals().at(grammar.entry()).type,
      "compiled mutation returned a natively invalid child");
  require_membership(grammar, child);
  check(child.derivation && !child.derivation->seed_replayable &&
        child.derivation->grammar_hash == grammar.content_hash(),
      "compiled mutation returned uncertified provenance");
}

void check_counters(const VariationCounters& counters,
    std::uint64_t returned, const char* message) {
  check(counters.mutation_attempts == returned &&
        counters.changed_children + counters.unchanged_children == returned &&
        counters.fallback_children <= counters.unchanged_children &&
        counters.changed_children > 0 && counters.acceptance_rejections == 0 &&
        counters.generation_rejections == 0,
      message);
}

std::shared_ptr<const CompiledGrammar> repeated_hole_grammar() {
  return compile(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","type":"Int"},
    "search_limits":{"max_nodes":9,"max_depth":7},
    "execution_limits":{"fuel":100},
    "templates":[{
      "id":"TwiceWithFixed","type":"Int","scope":[],
      "holes":[{"id":"value","type":"Int","scope":[]}],
      "body":{"signature":"add(Int,Int)->Int","args":[
        {"constant":{"type":"Int","values":["99"]}},
        {"signature":"add(Int,Int)->Int","args":[
          {"hole":"value"},{"hole":"value"}]}]}
    }],
    "nonterminals":[
      {"id":"Value","type":"Int","scope":[],"alternatives":[
        {"id":"value","weight":1,"expression":{"constant":{
          "type":"Int","values":["1","2","3"]}}}]},
      {"id":"Main","type":"Int","scope":[],"alternatives":[
        {"id":"main","weight":1,"expression":{
          "template":"TwiceWithFixed","holes":{"value":{"ref":"Value"}}}}]}
    ]
  })");
}

void check_repeated_template(const ProgramGenome& genome) {
  check(genome.ast.nodes.size() == 9 &&
        genome.ast.nodes[3].kind == NodeKind::ADD &&
        genome.ast.nodes[4].kind == NodeKind::CONST &&
        genome.ast.consts.at(genome.ast.nodes[4].i0).tag == ValueTag::Int &&
        genome.ast.consts.at(genome.ast.nodes[4].i0).i == 99 &&
        genome.ast.nodes[5].kind == NodeKind::ADD &&
        genome.ast.nodes[6].kind == NodeKind::CONST &&
        genome.ast.nodes[7].kind == NodeKind::CONST,
      "mutation changed the fixed template skeleton or its constant");
  const auto& left = genome.ast.consts.at(genome.ast.nodes[6].i0);
  const auto& right = genome.ast.consts.at(genome.ast.nodes[7].i0);
  check(left.tag == ValueTag::Int && right.tag == ValueTag::Int &&
        left.i == right.i && left.i >= 1 && left.i <= 3,
      "mutation split an atomic repeated template hole or escaped its domain");
}

void test_repeated_holes_and_fixed_skeleton() {
  const auto grammar = repeated_hole_grammar();
  for (const double subtree_probability : {1.0, 0.0}) {
    VariationContext context(grammar);
    auto genome = generate_derivation(*grammar, 7).genome;
    for (std::uint64_t seed = 0; seed < 64; ++seed) {
      genome = mutate(genome, 1000 + seed, context, subtree_probability);
      require_valid_child(*grammar, genome);
      check_repeated_template(genome);
    }
    check_counters(context.counters(), 64,
        "template mutation counters do not account for every returned child");
  }
}

void test_all_constant_domains() {
  const std::vector<std::pair<std::string, std::string>> domains{
      {"Int", "\"values\":[\"-3\",\"7\"]"},
      {"Float", "\"values\":[-0.0,2.25]"},
      {"Bool", "\"values\":[false,true]"},
      {"Char", "\"values\":[\"a\",\"\\ud83d\\ude00\"]"},
      {"String", "\"values\":[\"\",\"a\\u0000b\"]"},
      {"IntList", "\"values\":[[],[\"-3\",\"7\"]]"},
      {"FloatList", "\"values\":[[],[-0.0,2.25]]"},
      {"StringList", "\"values\":[[],[\"\",\"a\\u0000b\"]]"}};

  for (const auto& domain : domains) {
    const auto grammar = compile(constant_grammar(domain.first, domain.second));
    VariationContext context(grammar);
    auto genome = generate_derivation(*grammar, 3).genome;
    for (std::uint64_t seed = 0; seed < 64; ++seed) {
      genome = mutate(genome, 2000 + seed, context, 0.0);
      require_valid_child(*grammar, genome);
    }
    check_counters(context.counters(), 64,
        "typed constant mutation counters do not account for every returned child");
  }
}

std::shared_ptr<const CompiledGrammar> assigned_local_grammar() {
  return compile(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","category":"Program","type":"Int"},
    "inputs":[{"name":"input","type":"Int"}],
    "locals":[{"name":"x","type":"Int"}],
    "search_limits":{"max_nodes":8,"max_depth":7},
    "execution_limits":{"fuel":100},
    "nonterminals":[
      {"id":"Seed","type":"Int","scope":[],"alternatives":[
        {"id":"seed","weight":1,"expression":{"constant":{
          "type":"Int","values":["1","2"]}}}]},
      {"id":"LocalX","type":"Int","scope":[],"alternatives":[
        {"id":"x","weight":1,"expression":{"local":"x"}}]},
      {"id":"Main","category":"Program","type":"Int","scope":[],"alternatives":[
        {"id":"body","weight":1,"expression":{
          "control":"program(Block)->Program","type":"Int","args":[
            {"control":"block_cons(Statement,Block)->Block","type":"Int","args":[
              {"control":"assign(Int)->Statement","type":"Int","name":"x",
               "args":[{"ref":"Seed"}]},
              {"control":"block_cons(Statement,Block)->Block","type":"Int","args":[
                {"control":"return(Int)->Statement","type":"Int",
                 "args":[{"ref":"LocalX"}]},
                {"control":"block_nil()->Block","type":"Int","args":[]}]}]}]}}]}
    ]
  })");
}

void test_destination_local_mutation_returns_full_valid_children() {
  const auto grammar = assigned_local_grammar();
  VariationContext context(grammar);
  auto genome = generate_derivation(*grammar, 11).genome;
  for (std::uint64_t seed = 0; seed < 64; ++seed) {
    genome = mutate(genome, 3000 + seed, context, 1.0);
    check(genome.ast.nodes.size() == 8 &&
          genome.ast.nodes.front().kind == NodeKind::PROGRAM &&
          genome.ast.nodes[6].kind == NodeKind::VAR &&
          genome.ast.names.at(genome.ast.nodes[6].i0) == "x",
        "destination-local mutation did not return the complete parent program");
    require_valid_child(*grammar, genome);
  }
  check_counters(context.counters(), 64,
      "destination-local mutation counters do not account for every returned child");
}

void test_imported_invalid_parent_rejected() {
  const auto grammar = compile(constant_grammar("Int", "\"values\":[\"0\",\"1\"]"));
  VariationContext context(grammar);
  auto invalid = generate_derivation(*grammar, 5).genome;
  invalid.derivation.reset();
  invalid.ast.consts.at(invalid.ast.nodes[3].i0) = Value::from_int(9);
  check(static_cast<bool>(verify_ast(invalid.ast, {})),
      "invalid imported-parent fixture is not native-valid");
  try {
    (void)mutate(invalid, 4, context, 1.0);
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error("compiled mutation accepted an imported nonmember parent");
}

void test_full_int64_constant_range() {
  const auto grammar = compile(constant_grammar("Int",
      "\"range\":[\"-9223372036854775808\",\"9223372036854775807\"]"));
  VariationContext context(grammar);
  auto genome = generate_derivation(*grammar, 13).genome;
  for (std::uint64_t seed = 0; seed < 64; ++seed) {
    genome = mutate(genome, std::numeric_limits<std::uint64_t>::max() - seed,
        context, 0.0);
    require_valid_child(*grammar, genome);
    check(genome.ast.consts.at(genome.ast.nodes[3].i0).tag == ValueTag::Int,
        "full-range Int mutation changed the constant type");
  }
  check_counters(context.counters(), 64,
      "full-range Int mutation counters do not account for every returned child");
}

}  // namespace

int main() {
  try {
    test_repeated_holes_and_fixed_skeleton();
    test_all_constant_domains();
    test_destination_local_mutation_returns_full_valid_children();
    test_imported_invalid_parent_rejected();
    test_full_int64_constant_range();
    std::cout << "grammar mutation: domains, atomic templates, locals, and counters passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
