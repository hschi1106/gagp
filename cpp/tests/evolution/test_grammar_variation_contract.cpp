#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/evolution/grammar/variation_contract.hpp"
#include "gagp/evolution/repro/pack.hpp"

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

const VariationSite& site_for(const VariationAnalysis& analysis, std::uint32_t nt,
    std::size_t ordinal = 0) {
  for (const auto& site : analysis.sites) {
    if (site.nonterminal == nt && ordinal-- == 0) return site;
  }
  throw std::runtime_error("missing variation site for fixture nonterminal");
}

bool same_bindings(const std::vector<RegionBinding>& actual,
    const std::vector<RegionBinding>& expected) {
  if (actual.size() != expected.size()) return false;
  for (std::size_t i = 0; i < actual.size(); ++i)
    if (actual[i].name != expected[i].name || actual[i].type != expected[i].type) return false;
  return true;
}

const char* split_rules = R"({
  "format_version":"grammar-definition-v1",
  "entry":{"nonterminal":"Main","type":"Int"},
  "search_limits":{"max_nodes":12,"max_depth":7},
  "execution_limits":{"fuel":100},
  "nonterminals":[
    {"id":"Main","type":"Int","scope":[],"alternatives":[{"id":"pair","weight":1,
      "expression":{"signature":"add(Int,Int)->Int","args":[{"ref":"Left"},{"ref":"Right"}]}}]},
    {"id":"Left","type":"Int","scope":[],"alternatives":[{"id":"one","weight":1,
      "expression":{"constant":{"type":"Int","values":["1"]}}}]},
    {"id":"Right","type":"Int","scope":[],"alternatives":[{"id":"two","weight":1,
      "expression":{"constant":{"type":"Int","values":["2"]}}}]}
  ]
})";

CompiledGrammar template_grammar() {
  return compile(R"({
    "format_version":"grammar-definition-v1",
    "entry":{"nonterminal":"Main","type":"Int"},
    "search_limits":{"max_nodes":18,"max_depth":8},
    "execution_limits":{"fuel":100},
    "templates":[
      {"id":"Asymmetric","type":"Int","scope":[],
       "holes":[{"id":"value","type":"Int","scope":[]}],
       "body":{"signature":"add(Int,Int)->Int","args":[
         {"hole":"value"},{"signature":"neg(Int)->Int","args":[{"hole":"value"}]}]}},
      {"id":"Forward","type":"Int","scope":[],
       "holes":[{"id":"forwarded","type":"Int","scope":[]}],
       "body":{"template":"Asymmetric","holes":{"value":{"hole":"forwarded"}}}}
    ],
    "nonterminals":[
      {"id":"Value","type":"Int","scope":[],"alternatives":[
        {"id":"leaf","weight":1,"expression":{"constant":{"type":"Int","range":["0","9"]}}}]},
      {"id":"Main","type":"Int","scope":[],"alternatives":[{"id":"wrapped","weight":1,
        "expression":{"template":"Forward","holes":{"forwarded":{"ref":"Value"}}}}]}
    ]
  })");
}

void test_nonterminal_and_registry_compatibility() {
  const auto grammar = compile(split_rules);
  const auto genome = generate_derivation(grammar, 7).genome;
  CompatibilityRegistry shared;
  const auto analysis = analyze_variation(grammar, genome, &shared);
  const auto& left = site_for(analysis, nonterminal(grammar, "Left"));
  const auto& right = site_for(analysis, nonterminal(grammar, "Right"));
  check(left.type == RType::Int && right.type == RType::Int,
      "same-result-type rejection fixture changed type");
  check(!compatible_sites(left, right) && left.compatibility_key != right.compatibility_key &&
        left.compatibility_id != right.compatibility_id,
      "different nonterminals became compatible through result type alone");

  CompatibilityRegistry left_registry;
  CompatibilityRegistry right_registry;
  GenerationRequest left_request{nonterminal(grammar, "Left"), RType::Int, {}, {5, 4}};
  GenerationRequest right_request{nonterminal(grammar, "Right"), RType::Int, {}, {5, 4}};
  const auto standalone_left = analyze_variation(
      grammar, generate_derivation(grammar, 1, left_request).genome, left_request, &left_registry);
  const auto standalone_right = analyze_variation(
      grammar, generate_derivation(grammar, 2, right_request).genome, right_request, &right_registry);
  const auto& standalone_left_site = site_for(standalone_left, left_request.nonterminal);
  const auto& standalone_right_site = site_for(standalone_right, right_request.nonterminal);
  check(standalone_left_site.compatibility_id == 0 && standalone_right_site.compatibility_id == 0,
      "standalone registry fixture did not reuse the same numeric ID");
  check(!compatible_sites(standalone_left_site, standalone_right_site),
      "equal numeric IDs from different registries were treated as compatibility proof");
}

