#include "../../src/evolution/repro/pack_internal.hpp"
#include "../../src/runtime/payload/staging.hpp"
#include "../../src/evolution/repro/prep_internal.hpp"
#include "gagp/runtime/payload/payload.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/repro/gpu.hpp"
#include "gagp/evolution/repro/pack.hpp"
#include "gagp/evolution/repro/prep.hpp"
#include "../../src/evolution/repro/constant_prep.hpp"

namespace {

using gagp::Value;
using gagp::ValueTag;
using gagp::evo::AstNode;
using gagp::evo::NodeKind;
using gagp::evo::ProgramGenome;
using gagp::evo::RType;
using gagp::evo::ast_cache_key;
using gagp::evo::grammar::CompiledGrammar;
using gagp::evo::grammar::VariationContext;
using gagp::evo::grammar::compile_grammar;
using gagp::evo::grammar::generate_derivation;
using gagp::evo::grammar::parse_definition;
using gagp::evo::repro::CandidateRange;
using gagp::evo::repro::GpuReproConfig;
using gagp::evo::repro::PackedHostData;
using gagp::evo::repro::PreprocessOutput;

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void rejects_invalid(const std::function<void()>& action, const char* message) {
  try {
    action();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error(message);
}

std::shared_ptr<const CompiledGrammar> compile_shared(const std::string& text) {
  return std::make_shared<const CompiledGrammar>(
      compile_grammar(parse_definition(text)));
}

void test_gpu_run_resources_reject_oversized_search_space() {
  const auto grammar = compile_shared(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","type":"Int"},
    "search_limits":{"max_nodes":1025,"max_depth":4},
    "execution_limits":{"fuel":100},
    "nonterminals":[{"id":"Main","type":"Int","scope":[],"alternatives":[
      {"id":"one","weight":1,"expression":{"constant":{"type":"Int","values":["1"]}}}
    ]}]
  })");
  gagp::evo::EvolutionConfig config;
  config.compiled_grammar = grammar;
  config.generation_request = gagp::evo::grammar::entry_request(*grammar);
  config.reproduction_backend = gagp::evo::repro::ReproductionBackend::Gpu;
  config.fuel = 100;
  try {
    (void)gagp::evo::repro::make_gpu_repro_run_resources(config);
  } catch (const std::invalid_argument& error) {
    check(std::string(error.what()).find(
              "gpu reproduction mode capacity exceeded: generation request max_nodes=1025") !=
              std::string::npos,
          "GPU capacity diagnostic did not name the failed mode and limit");
    return;
  }
  throw std::runtime_error(
      "GPU run resources accepted a search space larger than kernel capacity");
}

GpuReproConfig compiled_config(const CompiledGrammar& grammar,
                               std::size_t population_size) {
  GpuReproConfig config;
  config.donor_pool_size_per_site = 2;
  config.population_size = static_cast<int>(population_size);
  config.pair_count = static_cast<int>((population_size + 1) / 2);
  config.candidates_per_program = 16;
  config.max_nodes = static_cast<int>(grammar.search_limits().max_nodes);
  config.max_donor_nodes = 1;
  config.max_names = 1;
  config.max_consts = 1;
  config.max_expr_depth = static_cast<int>(grammar.search_limits().max_depth);
  config.seed = 0x123456789abcdef0ULL;
  return config;
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
        "expression":{"constant":{"type":"Int","values":["1"]}}}]},
      {"id":"Right","type":"Int","scope":[],"alternatives":[{"id":"right","weight":1,
        "expression":{"constant":{"type":"Int","values":["2"]}}}]}
    ]
  })");
}

std::shared_ptr<const CompiledGrammar> repeated_hole_grammar() {
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
        "expression":{"constant":{"type":"Int","values":["7"]}}}]},
      {"id":"Main","type":"Int","scope":[],"alternatives":[{"id":"cancel","weight":1,
        "expression":{"template":"Cancel","holes":{"value":{"ref":"Value"}}}}]}
    ]
  })");
}

std::shared_ptr<const CompiledGrammar> scoped_repeated_hole_grammar() {
  return compile_shared(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","type":"Int"},
    "search_limits":{"max_nodes":20,"max_depth":10},
    "execution_limits":{"fuel":100},
    "templates":[{"id":"Sibling","type":"Int","scope":[],
      "holes":[{"id":"value","type":"Int","scope":[
        {"name":"x","type":"Int"}]}],
      "body":{"signature":"add(Int,Int)->Int","args":[
        {"signature":"let(Int,Int)->Int","args":[
          {"constant":{"type":"Int","values":["1"]}},{"hole":"value"}],
         "bind":{"1":["x"]}},
        {"signature":"let(Int,Int)->Int","args":[
          {"constant":{"type":"Int","values":["2"]}},{"hole":"value"}],
         "bind":{"1":["x"]}}]}}],
    "nonterminals":[
      {"id":"Main","type":"Int","scope":[],"alternatives":[
        {"id":"main","weight":1,"expression":{"template":"Sibling","holes":{
          "value":{"ref":"Scoped"}}}}]},
      {"id":"Scoped","type":"Int","scope":[{"name":"x","type":"Int"}],
       "alternatives":[{"id":"bound","weight":1,
         "expression":{"bound":"x"}}]}
    ]
  })");
}

