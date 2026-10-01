#include "gagp/core/host_threads.hpp"
#include "../batch_workers.hpp"
#include "compiled_decode.hpp"
#include "../../runtime/payload/staging.hpp"

#include <algorithm>
#include <atomic>
#include <future>
#include <thread>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

#include "../grammar/variation_internal.hpp"
#include "splice_metadata.hpp"

namespace gagp::evo::repro {
namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::invalid_argument(message);
}

void require_counter_room(std::uint64_t current, std::uint64_t increment) {
  require(increment <= std::numeric_limits<std::uint64_t>::max() - current,
          "compiled copyback counter accumulation overflow");
}

void check_offsets(const int* offsets, const int* lengths, int children, int capacity) {
  require(offsets && lengths && offsets[0] == 0, "compiled copyback has missing or invalid offsets");
  for (int i = 0; i < children; ++i) {
    require(lengths[i] >= 0 && lengths[i] <= capacity && offsets[i] >= 0 &&
                static_cast<std::int64_t>(offsets[i]) + lengths[i] == offsets[i + 1],
            "compiled copyback has inconsistent offsets or lengths");
  }
}

bool copied_tables_match(const AstProgram& child, const AstProgram& base) {
  if (child.nodes.size() != base.nodes.size() || child.names != base.names ||
      child.consts.size() != base.consts.size()) return false;
  for (std::size_t i = 0; i < child.nodes.size(); ++i)
    if (child.nodes[i].kind != base.nodes[i].kind || child.nodes[i].i0 != base.nodes[i].i0 ||
        child.nodes[i].i1 != base.nodes[i].i1) return false;
  for (std::size_t i = 0; i < child.consts.size(); ++i) {
    const auto& a = child.consts[i];
    const auto& b = base.consts[i];
    if (a.tag != b.tag) return false;
    if (a.tag == ValueTag::Invalid) continue;
    if (a.tag == ValueTag::Float) {
      if (std::memcmp(&a.f, &b.f, sizeof(double)) != 0) return false;
    } else if (a.tag == ValueTag::Bool) {
      if (a.b != b.b) return false;
    } else if (a.i != b.i) return false;
  }
  return true;
}

AstProgram read_child(const PackedHostData& packed, const GpuReproChildView& view, int child) {
  AstProgram ast;
  for (int i = 0; i < view.child_used_len[child]; ++i) {
    const auto& node = view.child_nodes[view.child_node_offsets[child] + i];
    ast.nodes.push_back(AstNode{static_cast<NodeKind>(node.kind), node.i0, node.i1});
  }
  for (int i = 0; i < view.child_name_counts[child]; ++i) {
    const auto id = view.child_name_ids[view.child_name_offsets[child] + i];
    const auto found = packed.name_lookup.find(id);
    require(found != packed.name_lookup.end(), "compiled copyback has unknown name ID");
    ast.names.push_back(found->second);
  }
  for (int i = 0; i < view.child_const_counts[child]; ++i)
    ast.consts.push_back(view.child_consts[view.child_const_offsets[child] + i]);
  return ast;
}

}  // namespace

