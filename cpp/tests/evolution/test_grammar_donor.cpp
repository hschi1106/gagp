#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/grammar/donor.hpp"

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;

namespace {

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

CompiledGrammar compile(const std::string& text) {
  return compile_grammar(parse_definition(text));
}

std::uint32_t nonterminal(const CompiledGrammar& grammar, const std::string& stable_id) {
  const auto found = std::find_if(grammar.nonterminals().begin(), grammar.nonterminals().end(),
      [&](const CompiledNonterminal& value) { return value.stable_id == stable_id; });
  if (found == grammar.nonterminals().end())
    throw std::runtime_error("missing fixture nonterminal " + stable_id);
  return found->id;
}

const VariationSite& site_for(const VariationAnalysis& analysis, std::uint32_t nt) {
  const auto found = std::find_if(analysis.sites.begin(), analysis.sites.end(),
      [&](const VariationSite& site) { return site.nonterminal == nt; });
  if (found == analysis.sites.end())
    throw std::runtime_error("missing analyzed destination site");
  return *found;
}

template <typename Action>
void rejects(const Action& action, const char* message) {
  try {
    action();
  } catch (const std::runtime_error&) {
    return;
  }
  throw std::runtime_error(message);
}

bool same_inputs(const std::vector<InputSpec>& actual,
    const std::vector<InputSpec>& expected) {
  if (actual.size() != expected.size()) return false;
  for (std::size_t i = 0; i < actual.size(); ++i)
    if (actual[i].name != expected[i].name || actual[i].type != expected[i].type)
      return false;
  return true;
}

CompiledGrammar assigned_local_grammar() {
  return compile(R"({
    "format_version":"grammar-definition-v1",
    "entry":{"nonterminal":"Main","category":"Program","type":"Int"},
    "inputs":[{"name":"input","type":"Int"}],
    "locals":[{"name":"x","type":"Int"},{"name":"y","type":"Int"}],
    "search_limits":{"max_nodes":20,"max_depth":10},
    "execution_limits":{"fuel":100},
    "templates":[{
      "id":"Identity","type":"Int","scope":[],
      "holes":[{"id":"value","type":"Int","scope":[]}],
      "body":{"hole":"value"}
    }],
    "nonterminals":[
      {"id":"One","type":"Int","scope":[],"alternatives":[{"id":"one","weight":1,
        "expression":{"constant":{"type":"Int","values":["1"]}}}]},
      {"id":"LocalX","type":"Int","scope":[],"alternatives":[{"id":"x","weight":1,
        "expression":{"template":"Identity","holes":{"value":{"local":"x"}}}}]},
      {"id":"Main","category":"Program","type":"Int","scope":[],"alternatives":[
        {"id":"body","weight":1,"expression":{"control":"program(Block)->Program","type":"Int","args":[
          {"control":"block_cons(Statement,Block)->Block","type":"Int","args":[
            {"control":"assign(Int)->Statement","type":"Int","name":"x","args":[{"ref":"One"}]},
            {"control":"block_cons(Statement,Block)->Block","type":"Int","args":[
              {"control":"assign(Int)->Statement","type":"Int","name":"y","args":[{"ref":"One"}]},
              {"control":"block_cons(Statement,Block)->Block","type":"Int","args":[
                {"control":"return(Int)->Statement","type":"Int","args":[{"ref":"LocalX"}]},
                {"control":"block_nil()->Block","type":"Int","args":[]}]}]}]}]}}]}
    ]
  })");
}

VariationSite destination_site(const CompiledGrammar& grammar) {
  const auto parent = generate_derivation(grammar, 37).genome;
  const auto analysis = analyze_variation(grammar, parent);
  return site_for(analysis, nonterminal(grammar, "LocalX"));
}

