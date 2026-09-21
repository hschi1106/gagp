#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/evolution/repro/pack.hpp"
#include "gagp/evolution/repro/prep.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "../../src/evolution/repro/constant_prep.hpp"
#include "../../src/evolution/repro/compiled_decode.hpp"
#include "../../src/evolution/repro/gpu/internal.hpp"
#include "../../src/evolution/repro/splice_metadata.hpp"

namespace {

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::repro;

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

void require_cuda(cudaError_t status, const char* operation) {
  if (status == cudaSuccess) return;
  throw std::runtime_error(std::string(operation) + ": " +
                           cudaGetErrorString(status));
}

struct Resources {
  GpuReproArena arena;
  GpuReproHostStaging staging;

  ~Resources() {
    destroy_gpu_repro_host_staging(&staging);
    destroy_gpu_repro_arena(&arena);
  }
};

std::shared_ptr<const grammar::CompiledGrammar> repeated_capture_grammar(
    bool mutable_constant = true) {
  const std::string scoped_expression = mutable_constant
      ? R"({"signature":"add(Int,Int)->Int","args":[
          {"bound":"x"},{"constant":{"type":"Int","values":["0","5"]}}]})"
      : R"({"bound":"x"})";
  return std::make_shared<const grammar::CompiledGrammar>(
      grammar::compile_grammar(grammar::parse_definition(std::string(R"({
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
         "expression":)" + scoped_expression + R"(}]}
    ]
  })"))));
}

GpuReproConfig compiled_config(const grammar::CompiledGrammar& grammar,
                               int population_size) {
  GpuReproConfig config;
  config.donor_pool_size_per_site = 2;
  config.population_size = population_size;
  config.pair_count = (population_size + 1) / 2;
  config.candidates_per_program = 16;
  config.max_nodes = static_cast<int>(grammar.search_limits().max_nodes);
  config.max_donor_nodes = 1;
  config.max_names = 1;
  config.max_consts = 2;
  config.max_expr_depth = static_cast<int>(grammar.search_limits().max_depth);
  config.tournament_k = population_size;
  config.mutation_ratio = 0.0;
  config.mutation_subtree_ratio = 0.0;
  config.seed = UINT64_C(0x123456789abcdef0);
  return config;
}

bool same_value(const Value& left, const Value& right) {
  if (left.tag != right.tag) return false;
  if (left.tag == ValueTag::Invalid) return true;
  if (left.tag == ValueTag::Bool) return left.b == right.b;
  if (left.tag == ValueTag::Float) return left.f == right.f;
  return left.i == right.i;
}

bool same_node(const PlainNode& left, const PlainNode& right) {
  return left.kind == right.kind && left.i0 == right.i0 && left.i1 == right.i1;
}

template <typename T>
std::vector<T> copy_from_device(const T* source, std::size_t count,
                                const char* operation) {
  std::vector<T> values(count);
  if (count != 0) {
    require(source != nullptr, std::string(operation) + " source is null");
    require_cuda(cudaMemcpy(values.data(), source, sizeof(T) * count,
                            cudaMemcpyDeviceToHost),
                 operation);
  }
  return values;
}

template <typename T, typename Equal>
void require_rows_equal(const std::vector<T>& actual,
                        const std::vector<T>& expected, Equal equal,
                        const char* description) {
  require(actual.size() == expected.size(),
          std::string(description) + " count changed during upload");
  for (std::size_t i = 0; i < expected.size(); ++i) {
    require(equal(actual[i], expected[i]),
            std::string(description) + " changed at row " +
                std::to_string(i));
  }
}

