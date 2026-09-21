#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "constant_prep.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/grammar/request.hpp"
#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/evolution/grammar/values.hpp"
#include "gagp/evolution/repro/prep.hpp"

namespace {

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;
using namespace gagp::evo::repro;

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

template <class Action>
void rejects(Action action, const char* message) {
  try {
    action();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error(message);
}

std::shared_ptr<const CompiledGrammar> compile(const std::string& text) {
  return std::make_shared<const CompiledGrammar>(
      compile_grammar(parse_definition(text)));
}

std::string leaf_grammar(const std::string& type,
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

void test_domain_materialization_and_order() {
  const std::vector<std::pair<std::string, std::string>> definitions{
      {"Int", "\"values\":[\"7\",\"-2\",\"7\"]"},
      {"Float", "\"values\":[-0.0,2.25,-0.0]"},
      {"Bool", "\"values\":[false,true,false]"},
      {"Char", "\"values\":[\"a\",\"\\ud83d\\ude00\",\"a\"]"},
      {"String", "\"values\":[\"\",\"a\\u0000b\",\"\"]"},
      {"IntList", "\"values\":[[],[\"-3\",\"7\"],[]]"},
      {"FloatList", "\"values\":[[],[-0.0,2.25],[]]"},
      {"StringList", "\"values\":[[],[\"\",\"a\\u0000b\"],[]]"}};

  for (const auto& definition : definitions) {
    const auto grammar = compile(leaf_grammar(definition.first,
                                              definition.second));
    const auto table = prepare_constant_mutation_domains(grammar);
    check(table->domains.size() == 1 && table->values.size() == 3,
          "finite domain shape changed during preparation");
    const auto& row = table->domains.front();
    check(row.integer_range == 0 && row.value_offset == 0 &&
              row.value_count == 3,
          "finite domain row is incorrect");
    for (std::size_t i = 0; i < grammar->constants()[0].values.size(); ++i) {
      const Value expected = materialize_constant(
          grammar->constants()[0].type, grammar->constants()[0].values[i]);
      check(canonical_json(encode_constant(table->values[i])) ==
                canonical_json(encode_constant(expected)),
            "finite domain payload or declared ordering changed");
    }
    check(canonical_json(encode_constant(table->values[0])) ==
              canonical_json(encode_constant(table->values[2])),
          "finite domain duplicate was removed");
    ConstantMutationTable mutation;
    mutation.grammar_domains = table;
    check(constant_mutation_table_bytes(mutation) ==
              sizeof(ConstantMutationDomain) + 3 * sizeof(Value) +
                  table->expression_domains.size() * sizeof(int),
          "logical table bytes omitted shared grammar-domain storage");
  }
}

void test_full_integer_range() {
  const auto grammar = compile(leaf_grammar(
      "Int", "\"range\":[\"-9223372036854775808\","
             "\"9223372036854775807\"]"));
  const auto table = prepare_constant_mutation_domains(grammar);
  check(table->domains.size() == 1 && table->values.empty(),
        "integer range was unexpectedly enumerated");
  const auto& row = table->domains.front();
  check(row.integer_range == 1 && row.value_count == 0 &&
            row.minimum == std::numeric_limits<std::int64_t>::min() &&
            row.maximum == std::numeric_limits<std::int64_t>::max(),
        "full integer range endpoints were not preserved");
}

void test_domains_retain_exact_grammar_owner() {
  const auto grammar = compile(leaf_grammar("Int", "\"values\":[\"1\"]"));
  const auto domains = prepare_constant_mutation_domains(grammar);
  check(domains->grammar_owner == grammar,
        "prepared domains did not retain their compiled grammar owner");
  static_assert(std::is_const<
                    std::remove_reference_t<decltype(*domains)>>::value,
                "prepared grammar domains must be immutable");

  rejects([&] {
    (void)prepare_constant_mutation_domains(
        std::shared_ptr<const CompiledGrammar>{});
  }, "null compiled grammar owner was accepted");
}

void test_preprocess_rejects_domains_from_another_grammar_owner() {
  const auto grammar = compile(leaf_grammar("Int", "\"values\":[\"1\"]"));
  const auto other = compile(leaf_grammar("Int", "\"values\":[\"1\"]"));
  std::vector<ProgramGenome> population{
      generate_derivation(*grammar, 31).genome};
  const auto request = entry_request(*grammar);
  VariationContext context(grammar, request);
  GpuReproConfig config;
  config.population_size = 1;
  config.candidates_per_program = 1;
  config.donor_pool_size_per_site = 1;
  config.max_nodes = static_cast<int>(request.budget.max_nodes);
  config.max_expr_depth = static_cast<int>(request.budget.max_depth);
  const auto foreign_domains = prepare_constant_mutation_domains(other);

  rejects([&] {
    (void)preprocess_population(population, config, context, foreign_domains);
  }, "preprocessing accepted domains owned by another compiled grammar");
}

std::shared_ptr<const CompiledGrammar> repeated_and_shared_grammar() {
  return compile(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","type":"Int"},
    "search_limits":{"max_nodes":14,"max_depth":9},
    "execution_limits":{"fuel":100},
    "templates":[{"id":"Repeated","type":"Int","scope":[],
      "holes":[{"id":"a","type":"Int","scope":[]},
               {"id":"b","type":"Int","scope":[]}],
      "body":{"signature":"add(Int,Int)->Int","args":[
        {"constant":{"type":"Int","values":["99"]}},
        {"signature":"add(Int,Int)->Int","args":[
          {"signature":"add(Int,Int)->Int","args":[
            {"hole":"a"},{"hole":"a"}]},{"hole":"b"}]}]}}],
    "nonterminals":[
      {"id":"Value","type":"Int","scope":[],"alternatives":[{
        "id":"one","weight":1,"expression":{"constant":{
          "type":"Int","values":["1"]}}}]},
      {"id":"Main","type":"Int","scope":[],"alternatives":[{
        "id":"main","weight":1,"expression":{"template":"Repeated",
          "holes":{"a":{"ref":"Value"},"b":{"ref":"Value"}}}}]}]
  })");
}

void test_logical_groups_ignore_constant_slots() {
  const auto grammar = repeated_and_shared_grammar();
  auto generated = generate_derivation(*grammar, 17);
  VerifiedAst verified;
  auto witness = reconstruct_derivation(
      *grammar, generated.genome, entry_request(*grammar), &verified);

  std::vector<std::size_t> mutable_nodes;
  std::vector<std::size_t> fixed_nodes;
  for (std::size_t i = 0; i < witness.nodes.size(); ++i) {
    if (generated.genome.ast.nodes[i].kind != NodeKind::CONST) continue;
    if (witness.nodes[i].fixed) fixed_nodes.push_back(i);
    else mutable_nodes.push_back(i);
  }
  check(mutable_nodes.size() == 3 && fixed_nodes.size() == 1,
        "test grammar did not reconstruct expected constant origins");

  // Force every mutable occurrence to alias one physical constant slot. The
  // witness still has one repeated logical choice and one independent choice.
  const int shared_slot = generated.genome.ast.nodes[mutable_nodes.front()].i0;
  for (const std::size_t node : mutable_nodes)
    generated.genome.ast.nodes[node].i0 = shared_slot;
  witness = reconstruct_derivation(
      *grammar, generated.genome, entry_request(*grammar), &verified);

  ConstantMutationTable table;
  table.grammar_domains = prepare_constant_mutation_domains(grammar);
  append_constant_mutation_stream(
      table, generated.genome.ast, verified, witness);
  check(table.streams.size() == 1 && table.groups.size() == 2,
        "logical constant choices were not prepared as two groups");
  const auto& stream = table.streams.front();
  check(stream.node_count == static_cast<int>(generated.genome.ast.nodes.size()) &&
            stream.group_count == 2,
        "constant stream does not cover the exact AST");
  check(table.groups[0].logical_instance < table.groups[1].logical_instance,
        "constant groups are not ordered like CPU mutation");

  int repeated_group = -1;
  int singleton_group = -1;
  for (std::size_t i = 0; i < table.groups.size(); ++i) {
    const auto& group = table.groups[i];
    if (group.node_count == 2) repeated_group = static_cast<int>(i);
    if (group.node_count == 1) singleton_group = static_cast<int>(i);
    for (int occurrence = 0; occurrence < group.node_count; ++occurrence) {
      const int node = table.group_nodes[static_cast<std::size_t>(
          group.node_offset + occurrence)];
      check(table.node_group_origins[
                static_cast<std::size_t>(stream.node_origin_offset + node)] ==
                static_cast<int>(i),
            "group occurrence and per-node origin disagree");
      check(generated.genome.ast.nodes[static_cast<std::size_t>(node)].i0 ==
                shared_slot,
            "test did not retain shared constant-pool aliasing");
    }
  }
  check(repeated_group >= 0 && singleton_group >= 0 &&
            repeated_group != singleton_group,
        "repeated and independent logical constants were merged");
  for (const std::size_t node : mutable_nodes) {
    const int origin = table.node_group_origins[
        static_cast<std::size_t>(stream.node_origin_offset) + node];
    check(origin == repeated_group || origin == singleton_group,
          "mutable constant lacks its logical group origin");
  }
  check(table.node_group_origins[
            static_cast<std::size_t>(stream.node_origin_offset) +
            fixed_nodes.front()] == kNoConstantMutationGroup,
        "fixed template constant became mutable");
  for (std::size_t i = 0; i < generated.genome.ast.nodes.size(); ++i) {
    if (generated.genome.ast.nodes[i].kind != NodeKind::CONST)
      check(table.node_group_origins[
                static_cast<std::size_t>(stream.node_origin_offset) + i] ==
                kNoConstantMutationGroup,
            "nonconstant node received a mutation group");
  }
}

void test_rejects_mismatched_sidecars() {
  const auto grammar = repeated_and_shared_grammar();
  const auto generated = generate_derivation(*grammar, 23);
  VerifiedAst verified;
  auto witness = reconstruct_derivation(
      *grammar, generated.genome, entry_request(*grammar), &verified);
  ConstantMutationTable table;
  table.grammar_domains = prepare_constant_mutation_domains(grammar);
  witness.nodes.pop_back();
  rejects([&] {
    append_constant_mutation_stream(
        table, generated.genome.ast, verified, witness);
  }, "mismatched witness node length was accepted");
}

}  // namespace

int main() {
  try {
    static_assert(std::is_trivially_copyable<ConstantMutationDomain>::value,
                  "domain row must be POD transport data");
    static_assert(std::is_trivially_copyable<ConstantMutationGroup>::value,
                  "group row must be POD transport data");
    static_assert(std::is_trivially_copyable<ConstantMutationStream>::value,
                  "stream row must be POD transport data");
    test_domain_materialization_and_order();
    test_full_integer_range();
    test_domains_retain_exact_grammar_owner();
    test_preprocess_rejects_domains_from_another_grammar_owner();
    test_logical_groups_ignore_constant_slots();
    test_rejects_mismatched_sidecars();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
