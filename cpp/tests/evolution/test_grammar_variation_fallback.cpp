#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/evolution/mutation.hpp"
#include "../../src/evolution/grammar/variation_internal.hpp"

using namespace gagp::evo;
using namespace gagp::evo::grammar;

namespace {

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

std::shared_ptr<const CompiledGrammar> fallback_grammar() {
  return std::make_shared<const CompiledGrammar>(
      compile_grammar(parse_definition(R"({
        "format_version":"grammar-definition-v2",
        "entry":{"nonterminal":"Main","category":"Program","type":"Int"},
        "locals":[{"name":"x","type":"Int"}],
        "search_limits":{"max_nodes":8,"max_depth":7},
        "execution_limits":{"fuel":100},
        "nonterminals":[{
          "id":"Main","category":"Program","type":"Int","scope":[],
          "alternatives":[
            {"id":"valid","weight":1,"expression":{
              "control":"program(Block)->Program","type":"Int","args":[
                {"control":"block_cons(Statement,Block)->Block","type":"Int","args":[
                  {"control":"return(Int)->Statement","type":"Int","args":[
                    {"constant":{"type":"Int","values":["0","1"]}}]},
                  {"control":"block_nil()->Block","type":"Int","args":[]}]}]}},
            {"id":"unassigned-local","weight":1,"expression":{
              "control":"program(Block)->Program","type":"Int","args":[
                {"control":"block_cons(Statement,Block)->Block","type":"Int","args":[
                  {"control":"return(Int)->Statement","type":"Int","args":[
                    {"local":"x"}]},
                  {"control":"block_nil()->Block","type":"Int","args":[]}] }]}}
          ]
        }]
      })")));
}

ProgramGenome find_valid_parent(const CompiledGrammar& grammar) {
  for (std::uint64_t seed = 0; seed <= 128; ++seed) {
    try {
      return generate_derivation(grammar, seed).genome;
    } catch (const std::invalid_argument&) {
      // The other admitted production deliberately fails native local analysis.
    }
  }
  throw std::runtime_error("no valid parent seed found within the bounded search");
}

void require_certified_member(const CompiledGrammar& grammar,
    const ProgramGenome& genome) {
  const auto verified = verify_ast(genome.ast, {});
  check(static_cast<bool>(verified),
      "variation fallback returned a natively invalid parent");
  require_membership(grammar, genome);
  check(genome.derivation && !genome.derivation->seed_replayable &&
        genome.derivation->grammar_hash == grammar.content_hash(),
      "variation fallback did not return certified reconstructed provenance");
}

void test_generation_failure_returns_certified_parent() {
  const auto grammar = fallback_grammar();
  const auto parent = find_valid_parent(*grammar);
  VariationContext context(grammar);

  for (std::uint64_t seed = 0; seed < 64; ++seed) {
    const auto child = mutate(parent, 1000 + seed, context, 1.0);
    require_certified_member(*grammar, child);
  }

  const auto& counters = context.counters();
  check(counters.mutation_attempts == 64,
      "compiled mutation did not count every fallback experiment");
  check(counters.generation_rejections > 0,
      "invalid unassigned-local donors never reached the generation boundary");
  check(counters.generation_rejections == counters.fallback_children,
      "generation failures did not map one-for-one to certified fallbacks");
  check(counters.changed_children > 0,
      "valid constant resampling never produced a changed child");
  check(counters.changed_children + counters.unchanged_children == 64,
      "mutation results were not completely classified");
  check(counters.fallback_children <= counters.unchanged_children,
      "a certified fallback was classified as changed");
}

void test_acceptance_failure_returns_certified_parent() {
  const auto grammar = fallback_grammar();
  VariationContext context(grammar);
  const auto certified = variation_detail::certify(
      find_valid_parent(*grammar), context);
  auto malformed = certified.ast;
  const auto constant = std::find_if(malformed.nodes.begin(), malformed.nodes.end(),
      [](const AstNode& node) { return node.kind == NodeKind::CONST; });
  check(constant != malformed.nodes.end(),
      "valid fallback fixture did not contain a constant node");
  constant->i0 = 999;

  const auto child = variation_detail::accept(
      std::move(malformed), certified, context);
  require_certified_member(*grammar, child);
  check(child.meta.program_key == certified.meta.program_key,
      "acceptance failure did not return the certified parent");

  const auto& counters = context.counters();
  check(counters.acceptance_rejections == 1 &&
        counters.fallback_children == 1 &&
        counters.unchanged_children == 1 &&
        counters.changed_children == 0,
      "acceptance-boundary fallback counters are inconsistent");
}

}  // namespace

int main() {
  try {
    test_generation_failure_returns_certified_parent();
    test_acceptance_failure_returns_certified_parent();
    std::cout << "grammar variation fallback: generation and acceptance guards passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
