#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/mutation.hpp"
#include "gagp/evolution/grammar/donor.hpp"
#include "gagp/evolution/grammar/variation.hpp"

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

bool same_generated_donor(const ContextualDonor& left, const ContextualDonor& right) {
  if (left.payload.begin != right.payload.begin || left.payload.end != right.payload.end ||
      left.nodes != right.nodes || left.depth != right.depth ||
      left.template_nesting != right.template_nesting ||
      left.genome.ast.nodes.size() != right.genome.ast.nodes.size() ||
      left.genome.ast.names != right.genome.ast.names ||
      left.genome.meta.program_key != right.genome.meta.program_key ||
      !same_inputs(left.inputs, right.inputs) ||
      !left.genome.derivation || !right.genome.derivation)
    return false;
  for (std::size_t i = 0; i < left.genome.ast.nodes.size(); ++i) {
    const auto& a = left.genome.ast.nodes[i];
    const auto& b = right.genome.ast.nodes[i];
    if (a.kind != b.kind || a.i0 != b.i0 || a.i1 != b.i1) return false;
  }
  const auto& a = *left.genome.derivation;
  const auto& b = *right.genome.derivation;
  return a.seed == b.seed && a.logical_steps == b.logical_steps &&
      a.derived_nodes == b.derived_nodes && a.nodes.size() == b.nodes.size() &&
      a.choices.size() == b.choices.size();
}