void verify_constant_upload(const PackedHostData& packed,
                            const GpuReproArena& arena) {
  require(packed.constant_mutation != nullptr,
          "compiled pack lost the constant mutation owner");
  const auto& expected = *packed.constant_mutation;
  require(expected.grammar_domains != nullptr,
          "compiled pack lost the immutable grammar-domain owner");
  const auto& grammar_domains = *expected.grammar_domains;
  require(!grammar_domains.domains.empty() && !grammar_domains.values.empty() &&
              !expected.groups.empty() && !expected.node_group_origins.empty() &&
              !expected.streams.empty(),
          "fixture did not exercise every constant transport table");

  const auto domains = copy_from_device(arena.d_constant_domains,
                                        grammar_domains.domains.size(),
                                        "cudaMemcpy constant domains back");
  const auto values = copy_from_device(arena.d_constant_values,
                                       grammar_domains.values.size(),
                                       "cudaMemcpy constant values back");
  const auto groups = copy_from_device(arena.d_constant_groups,
                                       expected.groups.size(),
                                       "cudaMemcpy constant groups back");
  const auto origins = copy_from_device(arena.d_constant_origins,
                                        expected.node_group_origins.size(),
                                        "cudaMemcpy constant origins back");
  const auto streams = copy_from_device(arena.d_constant_streams,
                                        expected.streams.size(),
                                        "cudaMemcpy constant streams back");
  const auto roots = copy_from_device(arena.d_constant_roots,
                                      expected.metadata_roots.size(),
                                      "cudaMemcpy constant roots back");

  require_rows_equal(domains, grammar_domains.domains,
      [](const ConstantMutationDomain& a, const ConstantMutationDomain& b) {
        return a.type == b.type && a.value_offset == b.value_offset &&
               a.value_count == b.value_count &&
               a.integer_range == b.integer_range && a.minimum == b.minimum &&
               a.maximum == b.maximum;
      }, "constant domains");
  require_rows_equal(values, grammar_domains.values, same_value,
                     "constant values");
  require_rows_equal(groups, expected.groups,
      [](const ConstantMutationGroup& a, const ConstantMutationGroup& b) {
        return a.logical_instance == b.logical_instance &&
               a.domain == b.domain && a.node_offset == b.node_offset &&
               a.node_count == b.node_count;
      }, "constant groups");
  require(origins == expected.node_group_origins,
          "constant origins changed during upload");
  require(roots == expected.metadata_roots,
          "constant metadata roots changed during upload");
  require_rows_equal(streams, expected.streams,
      [](const ConstantMutationStream& a, const ConstantMutationStream& b) {
        return a.node_origin_offset == b.node_origin_offset &&
               a.node_count == b.node_count &&
               a.group_offset == b.group_offset &&
               a.group_count == b.group_count &&
               a.metadata_root_offset == b.metadata_root_offset &&
               a.metadata_root_count == b.metadata_root_count;
      }, "constant streams");
}

AstProgram child_ast(const GpuReproChildView& copyback, int child,
                     const PackedHostData& packed) {
  AstProgram ast;
  const int node_offset = copyback.child_node_offsets[child];
  const int name_offset = copyback.child_name_offsets[child];
  const int const_offset = copyback.child_const_offsets[child];
  for (int i = 0; i < copyback.child_used_len[child]; ++i) {
    const auto& node = copyback.child_nodes[node_offset + i];
    ast.nodes.push_back(
        {static_cast<NodeKind>(node.kind), node.i0, node.i1});
  }
  for (int i = 0; i < copyback.child_name_counts[child]; ++i) {
    const std::uint64_t id = copyback.child_name_ids[name_offset + i];
    const auto found = packed.name_lookup.find(id);
    require(found != packed.name_lookup.end(),
            "copyback name has no immutable source mapping");
    ast.names.push_back(found->second);
  }
  ast.consts.assign(copyback.child_consts + const_offset,
                    copyback.child_consts + const_offset +
                        copyback.child_const_counts[child]);
  return ast;
}

std::int64_t execute_int(const ProgramGenome& genome) {
  const AstVerifyResult verified = verify_ast(genome.ast, {});
  require(verified.ok, "accepted child failed verification before execution");
  const ExecResult result = execute_bytecode_cpu(
      compile_for_eval(genome, verified.verified), {}, 100);
  require(!result.is_error && result.value.tag == ValueTag::Int,
          "accepted compiled child did not execute to Int");
  return result.value.i;
}