void test_template_sites_occurrences_and_budgets() {
  const auto grammar = template_grammar();
  CompatibilityRegistry registry;
  const auto first = analyze_variation(grammar, generate_derivation(grammar, 3).genome, &registry);
  const auto second = analyze_variation(grammar, generate_derivation(grammar, 91).genome, &registry);
  const auto main = nonterminal(grammar, "Main");
  const auto value = nonterminal(grammar, "Value");
  const auto& root = site_for(first, main);
  const auto& hole = site_for(first, value);
  const auto& other_hole = site_for(second, value);

  check(first.sites.size() == 2 &&
        std::count_if(first.sites.begin(), first.sites.end(), [&](const VariationSite& site) {
          return site.nonterminal == main;
        }) == 1 &&
        std::count_if(first.sites.begin(), first.sites.end(), [&](const VariationSite& site) {
          return site.nonterminal == value;
        }) == 1 &&
        root.occurrences.size() == 1 && root.occurrences[0].begin == 3 &&
        root.occurrences[0].end == 7,
      "outer nonterminal did not admit whole fixed-template replacement");
  check(hole.template_id != kNoGrammarId && hole.slot != kNoGrammarId &&
        hole.occurrences.size() == 2,
      "forwarded repeated hole was not exposed as one atomic variation group");
  check(hole.occurrences[0].begin == 4 && hole.occurrences[0].end == 5 &&
        hole.occurrences[1].begin == 6 && hole.occurrences[1].end == 7,
      "forwarded repeated-hole physical spans changed");
  check(first.witness.nodes[3].fixed && first.witness.nodes[5].fixed,
      "template skeleton fixture no longer marks its primitive nodes fixed");

  check(compatible_sites(hole, other_hole) &&
        hole.compatibility_id == other_hole.compatibility_id,
      "same nonterminal and declared hole contract differed across independent parents");
  check(hole.replacement_budget.max_nodes == 6 && hole.replacement_budget.max_depth == 3,
      "repeated-hole budget did not use global outside-node allowance and tightest copy depth");
  check(hole.remaining_template_nesting == 254 && hole.materialized_nodes == 1 &&
        hole.materialized_depth == 1 && hole.template_nesting == 0,
      "forwarded hole did not retain its exact remaining template nesting");

  check(root.materialized_nodes == 4 && root.materialized_depth == 3 &&
        root.template_nesting == 2,
      "whole-template donor measurements did not describe its physical subtree");
  check(donor_fits(hole, 6, 3, 254), "exact donor budget was rejected");
  check(!donor_fits(hole, 7, 3, 254), "node overflow was accepted");
  check(!donor_fits(hole, 6, 4, 254), "depth overflow was accepted");
  check(!donor_fits(hole, 6, 3, 255), "template-nesting overflow was accepted");
  check(!donor_fits(hole, 0, 3, 254) && !donor_fits(hole, 6, 0, 254),
      "empty donor dimensions were accepted");

  const auto request = donor_request(hole);
  check(request.nonterminal == value && request.type == RType::Int &&
        request.budget.max_nodes == 10 && request.budget.max_depth == 6,
      "expression donor request omitted the four-node, three-level envelope");
  const auto root_request = donor_request(root);
  check(root_request.budget.max_nodes == 18 && root_request.budget.max_depth == 8,
      "root donor request did not recover the complete original expression budget");

  VariationSite program;
  program.nonterminal = 7;
  program.type = RType::Int;
  program.category = NodeCategory::Program;
  program.replacement_budget = {11, 6};
  const auto program_request = donor_request(program);
  check(program_request.budget.max_nodes == 11 && program_request.budget.max_depth == 6,
      "Program donor request incorrectly added an expression envelope");
}