std::vector<ProgramGenome> decode_compiled_pass(
    const PackedHostData& packed, const GpuReproChildView& view,
    grammar::VariationContext& context,
    const PreparedParentCertificates* certificates) {
  const auto& c = packed.config;
  require(packed.compiled_sources &&
              packed.compiled_grammar == context.grammar_owner(),
          "compiled copyback requires its prepared grammar and sources");
  require(c.population_size > 0 && c.population_size <= 65536 &&
              c.pair_count == (c.population_size + 1) / 2 &&
              view.config.population_size == c.population_size && view.config.pair_count == c.pair_count &&
              view.config.compiled_pass == c.compiled_pass &&
              view.config.max_nodes == c.max_nodes && view.config.max_names == c.max_names &&
              view.config.max_consts == c.max_consts &&
              packed.compiled_sources->parents.size() == static_cast<std::size_t>(c.population_size),
          "compiled copyback configuration differs from preparation");
  const bool mutation = c.compiled_pass == CompiledVariationPass::Mutation;
  require(mutation || c.compiled_pass == CompiledVariationPass::Crossover,
          "compiled copyback has an unknown operator pass");
  const auto admission_use = (mutation && std::getenv("GAGP_FINAL_EXECUTION_ADMISSION")) ||
      (certificates && !certificates->admitted_parents.empty())
      ? grammar::variation_detail::AdmissionUse::Execution
      : grammar::variation_detail::AdmissionUse::Variation;
  const int physical_children = c.pair_count * 2;
  check_offsets(view.child_node_offsets, view.child_used_len, physical_children, c.max_nodes);
  check_offsets(view.child_name_offsets, view.child_name_counts, physical_children, c.max_names);
  check_offsets(view.child_const_offsets, view.child_const_counts, physical_children, c.max_consts);
  require(view.child_splices && view.child_meta && view.child_nodes &&
              (view.child_name_offsets[physical_children] == 0 || view.child_name_ids) &&
              (view.child_const_offsets[physical_children] == 0 || view.child_consts) &&
              (mutation || (view.parent_a && view.parent_b && view.cand_a && view.cand_b && view.selection_counters)),
          "compiled copyback has missing output arrays");
  std::vector<std::optional<ProgramGenome>> parents(c.population_size);
  const auto parent = [&](int index) -> const ProgramGenome& {
    require(index >= 0 && index < c.population_size, "compiled copyback has invalid base parent");
    auto& cached = parents[index];
    if (!cached) {
      ProgramGenome source;
      source.ast = packed.compiled_sources->parents[index];
      if (certificates && certificates->sources == packed.compiled_sources &&
          certificates->context.get() == &context && certificates->admitted_parents.size() == parents.size()) {
        cached = certificates->admitted_parents[index];
      } else if (certificates && certificates->sources == packed.compiled_sources &&
          certificates->context.get() == &context &&
          certificates->metadata.size() == parents.size() &&
          certificates->analyses.size() == parents.size() &&
          certificates->analyses[index].analysis && certificates->analyses[index].reads &&
          certificates->analyses[index].reads->read_snapshot_unchanged()) {
        source.meta = certificates->metadata[index];
        source.derivation = std::make_shared<const grammar::DerivationMetadata>(
            certificates->analyses[index].analysis->witness);
        cached = std::move(source);
      } else {
        cached = grammar::variation_detail::certify(std::move(source), context);
      }
    }
    return *cached;
  };
  std::vector<ProgramGenome> result;
  result.reserve(c.population_size);
  if (!mutation) {
    require(packed.metas.size() == static_cast<std::size_t>(c.population_size),
            "compiled copyback has missing prepared candidate counts");
    // Each candidate pair is classified once. Check the disjoint rejection
    // counts against the actual selected parents before changing run counters.
    std::uint64_t contract_rejections = 0;
    std::uint64_t budget_rejections = 0;
    for (int i = 0; i < c.pair_count; ++i) {
      const int a = view.parent_a[i], b = view.parent_b[i];
      require(a >= 0 && a < c.population_size && b >= 0 && b < c.population_size,
              "compiled copyback has invalid selected parents");
      const int count_a = packed.metas[a].candidate_count;
      const int count_b = packed.metas[b].candidate_count;
      require(count_a >= 0 && count_a <= c.candidates_per_program && count_a <= c.max_nodes &&
                  count_b >= 0 && count_b <= c.candidates_per_program && count_b <= c.max_nodes,
              "compiled copyback has invalid prepared candidate counts");
      const auto pairs = static_cast<std::uint64_t>(count_a) * count_b;
      const auto& counts = view.selection_counters[i];
      require(counts.contract_rejections <= pairs &&
                  counts.budget_rejections <= pairs - counts.contract_rejections,
              "compiled copyback rejection counts exceed candidate pairs");
      require_counter_room(contract_rejections, counts.contract_rejections);
      require_counter_room(budget_rejections, counts.budget_rejections);
      contract_rejections += counts.contract_rejections;
      budget_rejections += counts.budget_rejections;
    }
    require_counter_room(context.counters().crossover_attempts,
                         static_cast<std::uint64_t>(c.pair_count));
    require_counter_room(context.counters().contract_rejections, contract_rejections);
    require_counter_room(context.counters().budget_rejections, budget_rejections);
    context.counters().crossover_attempts += c.pair_count;
    context.counters().contract_rejections += contract_rejections;
    context.counters().budget_rejections += budget_rejections;
  }
  // Crossover classifies the odd discarded sibling; mutation visits actual
  // offspring only, matching the generation operator order and counter contract.
  const int accepted_count = mutation ? c.population_size : physical_children;
  constexpr int analysis_batch = 256;
  std::vector<ProgramGenome> staged;
  std::vector<int> staged_indices;
  std::vector<std::optional<ProgramGenome>> admitted;
  std::vector<grammar::VariationCounters> admission_counts;
  std::vector<std::shared_ptr<payload::StagedPayloads>> admission_reads;
  std::vector<std::shared_ptr<payload::StagedPayloads>> parent_reads;
  std::vector<std::optional<std::uint32_t>> parent_roots;
  std::unique_ptr<detail::BatchWorkers> team;
  for (int i = 0; i < accepted_count; ++i) {
    if (i % analysis_batch == 0) {
      const auto count = std::min(analysis_batch, accepted_count - i);
      staged.clear(); staged.reserve(count); staged_indices.assign(count, -1);
      std::vector<std::optional<ProgramGenome>> reconstructed(count);
      const auto reconstruct = [&](int offset) {
        const auto index = i + offset;
        const auto& proposal = view.child_splices[index];
        if (!proposal.applied && proposal.mutation_outcome != CompiledMutationOutcome::Constant)
          return;
        try {
          ProgramGenome candidate;
          candidate.ast = read_child(packed, view, index);
          reconstruct_compiled_child_metadata(candidate.ast, packed, proposal);
          reconstructed[offset] = std::move(candidate);
        } catch (const std::exception&) {
          // Speculation is never admission. The ordered path below repeats the
          // original checks and reports errors at the original child position.
          staged_indices[offset] = -1;
        }
      };
      if (count >= 32 && certificates &&
          certificates->sources == packed.compiled_sources &&
          certificates->context.get() == &context &&
          !payload::StagedPayloads::has_active_scope()) {
        if (!team) team = std::make_unique<detail::BatchWorkers>(
            gagp::host_thread_limit());
        std::atomic<int> next{0};
        team->run([&] {
          for (;;) {
            const int offset = next.fetch_add(1, std::memory_order_relaxed);
            if (offset >= count) break;
            reconstruct(offset);
          }
        });
      } else {
        for (int offset = 0; offset < count; ++offset) reconstruct(offset);
      }
      for (int offset = 0; offset < count; ++offset) {
        if (!reconstructed[offset]) continue;
        staged_indices[offset] = static_cast<int>(staged.size());
        staged.push_back(std::move(*reconstructed[offset]));
      }
      admitted.clear(); admitted.resize(count);
      admission_counts.assign(count, {});
      admission_reads.clear(); admission_reads.resize(count);
      parent_reads.assign(count, nullptr); parent_roots.assign(count, std::nullopt);
      bool parallel_admission = gagp::host_thread_limit() > 1 && staged.size() >= 32 && certificates &&
          certificates->sources == packed.compiled_sources &&
          certificates->context.get() == &context &&
          !payload::StagedPayloads::has_active_scope();
      std::vector<const ProgramGenome*> bases(count, nullptr);
      if (parallel_admission) {
        try {
          for (int offset = 0; offset < count; ++offset) {
            if (staged_indices[offset] < 0) continue;
            const int index = i + offset;
            const auto parent_index = mutation ? index :
                (index % 2 ? view.parent_b[index / 2] : view.parent_a[index / 2]);
            bases[offset] = &parent(parent_index);
            if (certificates->analyses.size() == parents.size() &&
                certificates->metadata.size() == parents.size()) {
              const auto& row = certificates->analyses[parent_index];
              if (row.analysis && row.reads && row.reads->read_snapshot_unchanged()) {
                parent_reads[offset] = row.reads;
                parent_roots[offset] = row.analysis->witness.request.nonterminal;
              }
            }
          }
        } catch (const std::exception&) { parallel_admission = false; }
      }
      if (parallel_admission) {
        std::atomic<int> next{0};
        if (!team) team = std::make_unique<detail::BatchWorkers>(
            gagp::host_thread_limit());
        team->run([&] {
            grammar::VariationContext local(context.grammar_owner(), context.requests(),
                128, context.offspring_budget());
            for (;;) {
              const int offset = next.fetch_add(1, std::memory_order_relaxed);
              if (offset >= count) break;
              if (staged_indices[offset] < 0) continue;
              try {
                // Validate before entering the worker's own payload scope:
                // snapshots conservatively decline reuse inside any active scope.
                const auto root = parent_reads[offset] && parent_reads[offset]->read_snapshot_unchanged()
                    ? parent_roots[offset] : std::nullopt;
                admission_reads[offset] = std::make_shared<payload::StagedPayloads>();
                payload::StagedPayloads::Scope scope(*admission_reads[offset]);
                local.counters() = {};
                admitted[offset] = grammar::variation_detail::accept(
                    staged[staged_indices[offset]].ast, *bases[offset], local, root, admission_use);
                admission_counts[offset] = local.counters();
              } catch (const std::exception&) { admitted[offset].reset(); }
            }
        });
        std::vector<payload::StagedPayloads*> reads;
        for (const auto& read : admission_reads) if (read) reads.push_back(read.get());
        if (!payload::StagedPayloads::commit_all(reads))
          for (auto& child : admitted) child.reset();
      } else {
        try {
          // On 1T, ordered admission below owns the same cache used by the
          // next mutation preparation. Speculatively admitting into throwaway
          // worker contexts would discard that analysis and repeat it next pass.
          if (gagp::host_thread_limit() > 1 && staged.size() >= 32)
            context.cache().warm_candidates(staged, context.requests());
        } catch (const std::exception&) {}
      }
    }
    const auto& splice = view.child_splices[i];
    const int expected_parent = mutation ? i : (i % 2 ? view.parent_b[i / 2] : view.parent_a[i / 2]);
    require(splice.base_parent == expected_parent, "compiled child changed its selected base parent");
    const auto& base = parent(expected_parent);
    const auto outcome = splice.mutation_outcome;
    require(mutation || outcome == CompiledMutationOutcome::None,
            "compiled crossover published a mutation outcome");
    if (splice.applied) {
      if (mutation) {
        require(outcome == CompiledMutationOutcome::Subtree &&
                    splice.source_kind == SpliceSourceKind::CompiledDonor,
                "compiled mutation published inconsistent structural provenance");
      } else {
        require(splice.source_kind == SpliceSourceKind::Parent &&
                    splice.source_index == (i % 2 ? view.parent_a[i / 2] : view.parent_b[i / 2]) &&
                    splice.destination_candidate == (i % 2 ? view.cand_b[i / 2] : view.cand_a[i / 2]) &&
                    splice.source_candidate == (i % 2 ? view.cand_a[i / 2] : view.cand_b[i / 2]),
                "compiled crossover changed its selected source or candidate");
      }
    }
    if (mutation && outcome != CompiledMutationOutcome::None)
      ++context.counters().mutation_attempts;
    require(view.child_used_len[i] > 0 && view.child_meta[i].valid &&
                view.child_meta[i].node_count == view.child_used_len[i],
            "compiled child has invalid physical metadata");
    const int staged_index = staged_indices[i % analysis_batch];
    AstProgram ast;
    if (staged_index >= 0) ast = std::move(staged[staged_index].ast);
    else {
      ast = read_child(packed, view, i);
      reconstruct_compiled_child_metadata(ast, packed, splice);
    }
    const bool performed = splice.applied != 0 || outcome == CompiledMutationOutcome::Constant;
    ProgramGenome child;
    if (!performed) {
      // Device fallback/skip must actually contain the untouched source. Never
      // silently replace arbitrary device output with a host parent here.
      require(copied_tables_match(ast, packed.compiled_sources->parents[expected_parent]),
              "compiled device fallback did not copy its base parent");
      if (mutation && outcome == CompiledMutationOutcome::None) child = base;
      else {
        if (outcome == CompiledMutationOutcome::Capacity) ++context.counters().budget_rejections;
        if (outcome == CompiledMutationOutcome::Invalid) ++context.counters().contract_rejections;
        child = grammar::variation_detail::fallback(base, context);
      }
    } else {
      const auto offset = i % analysis_batch;
      if (admitted[offset] && admission_reads[offset]->read_snapshot_unchanged() &&
          (!parent_reads[offset] || parent_reads[offset]->read_snapshot_unchanged())) {
        child = std::move(*admitted[offset]);
        const auto& counts = admission_counts[offset];
        auto& total = context.counters();
        total.acceptance_rejections += counts.acceptance_rejections;
        total.budget_rejections += counts.budget_rejections;
        total.fallback_children += counts.fallback_children;
        total.unchanged_children += counts.unchanged_children;
        total.changed_children += counts.changed_children;
      } else {
        child = grammar::variation_detail::accept(std::move(ast), base, context, std::nullopt, admission_use);
      }
    }
    if (i < c.population_size) result.push_back(std::move(child));
  }
  return result;
}

}  // namespace gagp::evo::repro
