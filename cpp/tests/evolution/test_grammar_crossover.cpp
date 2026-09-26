#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/crossover.hpp"
#include "gagp/evolution/mutation.hpp"
#include "gagp/evolution/repro/prep.hpp"
#include "../../src/evolution/grammar/variation_internal.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "../fixtures/closed_crossover.hpp"

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;

namespace {

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void rejects(const std::function<void()>& action, const char* message) {
  try {
    action();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error(message);
}

std::shared_ptr<const CompiledGrammar> compile_shared(const std::string& text) {
  return std::make_shared<const CompiledGrammar>(compile_grammar(parse_definition(text)));
}

std::uint32_t nonterminal(const CompiledGrammar& grammar, const std::string& stable_id) {
  const auto found = std::find_if(grammar.nonterminals().begin(), grammar.nonterminals().end(),
      [&](const auto& value) { return value.stable_id == stable_id; });
  if (found == grammar.nonterminals().end())
    throw std::runtime_error("missing fixture nonterminal " + stable_id);
  return found->id;
}

std::vector<std::int64_t> integer_constants(const ProgramGenome& genome) {
  std::vector<std::int64_t> result;
  for (const auto& node : genome.ast.nodes) {
    if (node.kind != NodeKind::CONST) continue;
    const auto& value = genome.ast.consts.at(static_cast<std::size_t>(node.i0));
    check(value.tag == ValueTag::Int, "fixture emitted a non-Int constant");
    result.push_back(value.i);
  }
  return result;
}

void certify_and_execute(const CompiledGrammar& grammar, const ProgramGenome& child,
    const GenerationRequest& request) {
  require_membership(grammar, child, request);
  std::vector<InputSpec> inputs;
  std::vector<std::string> names;
  for (const auto& input : grammar.inputs()) {
    inputs.push_back({input.name, input.type});
    names.push_back(input.name);
  }
  const auto verified = verify_ast(child.ast, inputs);
  check(static_cast<bool>(verified),
      "compiled crossover child failed native verification");
  const auto bytecode = compile_for_eval(child, verified.verified, names);
  const auto result = execute_bytecode_cpu(
      bytecode, {}, static_cast<int>(grammar.execution_limits().fuel));
  check(!result.is_error, "compiled crossover child failed native execution");
  check(child.derivation && !child.derivation->seed_replayable &&
        child.derivation->grammar_hash == grammar.content_hash(),
      "compiled crossover child lacks reconstructed grammar provenance");
}

std::shared_ptr<const CompiledGrammar> repeated_hole_grammar(
    const std::string& values = "[\"1\",\"9\"]") {
  return compile_shared(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","type":"Int"},
    "search_limits":{"max_nodes":12,"max_depth":7},
    "execution_limits":{"fuel":100},
    "templates":[{"id":"Cancel","type":"Int","scope":[],
      "holes":[{"id":"value","type":"Int","scope":[]}],
      "body":{"signature":"add(Int,Int)->Int","args":[
        {"hole":"value"},{"signature":"neg(Int)->Int","args":[{"hole":"value"}]}]}}],
    "nonterminals":[
      {"id":"Value","type":"Int","scope":[],"alternatives":[{"id":"leaf","weight":1,
        "expression":{"constant":{"type":"Int","values":)" + values + R"(}}}]},
      {"id":"Main","type":"Int","scope":[],"alternatives":[{"id":"cancel","weight":1,
        "expression":{"template":"Cancel","holes":{"value":{"ref":"Value"}}}}]}
    ]
  })");
}

ProgramGenome repeated_parent(const CompiledGrammar& grammar, std::int64_t value) {
  auto genome = generate_derivation(grammar, static_cast<std::uint64_t>(value)).genome;
  check(genome.ast.consts.size() == 1 && integer_constants(genome).size() == 2,
      "repeated-hole fixture no longer shares one constant table entry");
  genome.ast.consts[0] = Value::from_int(value);
  genome.meta = build_genome_meta(genome.ast);
  return genome;
}

void test_repeated_holes_are_replaced_atomically() {
  const auto grammar = repeated_hole_grammar();
  const auto parent_a = repeated_parent(*grammar, 1);
  const auto parent_b = repeated_parent(*grammar, 9);
  VariationContext context(grammar);

  for (std::uint64_t seed = 0; seed < 64; ++seed) {
    const auto children = crossover(parent_a, parent_b, seed, context);
    const auto left = integer_constants(children.first);
    const auto right = integer_constants(children.second);
    check(left.size() == 2 && left[0] == 9 && left[1] == 9,
        "first child split or failed to replace a repeated logical hole");
    check(right.size() == 2 && right[0] == 1 && right[1] == 1,
        "second child split or failed to replace a repeated logical hole");
    certify_and_execute(*grammar, children.first, context.request());
    certify_and_execute(*grammar, children.second, context.request());
  }

  const auto& counters = context.counters();
  check(counters.crossover_attempts == 64 && counters.changed_children == 128 &&
        counters.unchanged_children == 0 && counters.fallback_children == 0,
      "atomic repeated-hole crossover counters did not classify changed children");
  check(counters.contract_rejections == 128 && counters.budget_rejections == 0 &&
        counters.acceptance_rejections == 0,
      "atomic repeated-hole crossover rejection counters changed unexpectedly");
}

std::shared_ptr<const CompiledGrammar> split_nonterminal_grammar() {
  return compile_shared(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","type":"Int"},
    "search_limits":{"max_nodes":12,"max_depth":7},
    "execution_limits":{"fuel":100},
    "nonterminals":[
      {"id":"Main","type":"Int","scope":[],"alternatives":[{"id":"pair","weight":1,
        "expression":{"signature":"add(Int,Int)->Int","args":[
          {"ref":"Left"},{"ref":"Right"}]}}]},
      {"id":"Left","type":"Int","scope":[],"alternatives":[{"id":"left","weight":1,
        "expression":{"constant":{"type":"Int","values":["1","10"]}}}]},
      {"id":"Right","type":"Int","scope":[],"alternatives":[{"id":"right","weight":1,
        "expression":{"constant":{"type":"Int","values":["2","20"]}}}]}
    ]
  })");
}