void test_asymmetric_forwarding_uses_physical_nesting() {
  const auto grammar = compile(R"({
    "format_version":"grammar-definition-v1",
    "entry":{"nonterminal":"Main","type":"Int"},
    "search_limits":{"max_nodes":18,"max_depth":8},
    "execution_limits":{"fuel":100},
    "templates":[
      {"id":"Inner","type":"Int","scope":[],
       "holes":[{"id":"inner","type":"Int","scope":[]}],
       "body":{"signature":"neg(Int)->Int","args":[{"hole":"inner"}]}},
      {"id":"Outer","type":"Int","scope":[],
       "holes":[{"id":"outer","type":"Int","scope":[]}],
       "body":{"signature":"add(Int,Int)->Int","args":[
         {"hole":"outer"},
         {"template":"Inner","holes":{"inner":{"hole":"outer"}}}]}}
    ],
    "nonterminals":[
      {"id":"Value","type":"Int","scope":[],"alternatives":[{"id":"leaf","weight":1,
        "expression":{"constant":{"type":"Int","values":["4"]}}}]},
      {"id":"Main","type":"Int","scope":[],"alternatives":[{"id":"outer","weight":1,
        "expression":{"template":"Outer","holes":{"outer":{"ref":"Value"}}}}]}
    ]
  })");
  const auto analysis = analyze_variation(grammar, generate_derivation(grammar, 13).genome);
  const auto& site = site_for(analysis, nonterminal(grammar, "Value"));
  check(site.occurrences.size() == 2 &&
        site.occurrences[0].begin == 4 && site.occurrences[0].end == 5 &&
        site.occurrences[1].begin == 6 && site.occurrences[1].end == 7,
      "asymmetric outer-hole forwarding did not remain one atomic logical site");
  check(analysis.witness.choices.size() == 3 &&
        analysis.witness.choices[1].template_instance ==
            analysis.witness.choices[2].template_instance &&
        analysis.witness.choices[1].slot == analysis.witness.choices[2].slot &&
        analysis.witness.choices[1].enclosing_template_depth == 1 &&
        analysis.witness.choices[2].enclosing_template_depth == 2,
      "copied outer-hole choices did not retain distinct physical enclosing depths");
  check(analysis.witness.nodes[4].template_depth == 1 &&
        analysis.witness.nodes[6].template_depth == 2,
      "copied outer-hole nodes did not shift to their physical template depths");
  check(site.remaining_template_nesting == 254 &&
        site.replacement_budget.max_nodes == 6 && site.replacement_budget.max_depth == 3,
      "asymmetric forwarding did not use the tightest physical nesting and depth budgets");
  check(site.materialized_nodes == 1 && site.materialized_depth == 1 &&
        site.template_nesting == 0 &&
        donor_fits(site, site.materialized_nodes, site.materialized_depth, site.template_nesting),
      "asymmetric forwarding lost its exact donor measurements");
}

CompiledGrammar local_scope_grammar() {
  return compile(R"({
    "format_version":"grammar-definition-v1",
    "entry":{"nonterminal":"Main","category":"Program","type":"Int"},
    "inputs":[{"name":"input","type":"Int"}],
    "locals":[{"name":"x","type":"Int"},{"name":"y","type":"Int"}],
    "search_limits":{"max_nodes":20,"max_depth":10},
    "execution_limits":{"fuel":100},
    "nonterminals":[
      {"id":"Shared","type":"Int","scope":[],"alternatives":[{"id":"one","weight":1,
        "expression":{"constant":{"type":"Int","values":["1"]}}}]},
      {"id":"LocalX","type":"Int","scope":[],"alternatives":[{"id":"x","weight":1,
        "expression":{"local":"x"}}]},
      {"id":"Main","category":"Program","type":"Int","scope":[],"alternatives":[
        {"id":"body","weight":1,"expression":{"control":"program(Block)->Program","type":"Int","args":[
          {"control":"block_cons(Statement,Block)->Block","type":"Int","args":[
            {"control":"assign(Int)->Statement","type":"Int","name":"x","args":[{"ref":"Shared"}]},
            {"control":"block_cons(Statement,Block)->Block","type":"Int","args":[
              {"control":"assign(Int)->Statement","type":"Int","name":"y","args":[{"ref":"Shared"}]},
              {"control":"block_cons(Statement,Block)->Block","type":"Int","args":[
                {"control":"return(Int)->Statement","type":"Int","args":[{"ref":"LocalX"}]},
                {"control":"block_nil()->Block","type":"Int","args":[]}]}]}]}]}}]}
    ]
  })");
}