std::shared_ptr<const CompiledGrammar> named_constant_donor_grammar() {
  return compile_shared(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","category":"Program","type":"Int"},
    "inputs":[{"name":"input","type":"Int"}],
    "locals":[{"name":"x","type":"Int"},{"name":"y","type":"Int"}],
    "search_limits":{"max_nodes":20,"max_depth":10},
    "execution_limits":{"fuel":100},
    "nonterminals":[
      {"id":"Initial","type":"Int","scope":[],"alternatives":[{"id":"one","weight":1,
        "expression":{"constant":{"type":"Int","values":["1"]}}}]},
      {"id":"LocalExpr","type":"Int","scope":[],"alternatives":[{"id":"sum","weight":1,
        "expression":{"signature":"add(Int,Int)->Int","args":[
          {"signature":"add(Int,Int)->Int","args":[{"local":"x"},{"local":"y"}]},
          {"signature":"add(Int,Int)->Int","args":[
            {"constant":{"type":"Int","values":["7"]}},
            {"constant":{"type":"Int","values":["8"]}}]}]}}]},
      {"id":"Main","category":"Program","type":"Int","scope":[],"alternatives":[
        {"id":"body","weight":1,"expression":{"control":"program(Block)->Program","type":"Int","args":[
          {"control":"block_cons(Statement,Block)->Block","type":"Int","args":[
            {"control":"assign(Int)->Statement","type":"Int","name":"x","args":[{"ref":"Initial"}]},
            {"control":"block_cons(Statement,Block)->Block","type":"Int","args":[
              {"control":"assign(Int)->Statement","type":"Int","name":"y","args":[{"ref":"Initial"}]},
              {"control":"block_cons(Statement,Block)->Block","type":"Int","args":[
                {"control":"return(Int)->Statement","type":"Int","args":[{"ref":"LocalExpr"}]},
                {"control":"block_nil()->Block","type":"Int","args":[]}]}]}]}]}}]}
    ]
  })");
}

bool same_candidate(const CandidateRange& left, const CandidateRange& right) {
  return left.start == right.start && left.stop == right.stop &&
         left.tag == right.tag && left.aux == right.aux &&
         left.scope_signature == right.scope_signature &&
         left.binder_signature == right.binder_signature &&
         left.visible_env_signature == right.visible_env_signature &&
         left.compatibility_id == right.compatibility_id &&
         left.occurrence_offset == right.occurrence_offset &&
         left.occurrence_count == right.occurrence_count &&
         left.replacement_max_nodes == right.replacement_max_nodes &&
         left.replacement_max_depth == right.replacement_max_depth &&
         left.remaining_template_nesting == right.remaining_template_nesting &&
         left.materialized_nodes == right.materialized_nodes &&
         left.materialized_depth == right.materialized_depth &&
         left.template_nesting == right.template_nesting &&
         left.donor_offset == right.donor_offset &&
         left.donor_count == right.donor_count;
}

void test_disabled_mutation_skips_speculative_analysis() {
  const auto grammar = split_nonterminal_grammar();
  std::vector<ProgramGenome> population(128,
      gagp::evo::repro::compact_genome_tables(generate_derivation(*grammar, 1).genome));
  auto config = compiled_config(*grammar, population.size());
  config.compiled_pass = gagp::evo::repro::CompiledVariationPass::Mutation;
  config.mutation_ratio = 0.0;
  VariationContext context(grammar), reference_context(grammar);
  const auto result = gagp::evo::repro::preprocess_population(population, config, context);
  const auto reference = gagp::evo::repro::preprocess_population(
      population, config, reference_context, nullptr, false);
  check(result.donor_pool.empty(), "disabled mutation generated donors");
  check(context.cache().counters().hits + context.cache().counters().misses == population.size(),
        "disabled mutation redundantly analyzed speculative donor parents");
  check(result.compatibility_keys == reference.compatibility_keys &&
            result.population_identities == reference.population_identities,
        "disabled mutation changed source or compatibility identities");
  for (std::size_t i = 0; i < population.size(); ++i) {
    check(result.candidates[i].size() == reference.candidates[i].size(),
          "disabled mutation changed candidate count");
    for (std::size_t j = 0; j < result.candidates[i].size(); ++j)
      check(same_candidate(result.candidates[i][j], reference.candidates[i][j]),
            "disabled mutation changed candidate selection or order");
  }
}