ProgramGenome split_parent(const CompiledGrammar& grammar, std::int64_t left,
    std::int64_t right) {
  auto genome = generate_derivation(grammar, static_cast<std::uint64_t>(left + right)).genome;
  check(genome.ast.nodes.size() == 7 && genome.ast.nodes[4].kind == NodeKind::CONST &&
        genome.ast.nodes[5].kind == NodeKind::CONST,
      "split-nonterminal fixture shape changed");
  genome.ast.consts.at(static_cast<std::size_t>(genome.ast.nodes[4].i0)) = Value::from_int(left);
  genome.ast.consts.at(static_cast<std::size_t>(genome.ast.nodes[5].i0)) = Value::from_int(right);
  genome.meta = build_genome_meta(genome.ast);
  return genome;
}

void test_same_type_different_nonterminals_never_cross() {
  const auto grammar = split_nonterminal_grammar();
  const auto parent_a = split_parent(*grammar, 1, 2);
  const auto parent_b = split_parent(*grammar, 10, 20);
  VariationContext context(grammar);

  for (std::uint64_t seed = 0; seed < 64; ++seed) {
    const auto children = crossover(parent_a, parent_b, 1000 + seed, context);
    for (const auto* child : {&children.first, &children.second}) {
      const auto values = integer_constants(*child);
      check(values.size() == 2 && (values[0] == 1 || values[0] == 10) &&
            (values[1] == 2 || values[1] == 20),
          "same-typed distinct nonterminals crossed their exact contracts");
      certify_and_execute(*grammar, *child, context.request());
    }
  }

  const auto& counters = context.counters();
  check(counters.crossover_attempts == 64 && counters.contract_rejections == 384 &&
        counters.budget_rejections == 0 && counters.acceptance_rejections == 0 &&
        counters.fallback_children == 0,
      "different-nonterminal crossover counters changed unexpectedly");
  check(counters.unchanged_children + counters.changed_children == 128,
      "successful crossover children were not completely classified");
}