void test_exact_native_scope_and_request_identity() {
  const auto grammar = local_scope_grammar();
  const auto analysis = analyze_variation(grammar, generate_derivation(grammar, 5).genome);
  const auto shared = nonterminal(grammar, "Shared");
  const auto local_x = nonterminal(grammar, "LocalX");
  const auto& before_x = site_for(analysis, shared, 0);
  const auto& after_x = site_for(analysis, shared, 1);
  const auto& local = site_for(analysis, local_x);
  check(same_bindings(before_x.available_locals, {{"input", RType::Int}}),
      "site before assignment did not retain its exact native locals");
  check(same_bindings(after_x.available_locals,
          {{"input", RType::Int}, {"x", RType::Int}}),
      "site after assignment did not retain its exact native locals");
  check(!compatible_sites(before_x, after_x),
      "a native local-scope change did not change the compatibility contract");
  check(same_bindings(local.available_locals,
          {{"input", RType::Int}, {"x", RType::Int}, {"y", RType::Int}}) &&
        same_bindings(local.free_locals, {{"x", RType::Int}}),
      "free local references or their exact available scope were lost");

  const auto scoped = compile(R"({
    "format_version":"grammar-definition-v1",
    "entry":{"nonterminal":"Entry","type":"Int"},
    "search_limits":{"max_nodes":8,"max_depth":5},"execution_limits":{"fuel":100},
    "nonterminals":[
      {"id":"Entry","type":"Int","scope":[],"alternatives":[{"id":"zero","weight":1,
        "expression":{"constant":{"type":"Int","values":["0"]}}}]},
      {"id":"Scoped","type":"Int","scope":[{"name":"x","type":"Int"},{"name":"y","type":"Bool"}],
       "alternatives":[{"id":"one","weight":1,
        "expression":{"constant":{"type":"Int","values":["1"]}}}]}
    ]
  })");
  const auto scoped_nt = nonterminal(scoped, "Scoped");
  GenerationRequest xy{scoped_nt, RType::Int,
      {{"x", RType::Int}, {"y", RType::Bool}, {"extra", RType::String}}, {5, 4}};
  GenerationRequest yx{scoped_nt, RType::Int,
      {{"y", RType::Bool}, {"x", RType::Int}, {"extra", RType::String}}, {5, 4}};
  const auto genome = generate_derivation(scoped, 8, xy).genome;
  const auto xy_analysis = analyze_variation(scoped, genome, xy);
  const auto yx_analysis = analyze_variation(scoped, genome, yx);
  const auto& xy_site = site_for(xy_analysis, scoped_nt);
  const auto& yx_site = site_for(yx_analysis, scoped_nt);
  check(same_bindings(xy_site.visible_environment, xy.visible_environment) &&
        same_bindings(yx_site.visible_environment, yx.visible_environment) &&
        !compatible_sites(xy_site, yx_site),
      "visible request environment order did not participate in the exact contract key");
}

void test_compaction_and_untrusted_provenance() {
  const auto grammar = compile(R"({
    "format_version":"grammar-definition-v1",
    "entry":{"nonterminal":"Read","type":"Int"},
    "inputs":[{"name":"n","type":"Int"}],
    "search_limits":{"max_nodes":6,"max_depth":5},"execution_limits":{"fuel":100},
    "nonterminals":[{"id":"Read","type":"Int","scope":[],"alternatives":[
      {"id":"n","weight":1,"expression":{"input":"n"}}]}]
  })");
  auto imported = generate_derivation(grammar, 2).genome;
  imported.derivation.reset();
  imported.ast.names.push_back("unused");
  imported.ast.consts.push_back(Value::from_int(999));
  imported.meta = build_genome_meta(imported.ast);
  const auto original = analyze_variation(grammar, imported);
  const auto compacted = analyze_variation(grammar, repro::compact_genome_tables(imported));
  check(original.sites.size() == 1 && compacted.sites.size() == 1 &&
        original.sites[0].compatibility_key == compacted.sites[0].compatibility_key,
      "table compaction changed a normalized variation contract key");

  auto stale = imported;
  auto false_witness = std::make_shared<DerivationMetadata>();
  false_witness->grammar_hash = "untrusted-stale-witness";
  false_witness->seed_replayable = true;
  false_witness->choices.push_back({kNoGrammarId, kNoGrammarId, kNoGrammarId, 99, 100});
  stale.derivation = false_witness;
  const auto reconstructed = analyze_variation(grammar, stale);
  check(!reconstructed.witness.seed_replayable &&
        reconstructed.witness.grammar_hash == grammar.content_hash() &&
        reconstructed.witness.choices.size() == 1 &&
        reconstructed.sites[0].compatibility_key == original.sites[0].compatibility_key,
      "variation analysis trusted imported or stale witness metadata");
}

}  // namespace

int main() {
  try {
    test_nonterminal_and_registry_compatibility();
    test_template_sites_occurrences_and_budgets();
    test_asymmetric_forwarding_uses_physical_nesting();
    test_exact_native_scope_and_request_identity();
    test_compaction_and_untrusted_provenance();
    std::cout << "grammar variation contract: exact keys, template groups, budgets, scopes, and reconstruction passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
