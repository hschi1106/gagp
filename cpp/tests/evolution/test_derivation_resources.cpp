#include "gagp/core/host_threads.hpp"
#include <iostream>
#include <future>
#include <memory>
#include <thread>
#include <stdexcept>

#include "gagp/evolution/grammar/derivation_resources.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/grammar/joint_budget.hpp"
#include "gagp/evolution/grammar/donor.hpp"
#include "gagp/evolution/grammar/values.hpp"
#include "gagp/runtime/payload/payload.hpp"
#include "gagp/evolution/grammar/random.hpp"
#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/evolution/mutation.hpp"
#include "gagp/evolution/crossover.hpp"
#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/population_init.hpp"
#include "../../src/evolution/grammar/variation_internal.hpp"
#include "../../src/evolution/grammar/donor_internal.hpp"
#include "../../src/runtime/payload/staging.hpp"

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;
namespace {
using Json = cli_detail::JsonValue;
Json json(const std::string& text) { return cli_detail::JsonParser(text).parse(); }
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<class Action> void rejects(Action action) {
  try { action(); } catch (const std::invalid_argument&) { return; }
  throw std::runtime_error("invalid resource declaration accepted");
}
CompiledGrammar compile(const Json& document) { return compile_grammar(parse_definition(canonical_json(document))); }
Json definition() {
  return json(R"({"format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","category":"Program","type":"Int"},
    "search_limits":{"max_nodes":12,"max_depth":8},"execution_limits":{"fuel":100},
    "nonterminals":[{"id":"Main","category":"Program","type":"Int","scope":[],"alternatives":[
      {"id":"body","weight":1,"expression":{"control":"program(Block)->Program","type":"Int","args":[
        {"control":"block_cons(Statement,Block)->Block","type":"Int","args":[
          {"control":"return(Int)->Statement","type":"Int","args":[
            {"signature":"neg(Int)->Int","args":[{"constant":{"type":"Int","values":["7"]}}]}]},
          {"control":"block_nil()->Block","type":"Int","args":[]}]}]}}]}]})");
}
Json& root(Json& document) {
  return document.object_v.at("nonterminals").array_v[0].object_v.at("alternatives").array_v[0].object_v.at("expression");
}
void annotate(Json& expression) {
  expression.object_v["resource_charge"] = expression.object_v.count("control") ?
      json(R"({"nodes":1,"depth":0,"resets_depth":true})") :
      expression.object_v.count("signature") ? json(R"({"nodes":0,"depth":0,"resets_depth":false})") :
      json(R"({"nodes":1,"depth":1,"resets_depth":false})");
  if (expression.object_v.count("args")) for (auto& child : expression.object_v.at("args").array_v) annotate(child);
}
void test_projection_and_provenance() {
  auto document = definition(); const auto physical = compile(document);
  const auto first = generate_derivation(physical,42).genome;
  auto plain = project_derivation_resources(physical,first);
  check(first.ast.nodes.size() == 6 && plain.subtree().nodes == 6 && plain.subtree().peak() == 5,
        "default projection does not match physical nodes/depth");
  annotate(root(document)); const auto charged = compile(document);
  check(resource_charges_are_local(charged), "distinct native-kind costs were not certified local");
  auto second = generate_derivation(charged,42).genome;
  check(physical.content_hash() != charged.content_hash(),"resource charges omitted from grammar identity");
  check(ast_cache_key(first.ast) == ast_cache_key(second.ast),"resource charges changed program or fuel");
  auto projected = project_derivation_resources(charged,second);
  check(second.derivation->resources && second.derivation->resources->subtree().nodes == 5,
        "generation did not retain authored resource costs");
  check(projected.subtree().nodes == 5 && projected.subtree().peak() == 1,
        "zero administrative charges or statement depth reset ignored");
  auto forged = std::make_shared<DerivationMetadata>(*second.derivation);
  for (auto& node : forged->nodes) node.expression = kNoGrammarId;
  forged->grammar_hash = "untrusted"; second.derivation = forged;
  forged->resources = first.derivation->resources;
  check(project_derivation_resources(charged,second).subtree().nodes == 5,
        "resource accounting trusted attached provenance");
  const auto analysis = analyze_variation(charged, second);
  check(analysis.witness.resources->subtree().nodes == 5,
        "variation trusted forged resource provenance");
  check(!analysis.sites.empty() && analysis.sites.front().projected_resources.nodes == 5,
        "variation site did not retain reconstructed projected costs");
  VariationContext limited(std::make_shared<const CompiledGrammar>(charged), 128, ProjectedBudget{4, 8});
  const auto local_analysis = limited.analyze(second);
  const auto& local_site = local_analysis->sites.front();
  check(local_site.has_projected_allowance && !donor_fits(local_site, local_site),
        "certified resource allowance did not reject projected-over-budget crossover");
  second.ast.consts.at(second.ast.nodes.at(4).i0) = Value::from_int(8);
  rejects([&] { (void)project_derivation_resources(charged,second); });
  auto request = entry_request(charged); request.budget.max_nodes = 5;
  rejects([&] { (void)generate_derivation(charged,1,request); });
}
void test_repeated_holes() {
  const auto document = json(R"({"format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"E","type":"Int"},"search_limits":{"max_nodes":12,"max_depth":8},
    "execution_limits":{"fuel":100},"templates":[{"id":"Twice","type":"Int","scope":[],
      "holes":[{"id":"x","type":"Int","scope":[]}],"body":{"signature":"add(Int,Int)->Int",
      "resource_charge":{"nodes":0,"depth":0,"resets_depth":false},"args":[{"hole":"x"},{"hole":"x"}]}}],
    "nonterminals":[{"id":"E","type":"Int","scope":[],"alternatives":[{"id":"twice","weight":1,
      "expression":{"template":"Twice","holes":{"x":{"constant":{"type":"Int","values":["1"]},
      "resource_charge":{"nodes":2,"depth":1,"resets_depth":false}}}}}]}]})");
  const auto grammar = compile(document);
  const auto member = generate_derivation(grammar,42).genome;
  auto projection = project_derivation_resources(grammar,member);
  check(member.ast.nodes.size() == 7 && projection.subtree().nodes == 8 && projection.subtree().peak() == 4,
        "repeated holes did not account each physical occurrence and default envelope");
  const auto allowance = projection.replacement({4,5},10,6);
  check(allowance.accepts({3,1,0}) && !allowance.accepts({4,1,0}),"atomic replacement charge allowance differs");
}
void test_invalid_charges() {
  for (const auto text : {R"({"nodes":-1,"depth":0,"resets_depth":false})",
      R"({"nodes":1.5,"depth":0,"resets_depth":false})",R"({"nodes":65537,"depth":0,"resets_depth":false})",
      R"({"nodes":1,"depth":257,"resets_depth":false})",R"({"nodes":1,"depth":0,"resets_depth":0})",
      R"({"nodes":1,"depth":0})",R"({"nodes":1,"depth":0,"resets_depth":false,"extra":0})"}) {
    auto document = definition(); root(document).object_v["resource_charge"] = json(text);
    rejects([&] { (void)parse_definition(canonical_json(document)); });
  }
  auto document = definition();
  root(document) = json(R"({"ref":"Main","resource_charge":{"nodes":1,"depth":1,"resets_depth":false}})");
  rejects([&] { (void)parse_definition(canonical_json(document)); });
}