void test_destination_local_donor_contract() {
  const auto grammar = assigned_local_grammar();
  const auto destination = destination_site(grammar);
  check(destination.category == NodeCategory::Expression &&
        destination.available_locals.size() == 3 &&
        destination.available_locals[0].name == "input" &&
        destination.available_locals[1].name == "x" &&
        destination.available_locals[2].name == "y",
      "generated Program did not expose the expected destination-local frame");

  const auto donor = generate_donor(grammar, 91, destination);
  check(donor.payload.begin == 3 && donor.payload.end == 4 &&
        donor.genome.ast.nodes.size() == 5 &&
        donor.genome.ast.nodes[donor.payload.begin].kind == NodeKind::VAR &&
        donor.genome.ast.names.at(donor.genome.ast.nodes[donor.payload.begin].i0) == "x",
      "donor facade did not identify the expression inside its native envelope");
  check(donor.nodes == donor.payload.end - donor.payload.begin &&
        donor.nodes == 1 && donor.depth == 1 && donor.template_nesting == 1 &&
        donor_fits(destination, donor.nodes, donor.depth, donor.template_nesting),
      "donor facade reported incorrect payload measurements");
  check(same_inputs(donor.inputs,
          {{"input", RType::Int}, {"x", RType::Int}, {"y", RType::Int}}),
      "donor facade did not expose the destination's explicit evaluation inputs");

  const auto verified = verify_ast(donor.genome.ast, donor.inputs);
  check(verified && verified.verified.return_type == RType::Int &&
        verified.verified.subtree_end.at(donor.payload.begin) == donor.payload.end,
      "contextual donor is not a valid native program under its explicit inputs");
  check(donor.genome.derivation && !donor.genome.derivation->seed_replayable &&
        donor.genome.derivation->grammar_hash == grammar.content_hash(),
      "contextual donor advertised false seed provenance or lost grammar identity");
}

void test_exact_budget_and_template_nesting_boundaries() {
  const auto grammar = assigned_local_grammar();
  auto exact = destination_site(grammar);
  exact.replacement_budget = {1, 1};
  exact.remaining_template_nesting = 1;
  const auto donor = generate_donor(grammar, 4, exact);
  check(donor.nodes == 1 && donor.depth == 1 && donor.template_nesting == 1,
      "exact donor fit boundary did not produce the only legal local payload");

  auto no_nodes = exact;
  no_nodes.replacement_budget.max_nodes = 0;
  rejects([&] { (void)generate_donor(grammar, 4, no_nodes); },
      "donor generation accepted a zero-node destination budget");
  auto no_depth = exact;
  no_depth.replacement_budget.max_depth = 0;
  rejects([&] { (void)generate_donor(grammar, 4, no_depth); },
      "donor generation accepted a zero-depth destination budget");
  auto no_nesting = exact;
  no_nesting.remaining_template_nesting = 0;
  rejects([&] { (void)generate_donor(grammar, 4, no_nesting); },
      "donor generation accepted a template beyond the destination nesting allowance");
}

void test_malformed_site_rejection() {
  const auto grammar = assigned_local_grammar();
  const auto destination = destination_site(grammar);

  auto wrong_type = destination;
  wrong_type.type = RType::Float;
  rejects([&] { (void)generate_donor(grammar, 1, wrong_type); },
      "donor generation accepted a site with the wrong exact type");

  auto wrong_context = destination;
  wrong_context.context = destination.context == kNoGrammarId ? 0 : kNoGrammarId;
  rejects([&] { (void)generate_donor(grammar, 1, wrong_context); },
      "donor generation accepted a site with the wrong compiled context");

  auto missing_local = destination;
  missing_local.available_locals.erase(
      std::remove_if(missing_local.available_locals.begin(),
          missing_local.available_locals.end(),
          [](const RegionBinding& binding) { return binding.name == "x"; }),
      missing_local.available_locals.end());
  rejects([&] { (void)generate_donor(grammar, 1, missing_local); },
      "donor generation accepted a frame missing its required local");

  auto wrong_local_type = destination;
  const auto x = std::find_if(wrong_local_type.available_locals.begin(),
      wrong_local_type.available_locals.end(),
      [](const RegionBinding& binding) { return binding.name == "x"; });
  check(x != wrong_local_type.available_locals.end(),
      "malformed-local fixture lost destination local x");
  x->type = RType::Float;
  rejects([&] { (void)generate_donor(grammar, 1, wrong_local_type); },
      "donor generation accepted a destination local with the wrong exact type");
}

}  // namespace

int main() {
  try {
    test_destination_local_donor_contract();
    test_exact_budget_and_template_nesting_boundaries();
    test_malformed_site_rejection();
    std::cout << "grammar donor facade: contextual payload, inputs, provenance, and boundaries passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