void test_explicit_crossover_groups() {
  const auto grammar = compile_shared(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","type":"Int"},
    "search_limits":{"max_nodes":12,"max_depth":7},
    "execution_limits":{"fuel":100},
    "templates":[{"id":"Pair","type":"Int","scope":[],
      "holes":[{"id":"a","type":"Int","scope":[]},{"id":"b","type":"Int","scope":[]}],
      "body":{"signature":"add(Int,Int)->Int","args":[{"hole":"a"},{"hole":"b"}]}}],
    "nonterminals":[
      {"id":"Main","type":"Int","scope":[],"variation":false,"alternatives":[
        {"id":"pair","weight":1,"expression":{"template":"Pair","holes":{
          "a":{"ref":"Left"},"b":{"ref":"Right"}}}}]},
      {"id":"Left","type":"Int","scope":[],"alternatives":[
        {"id":"left","weight":1,"crossover_group":"integer",
         "expression":{"constant":{"type":"Int","range":["0","20"]}}}]},
      {"id":"Right","type":"Int","scope":[],"alternatives":[
        {"id":"right","weight":1,"crossover_group":"integer",
         "expression":{"constant":{"type":"Int","range":["0","20"]}}}]}
    ]})");
  const auto a = split_parent(*grammar, 1, 2);
  const auto b = split_parent(*grammar, 10, 20);
  VariationContext context(grammar);
  const auto analysis = context.analyze(a);
  check(analysis->sites.size() == 2 &&
      compatible_sites(analysis->sites[0], analysis->sites[1]),
      "explicit group did not bridge distinct nonterminals and template holes");
  bool crossed_slots = false;
  for (std::uint64_t seed = 0; seed < 8; ++seed) {
    const auto children = crossover(a, b, seed, context);
    const auto values = integer_constants(children.first);
    crossed_slots |= values[0] == 20 || values[1] == 10;
    certify_and_execute(*grammar, children.first, context.request());
    certify_and_execute(*grammar, children.second, context.request());
  }
  check(crossed_slots, "explicit group never exchanged different template holes");
  check(context.counters().acceptance_rejections == 0,
      "compatible group children failed destination admission");
  auto definition = parse_definition(grammar->canonical_definition());
  auto& rules = definition.document.object_v.at("nonterminals").array_v;
  auto& right = *std::find_if(rules.begin(), rules.end(), [](const auto& rule) {
    return rule.object_v.at("id").string_v == "Right";
  });
  auto& group = right.object_v.at("alternatives").array_v[0].object_v.at("crossover_group");
  group.string_v = "other";
  const auto separated = compile_shared(canonical_json(definition.document));
  const auto separated_sites = analyze_variation(*separated, split_parent(*separated, 1, 2));
  check(!compatible_sites(separated_sites.sites[0], separated_sites.sites[1]),
      "different authored groups unexpectedly matched");
  group.string_v.clear();
  rejects([&] { (void)compile_shared(canonical_json(definition.document)); },
      "empty crossover group accepted");
}