Json tradeoffs() {
  return json(R"({"format_version":"grammar-definition-v2","entry":{"nonterminal":"E","type":"Int"},
    "search_limits":{"max_nodes":12,"max_depth":8},"execution_limits":{"fuel":100},
    "nonterminals":[{"id":"E","type":"Int","scope":[],"alternatives":[
      {"id":"short","weight":1,"expression":{"constant":{"type":"Int","values":["1"]},
        "resource_charge":{"nodes":8,"depth":1,"resets_depth":false}}},
      {"id":"long","weight":1,"expression":{"signature":"neg(Int)->Int",
        "resource_charge":{"nodes":0,"depth":1,"resets_depth":false},"args":[
          {"constant":{"type":"Int","values":["2"]}}]}}]}]})");
}
void test_resource_invariance_certificate() {
  const auto cold = compile(tradeoffs());
  std::vector<std::future<std::vector<bool>>> readers;
  for (int i = 0; i < 4; ++i)
    readers.push_back(std::async(std::launch::async, [cold] {
      cold.require_executable();
      const auto result = cold.resource_invariant_roots({cold.entry(), cold.entry()});
      cold.require_executable();
      return result;
    }));
  for (auto& reader : readers)
    check(reader.get() == std::vector<bool>({true, true}),
          "concurrent executable/resource certificates changed shared-copy results");
  const auto unambiguous = compile(tradeoffs());
  check(!resource_charges_are_local(unambiguous), "fixture must defeat native-kind locality");
  auto certificate = certify_resource_invariance(unambiguous, {unambiguous.entry()});
  check(unambiguous.resource_invariant_roots({unambiguous.entry(), unambiguous.entry()}) ==
        std::vector<bool>({true, true}), "shared resource certificates lost root order or duplicates");
  const auto copied = unambiguous;
  check(copied.resource_invariant_roots({copied.entry()}).front(), "grammar copy lost its certificate");
  check(certificate.complete && certificate.roots == std::vector<bool>{true},
        "disjoint tree shapes did not certify invariant charges");
  // Repeated equivalent definitions must collapse without hiding a
  // cost difference several materialized nodes below their common root label.
  auto nested_doc = tradeoffs();
  auto& nested_nts = nested_doc.object_v.at("nonterminals").array_v;
  auto clone = nested_nts[0]; clone.object_v.at("id").string_v = "F";
  nested_nts.push_back(clone);
  nested_nts.push_back(json(R"({"id":"Root","type":"Int","scope":[],"alternatives":[
    {"id":"left","weight":1,"expression":{"signature":"neg(Int)->Int","args":[{"ref":"E"}]}},
    {"id":"right","weight":1,"expression":{"signature":"neg(Int)->Int","args":[{"ref":"F"}]}}]})"));
  nested_doc.object_v.at("entry").object_v.at("nonterminal").string_v = "Root";
  auto equivalent = compile(nested_doc);
  check(certify_resource_invariance(equivalent, {equivalent.entry()}).roots.front(),
        "equivalent nested definitions were not certified");
  nested_nts[1].object_v.at("alternatives").array_v[0].object_v.at("expression")
      .object_v.at("resource_charge").object_v.at("nodes").number_v = 7;
  auto nested_conflict = compile(nested_doc);
  check(!certify_resource_invariance(nested_conflict, {nested_conflict.entry()}).roots.front(),
        "partition merging hid a descendant charge conflict");
  certificate = certify_resource_invariance(unambiguous, {unambiguous.entry()}, 0);
  check(!certificate.complete && certificate.roots == std::vector<bool>{false},
        "capacity exhaustion certified an unchecked root");
  rejects([&] { (void)certify_resource_invariance(unambiguous, {kNoGrammarId}); });

  auto document = tradeoffs();
  auto& alternatives = document.object_v.at("nonterminals").array_v[0]
      .object_v.at("alternatives").array_v;
  auto duplicate = alternatives[0];
  duplicate.object_v.at("id").string_v = "overlapping";
  alternatives.push_back(duplicate);
  auto same = compile(document);
  check(certify_resource_invariance(same, {same.entry()}).roots.front(),
        "equal-cost ambiguity was rejected");
  alternatives.back().object_v.at("expression").object_v.at("resource_charge")
      .object_v.at("nodes").number_v = 1;
  auto conflicting = compile(document);
  check(!certify_resource_invariance(conflicting, {conflicting.entry()}).roots.front(),
        "overlapping distinct costs were certified");

  document = tradeoffs();
  auto& recursive = document.object_v.at("nonterminals").array_v[0]
      .object_v.at("alternatives").array_v[1].object_v.at("expression");
  recursive.object_v.at("args").array_v[0] = json(R"({"ref":"E"})");
  auto recursion = compile(document);
  certificate = certify_resource_invariance(recursion, {recursion.entry()});
  check(certificate.complete && certificate.roots.front(),
        "productive recursive resource grammar was not certified");
}
void test_joint_tradeoffs() {
  const auto grammar = compile(tradeoffs());
  check(!resource_charges_are_local(grammar), "conflicting native-kind costs were incorrectly certified local");
  VariationContext ambiguous(std::make_shared<const CompiledGrammar>(grammar), 128, ProjectedBudget{5, 5});
  check(!ambiguous.analyze(generate_derivation(grammar, 42).genome)->sites.front().has_projected_allowance,
        "uncertified grammar received an early resource filter");
  const auto request = entry_request(grammar);
  const auto frontier = joint_resource_frontier(grammar,request,{20,8});
  check(frontier.size() == 2 && frontier[0].physical_nodes == 5 && frontier[0].projected.nodes == 12 &&
        frontier[1].physical_nodes == 6 && frontier[1].projected.nodes == 5,
        "joint frontier dropped the physical/projected tradeoff");
  auto narrow = request; narrow.budget.max_nodes = 5;
  check(joint_resource_frontier(grammar,narrow,{5,8}).empty(),"independent minima invented a feasible derivation");
  check(joint_resource_frontier(grammar,request,{5,4}).empty(),"joint frontier ignored physical/projected depth");
  check(joint_resource_frontier(grammar,request,{5,5}).size() == 1,"feasible longer derivation was pruned");
  rejects([&] { (void)joint_resource_frontier(grammar,request,{20,8},0); });
  bool exhausted = false;
  try { (void)joint_resource_frontier(grammar,request,{20,8},1); }
  catch (const std::runtime_error&) { exhausted = true; }
  check(exhausted,"frontier capacity silently truncated alternatives");
  auto document = tradeoffs();
  auto& alternatives = document.object_v.at("nonterminals").array_v[0].object_v.at("alternatives").array_v;
  alternatives[0].object_v["generation_stages"] = json(R"(["initial"])");
  alternatives[1].object_v["generation_stages"] = json(R"(["mutation"])");
  const auto staged = compile(document); auto mutation = entry_request(staged); mutation.stage = GenerationStage::Mutation;
  check(joint_resource_frontier(staged,entry_request(staged),{5,8}).empty() &&
        joint_resource_frontier(staged,mutation,{5,8}).size() == 1,"resource frontier mixed generation stages");
}
void test_donor_resource_flow() {
  auto grammar = std::make_shared<const CompiledGrammar>(compile(tradeoffs()));
  VariationContext context(grammar);
  auto member = generate_derivation(*grammar, 42).genome;
  const auto analysis = context.analyze(member);
  check(analysis->sites.size() == 1,"unexpected root donor contract");
  bool short_seen = false, long_seen = false;
  for (unsigned seed = 0; seed < 128; ++seed) {
    const auto donor = generate_donor(context, seed, analysis->sites.front());
    const auto& cost = donor.projected_resources;
    const auto destination_cost = project_derivation_resources_in_frame(*grammar, donor.genome,
        donor_request(analysis->sites.front()), donor.frame).subtree(donor.payload.begin);
    check(destination_cost.nodes == cost.nodes && destination_cost.carried_depth == cost.carried_depth &&
        destination_cost.reset_depth == cost.reset_depth,
        "framed resource projection differs from destination donor witness");
    if (donor.nodes == 1) {
      short_seen = true;
      check(cost.nodes == 8 && cost.carried_depth == 1 && cost.reset_depth == 0,
            "short donor cost confused authored charges with physical nodes");
    } else {
      long_seen = true;
      check(donor.nodes == 2 && cost.nodes == 1 && cost.carried_depth == 2 && cost.reset_depth == 0,
            "long donor cost included its standalone envelope");
    }
    check(donor.genome.derivation->resources->subtree().nodes == cost.nodes + 4,
          "donor frame lost the complete program projection");
  }
  check(short_seen && long_seen,"donor resource flow did not cover both tradeoffs");
}
void test_projected_offspring_admission() {
  auto grammar = std::make_shared<const CompiledGrammar>(compile(tradeoffs()));
  ProgramGenome short_tree, long_tree;
  for (unsigned seed = 0; seed < 128 && (short_tree.ast.nodes.empty() || long_tree.ast.nodes.empty()); ++seed) {
    auto member = generate_derivation(*grammar, seed).genome;
    (member.ast.nodes.size() == 5 ? short_tree : long_tree) = std::move(member);
  }
  check(!short_tree.ast.nodes.empty() && !long_tree.ast.nodes.empty(), "missing admission test parents");
  VariationContext node_limited(grammar, 128, ProjectedBudget{5, 5});
  const auto rejected = variation_detail::accept(short_tree.ast, long_tree, node_limited);
  check(ast_cache_key(rejected.ast) == ast_cache_key(long_tree.ast) &&
        node_limited.counters().budget_rejections == 1,
        "physically smaller but projected-over-budget offspring was accepted");
  for (unsigned seed = 0; seed < 32; ++seed) {
    const auto child = mutate(long_tree, seed, node_limited, 1.0);
    check(child.derivation->resources->subtree().nodes <= 5,
          "CPU mutation bypassed projected offspring admission");
  }
  check(node_limited.counters().budget_rejections == 1,
        "budget-aware CPU donor generation left invalid proposals for admission");
  VariationContext depth_limited(grammar, 128, ProjectedBudget{20, 4});
  const auto repaired = variation_detail::accept(short_tree.ast, long_tree, depth_limited);
  check(ast_cache_key(repaired.ast) == ast_cache_key(short_tree.ast) &&
        depth_limited.counters().changed_children == 1,
        "projected admission prevented repair of an initially deep parent");
  const auto rejected_depth = variation_detail::accept(long_tree.ast, short_tree, depth_limited);
  check(ast_cache_key(rejected_depth.ast) == ast_cache_key(short_tree.ast) &&
        depth_limited.counters().budget_rejections == 1,
        "projected depth allowance was ignored");
  VariationContext unrestricted(grammar);
  const auto admitted = variation_detail::accept(short_tree.ast, long_tree, unrestricted);
  check(ast_cache_key(admitted.ast) == ast_cache_key(short_tree.ast),
        "optional offspring budget changed default admission");
  VariationContext crossover_budget(grammar, 128, ProjectedBudget{20, 4});
  const auto children = crossover(short_tree, long_tree, 42, crossover_budget);
  check(ast_cache_key(children.first.ast) == ast_cache_key(short_tree.ast) &&
        ast_cache_key(children.second.ast) == ast_cache_key(long_tree.ast),
        "crossover selected a pair that only fits in one projected direction");
  check(crossover_budget.counters().budget_rejections == 1 &&
        crossover_budget.counters().acceptance_rejections == 0 &&
        crossover_budget.counters().changed_children == 0,
        "projected crossover rejected children only after candidate selection");
}
void test_crossover_admission_matches_full_analysis() {
  auto document = tradeoffs();
  document.object_v.at("nonterminals").array_v[0].object_v.at("alternatives").array_v.push_back(json(R"({
    "id":"recursive","weight":2,"expression":{"signature":"neg(Int)->Int",
    "args":[{"ref":"E"}]}})"));
  auto grammar = std::make_shared<const CompiledGrammar>(compile(document));
  for (std::uint64_t seed = 0; seed < 32; ++seed) {
    auto a = generate_derivation(*grammar, seed).genome;
    auto b = generate_derivation(*grammar, seed + 32).genome;
    VariationContext reference(grammar, 128, ProjectedBudget{20, 4});
    VariationContext actual(grammar, 128, ProjectedBudget{20, 4});
    const auto aa = reference.analyze(a), bb = reference.analyze(b);
    GrammarRandom random(seed);
    const VariationSite* left = nullptr; const VariationSite* right = nullptr;
    std::uint64_t eligible = 0, contracts = 0, budgets = 0;
    for (const auto& x : aa->sites) for (const auto& y : bb->sites) {
      if (!compatible_sites(x, y)) { ++contracts; continue; }
      if (!donor_fits(x, y) || !donor_fits(y, x)) { ++budgets; continue; }
      bool invalid = false;
      const auto fits = [&](const ProgramGenome& base, const VariationSite& destination,
                            const ProgramGenome& donor, const VariationSite& source) {
        ProgramGenome child;
        child.ast = variation_detail::splice(base.ast, destination, donor.ast,
            source.occurrences.front(), source.occurrence_binder_ids.front(), destination.crossover_closed);
        try { return reference.offspring_budget()->accepts(reference.analyze(child)->witness.resources->subtree()); }
        catch (const std::invalid_argument&) { invalid = true; return false; }
      };
      if (!fits(a, x, b, y) || !fits(b, y, a, x)) {
        if (invalid) ++contracts; else ++budgets;
        continue;
      }
      if (random.bounded(++eligible) == 0) { left = &x; right = &y; }
    }
    variation_detail::SelectedCrossoverSites selected;
    const auto children = variation_detail::crossover_with_sites(a, b, seed, actual, &selected);
    check(bool(selected) == bool(left), "crossover admission changed feasible-pair selection");
    if (left) {
      const auto same_site = [](const VariationSite& a, const VariationSite& b) {
        return a.nonterminal == b.nonterminal && a.occurrences.size() == b.occurrences.size() &&
            std::equal(a.occurrences.begin(), a.occurrences.end(), b.occurrences.begin(),
                [](const auto& x, const auto& y) { return x.begin == y.begin && x.end == y.end; });
      };
      check(same_site(selected->first, *left) && same_site(selected->second, *right),
          "crossover changed selected coordinates despite equivalent ASTs");
      const auto expected_a = variation_detail::accept(variation_detail::splice(a.ast, *left, b.ast,
          right->occurrences.front(), right->occurrence_binder_ids.front(), left->crossover_closed), a, reference);
      const auto expected_b = variation_detail::accept(variation_detail::splice(b.ast, *right, a.ast,
          left->occurrences.front(), left->occurrence_binder_ids.front(), right->crossover_closed), b, reference);
      check(ast_cache_key(children.first.ast) == ast_cache_key(expected_a.ast) &&
          ast_cache_key(children.second.ast) == ast_cache_key(expected_b.ast),
          "crossover reservoir selection differs from full-analysis oracle");
    } else {
      check(ast_cache_key(children.first.ast) == ast_cache_key(a.ast) &&
          ast_cache_key(children.second.ast) == ast_cache_key(b.ast), "crossover fallback changed");
    }
    check(actual.counters().contract_rejections == contracts && actual.counters().budget_rejections == budgets,
        "crossover trial rejection classification changed");
  }
}
void test_projected_generation_and_initialization() {
  const auto grammar = std::make_shared<const CompiledGrammar>(compile(tradeoffs()));
  const auto request = entry_request(*grammar);
  for (unsigned seed = 0; seed < 16; ++seed) {
    const auto generated = generate_derivation(*grammar, seed, request, {5, 5});
    check(generated.genome.ast.nodes.size() == 6,
          "projected generation sampled an inadmissible physical/projected tradeoff");
    const auto replay = generate_derivation(*grammar, generated.derivation.seed, request);
    check(ast_cache_key(replay.genome.ast) == ast_cache_key(generated.genome.ast),
          "accepted budgeted generation lost ordinary seed replay");
  }
  bool exhausted = false;
  try { (void)generate_derivation(*grammar, 1, request, {4, 5}, 2); }
  catch (const std::runtime_error&) { exhausted = true; }
  check(exhausted,"projected generation ignored bounded exhaustion");
  rejects([&] { (void)generate_derivation(*grammar, 1, request, {5, 5}, 0); });
  EvolutionConfig config;
  config.compiled_grammar = grammar;
  config.generation_request = request;
  config.population_size = 4;
  config.fuel = 100;
  config.initial_resource_budget = ProjectedBudget{5, 5};
  const auto cases = prepare_case_set({{{}, Value::from_int(0)}});
  const auto initial = initialize_population(config, cases);
  check(initial.population.size() == 4 && !initial.replayed,"budgeted initialization failed");
  for (const auto& member : initial.population)
    check(member.ast.nodes.size() == 6,"initial population ignored projected budget");
  check(initialize_population(config, cases, &initial.population).replayed,
        "valid projected-budget population did not replay");
  auto invalid = initial.population;
  for (unsigned seed = 0; seed < 128; ++seed) {
    auto candidate = generate_derivation(*grammar, seed).genome;
    if (candidate.ast.nodes.size() == 5) { invalid.front() = std::move(candidate); break; }
  }
  // A forged cheap resource index cannot admit the expensive materialized tree.
  invalid.front().derivation = initial.population.front().derivation;
  rejects([&] { (void)initialize_population(config, cases, &invalid); });
  VariationContext context(grammar, 128, ProjectedBudget{4, 5});
  const auto analysis = context.analyze(initial.population.front());
  exhausted = false;
  try { (void)generate_donor(context, 1, analysis->sites.front(), initial.population.front(), 2); }
  catch (const std::runtime_error&) { exhausted = true; }
  check(exhausted,"projected donor generation ignored bounded exhaustion");
  auto mixed_document = tradeoffs();
  mixed_document.object_v.at("nonterminals").array_v.push_back(json(R"({
    "id":"B","type":"Bool","scope":[],"alternatives":[{"id":"boolean","weight":1,
    "expression":{"constant":{"type":"Bool","values":[true]}}}]})"));
  config.compiled_grammar = std::make_shared<const CompiledGrammar>(compile(mixed_document));
  const auto roots = named_population_requests(*config.compiled_grammar, {"E", "B"});
  config.generation_request = roots[0]; config.additional_generation_requests = {roots[1]};
  const auto mixed_cases = prepare_case_set({{{}, Value::from_int(0)}, {{}, Value::from_bool(true)}});
  const auto mixed = initialize_population(config, mixed_cases);
  VariationContext mixed_context(config.compiled_grammar, roots);
  for (const auto& member : mixed.population)
    check(config.initial_resource_budget->accepts(mixed_context.analyze(member)->witness.resources->subtree()),
          "mixed-root initial population ignored projected budget");
  check(initialize_population(config, mixed_cases, &mixed.population).replayed,
        "mixed-root projected-budget replay failed");
}
void test_shared_hole_tradeoffs() {
  auto document = tradeoffs();
  document.object_v["templates"] = json(R"([{"id":"Twice","type":"Int","scope":[],
    "holes":[{"id":"x","type":"Int","scope":[]}],"body":{"signature":"add(Int,Int)->Int",
    "resource_charge":{"nodes":0,"depth":0,"resets_depth":false},"args":[{"hole":"x"},{"hole":"x"}]}}])");
  document.object_v.at("entry").object_v.at("nonterminal").string_v = "Root";
  document.object_v.at("nonterminals").array_v.push_back(json(R"({"id":"Root","type":"Int","scope":[],
    "alternatives":[{"id":"twice","weight":1,"expression":{"template":"Twice","holes":{"x":{"ref":"E"}}}}]})"));
  const auto grammar = compile(document); auto request = entry_request(grammar);
  const auto all = joint_resource_frontier(grammar,request,{30,8});
  check(all.size() == 2 && all[0].physical_nodes == 7 && all[0].projected.nodes == 20 &&
        all[1].physical_nodes == 9 && all[1].projected.nodes == 6,"shared-hole frontier lost linked alternatives");
  request.budget.max_nodes = 8;
  check(joint_resource_frontier(grammar,request,{13,8}).empty(),"repeated hole illegally mixed different derivations");
}
void test_projected_donor_retry_sequence() {
  auto grammar = std::make_shared<const CompiledGrammar>(compile(tradeoffs()));
  VariationContext context(grammar, 128, ProjectedBudget{5, 5});
  const auto parent = generate_derivation(*grammar, 42).genome;
  const auto site = context.analyze(parent)->sites.front();
  for (std::uint64_t seed = 0; seed < 16; ++seed) {
    std::string expected;
    GrammarRandom attempts(seed);
    auto attempt_seed = seed;
    for (int attempt = 0; attempt < 4; ++attempt) {
      auto donor = generate_donor(context, attempt_seed, site);
      ProgramGenome child;
      child.ast = variation_detail::splice(parent.ast, site, donor.genome.ast,
          donor.payload, site.occurrence_binder_ids.front());
      const bool expected_admission = context.offspring_budget()->accepts(
          context.analyze(child)->witness.resources->subtree());
      check(validate_budgeted_derivation(*grammar, child, context.request(),
                *context.offspring_budget()) == expected_admission,
            "budgeted execution admission differs from full variation analysis");
      if (expected_admission) {
        expected = ast_cache_key(donor.genome.ast);
        break;
      }
      attempt_seed = attempts.next();
    }
    std::string actual;
    try { actual = ast_cache_key(generate_donor(context, seed, site, parent, 4).genome.ast); }
    catch (const std::runtime_error&) {}
    check(actual == expected, "cached budget rejection changed donor retry sequence or exhaustion");
  }
  const std::vector<std::uint64_t> seeds{0, 1, 2, 3, 0, 1, 2, 3, 5};
  // Accepted children may evict the parent; pool-owned certification must survive.
  VariationContext pool_context(grammar, 1, ProjectedBudget{5, 5});
  check(generate_donor_pool(pool_context, {}, VariationSite{}, ProgramGenome{}, 0).empty(),
        "empty pool must perform no generation or destination validation");
  check(pool_context.cache().counters().hits == 0 && pool_context.cache().counters().misses == 0,
        "empty pool touched analysis accounting");
  bool invalid_attempts = false;
  try { (void)generate_donor_pool(pool_context, seeds, site, parent, 0); }
  catch (const std::invalid_argument&) { invalid_attempts = true; }
  check(invalid_attempts, "donor workers swallowed an invalid attempt limit");
  // Follow an exceptional batch with a successful call and a partial batch.
  const auto pool = generate_donor_pool(pool_context, seeds, site, parent, 4);
  check(pool.size() == seeds.size(), "donor pool lost failed seed slots");
  for (std::size_t i = 0; i < seeds.size(); ++i) {
    std::optional<ContextualDonor> independent;
    try { independent = generate_donor(context, seeds[i], site, parent, 4); }
    catch (const std::runtime_error&) {}
    check(pool[i].has_value() == independent.has_value(),
          "pool admission reuse changed bounded exhaustion");
    if (independent)
      check(ast_cache_key(pool[i]->genome.ast) == ast_cache_key(independent->genome.ast),
            "pool admission reuse changed the selected donor");
  }
}
void test_destination_resource_interpretation() {
  auto document = tradeoffs();
  auto& nonterminals = document.object_v.at("nonterminals").array_v;
  nonterminals[0].object_v["mutation_entry"] = json(R"("M")");
  nonterminals.push_back(json(R"({"id":"M","type":"Int","scope":[],"alternatives":[
    {"id":"donor","weight":1,"expression":{"constant":{"type":"Int","values":["1"]},
    "resource_charge":{"nodes":20,"depth":1,"resets_depth":false}}}]})"));
  auto grammar = std::make_shared<const CompiledGrammar>(compile(document));
  VariationContext context(grammar, 128, ProjectedBudget{12, 5});
  const auto parent = generate_derivation(*grammar, 42).genome;
  const auto site = context.analyze(parent)->sites.front();
  check(!site.has_projected_allowance && grammar->resource_invariant_roots({grammar->entry()}).front(),
        "destination interpretation fixture did not use root invariance");
  const auto donor = generate_donor(context, 1, site);
  check(donor.projected_resources.nodes == 20, "mutation entry cost fixture changed");
  const auto target = project_derivation_resources_in_frame(*grammar, donor.genome,
      donor_request(site), donor.frame);
  check(target.subtree(donor.payload.begin).nodes == 8, "destination cost was not reconstructed");
  const auto accepted = generate_donor(context, 1, site, parent, 1);
  check(ast_cache_key(accepted.genome.ast) == ast_cache_key(donor.genome.ast),
        "mutation-entry cost incorrectly rejected a destination-valid donor");
}
void test_certified_donor_budget_rejection() {
  auto document = tradeoffs();
  auto& alternatives = document.object_v.at("nonterminals").array_v[0]
      .object_v.at("alternatives").array_v;
  alternatives[0].object_v.at("expression").object_v.at("resource_charge") =
      json(R"({"nodes":1,"depth":1,"resets_depth":false})");
  alternatives[1].object_v.at("expression").object_v.at("resource_charge") =
      json(R"({"nodes":3,"depth":1,"resets_depth":false})");
  auto grammar = std::make_shared<const CompiledGrammar>(compile(document));
  check(resource_charges_are_local(*grammar), "test requires invariant resource charges");
  VariationContext context(grammar, 128, ProjectedBudget{5, 5});
  const auto parent = generate_derivation(*grammar, 42).genome;
  const auto certified = context.analyze(parent)->sites.front();
  check(certified.has_projected_allowance, "local resource certificate did not reach donor site");
  auto forged = certified;
  forged.projected_allowance = {};  // Caller metadata must not force rejection.
  bool saw_accept = false, saw_reject = false;
  std::vector<std::uint64_t> seeds;
  std::vector<std::string> expected;
  for (std::uint64_t seed = 0; seed < 32; ++seed) {
    seeds.push_back(seed);
    expected.emplace_back();
    GrammarRandom retries(seed);
    auto attempt_seed = seed;
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
      auto donor = generate_donor(context, attempt_seed, certified);
      ProgramGenome child;
      child.ast = variation_detail::splice(parent.ast, certified, donor.genome.ast,
          donor.payload, certified.occurrence_binder_ids.front());
      const bool accepted = context.offspring_budget()->accepts(
          context.analyze(child)->witness.resources->subtree());
      check(certified.projected_allowance.accepts(donor.projected_resources) == accepted,
            "certified donor allowance differs from complete child reconstruction");
      saw_accept |= accepted; saw_reject |= !accepted;
      if (accepted) { expected.back() = ast_cache_key(donor.genome.ast); break; }
      attempt_seed = retries.next();
    }
    std::string actual;
    try { actual = ast_cache_key(generate_donor(context, seed, forged, parent, 4).genome.ast); }
    catch (const std::runtime_error&) {}
    check(actual == expected.back(), "early budget rejection changed retry or exhaustion");
  }
  check(saw_accept && saw_reject, "certified donor test did not exercise both decisions");
  const auto pool = generate_donor_pool(context, seeds, forged, parent, 4);
  for (std::size_t i = 0; i < seeds.size(); ++i)
    check((pool[i] ? ast_cache_key(pool[i]->genome.ast) : std::string{}) == expected[i],
          "parallel certified rejection differs from full reconstruction");
}
void test_scalar_resource_pool() {
  for (const auto* type : {"Int", "Float"}) {
    auto document = tradeoffs();
    const bool floating = std::string(type) == "Float";
    document.object_v.at("entry").object_v.at("type").string_v = type;
    auto& nt = document.object_v.at("nonterminals").array_v[0];
    nt.object_v.at("type").string_v = type;
    nt.object_v.at("alternatives") = json(floating ? R"([
      {"id":"negative_zero","weight":1,"expression":{"constant":{"type":"Float","values":[-0.0]},
        "resource_charge":{"nodes":8,"depth":1,"resets_depth":false}}},
      {"id":"positive_zero","weight":1,"expression":{"constant":{"type":"Float","values":[0.0]},
        "resource_charge":{"nodes":1,"depth":1,"resets_depth":false}}}])" : R"([
      {"id":"expensive","weight":1,"expression":{"constant":{"type":"Int","values":["1","2"]},
        "resource_charge":{"nodes":8,"depth":1,"resets_depth":false}}},
      {"id":"cheap","weight":1,"expression":{"constant":{"type":"Int","values":["3","4","5"]},
        "resource_charge":{"nodes":1,"depth":1,"resets_depth":false}}}])");
    auto grammar = std::make_shared<const CompiledGrammar>(compile(document));
    for (std::uint64_t parent_seed = 0; parent_seed < 4; ++parent_seed) {
      VariationContext context(grammar, 128, ProjectedBudget{5, 5});
      const auto parent = generate_derivation(*grammar, parent_seed).genome;
      const auto site = context.analyze(parent)->sites.front();
      std::vector<std::uint64_t> seeds;
      for (std::uint64_t seed = 0; seed < 32; ++seed) seeds.push_back(seed);
      const auto pool = generate_donor_pool(context, seeds, site, parent, 4);
      for (std::size_t i = 0; i < seeds.size(); ++i) {
        std::string expected;
        GrammarRandom attempts(seeds[i]);
        auto seed = seeds[i];
        for (int attempt = 0; attempt < 4; ++attempt) {
          const auto donor = generate_donor(context, seed, site);
          ProgramGenome child;
          child.ast = variation_detail::splice(parent.ast, site, donor.genome.ast,
              donor.payload, site.occurrence_binder_ids.front());
          if (context.offspring_budget()->accepts(
                  context.analyze(child)->witness.resources->subtree())) {
            expected = ast_cache_key(donor.genome.ast);
            break;
          }
          seed = attempts.next();
        }
        const auto actual = pool[i] ? ast_cache_key(pool[i]->genome.ast) : std::string{};
        check(actual == expected, "scalar resource reuse changed domain/zero admission or retry order");
      }
    }
  }
}