void test_prefetched_parent_analysis_matches_sequential(bool capacity_split) {
  auto grammar = split_nonterminal_grammar();
  if (capacity_split) {
    auto document = gagp::cli_detail::JsonParser(grammar->canonical_definition()).parse();
    document.object_v.at("search_limits").object_v.at("max_nodes").number_v = 1024;
    grammar = compile_shared(gagp::evo::grammar::canonical_json(document));
  }
  // 257 jobs exceed the former 128-job window. With 1024-node donor budgets
  // and eight seeds, the storage bound instead forces 128 + 128 + 1 jobs.
  std::vector<ProgramGenome> population(257,
      gagp::evo::repro::compact_genome_tables(generate_derivation(*grammar, 1).genome));
  auto config = compiled_config(*grammar, population.size());
  config.compiled_pass = gagp::evo::repro::CompiledVariationPass::Mutation;
  config.donor_pool_size_per_site = 8;
  config.mutation_ratio = 1.0;
  config.mutation_subtree_ratio = 1.0;
  VariationContext context(grammar), reference_context(grammar);
  const auto result = gagp::evo::repro::preprocess_population(population, config, context);
  gagp::payload::StagedPayloads transaction;
  // An enclosing transaction forces the original per-seed donor path and must
  // not attempt a nested snapshot or commit for the parent-analysis handoff.
  const auto reference = [&] {
    gagp::payload::StagedPayloads::Scope scope(transaction);
    return gagp::evo::repro::preprocess_population(population, config, reference_context);
  }();
  check(result.population_identities == reference.population_identities &&
            result.compatibility_keys == reference.compatibility_keys &&
            result.donor_identities == reference.donor_identities,
        "prefetched parent analysis changed parent or donor identity/order");
  check(!result.donor_pool.empty(), "prefetch oracle did not exercise donor generation");
  for (std::size_t i = 0; i < population.size(); ++i) {
    check(result.candidates[i].size() == reference.candidates[i].size(),
          "prefetch oracle candidate count differs");
    for (std::size_t j = 0; j < result.candidates[i].size(); ++j)
      check(same_candidate(result.candidates[i][j], reference.candidates[i][j]),
            "prefetched parent analysis changed candidate or donor slices");
  }
}

void test_warmed_population_handoff() {
  const auto grammar = split_nonterminal_grammar();
  const auto token = Value::from_string_hash_len(17823649, 5);
  for (const bool changed_payload : {false, true}) {
    gagp::payload::register_string(token, "alive");
    auto member = gagp::evo::repro::compact_genome_tables(
        generate_derivation(*grammar, 1).genome);
    // Unused constants still belong to the exact runtime identity and its
    // payload read dependencies; changing one must invalidate the handoff.
    member.ast.consts.push_back(token);
    std::vector<ProgramGenome> population(128, member);
    auto config = compiled_config(*grammar, population.size());
    config.compiled_pass = gagp::evo::repro::CompiledVariationPass::Mutation;
    config.mutation_ratio = 1.0;
    config.mutation_subtree_ratio = 1.0;
    VariationContext context(grammar), reference_context(grammar);
    std::vector<gagp::evo::grammar::WarmPopulationMember> handoff;
    context.cache().warm_population(population, context.requests(), 8, &handoff);
    reference_context.cache().warm_population(population, reference_context.requests());
    check(handoff.size() == population.size(), "warm handoff lost population rows");
    if (changed_payload) gagp::payload::register_string(token, "other");
    for (const auto& row : handoff) {
      if (row.reads)
        check(row.reads->read_snapshot_unchanged() != changed_payload,
              "warm handoff did not track payload contents");
    }
    const auto result = gagp::evo::repro::preprocess_warmed_population(
        population, config, context, nullptr, true, handoff);
    const auto reference = gagp::evo::repro::preprocess_population(
        population, config, reference_context);
    check(result.population_identities == reference.population_identities &&
              result.compatibility_keys == reference.compatibility_keys &&
              result.donor_identities == reference.donor_identities,
          "warm handoff changed identities or registry order");
    check(!result.donor_pool.empty(), "warm handoff oracle generated no donors");
    for (std::size_t i = 0; i < population.size(); ++i) {
      check(result.candidates[i].size() == reference.candidates[i].size(),
            "warm handoff changed candidate count");
      for (std::size_t j = 0; j < result.candidates[i].size(); ++j)
        check(same_candidate(result.candidates[i][j], reference.candidates[i][j]),
              "warm handoff changed candidates or donor slices");
      if (changed_payload && handoff[i].analysis)
        check(result.population_identities[i] != handoff[i].runtime_identity,
              "changed payload reused stale runtime identity");
    }
    const auto packed = gagp::evo::repro::pack_warmed_population(
        population, result, config, handoff);
    const auto ordinary = gagp::evo::repro::pack_population(population, result, config);
    check(packed.program_name_ids == ordinary.program_name_ids &&
              packed.compatibility_keys == ordinary.compatibility_keys &&
              packed.program_nodes.size() == ordinary.program_nodes.size(),
          "warm packing changed names, contracts or node counts");
    for (std::size_t i = 0; i < packed.program_nodes.size(); ++i)
      check(packed.program_nodes[i].kind == ordinary.program_nodes[i].kind &&
                packed.program_nodes[i].i0 == ordinary.program_nodes[i].i0 &&
                packed.program_nodes[i].i1 == ordinary.program_nodes[i].i1,
            "warm packing changed physical nodes");
    auto forged = result;
    forged.population_identities.front() = "forged";
    rejects_invalid([&] { (void)gagp::evo::repro::pack_warmed_population(
        population, forged, config, handoff); }, "warm packing accepted forged identity");
    if (changed_payload && handoff.front().analysis) {
      forged.population_identities.front() = handoff.front().runtime_identity;
      rejects_invalid([&] { (void)gagp::evo::repro::pack_warmed_population(
          population, forged, config, handoff); }, "warm packing accepted expired payload identity");
    }
    // Retention is bounded even when the population exceeds cache capacity.
    population.push_back(member);
    context.cache().warm_population(population, context.requests(), 8, &handoff);
    check(handoff.empty(), "warm handoff exceeded cache retention bound");
  }
}