void test_closed_crossover_scope() {
  const auto grammar = compile_shared(kClosedCrossoverGrammar);
  const auto parent = generate_derivation(*grammar, 4).genome;
  const auto analysis = analyze_variation(*grammar, parent);
  check(analysis.sites.size() == 2 && analysis.sites[0].crossover_closed &&
      analysis.sites[1].crossover_closed && compatible_sites(analysis.sites[0], analysis.sites[1]),
      "closed payloads with different declared scopes did not match");
  check(analysis.sites[0].occurrence_binder_ids.front().size() !=
      analysis.sites[1].occurrence_binder_ids.front().size(), "fixture did not cross lexical arities");
  const auto& outside = analysis.sites[0];
  const auto& inside = analysis.sites[1];
  ProgramGenome exchanged;
  exchanged.ast = variation_detail::splice(parent.ast, outside, parent.ast,
      inside.occurrences.front(), inside.occurrence_binder_ids.front(), true);
  VariationContext context(grammar);
  exchanged = variation_detail::accept(std::move(exchanged.ast), parent, context);
  certify_and_execute(*grammar, exchanged, entry_request(*grammar));
  check(integer_constants(exchanged)[0] == 2, "closed cross-scope replacement did not occur");
  auto captured = parent;
  auto& node = captured.ast.nodes[inside.occurrences.front().begin];
  node.kind = NodeKind::REGION_VAR;
  node.i0 = inside.occurrence_binder_ids.front().front();
  node.i1 = 0;
  captured.meta = build_genome_meta(captured.ast);
  const auto open_analysis = analyze_variation(*grammar, captured);
  check(!open_analysis.sites[1].crossover_closed &&
      !compatible_sites(open_analysis.sites[0], open_analysis.sites[1]),
      "capturing subtree incorrectly erased its external scope");
}

void test_stale_provenance_compaction_and_semantic_unchanged_counting() {
  const auto grammar = repeated_hole_grammar("[\"7\"]");
  auto parent_a = repeated_parent(*grammar, 7);
  auto parent_b = parent_a;

  parent_a.ast.names.push_back("unused-a");
  parent_a.ast.consts.push_back(Value::from_int(111));
  auto stale = std::make_shared<DerivationMetadata>();
  stale->grammar_hash = "stale-imported-provenance";
  stale->seed_replayable = true;
  parent_a.derivation = stale;

  parent_b.ast.consts.push_back(Value::from_int(7));
  bool first_constant = true;
  for (auto& node : parent_b.ast.nodes) {
    if (node.kind != NodeKind::CONST) continue;
    node.i0 = first_constant ? 0 : 1;
    first_constant = false;
  }
  parent_b.ast.names.push_back("unused-b");
  parent_b.ast.consts.push_back(Value::from_int(222));

  VariationContext context(grammar);
  std::pair<ProgramGenome, ProgramGenome> children;
  for (std::uint64_t seed = 0; seed < 64; ++seed) {
    children = crossover(parent_a, parent_b, 77 + seed, context);
    certify_and_execute(*grammar, children.first, context.request());
    certify_and_execute(*grammar, children.second, context.request());
  }
  for (const auto* child : {&children.first, &children.second}) {
    check(child->ast.names.empty() && child->ast.consts.size() <= 2,
        "compiled crossover child retained unused table entries");
    check(integer_constants(*child) == std::vector<std::int64_t>({7, 7}),
        "table compaction changed materialized singleton constants");
    certify_and_execute(*grammar, *child, context.request());
  }
  parent_a.ast.consts[0] = Value::from_int(3);
  parent_b.ast.consts[1] = Value::from_int(4);
  check(integer_constants(children.first) == std::vector<std::int64_t>({7, 7}) &&
        integer_constants(children.second) == std::vector<std::int64_t>({7, 7}),
      "compiled crossover child tables alias their imported parents");

  const auto& counters = context.counters();
  check(counters.crossover_attempts == 64 && counters.unchanged_children == 128 &&
        counters.changed_children == 0 && counters.fallback_children == 0 &&
        counters.acceptance_rejections == 0,
      "materialized-equal crossover was not counted as unchanged");
}