CompiledGrammar assigned_local_grammar() {
  return compile(R"({
    "format_version":"grammar-definition-v2",
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
      {"id":"LocalX","type":"Int","scope":[],"mutation_locals":[{"name":"x","type":"Int"}],"alternatives":[{"id":"x","weight":1,
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
          {{"input", RType::Int}, {"x", RType::Int}}),
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

void test_worker_frame_cost_cache_identity_and_keys() {
  const auto grammar = std::make_shared<const CompiledGrammar>(assigned_local_grammar());
  auto destination = destination_site(*grammar);
  destination.replacement_budget = {1, 1};
  destination.remaining_template_nesting = 1;
  VariationContext context(grammar, 2);

  const auto cold = generate_donor(*grammar, 73, destination);
  const auto first = generate_donor(context, 73, destination);
  const auto repeated = generate_donor(context, 73, destination);
  check(same_generated_donor(cold, first) && same_generated_donor(first, repeated),
      "cached contextual costs changed fixed-seed donor generation");
  check(context.frame_cost_cache_counters().misses == 1 &&
        context.frame_cost_cache_counters().hits == 1,
      "repeated contextual donor did not reuse its frame-cost table");

  auto deeper = destination;
  deeper.replacement_budget.max_depth = 2;
  (void)generate_donor(context, 73, deeper);
  check(context.frame_cost_cache_counters().misses == 2,
      "effective donor depth was omitted from the frame-cost cache key");

  auto fewer_locals = destination;
  fewer_locals.available_locals.erase(
      std::remove_if(fewer_locals.available_locals.begin(), fewer_locals.available_locals.end(),
          [](const RegionBinding& binding) { return binding.name == "y"; }),
      fewer_locals.available_locals.end());
  (void)generate_donor(context, 73, fewer_locals);
  check(context.frame_cost_cache_counters().misses == 2,
      "incidental destination local fragmented the declared donor interface");
  deeper.replacement_budget.max_depth = 3;
  (void)generate_donor(context, 73, deeper);
  check(context.frame_cost_cache_counters().evictions == 1,
      "worker frame-cost cache did not enforce its configured bound");

  const auto costly_grammar = std::make_shared<const CompiledGrammar>(compile(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Pair","type":"Int"},
    "locals":[{"name":"x","type":"Int"}],
    "search_limits":{"max_nodes":7,"max_depth":5},
    "execution_limits":{"fuel":100},
    "nonterminals":[{"id":"Pair","type":"Int","scope":[],"alternatives":[
      {"id":"unavailable","weight":1,"expression":{"local":"x"}},
      {"id":"pair","weight":1,"expression":{"signature":"add(Int,Int)->Int","args":[
        {"constant":{"type":"Int","values":["1"]}},
        {"constant":{"type":"Int","values":["2"]}}]}}
    ]}]
  })"));
  const auto& pair = costly_grammar->nonterminals().at(nonterminal(*costly_grammar, "Pair"));
  VariationSite costly_site;
  costly_site.nonterminal = pair.id;
  costly_site.type = pair.type;
  costly_site.category = pair.category;
  costly_site.context = pair.context;
  costly_site.replacement_budget = {3, 2};
  costly_site.remaining_template_nesting = 1;
  VariationContext costly_context(costly_grammar, 4);
  (void)generate_donor(costly_context, 73, costly_site);
  costly_site.replacement_budget.max_nodes = 1;
  rejects([&] { (void)generate_donor(costly_context, 73, costly_site); },
      "cached contextual costs bypassed the per-request node feasibility check");
  check(costly_context.frame_cost_cache_counters().misses == 1 &&
        costly_context.frame_cost_cache_counters().hits == 1,
      "impossible node budget did not consult the matching cached cost table");
}

void test_fixed_interface_and_bounded_pool_refresh() {
  auto definition = parse_definition(assigned_local_grammar().canonical_definition());
  for (auto& rule : definition.document.object_v.at("nonterminals").array_v)
    rule.object_v.erase("mutation_locals");
  const auto closed = compile(canonical_json(definition.document));
  rejects([&] { (void)generate_donor(closed, 91, destination_site(closed)); },
      "undeclared destination local leaked into a fresh donor");

  const auto grammar = std::make_shared<const CompiledGrammar>(compile(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Value","type":"Int"},
    "search_limits":{"max_nodes":5,"max_depth":4},"execution_limits":{"fuel":100},
    "nonterminals":[{"id":"Value","type":"Int","scope":[],"alternatives":[
      {"id":"value","weight":1,"expression":{"constant":{"type":"Int","range":["0","1000000"]}}}]}]
  })"));
  const auto parent = generate_derivation(*grammar, 1).genome;
  VariationContext context(grammar), replay(grammar);
  const auto site = context.analyze(parent)->sites.front();
  std::vector<DonorPoolJob> jobs;
  for (std::uint64_t i = 0; i < 17; ++i) jobs.push_back({&parent, site, {i * 2 + 20, i * 2 + 21}});
  const auto result = try_generate_donor_pools(context, jobs);
  const auto repeated = try_generate_donor_pools(replay, jobs);
  check(result && repeated && result->size() == 17, "bounded pool batch unavailable");
  for (std::size_t i = 0; i < 17; ++i) {
    for (std::size_t slot = 0; slot < 2; ++slot) {
      check((*result)[i][slot] && (*repeated)[i][slot] &&
          same_generated_donor(*(*result)[i][slot], *(*repeated)[i][slot]),
          "bounded pools are not reproducible");
      check(same_generated_donor(*(*result)[i][slot], *(*result)[(i / 8) * 8][slot]),
          "equivalent destinations did not share their bounded pool");
    }
  }
  check((*result)[0][0]->genome.meta.program_key != (*result)[8][0]->genome.meta.program_key &&
        (*result)[8][0]->genome.meta.program_key != (*result)[16][0]->genome.meta.program_key,
      "bounded pool did not refresh after eight destinations");
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

void test_mutation_entry() {
  const std::string source = R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Fixed","type":"Int"},
    "inputs":[],"locals":[],"templates":[],
    "search_limits":{"max_nodes":8,"max_depth":6},
    "execution_limits":{"fuel":100},
    "nonterminals":[
      {"id":"Fixed","type":"Int","scope":[],"mutation_entry":"Donor",
       "alternatives":[{"id":"fixed","weight":1,"expression":{"constant":{
         "type":"Int","range":["-8","8"],
         "sample_from":{"type":"Int","values":["2"]}}}}]},
      {"id":"Donor","type":"Int","scope":[],"variation":false,
       "alternatives":[{"id":"seven","weight":1,"expression":{
         "constant":{"type":"Int","values":["7"]}}}]}
    ]})";
  const auto grammar = std::make_shared<const CompiledGrammar>(compile(source));
  const auto parent = generate_derivation(*grammar, 3).genome;
  const auto value = [](const ProgramGenome& genome) {
    return genome.ast.consts.at(genome.ast.nodes.at(3).i0).i;
  };
  check(value(parent) == 2, "mutation entry changed ordinary construction");
  auto construction = entry_request(*grammar);
  construction.stage = GenerationStage::Mutation;
  check(value(generate_derivation(*grammar, 3, construction).genome) == 2,
      "nested mutation construction incorrectly redirected a nonterminal");
  VariationContext context(grammar);
  const auto child = mutate(parent, 19, context, 1.0);
  check(value(child) == 7, "subtree mutation ignored its construction entry");
  check(context.analyze(child)->sites.size() == 1,
      "mutated child lost destination membership or exposed helper sites");

  auto escaping = source;
  escaping.replace(escaping.find("[\"7\"]"), 5, "[\"9\"]");
  const auto escaping_grammar = std::make_shared<const CompiledGrammar>(compile(escaping));
  VariationContext escaping_context(escaping_grammar);
  const auto escaping_parent = generate_derivation(*escaping_grammar, 3).genome;
  check(value(mutate(escaping_parent, 19, escaping_context, 1.0)) == 2,
      "mutation entry bypassed destination membership");

  // A differently typed construction entry is rejected before generation.
  auto invalid = source;
  const auto position = invalid.find("\"id\":\"Donor\",\"type\":\"Int\"");
  check(position != std::string::npos, "mutation entry fixture lost donor declaration");
  invalid.replace(position, std::string("\"id\":\"Donor\",\"type\":\"Int\"").size(),
      "\"id\":\"Donor\",\"type\":\"Bool\"");
  bool rejected = false;
  try { (void)compile(invalid); }
  catch (const std::invalid_argument& error) {
    rejected = std::string(error.what()).find("mutation_entry") != std::string::npos;
  }
  check(rejected, "mutation entry accepted a different exact type");
}

}  // namespace

int main() {
  try {
    test_destination_local_donor_contract();
    test_exact_budget_and_template_nesting_boundaries();
    test_worker_frame_cost_cache_identity_and_keys();
    test_malformed_site_rejection();
    test_fixed_interface_and_bounded_pool_refresh();
    test_mutation_entry();
    std::cout << "grammar donor facade: contextual payload, inputs, provenance, and boundaries passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