void test_independent_donor_pool_batch() {
  auto grammar = std::make_shared<const CompiledGrammar>(compile(tradeoffs()));
  VariationContext context(grammar, 128, ProjectedBudget{5, 5});
  std::vector<ProgramGenome> parents;
  for (std::uint64_t seed = 0; seed < 137; ++seed) parents.push_back(generate_derivation(*grammar, seed).genome);
  std::vector<DonorPoolJob> jobs;
  for (std::size_t i = 0; i < parents.size(); ++i)
    jobs.push_back({&parents[i], context.analyze(parents[i])->sites.front(), {i * 4, i * 4 + 1, i * 4 + 2, i * 4 + 3}});
  const auto batch = try_generate_donor_pools(context, jobs, 4);
  if (gagp::host_thread_limit() == 1) check(!batch, "1T must decline optional parallel donor batching");
  if (gagp::host_thread_limit() > 1) {
    check(bool(batch), "independent scalar donor batch unexpectedly declined");
    check(batch->size() == jobs.size(), "donor batch changed pool count");
    std::vector<WarmPopulationMember> proofs;
    for (const auto& parent : parents) {
      WarmPopulationMember proof;
      proof.reads = std::make_shared<payload::StagedPayloads>();
      {
        payload::StagedPayloads::Scope scope(*proof.reads);
        proof.analysis = context.analyze(parent, &proof.runtime_identity);
      }
      proofs.push_back(std::move(proof));
    }
    const auto warmed = try_generate_warmed_donor_pools(context, jobs, proofs, 4);
    auto poisoned = std::make_shared<VariationAnalysis>(*proofs.front().analysis);
    poisoned->sites.clear();
    proofs.front().analysis = std::move(poisoned);
    proofs.front().reads = std::make_shared<payload::StagedPayloads>();
    const auto invalidated = try_generate_warmed_donor_pools(context, jobs, proofs, 4);
    check(warmed && invalidated, "donor handoff failed reuse or unsealed-proof fallback");
    for (std::size_t i = 0; i < jobs.size(); ++i) {
      const auto reference = generate_donor_pool(context, jobs[i].seeds, jobs[i].site, *jobs[i].destination, 4);
      check((*batch)[i].size() == reference.size(), "donor batch changed seed count");
      for (std::size_t j = 0; j < reference.size(); ++j)
        for (const auto* result : {&*batch, &*warmed, &*invalidated})
          check((reference[j] ? ast_cache_key(reference[j]->genome.ast) : std::string{}) ==
              ((*result)[i][j] ? ast_cache_key((*result)[i][j]->genome.ast) : std::string{}),
              "donor batch changed seeded proposals or bounded exhaustion");
    }
  }
  rejects([&] { (void)try_generate_donor_pools(context, jobs, 0); });
  jobs[0].seeds.resize(1);
  check(!try_generate_donor_pools(context, jobs, 4), "single-seed registry behavior must retain original path");
}