void test_exact_contracts_and_shared_occurrences() {
  {
    const auto grammar = split_nonterminal_grammar();
    std::vector<ProgramGenome> population = {
        gagp::evo::repro::compact_genome_tables(generate_derivation(*grammar, 1).genome),
        gagp::evo::repro::compact_genome_tables(generate_derivation(*grammar, 2).genome),
    };
    VariationContext context(grammar);
    const auto prep = gagp::evo::repro::preprocess_population(
        population, compiled_config(*grammar, population.size()), context);

    const auto& candidates = prep.candidates.front();
    const auto left = std::find_if(candidates.begin(), candidates.end(),
        [](const CandidateRange& value) { return value.start == 4; });
    const auto right = std::find_if(candidates.begin(), candidates.end(),
        [](const CandidateRange& value) { return value.start == 5; });
    check(left != candidates.end() && right != candidates.end(),
        "compacted split parent lost its leaf candidates");
    check(left->aux == static_cast<int>(RType::Int) && right->aux == left->aux,
        "split leaf fixture stopped producing same-typed sites");
    check(left->compatibility_id != right->compatibility_id,
        "same-typed distinct nonterminals shared a compatibility id");
    check(prep.compatibility_keys.at(left->compatibility_id) !=
              prep.compatibility_keys.at(right->compatibility_id),
        "same-typed distinct nonterminals shared a compatibility key");
  }

  {
    const auto grammar = repeated_hole_grammar();
    std::vector<ProgramGenome> population = {
        gagp::evo::repro::compact_genome_tables(generate_derivation(*grammar, 7).genome),
        gagp::evo::repro::compact_genome_tables(generate_derivation(*grammar, 8).genome),
    };
    VariationContext context(grammar);
    const auto prep = gagp::evo::repro::preprocess_population(
        population, compiled_config(*grammar, population.size()), context);
    const auto repeated = std::find_if(prep.candidates.front().begin(),
        prep.candidates.front().end(),
        [](const CandidateRange& value) { return value.occurrence_count == 2; });
    check(repeated != prep.candidates.front().end(),
        "shared template hole was not represented by one two-occurrence candidate");
    check(repeated->occurrence_offset >= 0 &&
              static_cast<std::size_t>(repeated->occurrence_offset + 2) <=
                  prep.occurrences.size(),
        "shared-hole occurrence range escaped the flattened table");
    const auto& first = prep.occurrences[static_cast<std::size_t>(repeated->occurrence_offset)];
    const auto& second = prep.occurrences[static_cast<std::size_t>(repeated->occurrence_offset + 1)];
    check(first.start != second.start && first.stop > first.start && second.stop > second.start,
        "shared-hole occurrence table did not retain both concrete spans");
  }

  {
    const auto grammar = scoped_repeated_hole_grammar();
    std::vector<ProgramGenome> population = {
        gagp::evo::repro::compact_genome_tables(generate_derivation(*grammar, 3).genome),
        gagp::evo::repro::compact_genome_tables(generate_derivation(*grammar, 4).genome),
    };
    VariationContext context(grammar);
    const auto prep = gagp::evo::repro::preprocess_population(
        population, compiled_config(*grammar, population.size()), context);
    const auto repeated = std::find_if(prep.candidates.front().begin(),
        prep.candidates.front().end(),
        [](const CandidateRange& value) { return value.occurrence_count == 2; });
    check(repeated != prep.candidates.front().end(),
        "scoped shared hole was not represented by one candidate");
    const auto& first = prep.occurrences.at(
        static_cast<std::size_t>(repeated->occurrence_offset));
    const auto& second = prep.occurrences.at(
        static_cast<std::size_t>(repeated->occurrence_offset + 1));
    check(first.binder_count == 1 && second.binder_count == 1 &&
              first.binder_offset >= 0 && second.binder_offset >= 0,
        "scoped occurrence binder slices lost their formal arity");
    const int first_binder = prep.occurrence_binder_ids.at(
        static_cast<std::size_t>(first.binder_offset));
    const int second_binder = prep.occurrence_binder_ids.at(
        static_cast<std::size_t>(second.binder_offset));
    check(first_binder >= 0 && second_binder >= 0 &&
              first_binder != second_binder,
        "scoped occurrences lost their distinct physical binder IDs");
    check(repeated->donor_count > 0,
        "scoped candidate did not retain a contextual donor");
    auto packed = gagp::evo::repro::pack_population(
        population, prep, compiled_config(*grammar, population.size()));
    check(packed.compiled_sources &&
              packed.compiled_sources->parents.size() == population.size() &&
              packed.compiled_sources->donors.size() == prep.donor_pool.size(),
        "compiled packing lost its owned generic source snapshots");
    for (std::size_t p = 0; p < population.size(); ++p) {
      check(!packed.compiled_sources->parents[p].lexical_regions.empty() &&
                ast_cache_key(packed.compiled_sources->parents[p]) ==
                    ast_cache_key(population[p].ast),
          "compiled packing changed lexical source sidecars or binder IDs");
    }
    for (std::size_t d = 0; d < prep.donor_pool.size(); ++d)
      check(ast_cache_key(packed.compiled_sources->donors[d]) ==
                ast_cache_key(prep.donor_pool[d].ast),
          "compiled packing lost contextual donor metadata");
    const auto shared_copy = packed;
    check(shared_copy.compiled_sources == packed.compiled_sources,
        "copying prepared inputs duplicated immutable source snapshots");
    const auto source_key = ast_cache_key(packed.compiled_sources->parents.front());
    population.front().ast.lexical_regions.clear();
    check(ast_cache_key(packed.compiled_sources->parents.front()) == source_key,
        "source snapshots borrowed mutable population metadata");
    for (int i = 0; i < repeated->donor_count; ++i) {
      const auto& donor = prep.donor_contracts.at(
          static_cast<std::size_t>(repeated->donor_offset + i));
      check(donor.binder_count == first.binder_count && donor.binder_offset >= 0 &&
                prep.donor_binder_ids.at(
                    static_cast<std::size_t>(donor.binder_offset)) == first_binder,
          "contextual donor lost its formal binder IDs");
    }
  }
}