void require_base_copy(const PackedHostData& packed,
                       const GpuReproChildView& copyback, int child,
                       int base_parent) {
  const auto& meta = packed.metas.at(static_cast<std::size_t>(base_parent));
  require(copyback.child_used_len[child] == meta.used_len &&
              copyback.child_name_counts[child] == meta.name_count &&
              copyback.child_const_counts[child] == meta.const_count,
          "unapplied child sizes do not match its identified base parent");
  const std::size_t parent_node_base =
      static_cast<std::size_t>(base_parent) * packed.config.max_nodes;
  const std::size_t parent_name_base =
      static_cast<std::size_t>(base_parent) * packed.config.max_names;
  const std::size_t parent_const_base =
      static_cast<std::size_t>(base_parent) * packed.config.max_consts;
  const int child_node_base = copyback.child_node_offsets[child];
  const int child_name_base = copyback.child_name_offsets[child];
  const int child_const_base = copyback.child_const_offsets[child];
  for (int i = 0; i < meta.used_len; ++i) {
    require(same_node(copyback.child_nodes[child_node_base + i],
                      packed.program_nodes[parent_node_base + i]),
            "unapplied child nodes do not match its base parent");
  }
  for (int i = 0; i < meta.name_count; ++i) {
    require(copyback.child_name_ids[child_name_base + i] ==
                packed.program_name_ids[parent_name_base + i],
            "unapplied child names do not match its base parent");
  }
  for (int i = 0; i < meta.const_count; ++i) {
    require(same_value(copyback.child_consts[child_const_base + i],
                       packed.program_consts[parent_const_base + i]),
            "unapplied child constants do not match its base parent");
  }
}

void verify_children(const PackedHostData& packed,
                     const GpuReproChildView& copyback) {
  bool saw_applied = false;
  const int child_count = packed.config.pair_count * 2;
  for (int child = 0; child < child_count; ++child) {
    const int pair = child / 2;
    const bool first = child % 2 == 0;
    const int base_parent =
        first ? copyback.parent_a[pair] : copyback.parent_b[pair];
    const int source_parent =
        first ? copyback.parent_b[pair] : copyback.parent_a[pair];
    const int destination = first ? copyback.cand_a[pair] : copyback.cand_b[pair];
    const int source = first ? copyback.cand_b[pair] : copyback.cand_a[pair];
    require(base_parent == 1 && source_parent == 1,
            "full tournament did not select the highest-fitness parent");
    require(destination >= 0 &&
                destination < packed.metas[base_parent].candidate_count &&
                source >= 0 && source < packed.metas[source_parent].candidate_count,
            "selection published an invalid compiled candidate index");

    const auto& splice = copyback.child_splices[child];
    require(splice.base_parent == base_parent,
            "child provenance does not identify its selected base parent");
    require(copyback.child_meta[child].valid != 0,
            "compiled crossover published an invalid child");
    if (splice.applied == 0) {
      require_base_copy(packed, copyback, child, base_parent);
      continue;
    }

    saw_applied = true;
    require(splice.applied == 1 &&
                splice.destination_candidate == destination &&
                splice.source_kind == SpliceSourceKind::Parent &&
                splice.source_index == source_parent &&
                splice.source_candidate == source,
            "applied child provenance does not match actual selection");
    AstProgram ast = child_ast(copyback, child, packed);
    reconstruct_compiled_child_metadata(ast, packed, splice);
    const AstVerifyResult verified = verify_ast(ast, {});
    require(verified.ok,
            "compiled child failed native verification after reconstruction: " +
                verified.diagnostic.message);
    ProgramGenome genome;
    genome.ast = ast;
    genome.meta = build_genome_meta(ast);
    const ExecResult result = execute_bytecode_cpu(
        compile_for_eval(genome, verified.verified), {}, 100);
    require(!result.is_error && result.value.tag == ValueTag::Int &&
                (result.value.i == 3 || result.value.i == 13),
            "compiled transport changed repeated-capture execution");
  }
  require(saw_applied, "compiled transport did not apply any crossover");
}