void test_payload_pool_conflict_replay() {
  auto document = tradeoffs();
  document.object_v.at("entry").object_v.at("type").string_v = "String";
  auto& nt = document.object_v.at("nonterminals").array_v[0];
  nt.object_v.at("type").string_v = "String";
  nt.object_v.at("alternatives") = json(R"([
    {"id":"value","weight":1,"expression":{"constant":{"type":"String","values":["a","b"]}}}])");
  auto grammar = std::make_shared<const CompiledGrammar>(compile(document));
  VariationContext context(grammar, 128, ProjectedBudget{5, 5});
  auto parent = generate_derivation(*grammar, 0).genome;
  parent.ast.consts[0] = payload::make_string_value("b");
  const auto site = context.analyze(parent)->sites.front();
  std::vector<std::uint64_t> seeds;
  std::vector<std::string> expected;
  bool saw_a = false;
  for (std::uint64_t seed = 0; seed < 16; ++seed) {
    seeds.push_back(seed);
    const auto donor = generate_donor(context, seed, site, parent, 4);
    std::string text;
    check(payload::lookup_string(donor.genome.ast.consts.front(), &text), "reference donor payload missing");
    expected.push_back(text); saw_a |= text == "a";
  }
  check(saw_a, "collision fixture did not exercise the target key");
  const auto colliding = payload::make_string_value("a");
  payload::register_string(colliding, "z");
  const std::vector<DonorPoolJob> jobs{{&parent, site, seeds}, {&parent, site, seeds}};
  check(!try_generate_donor_pools(context, jobs, 4), "colliding batch must decline atomic commit");
  std::string unchanged;
  check(payload::lookup_string(colliding, &unchanged) && unchanged == "z",
        "failed donor batch changed committed payload contents");
  const auto pool = generate_donor_pool(context, seeds, site, parent, 4);
  check(pool.size() == seeds.size(), "conflict replay dropped seed slots");
  for (std::size_t i = 0; i < pool.size(); ++i) {
    check(pool[i].has_value(), "payload conflict changed donor acceptance");
    std::string text;
    check(payload::lookup_string(pool[i]->genome.ast.consts.front(), &text) && text == expected[i],
          "payload conflict replay changed seeded donor contents");
  }
  std::string committed;
  check(payload::lookup_string(colliding, &committed) && committed == "a",
        "conflict did not replay sequential registry writes");
  payload::clear();
  (void)payload::make_string_value("b");  // Keep the destination valid; "a" must be newly committed.
  const auto batch = try_generate_donor_pools(context, jobs, 4);
  if (gagp::host_thread_limit() == 1) check(!batch, "1T must decline optional parallel donor batching");
  if (gagp::host_thread_limit() > 1) {
    check(bool(batch), "nonconflicting payload batch declined");
    for (const auto& result : *batch) for (std::size_t i = 0; i < result.size(); ++i) {
      std::string text;
      check(result[i] && payload::lookup_string(result[i]->genome.ast.consts.front(), &text) &&
            text == expected[i], "payload batch changed proposal order or lost committed contents");
    }
    check(payload::lookup_string(colliding, &committed) && committed == "a",
          "successful batch did not publish its new payload");
    WarmPopulationMember proof;
    proof.reads = std::make_shared<payload::StagedPayloads>();
    {
      payload::StagedPayloads::Scope scope(*proof.reads);
      proof.analysis = context.analyze(parent, &proof.runtime_identity);
    }
    payload::register_string(parent.ast.consts.front(), "outside-domain");
    rejects([&] { (void)try_generate_warmed_donor_pools(context, jobs, {proof, proof}, 4); });
    payload::register_string(parent.ast.consts.front(), "b");
  }
  payload::clear();
  {
    payload::StagedPayloads transaction;
    payload::StagedPayloads::Scope scope(transaction);
    (void)payload::make_string_value("b");
    check(!try_generate_donor_pools(context, jobs, 4),
          "donor batch must defer to the enclosing uncommitted payload view");
    const auto nested = generate_donor_pool(context, seeds, site, parent, 4);
    check(nested.size() == seeds.size(), "enclosing transaction lost donor seed slots");
    for (std::size_t i = 0; i < nested.size(); ++i) {
      std::string text;
      check(nested[i] && payload::lookup_string(nested[i]->genome.ast.consts.front(), &text) &&
                text == expected[i],
            "enclosing payload transaction changed donor contents or acceptance");
    }
  }
  check(!payload::lookup_string(colliding, &committed) &&
            !payload::lookup_string(parent.ast.consts.front(), &committed),
        "aborted enclosing transaction leaked donor or destination payloads");
}