void test_malformed_import_is_an_error_even_with_stale_metadata() {
  const auto grammar = repeated_hole_grammar();
  const auto valid = repeated_parent(*grammar, 1);
  auto malformed = valid;
  malformed.ast.nodes[4].i0 = 999;
  malformed.derivation = valid.derivation;
  VariationContext context(grammar);

  rejects([&] { (void)crossover(malformed, valid, 9, context); },
      "malformed imported parent became a fallback candidate");
  const auto& counters = context.counters();
  check(counters.crossover_attempts == 1 && counters.fallback_children == 0 &&
        counters.unchanged_children == 0 && counters.changed_children == 0,
      "malformed-parent error was counted as a produced child or fallback");
}

void test_explicit_generation_request_context() {
  const auto grammar = split_nonterminal_grammar();
  GenerationRequest request{nonterminal(*grammar, "Left"), RType::Int, {}, {5, 4}};
  const auto parent_a = generate_derivation(*grammar, 3, request).genome;
  const auto parent_b = generate_derivation(*grammar, 19, request).genome;
  VariationContext context(grammar, request);
  const auto children = crossover(parent_a, parent_b, 4, context);
  certify_and_execute(*grammar, children.first, request);
  certify_and_execute(*grammar, children.second, request);
  check(context.counters().crossover_attempts == 1 &&
        context.counters().unchanged_children + context.counters().changed_children == 2,
      "explicit-request crossover did not classify both children");
}

std::shared_ptr<const CompiledGrammar> asymmetric_budget_grammar() {
  return compile_shared(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","type":"Int"},
    "search_limits":{"max_nodes":11,"max_depth":7},
    "execution_limits":{"fuel":100},
    "nonterminals":[
      {"id":"Value","type":"Int","scope":[],"alternatives":[
        {"id":"leaf","weight":1,"expression":{"constant":{"type":"Int","values":["3"]}}},
        {"id":"pair","weight":1,"expression":{"signature":"add(Int,Int)->Int","args":[
          {"constant":{"type":"Int","values":["3"]}},
          {"constant":{"type":"Int","values":["4"]}}]}}]},
      {"id":"Main","type":"Int","scope":[],"alternatives":[
        {"id":"shallow","weight":1,"expression":{"signature":"add(Int,Int)->Int","args":[
          {"ref":"Value"},{"constant":{"type":"Int","values":["1"]}}]}},
        {"id":"deep","weight":1,"expression":{"signature":"add(Int,Int)->Int","args":[
          {"signature":"neg(Int)->Int","args":[
            {"signature":"neg(Int)->Int","args":[{"ref":"Value"}]}]},
          {"signature":"add(Int,Int)->Int","args":[
            {"constant":{"type":"Int","values":["1"]}},
            {"constant":{"type":"Int","values":["2"]}}]}]}}
      ]}
    ]
  })");
}

const VariationSite& site_for(const VariationAnalysis& analysis, std::uint32_t nt) {
  const auto found = std::find_if(analysis.sites.begin(), analysis.sites.end(),
      [&](const auto& site) { return site.nonterminal == nt; });
  if (found == analysis.sites.end())
    throw std::runtime_error("missing budget fixture variation site");
  return *found;
}