std::vector<ProgramGenome> crossover_children(
    const std::shared_ptr<const grammar::CompiledGrammar>& grammar,
    grammar::VariationContext& context) {
  std::vector<ProgramGenome> population = {
      compact_genome_tables(grammar::generate_derivation(*grammar, 31).genome),
      compact_genome_tables(grammar::generate_derivation(*grammar, 32).genome),
  };
  GpuReproConfig requested = compiled_config(*grammar, population.size());
  const PreprocessOutput prep =
      preprocess_population(population, requested, context);
  const PackedHostData packed = pack_population(population, prep, requested);

  Resources resources;
  ReproductionStats stats;
  std::string message;
  require(ensure_gpu_repro_arena_capacity(&resources.arena, packed.config,
                                          &message), message);
  require(ensure_gpu_repro_host_staging_capacity(
              &resources.staging, packed.config, &message), message);
  require(upload_gpu_repro_inputs(packed, &resources.arena, &stats, &message),
          message);
  require(launch_gpu_repro_kernels(&resources.arena, packed.config,
                                   {0.0, 1.0}, &stats, &message), message);
  GpuReproChildView copyback;
  require(copyback_gpu_repro_children(resources.arena, packed.config,
                                      &resources.staging, &copyback, &stats,
                                      &message), message);
  verify_children(packed, copyback);
  {
    GpuReproChildView corrupted = copyback;
    std::vector<PackedSelectionCounters> counts(
        copyback.selection_counters, copyback.selection_counters + packed.config.pair_count);
    corrupted.selection_counters = counts.data();
    const auto pairs = static_cast<std::uint64_t>(
        packed.metas[copyback.parent_a[0]].candidate_count) *
        packed.metas[copyback.parent_b[0]].candidate_count;
    const auto attempts_before = context.counters().crossover_attempts;
    const auto contracts_before = context.counters().contract_rejections;
    const auto budgets_before = context.counters().budget_rejections;
    for (bool overflowing_sum : {false, true}) {
      counts[0].contract_rejections = overflowing_sum ? pairs : UINT64_MAX;
      counts[0].budget_rejections = 1;
      bool rejected = false;
      try { (void)decode_compiled_pass(packed, corrupted, context); }
      catch (const std::invalid_argument& error) {
        rejected = std::string(error.what()).find("rejection counts exceed") != std::string::npos;
      }
      require(rejected, "decoder accepted impossible selection rejection counters");
      require(context.counters().crossover_attempts == attempts_before &&
                  context.counters().contract_rejections == contracts_before &&
                  context.counters().budget_rejections == budgets_before,
              "invalid selection counters changed the run statistics");
    }
  }
  {
    GpuReproChildView valid_counts = copyback;
    std::vector<PackedSelectionCounters> counts(
        copyback.selection_counters, copyback.selection_counters + packed.config.pair_count);
    valid_counts.selection_counters = counts.data();
    const auto saved = context.counters();
    context.counters().crossover_attempts = UINT64_MAX;
    bool rejected = false;
    try { (void)decode_compiled_pass(packed, valid_counts, context); }
    catch (const std::invalid_argument& error) {
      rejected = std::string(error.what()).find("counter accumulation overflow") != std::string::npos;
    }
    require(rejected, "decoder allowed variation counters to wrap");
    require(context.counters().crossover_attempts == UINT64_MAX,
            "counter overflow changed the run statistics");
    context.counters() = saved;
  }
  const auto crossover_before = context.counters().crossover_attempts;
  std::vector<ProgramGenome> children =
      decode_compiled_pass(packed, copyback, context);
  require(context.counters().crossover_attempts - crossover_before ==
              static_cast<std::uint64_t>(packed.config.pair_count),
          "compiled crossover decode recorded the wrong attempt count");
  for (const auto& child : children) {
    require(child.derivation != nullptr,
            "compiled crossover child was not certified by native grammar acceptance");
    grammar::require_membership(*grammar, child);
    require(context.cache().analyze(child) != nullptr,
            "compiled crossover child was not reusable through the shared cache");
  }
  return children;
}