void check_deterministic_prep(const PreprocessOutput& first,
                              const PreprocessOutput& second) {
  check(first.compatibility_keys == second.compatibility_keys &&
            first.population_identities == second.population_identities &&
            first.donor_identities == second.donor_identities &&
            first.occurrence_binder_ids == second.occurrence_binder_ids &&
            first.donor_binder_ids == second.donor_binder_ids &&
            first.occurrences.size() == second.occurrences.size() &&
            first.donor_pool.size() == second.donor_pool.size() &&
            first.donor_contracts.size() == second.donor_contracts.size() &&
            first.candidates.size() == second.candidates.size(),
        "compiled preparation table sizes or keys were nondeterministic");
  for (std::size_t i = 0; i < first.occurrences.size(); ++i) {
    check(first.occurrences[i].start == second.occurrences[i].start &&
              first.occurrences[i].stop == second.occurrences[i].stop &&
              first.occurrences[i].binder_offset == second.occurrences[i].binder_offset &&
              first.occurrences[i].binder_count == second.occurrences[i].binder_count,
        "compiled preparation occurrence offsets were nondeterministic");
  }
  for (std::size_t p = 0; p < first.candidates.size(); ++p) {
    check(first.candidates[p].size() == second.candidates[p].size(),
        "compiled preparation candidate count was nondeterministic");
    for (std::size_t i = 0; i < first.candidates[p].size(); ++i) {
      check(same_candidate(first.candidates[p][i], second.candidates[p][i]),
          "compiled preparation candidate offsets or donor ranges were nondeterministic");
    }
  }
  for (std::size_t i = 0; i < first.donor_pool.size(); ++i) {
    check(first.donor_pool[i].type == second.donor_pool[i].type &&
              ast_cache_key(first.donor_pool[i].ast) ==
                  ast_cache_key(second.donor_pool[i].ast),
        "compiled donor generation was nondeterministic");
    const auto& a = first.donor_contracts[i];
    const auto& b = second.donor_contracts[i];
    check(a.compatibility_id == b.compatibility_id &&
              a.materialized_nodes == b.materialized_nodes &&
              a.materialized_depth == b.materialized_depth &&
              a.template_nesting == b.template_nesting &&
              a.binder_offset == b.binder_offset &&
              a.binder_count == b.binder_count,
        "compiled donor contract table was nondeterministic");
  }
}