void test_nested_oversized_donor_is_rejected_but_entry_swap_remains_legal() {
  const auto grammar = asymmetric_budget_grammar();
  const auto value_nt = nonterminal(*grammar, "Value");
  ProgramGenome tight_parent;
  ProgramGenome deep_donor_parent;
  bool found_tight = false;
  bool found_deep_donor = false;
  for (std::uint64_t seed = 0; seed < 4096 && (!found_tight || !found_deep_donor); ++seed) {
    auto candidate = generate_derivation(*grammar, seed).genome;
    const auto analysis = analyze_variation(*grammar, candidate);
    const auto& site = site_for(analysis, value_nt);
    if (!found_tight && site.replacement_budget.max_depth == 1 &&
        site.materialized_depth == 1) {
      tight_parent = std::move(candidate);
      found_tight = true;
    } else if (!found_deep_donor && site.materialized_depth > 1) {
      deep_donor_parent = std::move(candidate);
      found_deep_donor = true;
    }
  }
  check(found_tight && found_deep_donor,
      "could not generate both sides of the asymmetric budget fixture");

  const auto tight_analysis = analyze_variation(*grammar, tight_parent);
  const auto donor_analysis = analyze_variation(*grammar, deep_donor_parent);
  const auto& tight_site = site_for(tight_analysis, value_nt);
  const auto& donor_site = site_for(donor_analysis, value_nt);
  check(compatible_sites(tight_site, donor_site) &&
        !donor_fits(tight_site, donor_site.materialized_nodes,
            donor_site.materialized_depth, donor_site.template_nesting),
      "budget fixture did not create a compatible oversized nested donor");

  VariationContext context(grammar);
  for (std::uint64_t seed = 0; seed < 64; ++seed) {
    const auto children = crossover(tight_parent, deep_donor_parent, 4000 + seed, context);
    certify_and_execute(*grammar, children.first, context.request());
    certify_and_execute(*grammar, children.second, context.request());
  }
  const auto& counters = context.counters();
  check(counters.crossover_attempts == 64 && counters.budget_rejections > 0,
      "oversized compatible nested donors were not counted as budget rejections");
  check(counters.unchanged_children + counters.changed_children == 128 &&
        counters.fallback_children <= counters.unchanged_children &&
        counters.acceptance_rejections == 0,
      "budget-filtered crossover did not conserve child accounting");
}