void exercise_mutation_pass(
    const std::shared_ptr<const grammar::CompiledGrammar>& grammar,
    grammar::VariationContext& context,
    const std::vector<ProgramGenome>& crossover_population,
    double subtree_ratio, CompiledMutationOutcome expected_outcome,
    bool expect_constant_change, double mutation_ratio = 1.0) {
  GpuReproConfig requested =
      compiled_config(*grammar, crossover_population.size());
  requested.compiled_pass = CompiledVariationPass::Mutation;
  requested.mutation_ratio = mutation_ratio;
  requested.mutation_subtree_ratio = subtree_ratio;
  const PreprocessOutput prep =
      preprocess_population(crossover_population, requested, context);
  const PackedHostData packed =
      pack_population(crossover_population, prep, requested);
  if (subtree_ratio == 0.0 && expected_outcome == CompiledMutationOutcome::Subtree)
    require(packed.constant_mutation->groups.empty(),
            "no-groups fallback fixture unexpectedly has mutable constants");
  require(packed.config.compiled_pass == CompiledVariationPass::Mutation,
          "compiled mutation pass was lost during packing");
  require(packed.constant_mutation != nullptr,
          "compiled mutation pack lost constant preparation");
  for (const auto& stream : packed.constant_mutation->streams) {
    require(stream.metadata_root_offset >= 0 &&
                stream.metadata_root_count >= 0 &&
                static_cast<std::size_t>(stream.metadata_root_offset) +
                        stream.metadata_root_count <=
                    packed.constant_mutation->metadata_roots.size(),
            "compiled mutation stream has an invalid metadata root slice");
  }

  Resources resources;
  ReproductionStats stats;
  std::string message;
  require(ensure_gpu_repro_arena_capacity(&resources.arena, packed.config,
                                          &message), message);
  require(ensure_gpu_repro_host_staging_capacity(
              &resources.staging, packed.config, &message), message);
  require(upload_gpu_repro_inputs(packed, &resources.arena, &stats, &message),
          message);
  if (!packed.constant_mutation->groups.empty())
    verify_constant_upload(packed, resources.arena);
  require(launch_gpu_repro_kernels(&resources.arena, packed.config,
                                   {0.0, 1.0}, &stats, &message), message);
  GpuReproChildView copyback;
  require(copyback_gpu_repro_children(resources.arena, packed.config,
                                      &resources.staging, &copyback, &stats,
                                      &message), message);

  for (int child = 0; child < packed.config.population_size; ++child) {
    const auto& splice = copyback.child_splices[child];
    require(splice.base_parent == child,
            "compiled mutation did not preserve direct input provenance");
    require(splice.mutation_outcome == expected_outcome,
            "compiled mutation reported outcome " + std::to_string(static_cast<int>(splice.mutation_outcome)) +
            " instead of " + std::to_string(static_cast<int>(expected_outcome)) + " for child " + std::to_string(child));
    require(copyback.child_meta[child].valid != 0,
            "compiled mutation published an invalid child");
    if (expected_outcome == CompiledMutationOutcome::Constant) {
      require(splice.applied == 0 &&
                  splice.source_kind == SpliceSourceKind::None,
              "constant mutation published subtree splice provenance");
    } else if (expected_outcome == CompiledMutationOutcome::Subtree) {
      require(splice.applied == 1 &&
                  splice.source_kind == SpliceSourceKind::CompiledDonor &&
                  splice.source_index >= 0 &&
                  splice.source_index < packed.config.compiled_donor_count,
              "subtree mutation did not publish compiled donor provenance");
    } else {
      require(splice.applied == 0 &&
                  splice.source_kind == SpliceSourceKind::None,
              "skipped mutation published operator provenance");
    }
  }

  const auto mutation_before = context.counters().mutation_attempts;
  const std::vector<ProgramGenome> accepted =
      decode_compiled_pass(packed, copyback, context);
  const std::uint64_t expected_attempts =
      expected_outcome == CompiledMutationOutcome::None
          ? 0
          : static_cast<std::uint64_t>(packed.config.population_size);
  require(context.counters().mutation_attempts - mutation_before ==
              expected_attempts,
          "compiled mutation decode recorded the wrong attempt count");
  require(accepted.size() == crossover_population.size(),
          "compiled mutation decode changed the logical population size");
  for (std::size_t child = 0; child < accepted.size(); ++child) {
    require(accepted[child].derivation != nullptr,
            "compiled mutation child was not certified by native grammar acceptance");
    grammar::require_membership(*grammar, accepted[child]);
    require(context.cache().analyze(accepted[child]) != nullptr,
            "compiled mutation child was not reusable through the shared cache");
    if (expect_constant_change) {
      const std::int64_t before = execute_int(crossover_population[child]);
      const std::int64_t after = execute_int(accepted[child]);
      require((before == 3 && after == 13) ||
                  (before == 13 && after == 3),
              "constant mutation did not update every repeated logical copy");
    }
  }
  if (expected_outcome == CompiledMutationOutcome::None ||
      expected_outcome == CompiledMutationOutcome::Constant) {
    GpuReproChildView corrupted = copyback;
    const int total = copyback.child_const_offsets[packed.config.pair_count * 2];
    std::vector<Value> changed_constants(copyback.child_consts, copyback.child_consts + total);
    require(!changed_constants.empty(), "decoder rejection fixture has no constants");
    changed_constants.front() = Value::from_int(999);
    corrupted.child_consts = changed_constants.data();
    if (expected_outcome == CompiledMutationOutcome::None) {
      bool rejected = false;
      try { (void)decode_compiled_pass(packed, corrupted, context); }
      catch (const std::invalid_argument&) { rejected = true; }
      require(rejected, "decoder silently replaced a corrupted device skip with its parent");
    } else {
      const auto rejected_before = context.counters().acceptance_rejections;
      const auto recovered = decode_compiled_pass(packed, corrupted, context);
      require(context.counters().acceptance_rejections > rejected_before &&
                  ast_cache_key(recovered.front().ast) == ast_cache_key(crossover_population.front().ast),
              "native acceptance did not reject an out-of-grammar device child");
    }
  }

}