ProgramGenome unpack_donor_fragment(const PackedHostData& packed, std::size_t index) {
  ProgramGenome result;
  const int node_count = packed.donor_lens.at(index);
  const int name_count = packed.donor_name_counts.at(index);
  const int const_count = packed.donor_const_counts.at(index);
  const std::size_t node_base = index * static_cast<std::size_t>(packed.config.max_donor_nodes);
  const std::size_t name_base = index * static_cast<std::size_t>(packed.config.max_names);
  const std::size_t const_base = index * static_cast<std::size_t>(packed.config.max_consts);
  for (int i = 0; i < node_count; ++i) {
    const auto& node = packed.donor_nodes.at(node_base + static_cast<std::size_t>(i));
    result.ast.nodes.push_back(AstNode{static_cast<NodeKind>(node.kind), node.i0, node.i1});
  }
  for (int i = 0; i < name_count; ++i) {
    const std::uint64_t id = packed.donor_name_ids.at(name_base + static_cast<std::size_t>(i));
    result.ast.names.push_back(packed.name_lookup.at(id));
  }
  result.ast.consts.insert(result.ast.consts.end(),
      packed.donor_consts.begin() + static_cast<std::ptrdiff_t>(const_base),
      packed.donor_consts.begin() + static_cast<std::ptrdiff_t>(const_base + const_count));
  return result;
}

PreprocessOutput prepare_after_local_owners_expire(std::vector<ProgramGenome>* population,
                                                   GpuReproConfig* config) {
  auto grammar = named_constant_donor_grammar();
  *population = {
      gagp::evo::repro::compact_genome_tables(generate_derivation(*grammar, 11).genome),
      gagp::evo::repro::compact_genome_tables(generate_derivation(*grammar, 12).genome),
  };
  *config = compiled_config(*grammar, population->size());
  VariationContext context(grammar);
  return gagp::evo::repro::preprocess_population(*population, *config, context);
}