void test_mixed_exact_root_variation() {
  const auto grammar = compile_shared(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Integer","type":"Int"},
    "search_limits":{"max_nodes":9,"max_depth":5},
    "execution_limits":{"fuel":100},
    "nonterminals":[
      {"id":"Predicate","type":"Bool","scope":[],"alternatives":[
        {"id":"value","weight":1,"expression":{"constant":{"type":"Bool","values":[true,false]}}}]},
      {"id":"Integer","type":"Int","scope":[],"alternatives":[
        {"id":"choice","weight":1,"expression":{"signature":"if(Bool,Int,Int)->Int","args":[
          {"ref":"Predicate"},{"constant":{"type":"Int","values":["11"]}},
          {"constant":{"type":"Int","values":["22"]}}]}}]},
      {"id":"Real","type":"Float","scope":[],"alternatives":[
        {"id":"choice","weight":1,"expression":{"signature":"if(Bool,Float,Float)->Float","args":[
          {"ref":"Predicate"},{"constant":{"type":"Float","values":[1.25]}},
          {"constant":{"type":"Float","values":[2.5]}}]}}]},
      {"id":"Excluded","type":"Float","scope":[],"alternatives":[
        {"id":"value","weight":1,"expression":{"constant":{"type":"Float","values":[9.0]}}}]}
    ]})");
  auto integer_request = entry_request(*grammar);
  auto real_request = integer_request;
  real_request.nonterminal = nonterminal(*grammar, "Real");
  real_request.type = RType::Float;
  const std::vector<GenerationRequest> requests{integer_request, real_request};
  VariationContext context(grammar, requests);
  auto integer = generate_derivation(*grammar, 1, integer_request).genome;
  auto real = generate_derivation(*grammar, 2, real_request).genome;
  for (std::uint64_t seed = 0; seed < 32; ++seed) {
    auto children = crossover(integer, real, seed, context);
    certify_and_execute(*grammar, children.first, integer_request);
    certify_and_execute(*grammar, children.second, real_request);
    integer = mutate(children.first, seed + 100, context, 0.5);
    real = mutate(children.second, seed + 200, context, 0.5);
    certify_and_execute(*grammar, integer, integer_request);
    certify_and_execute(*grammar, real, real_request);
  }
  check(context.counters().changed_children > 0 && context.counters().fallback_children == 0,
        "mixed roots did not exchange shared nonterminal subtrees");
  repro::GpuReproConfig config;
  config.population_size = 2;
  config.pair_count = 1;
  config.max_nodes = static_cast<int>(integer_request.budget.max_nodes);
  config.max_expr_depth = static_cast<int>(integer_request.budget.max_depth);
  const auto prepared = repro::preprocess_population({integer, real}, config, context);
  check(prepared.candidates.size() == 2 && !prepared.candidates[0].empty() &&
        !prepared.candidates[1].empty() && !prepared.donor_pool.empty(),
        "shared GPU preparation dropped a mixed root or its donors");
  bool shared_contract = false;
  for (const auto& left : prepared.candidates[0])
    for (const auto& right : prepared.candidates[1])
      shared_contract |= left.compatibility_id == right.compatibility_id;
  check(shared_contract, "mixed GPU preparation partitioned shared subtree contracts");
  const auto first = context.analyze(integer);
  const auto hits = context.cache().counters().hits;
  auto forged = std::make_shared<DerivationMetadata>(*integer.derivation);
  forged->request = real_request;
  integer.derivation = forged;
  check(context.analyze(integer) == first && context.cache().counters().hits == hits + 1,
        "mixed analysis trusted attached root provenance or missed its cache");

  // A valid member of another admitted root still cannot replace this parent's
  // root: acceptance must preserve the destination contract, not just the union.
  const auto certified = variation_detail::certify(integer, context);
  const auto rejected = variation_detail::accept(real.ast, certified, context);
  check(rejected.meta.program_key == certified.meta.program_key &&
        context.counters().acceptance_rejections == 1,
        "mixed acceptance allowed an unrelated admitted root to replace its parent");

  auto excluded_request = real_request;
  excluded_request.nonterminal = nonterminal(*grammar, "Excluded");
  const auto excluded = generate_derivation(*grammar, 3, excluded_request).genome;
  rejects([&] { (void)context.analyze(excluded); },
          "exact root type bypassed the admitted nonterminal's domain");
  VariationAnalysisCache cache(grammar, 2);
  const auto first_real = cache.analyze_member(real, requests);
  check(cache.analyze_member(real, requests) == first_real,
        "mixed analysis was not retained in the bounded cache");
  rejects([&] { (void)cache.analyze_member(real, {integer_request, excluded_request}); },
          "cache key omitted a later root contract");
  rejects([&] { (void)cache.analyze_member(real, {integer_request}); },
          "single-root cache reused an incompatible mixed-root member");
  rejects([&] { VariationContext invalid(grammar, std::vector<GenerationRequest>{}); },
          "empty mixed root request set was accepted");
  rejects([&] { VariationContext invalid(grammar, std::vector<GenerationRequest>{real_request, excluded_request}); },
          "ambiguous same-type root contracts were accepted");
  auto smaller = real_request;
  smaller.budget.max_nodes = 8;
  rejects([&] { VariationContext invalid(grammar, std::vector<GenerationRequest>{integer_request, smaller}); },
          "mixed roots with unequal resource limits were accepted");
  auto open = real_request;
  open.visible_environment = {{"capture", RType::Int}};
  rejects([&] { VariationContext invalid(grammar, std::vector<GenerationRequest>{integer_request, open}); },
          "open mixed population root was accepted");
}

}  // namespace

int main() {
  try {
    test_mixed_exact_root_variation();
    test_repeated_holes_are_replaced_atomically();
    test_same_type_different_nonterminals_never_cross();
    test_explicit_crossover_groups();
    test_closed_crossover_scope();
    test_stale_provenance_compaction_and_semantic_unchanged_counting();
    test_malformed_import_is_an_error_even_with_stale_metadata();
    test_explicit_generation_request_context();
    test_nested_oversized_donor_is_rejected_but_entry_swap_remains_legal();
    std::cout << "compiled grammar crossover: atomic contracts, validation, compaction, execution, and counters passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