void test_compiled_mutation_transport() {
  const auto mutable_grammar = repeated_capture_grammar(true);
  grammar::VariationContext mutable_context(mutable_grammar);
  const auto mutable_children =
      crossover_children(mutable_grammar, mutable_context);
  exercise_mutation_pass(mutable_grammar, mutable_context, mutable_children,
                         0.0, CompiledMutationOutcome::Constant, true);
  exercise_mutation_pass(mutable_grammar, mutable_context, mutable_children,
                         1.0, CompiledMutationOutcome::Subtree, false);
  exercise_mutation_pass(mutable_grammar, mutable_context, mutable_children,
                         0.0, CompiledMutationOutcome::None, false, 0.0);

  const auto immutable_grammar = repeated_capture_grammar(false);
  grammar::VariationContext immutable_context(immutable_grammar);
  const auto immutable_children =
      crossover_children(immutable_grammar, immutable_context);
  exercise_mutation_pass(immutable_grammar, immutable_context,
                         immutable_children, 0.0,
                         CompiledMutationOutcome::Subtree, false);
}

void test_compiled_transport() {
  const auto grammar = repeated_capture_grammar();
  std::vector<ProgramGenome> population = {
      compact_genome_tables(grammar::generate_derivation(*grammar, 3).genome),
      compact_genome_tables(grammar::generate_derivation(*grammar, 4).genome),
  };
  GpuReproConfig requested = compiled_config(*grammar, population.size());
  grammar::VariationContext context(grammar);
  const PreprocessOutput prep =
      preprocess_population(population, requested, context);
  const PackedHostData packed = pack_population(population, prep, requested);
  require(packed.config.population_size >= 2 &&
              packed.config.tournament_k <= packed.config.population_size &&
              packed.config.pair_count ==
                  (packed.config.population_size + 1) / 2 &&
              packed.config.max_expr_depth > 0 &&
              packed.config.max_expr_depth <= packed.config.max_nodes,
          "compiled fixture produced an invalid launch configuration");
  require(packed.compiled_sources && packed.compiled_grammar,
          "compiled pack did not retain immutable sources");
  require(std::any_of(prep.candidates.front().begin(),
                      prep.candidates.front().end(),
                      [](const CandidateRange& candidate) {
                        return candidate.occurrence_count == 2;
                      }),
          "compiled fixture lost its repeated scoped hole");

  Resources resources;
  ReproductionStats stats;
  std::string message;
  require(ensure_gpu_repro_arena_capacity(&resources.arena, packed.config,
                                          &message),
          message);
  require(ensure_gpu_repro_host_staging_capacity(
              &resources.staging, packed.config, &message),
          message);
  PlainNode* reused_device_nodes = resources.arena.d_program_nodes;
  PlainNode* reused_host_nodes = resources.staging.child_nodes;
  require(ensure_gpu_repro_arena_capacity(&resources.arena, packed.config,
                                          &message) &&
              resources.arena.d_program_nodes == reused_device_nodes,
          "arena did not reuse sufficient compiled capacity");
  require(ensure_gpu_repro_host_staging_capacity(
              &resources.staging, packed.config, &message) &&
              resources.staging.child_nodes == reused_host_nodes,
          "host staging did not reuse sufficient compiled capacity");

  require(upload_gpu_repro_inputs(packed, &resources.arena, &stats, &message),
          message);
  require(resources.arena.uploaded_domains ==
              packed.constant_mutation->grammar_domains,
          "constant upload did not retain the immutable domain owner");
  verify_constant_upload(packed, resources.arena);
  require(upload_gpu_repro_inputs(packed, &resources.arena, &stats, &message) &&
              resources.arena.uploaded_domains ==
                  packed.constant_mutation->grammar_domains,
          "repeated upload did not reuse the immutable domain owner");

  destroy_gpu_repro_arena(&resources.arena);
  require(!resources.arena.uploaded_domains,
          "arena destruction retained a stale uploaded domain owner");
  require(ensure_gpu_repro_arena_capacity(&resources.arena, packed.config,
                                          &message) &&
              upload_gpu_repro_inputs(packed, &resources.arena, &stats,
                                      &message),
          message);
  require(resources.arena.uploaded_domains ==
              packed.constant_mutation->grammar_domains,
          "upload after arena recreation did not restore the domain owner");
  verify_constant_upload(packed, resources.arena);
  require(launch_gpu_repro_kernels(&resources.arena, packed.config,
                                   {0.0, 1.0}, &stats, &message),
          message);
  require(launch_gpu_repro_kernels(&resources.arena, packed.config,
                                   {0.0, 1.0}, &stats, &message), message);
  require(std::abs(stats.kernel_ms - stats.selection_kernel_ms -
                   stats.variation_kernel_ms) < 1e-6,
          "repeated compiled passes double-counted cumulative kernel timing");
  GpuReproChildView copyback;
  require(copyback_gpu_repro_children(resources.arena, packed.config,
                                      &resources.staging, &copyback, &stats,
                                      &message),
          message);
  verify_children(packed, copyback);

  PackedHostData malformed = packed;
  auto malformed_constants =
      std::make_shared<ConstantMutationTable>(*packed.constant_mutation);
  malformed_constants->streams.pop_back();
  malformed.constant_mutation = malformed_constants;
  message.clear();
  require(!upload_gpu_repro_inputs(malformed, &resources.arena, &stats,
                                   &message) &&
              message.find("table shape mismatch") != std::string::npos,
          "malformed compiled constant table passed upload preflight");

  GpuReproConfig grown = packed.config;
  grown.population_size = 4;
  grown.pair_count = 2;
  grown.tournament_k = 4;
  require(ensure_gpu_repro_arena_capacity(&resources.arena, grown, &message) &&
              gpu_repro_config_fits_capacity(grown, resources.arena.capacity),
          "compiled arena did not grow for a larger population");
  require(ensure_gpu_repro_host_staging_capacity(&resources.staging, grown,
                                                 &message) &&
              gpu_repro_config_fits_capacity(grown,
                                             resources.staging.capacity),
          "compiled host staging did not grow for a larger population");
  reused_device_nodes = resources.arena.d_program_nodes;
  reused_host_nodes = resources.staging.child_nodes;
  require(ensure_gpu_repro_arena_capacity(&resources.arena, packed.config,
                                          &message) &&
              resources.arena.d_program_nodes == reused_device_nodes &&
              ensure_gpu_repro_host_staging_capacity(
                  &resources.staging, packed.config, &message) &&
              resources.staging.child_nodes == reused_host_nodes,
          "grown compiled capacities were not reused by a smaller request");
}

}  // namespace

int main() {
  try {
    test_compiled_transport();
    test_compiled_mutation_transport();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "compiled GPU reproduction transport is coherent\n";
  return 0;
}