void test_recursive_frontier() {
  auto document = tradeoffs();
  auto& alternatives = document.object_v.at("nonterminals").array_v[0].object_v.at("alternatives").array_v;
  alternatives.push_back(json(R"({"id":"recursive","weight":1,"expression":{"signature":"neg(Int)->Int",
    "resource_charge":{"nodes":0,"depth":0,"resets_depth":true},"args":[{"ref":"E"}]}})"));
  const auto grammar = compile(document); auto request = entry_request(grammar);
  const auto costs = joint_resource_frontier(grammar,request,{30,8});
  // Enumerate actual admitted seeded trees independently and require a retained
  // cost no worse in every coordinate; resets create extra nondominated states.
  for (unsigned seed=0;seed<256;++seed) {
    const auto member = generate_derivation(grammar,seed).genome;
    const auto actual = project_derivation_resources(grammar,member).subtree();
    const auto full = reconstruct_derivation(grammar,member).resources->subtree();
    check(actual.nodes == full.nodes && actual.carried_depth == full.carried_depth &&
          actual.reset_depth == full.reset_depth,
          "resource-only query differs from the fully lowered canonical witness");
    bool covered = false;
    for (const auto& cost : costs)
      covered |= cost.physical_nodes <= member.ast.nodes.size() && cost.projected.nodes <= actual.nodes &&
          cost.projected.carried_depth <= actual.carried_depth && cost.projected.reset_depth <= actual.reset_depth;
    check(covered,"recursive resource frontier omitted a feasible generated derivation");
  }
  check(costs.size() > 2,"recursive reset tradeoffs were erased");
  std::size_t compared = 0;
  for (std::uint32_t physical_nodes=5;physical_nodes<=12;++physical_nodes)
    for (std::uint32_t physical_depth=4;physical_depth<=8;++physical_depth)
      for (std::uint64_t projected_nodes=0;projected_nodes<=20;++projected_nodes)
        for (std::uint64_t projected_depth=0;projected_depth<=8;++projected_depth) {
          auto bounded = request; bounded.budget = {physical_nodes,physical_depth};
          const auto frontier = joint_resource_frontier(grammar,bounded,{projected_nodes,projected_depth});
          std::vector<JointResourceCost> enumerated;
          // All possible shapes: zero or more reset negations around either
          // the expensive literal or the cheap two-node negated literal.
          for (unsigned base=0;base<2;++base) for (unsigned resets=0;resets<8;++resets) {
            const unsigned height = (base ? 2 : 1)+resets;
            JointResourceCost candidate{height+4,{base ? 5U : 12U,
                3+(resets ? 0U : base ? 2U : 1U),resets ? (base ? 2U : 1U) : 0U}};
            if (height+3<=physical_depth && candidate.physical_nodes<=physical_nodes &&
                candidate.projected.nodes<=projected_nodes && candidate.projected.peak()<=projected_depth)
              enumerated.push_back(candidate);
          }
          for (const auto& row : frontier) {
            bool exists = false;
            for (const auto& expected : enumerated)
              exists |= row.physical_nodes==expected.physical_nodes && row.projected.nodes==expected.projected.nodes &&
                  row.projected.carried_depth==expected.projected.carried_depth && row.projected.reset_depth==expected.projected.reset_depth;
            check(exists,"joint frontier invented a cost absent from exhaustive tree enumeration");
          }
          for (const auto& expected : enumerated) {
            bool retained = false;
            for (const auto& row : frontier)
              retained |= row.physical_nodes<=expected.physical_nodes && row.projected.nodes<=expected.projected.nodes &&
                  row.projected.carried_depth<=expected.projected.carried_depth && row.projected.reset_depth<=expected.projected.reset_depth;
            check(retained,"joint frontier removed a feasible exhaustive derivation");
          }
          ++compared;
        }
  check(compared == 7560,"joint resource exhaustive coverage changed");
}
}  // namespace
int main() {
  try { test_projection_and_provenance(); test_repeated_holes(); test_invalid_charges();
        test_resource_invariance_certificate(); test_joint_tradeoffs(); test_projected_donor_retry_sequence();
    test_destination_resource_interpretation(); test_certified_donor_budget_rejection(); test_scalar_resource_pool();
    test_independent_donor_pool_batch(); test_payload_pool_conflict_replay(); test_donor_resource_flow(); test_projected_offspring_admission();
        test_crossover_admission_matches_full_analysis(); test_projected_generation_and_initialization();
        test_shared_hole_tradeoffs(); test_recursive_frontier(); }
  catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
  std::cout<<"certified derivation resource projection passed\n";
}