void test_determinism_packing_ownership_and_guards() {
  std::vector<ProgramGenome> population;
  GpuReproConfig config;
  const PreprocessOutput prep = prepare_after_local_owners_expire(&population, &config);
  check(prep.compiled_grammar != nullptr,
        "compiled preparation did not retain grammar ownership");
  prep.compiled_grammar->require_executable();

  VariationContext replay_context(prep.compiled_grammar);
  const auto replay = gagp::evo::repro::preprocess_population(
      population, config, replay_context);
  check_deterministic_prep(prep, replay);
  const auto crossover_only = gagp::evo::repro::preprocess_population(
      population, config, replay_context, nullptr, false);
  check(crossover_only.donor_pool.empty() && crossover_only.donor_contracts.empty(),
        "crossover-only preparation built unused mutation donors");
  check(crossover_only.candidates.size() == prep.candidates.size(),
        "omitting donors changed candidate population");
  for (std::size_t p = 0; p < prep.candidates.size(); ++p) {
    check(crossover_only.candidates[p].size() == prep.candidates[p].size(),
          "omitting donors changed candidate count");
    for (std::size_t i = 0; i < prep.candidates[p].size(); ++i) {
      const auto& a = prep.candidates[p][i];
      const auto& b = crossover_only.candidates[p][i];
      check(a.occurrence_offset == b.occurrence_offset &&
            a.occurrence_count == b.occurrence_count &&
            a.compatibility_id == b.compatibility_id && b.donor_count == 0,
            "omitting donors changed site sampling or contracts");
    }
  }
  const auto crossover_packed = gagp::evo::repro::pack_population(population, crossover_only, config);
  check(crossover_packed.config.compiled_donor_count == 0,
        "empty crossover donor pool did not pack");

  auto constant_config = config;
  constant_config.compiled_pass = gagp::evo::repro::CompiledVariationPass::Mutation;
  constant_config.mutation_ratio = 1.0;
  constant_config.mutation_subtree_ratio = 0.0;
  const auto constant_only = gagp::evo::repro::preprocess_population(
      population, constant_config, replay_context);
  for (const int stream : constant_only.parent_constant_streams)
    check(constant_only.constant_mutation->streams.at(stream).group_count > 0,
          "constant-only preparation fixture has no eligible group");
  check(constant_only.donor_pool.empty() && constant_only.donor_contracts.empty(),
        "constant-only mutation prepared unreachable subtree donors");
  check(constant_only.candidates.size() == prep.candidates.size(),
        "constant-only mutation removed parent candidate tables");

  const auto packed = gagp::evo::repro::pack_population(population, prep, config);
  check(packed.compiled_grammar == prep.compiled_grammar &&
            packed.compatibility_keys == prep.compatibility_keys &&
            packed.occurrences.size() == prep.occurrences.size() &&
            packed.occurrence_binder_ids == prep.occurrence_binder_ids &&
            packed.donor_binder_ids == prep.donor_binder_ids &&
            packed.donor_contracts.size() == prep.donor_contracts.size(),
        "packing dropped the compiled schema or grammar owner");
  check(packed.config.compiled_donor_count ==
            static_cast<int>(prep.donor_pool.size()),
        "packed config did not record the actual compiled donor count");
  check(packed.constant_mutation && packed.constant_mutation == prep.constant_mutation &&
            packed.parent_constant_streams == prep.parent_constant_streams &&
            packed.donor_constant_streams == prep.donor_constant_streams &&
            packed.constant_mutation->streams.size() == population.size() + prep.donor_pool.size(),
        "packing dropped shared constant domains or stream ownership");
  const auto check_stream = [&](int id, const gagp::evo::AstProgram& ast) {
    const auto& stream = packed.constant_mutation->streams.at(static_cast<std::size_t>(id));
    check(stream.node_count == static_cast<int>(ast.nodes.size()),
        "constant stream retained a donor envelope or lost parent nodes");
    for (int i = 0; i < stream.node_count; ++i) {
      const int group = packed.constant_mutation->node_group_origins.at(
          static_cast<std::size_t>(stream.node_origin_offset + i));
      if (group < 0) continue;
      check(group >= stream.group_offset && group - stream.group_offset < stream.group_count &&
                ast.nodes[static_cast<std::size_t>(i)].kind == NodeKind::CONST,
          "packed constant origin escaped its stream or targeted a nonconstant");
    }
  };
  for (std::size_t i = 0; i < population.size(); ++i)
    check_stream(packed.parent_constant_streams[i], population[i].ast);
  for (std::size_t i = 0; i < prep.donor_pool.size(); ++i)
    check_stream(packed.donor_constant_streams[i], prep.donor_pool[i].ast);


  int required_max_names = 1;
  int required_max_consts = 1;
  int required_max_donor_nodes = 1;
  for (const auto& parent : population) {
    required_max_names = std::max(required_max_names,
        static_cast<int>(parent.ast.names.size()));
    required_max_consts = std::max(required_max_consts,
        static_cast<int>(parent.ast.consts.size()));
  }
  for (const auto& donor : prep.donor_pool) {
    required_max_names = std::max(required_max_names,
        static_cast<int>(donor.ast.names.size()));
    required_max_consts = std::max(required_max_consts,
        static_cast<int>(donor.ast.consts.size()));
    required_max_donor_nodes = std::max(required_max_donor_nodes,
        static_cast<int>(donor.ast.nodes.size()));
  }
  check(required_max_names > 1 && required_max_consts > 1 &&
            required_max_donor_nodes > 1,
        "fixture did not exercise all compiled packing prescan growth paths");
  check(required_max_names <= gagp::evo::repro::kGpuReproMaxNames &&
            required_max_consts <= gagp::evo::repro::kGpuReproMaxConsts &&
            required_max_donor_nodes <= gagp::evo::repro::kGpuReproKernelMaxNodes,
        "fixture unexpectedly exceeded the supported compiled packing capacities");
  check(packed.config.max_names == std::min(gagp::evo::repro::kGpuReproMaxNames, 2 * required_max_names) &&
            packed.config.max_consts == std::min(gagp::evo::repro::kGpuReproMaxConsts, 2 * required_max_consts) &&
            packed.config.max_donor_nodes == required_max_donor_nodes,
        "compiled packing prescan did not reserve bounded splice table unions");

  for (std::size_t p = 0; p < prep.candidates.size(); ++p) {
    const std::size_t valid_count = prep.candidates[p].size();
    check(valid_count < static_cast<std::size_t>(packed.config.candidates_per_program),
        "fixture did not leave room for compiled candidate padding");
    check(packed.metas[p].candidate_count == static_cast<int>(valid_count),
        "packed program metadata lost its exact valid candidate count");
    const std::size_t base = p *
        static_cast<std::size_t>(packed.config.candidates_per_program);
    for (std::size_t i = 0; i < valid_count; ++i) {
      check(same_candidate(packed.candidates[base + i], prep.candidates[p][i]),
          "compiled packing changed a valid candidate before padding");
    }
    for (std::size_t i = valid_count;
         i < static_cast<std::size_t>(packed.config.candidates_per_program); ++i) {
      const auto& padding = packed.candidates[base + i];
      check(padding.compatibility_id == gagp::evo::repro::kNoCompatibilityId &&
                padding.occurrence_count == 0 && padding.donor_count == 0,
          "compiled candidate padding recycled a valid contract");
    }
  }

  bool checked_named_constant_fragment = false;
  for (std::size_t i = 0; i < prep.donor_pool.size(); ++i) {
    const auto& donor = prep.donor_pool[i].ast;
    const bool has_name = !donor.names.empty();
    const bool has_int_constant = std::any_of(donor.consts.begin(), donor.consts.end(),
        [](const Value& value) { return value.tag == ValueTag::Int; });
    if (!has_name || !has_int_constant) continue;
    check(std::none_of(donor.nodes.begin(), donor.nodes.end(), [](const AstNode& node) {
            return node.kind == NodeKind::PROGRAM || node.kind == NodeKind::RETURN ||
                   node.kind == NodeKind::BLOCK_CONS || node.kind == NodeKind::BLOCK_NIL;
          }), "compiled donor retained its executable envelope");
    const auto unpacked = unpack_donor_fragment(packed, i);
    check(ast_cache_key(unpacked.ast) == ast_cache_key(donor),
        "packed donor fragment lost names, constants, or node indices");
    checked_named_constant_fragment = true;
    break;
  }
  check(checked_named_constant_fragment,
      "fixture did not produce a donor fragment containing both a name and a constant");

  auto changed_constant_population = population;
  check(!changed_constant_population.front().ast.consts.empty(),
      "fixture parent had no referenced constant to mutate");
  changed_constant_population.front().ast.consts.front() = Value::from_int(999);
  rejects_invalid(
      [&] {
        (void)gagp::evo::repro::pack_population(
            changed_constant_population, prep, config);
      },
      "compiled packing accepted stale preparation after a referenced constant changed");

  auto changed_node_population = population;
  check(!changed_node_population.front().ast.nodes.empty(),
      "fixture parent had no referenced node to mutate");
  ++changed_node_population.front().ast.nodes.front().i1;
  rejects_invalid(
      [&] {
        (void)gagp::evo::repro::pack_population(
            changed_node_population, prep, config);
      },
      "compiled packing accepted stale preparation after a referenced node changed");

  auto changed_donor_prep = prep;
  const auto donor_with_constant = std::find_if(
      changed_donor_prep.donor_pool.begin(), changed_donor_prep.donor_pool.end(),
      [](const auto& donor) { return !donor.ast.consts.empty(); });
  check(donor_with_constant != changed_donor_prep.donor_pool.end(),
      "fixture had no donor constant to mutate");
  donor_with_constant->ast.consts.front() = Value::from_int(1001);
  rejects_invalid(
      [&] {
        (void)gagp::evo::repro::pack_population(
            population, changed_donor_prep, config);
      },
      "compiled packing accepted stale preparation after a donor changed");

  auto invalid_constant_stream = prep;
  invalid_constant_stream.parent_constant_streams.front() =
      static_cast<int>(prep.constant_mutation->streams.size());
  rejects_invalid([&] {
    (void)gagp::evo::repro::pack_population(population, invalid_constant_stream, config);
  }, "compiled packing accepted an out-of-range constant stream");

  auto invalid_occurrence_binders = prep;
  invalid_occurrence_binders.occurrences.front().binder_offset =
      static_cast<int>(prep.occurrence_binder_ids.size()) + 1;
  rejects_invalid([&] {
    (void)gagp::evo::repro::pack_population(population, invalid_occurrence_binders, config);
  }, "compiled packing accepted an out-of-range occurrence binder slice");
  auto invalid_donor_binders = prep;
  invalid_donor_binders.donor_contracts.front().binder_count = -1;
  rejects_invalid([&] {
    (void)gagp::evo::repro::pack_population(population, invalid_donor_binders, config);
  }, "compiled packing accepted a negative donor binder count");

  auto stale_measure = prep;
  ++stale_measure.candidates.front().front().materialized_nodes;
  rejects_invalid([&] { (void)gagp::evo::repro::pack_population(population, stale_measure, config); },
      "compiled packing accepted a stale materialized node measure");
  auto invalid_depth = prep;
  invalid_depth.candidates.front().front().materialized_depth = 0;
  rejects_invalid([&] { (void)gagp::evo::repro::pack_population(population, invalid_depth, config); },
      "compiled packing accepted an invalid materialized depth");
  auto changed_limits = config;
  ++changed_limits.max_expr_depth;
  rejects_invalid([&] { (void)gagp::evo::repro::pack_population(population, prep, changed_limits); },
      "compiled packing accepted changed search limits after preparation");


  GpuReproConfig invalid = config;
  invalid.max_names = gagp::evo::repro::kGpuReproMaxNames + 1;
  rejects_invalid([&] { (void)gagp::evo::repro::pack_population(population, prep, invalid); },
      "compiled packing accepted an invalid name capacity");
}

}  // namespace

int main() {
  try {
    test_disabled_mutation_skips_speculative_analysis();
    test_prefetched_parent_analysis_matches_sequential(false);
    test_prefetched_parent_analysis_matches_sequential(true);
    test_warmed_population_handoff();
    test_gpu_run_resources_reject_oversized_search_space();
    test_exact_contracts_and_shared_occurrences();
    test_determinism_packing_ownership_and_guards();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "grammar reproduction preparation: exact contracts, donors, and packing passed\n";
  return 0;
}
