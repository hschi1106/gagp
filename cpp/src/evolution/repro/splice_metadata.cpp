#include "splice_metadata.hpp"

#include "gagp/evolution/repro/types.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>

namespace gagp::evo::repro {
namespace {

int table_index(std::size_t index) {
  if (index > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    throw std::overflow_error("splice metadata table index overflow");
  return static_cast<int>(index);
}

bool same_constant(const Value& a, const Value& b) {
  if (a.tag != b.tag) return false;
  if (a.tag == ValueTag::Bool) return a.b == b.b;
  if (a.tag == ValueTag::Float) return std::memcmp(&a.f, &b.f, sizeof(double)) == 0;
  return a.i == b.i;
}

template <class Position, class Rename>
void append_metadata(AstProgram& out, const AstProgram& source,
                     Position position, Rename rename) {
  const auto name = [&](int index) {
    const auto& value = source.names.at(static_cast<std::size_t>(index));
    const auto found = std::find(out.names.begin(), out.names.end(), value);
    if (found != out.names.end()) return table_index(found - out.names.begin());
    const int result = table_index(out.names.size());
    out.names.push_back(value);
    return result;
  };
  const auto constant = [&](int index) {
    const auto value = source.consts.at(static_cast<std::size_t>(index));
    for (std::size_t i = 0; i < out.consts.size(); ++i)
      if (same_constant(out.consts[i], value)) return table_index(i);
    const int result = table_index(out.consts.size());
    out.consts.push_back(value);
    return result;
  };
  const auto copy = [&](const auto& entries, auto& target, auto patch) {
    for (const auto& entry : entries) {
      const auto relocated = position(entry.node_index);
      if (!relocated) continue;
      auto result = entry;
      result.node_index = *relocated;
      patch(result);
      target.push_back(std::move(result));
    }
  };
  copy(source.linear_rec_binders, out.linear_rec_binders, [&](auto& x) {
    x.elem_name = name(x.elem_name); x.accum_name = name(x.accum_name);
    x.index_name = name(x.index_name);
  });
  copy(source.asgp_dc_binders, out.asgp_dc_binders, [&](auto& x) {
    x.solve_xs_name = name(x.solve_xs_name); x.solve_n_name = name(x.solve_n_name);
    x.solve_lo_name = name(x.solve_lo_name); x.divide_n_name = name(x.divide_n_name);
    x.combine_left_name = name(x.combine_left_name);
    x.combine_right_name = name(x.combine_right_name);
  });
  copy(source.asgp_dp1d_specs, out.asgp_dp1d_specs, [&](auto& x) {
    x.boundary_const = constant(x.boundary_const);
    x.solve_state_name = name(x.solve_state_name);
    x.transition_state_name = name(x.transition_state_name);
    for (auto& id : x.transition_dep_names) id = name(id);
  });
  copy(source.asgp_dp2d_specs, out.asgp_dp2d_specs, [&](auto& x) {
    x.boundary_const = constant(x.boundary_const);
    x.solve_i_name = name(x.solve_i_name); x.solve_j_name = name(x.solve_j_name);
    x.transition_i_name = name(x.transition_i_name);
    x.transition_j_name = name(x.transition_j_name);
    for (auto& id : x.transition_dep_names) id = name(id);
  });
  copy(source.lexical_regions, out.lexical_regions, [&](auto& x) {
    for (auto& binding : x.bindings) binding.id = rename(binding.id);
  });
  copy(source.bounded_region_specs, out.bounded_region_specs, [&](auto& x) {
    for (auto& phase : x.phases)
      for (auto& binding : phase.bindings) binding.binder_id = rename(binding.binder_id);
    for (auto& capture : x.parameters)
      capture.index = capture.kind == RegionCaptureKind::Name
                          ? name(capture.index) : rename(capture.index);
  });
  copy(source.traversal_specs, out.traversal_specs, [](auto&) {});
  copy(source.fuel_specs, out.fuel_specs, [](auto&) {});
}

template <class Visitor>
void visit_declarations(const AstProgram& source, std::size_t begin,
                        std::size_t end, Visitor visit) {
  for (const auto& region : source.lexical_regions)
    if (region.node_index >= begin && region.node_index < end)
      for (const auto& binding : region.bindings) visit(binding.id);
  for (const auto& region : source.bounded_region_specs)
    if (region.node_index >= begin && region.node_index < end)
      for (const auto& phase : region.phases)
        for (const auto& binding : phase.bindings) visit(binding.binder_id);
}

}  // namespace

void reconstruct_splice_metadata(
    AstProgram& child, const AstProgram& base, const AstProgram& donor,
    const std::vector<SpliceOccurrence>& occurrences,
    std::size_t donor_begin, std::size_t donor_end,
    const std::vector<int>& donor_binder_ids) {
  if (occurrences.empty()) {
    if (donor_begin != 0 || donor_end != 0 || !donor_binder_ids.empty())
      throw std::invalid_argument("invalid identity metadata source range");
  } else if (donor_begin >= donor_end || donor_end > donor.nodes.size()) {
    throw std::invalid_argument("invalid splice metadata source range");
  }
  const std::size_t inserted = donor_end - donor_begin;
  const std::size_t absent = std::numeric_limits<std::size_t>::max();
  std::vector<std::size_t> base_positions(base.nodes.size(), absent);
  std::vector<std::size_t> starts;
  std::size_t cursor = 0, produced = 0;
  for (const auto& occurrence : occurrences) {
    if (occurrence.begin < cursor || occurrence.end <= occurrence.begin ||
        occurrence.end > base.nodes.size() ||
        occurrence.binder_ids.size() != donor_binder_ids.size())
      throw std::invalid_argument("invalid splice metadata occurrence");
    while (cursor < occurrence.begin) base_positions[cursor++] = produced++;
    if (produced > child.nodes.size() || inserted > child.nodes.size() - produced)
      throw std::invalid_argument("splice metadata output length mismatch");
    starts.push_back(produced);
    produced += inserted;
    cursor = occurrence.end;
  }
  while (cursor < base.nodes.size()) base_positions[cursor++] = produced++;
  if (produced != child.nodes.size())
    throw std::invalid_argument("splice metadata output length mismatch");

  // Build transactionally; the node stream's shape is already fixed by device
  // provenance. Only REGION_VAR references below may change their binder IDs.
  AstProgram out;
  out.version = child.version;
  out.nodes = child.nodes;
  out.names = child.names;
  out.consts = child.consts;
  std::set<int> used;
  visit_declarations(base, 0, base.nodes.size(), [&](int id) { used.insert(id); });
  for (const auto& node : base.nodes)
    if (node.kind == NodeKind::REGION_VAR) used.insert(node.i0);
  for (const auto& occurrence : occurrences)
    used.insert(occurrence.binder_ids.begin(), occurrence.binder_ids.end());
  for (std::size_t i = 0; i < base_positions.size(); ++i) {
    if (base_positions[i] == absent) continue;
    if (out.nodes.at(base_positions[i]).kind != base.nodes[i].kind)
      throw std::invalid_argument("splice metadata base provenance mismatch");
  }
  append_metadata(out, base, [&](std::size_t index) -> std::optional<std::size_t> {
    const auto result = base_positions.at(index);
    if (result == absent) return std::nullopt;
    return result;
  }, [](int id) { return id; });

  int fresh = 0;
  for (std::size_t i = 0; i < occurrences.size(); ++i) {
    std::map<int, int> introduced, captures;
    for (std::size_t j = 0; j < donor_binder_ids.size(); ++j) {
      const auto entry = captures.emplace(donor_binder_ids[j], occurrences[i].binder_ids[j]);
      if (!entry.second && entry.first->second != occurrences[i].binder_ids[j])
        throw std::invalid_argument("ambiguous splice metadata capture mapping");
    }
    visit_declarations(donor, donor_begin, donor_end, [&](int id) {
      if (introduced.count(id)) return;
      while (fresh < std::numeric_limits<int>::max() && used.count(fresh)) ++fresh;
      if (fresh == std::numeric_limits<int>::max())
        throw std::overflow_error("splice metadata exhausted binder IDs");
      introduced.emplace(id, fresh);
      used.insert(fresh);
    });
    const auto rename = [&](int id) {
      const auto local = introduced.find(id);
      if (local != introduced.end()) return local->second;
      const auto capture = captures.find(id);
      if (capture == captures.end() || capture->second < 0)
        throw std::invalid_argument("unmapped splice metadata lexical capture");
      return capture->second;
    };
    for (std::size_t j = donor_begin; j < donor_end; ++j) {
      auto& node = out.nodes.at(starts[i] + j - donor_begin);
      if (node.kind != donor.nodes[j].kind)
        throw std::invalid_argument("splice metadata donor provenance mismatch");
      if (node.kind == NodeKind::REGION_VAR) node.i0 = rename(donor.nodes[j].i0);
    }
    append_metadata(out, donor, [&](std::size_t index) -> std::optional<std::size_t> {
      if (index < donor_begin || index >= donor_end) return std::nullopt;
      return starts[i] + index - donor_begin;
    }, rename);
  }
  const auto sort = [](auto& entries) {
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
      return a.node_index < b.node_index;
    });
  };
  sort(out.linear_rec_binders); sort(out.asgp_dc_binders);
  sort(out.asgp_dp1d_specs); sort(out.asgp_dp2d_specs);
  sort(out.lexical_regions); sort(out.bounded_region_specs);
  sort(out.traversal_specs); sort(out.fuel_specs);
  child = std::move(out);
}

void reconstruct_compiled_child_metadata(
    AstProgram& child, const PackedHostData& packed, const PackedChildSplice& splice) {
  const auto fail = []() { throw std::invalid_argument("invalid compiled child splice provenance"); };
  if (packed.config.contract_mode != ReproductionContractMode::CompiledGrammar ||
      !packed.compiled_sources || !packed.compiled_grammar ||
      (splice.applied != 0 && splice.applied != 1) || splice.base_parent < 0 ||
      static_cast<std::size_t>(splice.base_parent) >= packed.compiled_sources->parents.size() ||
      packed.config.candidates_per_program <= 0)
    fail();
  const int outcome = static_cast<int>(splice.mutation_outcome);
  if (outcome < static_cast<int>(CompiledMutationOutcome::None) ||
      outcome > static_cast<int>(CompiledMutationOutcome::NoDonor)) fail();
  if (splice.applied == 0) {
    if (splice.source_kind != SpliceSourceKind::None || splice.source_index != -1 ||
        splice.source_candidate != -1 || splice.source_begin != 0 || splice.source_end != 0 ||
        splice.occurrence_count != 0 || splice.mutation_outcome == CompiledMutationOutcome::Subtree)
      fail();
    // Constant mutation changes table references only. Restore all source
    // sidecars against the device's compacted tables without changing topology.
    reconstruct_splice_metadata(child, packed.compiled_sources->parents[splice.base_parent],
                               AstProgram{}, {}, 0, 0, {});
    return;
  }
  if (splice.mutation_outcome != CompiledMutationOutcome::None &&
      splice.mutation_outcome != CompiledMutationOutcome::Subtree) fail();
  const auto candidate = [&](int parent, int index) -> const CandidateRange& {
    if (parent < 0 || static_cast<std::size_t>(parent) >= packed.metas.size() ||
        index < 0 || index >= packed.metas[parent].candidate_count ||
        index >= packed.config.candidates_per_program)
      fail();
    const auto offset = static_cast<std::size_t>(parent) *
        static_cast<std::size_t>(packed.config.candidates_per_program) + index;
    if (offset >= packed.candidates.size()) fail();
    return packed.candidates[offset];
  };
  const auto binder_slice = [&](const std::vector<int>& values, int offset, int count) {
    if (offset < 0 || count < 0 || static_cast<std::size_t>(offset) > values.size() ||
        static_cast<std::size_t>(count) > values.size() - static_cast<std::size_t>(offset))
      fail();
    return std::vector<int>(values.begin() + offset, values.begin() + offset + count);
  };
  const auto& destination = candidate(splice.base_parent, splice.destination_candidate);
  if (destination.compatibility_id == kNoCompatibilityId ||
      destination.compatibility_id >= packed.compatibility_keys.size() ||
      destination.occurrence_count <= 0 || destination.occurrence_offset < 0 ||
      splice.occurrence_count != destination.occurrence_count ||
      static_cast<std::size_t>(destination.occurrence_offset) > packed.occurrences.size() ||
      static_cast<std::size_t>(destination.occurrence_count) >
          packed.occurrences.size() - static_cast<std::size_t>(destination.occurrence_offset))
    fail();
  std::vector<SpliceOccurrence> occurrences;
  for (int i = 0; i < destination.occurrence_count; ++i) {
    const auto& occurrence = packed.occurrences[destination.occurrence_offset + i];
    if (occurrence.start < 0 || occurrence.stop <= occurrence.start) fail();
    occurrences.push_back(SpliceOccurrence{static_cast<std::size_t>(occurrence.start),
        static_cast<std::size_t>(occurrence.stop),
        binder_slice(packed.occurrence_binder_ids, occurrence.binder_offset, occurrence.binder_count)});
  }
  const AstProgram* source = nullptr;
  std::vector<int> formals;
  int nodes = 0, depth = 0, nesting = 0;
  std::uint32_t compatibility = kNoCompatibilityId;
  if (splice.source_kind == SpliceSourceKind::Parent) {
    if (splice.source_index < 0 || static_cast<std::size_t>(splice.source_index) >=
        packed.compiled_sources->parents.size()) fail();
    const auto& contract = candidate(splice.source_index, splice.source_candidate);
    if (contract.occurrence_count <= 0 || contract.occurrence_offset < 0 ||
        static_cast<std::size_t>(contract.occurrence_offset) >= packed.occurrences.size() ||
        splice.source_begin != contract.start || splice.source_end != contract.stop)
      fail();
    const auto& occurrence = packed.occurrences[contract.occurrence_offset];
    if (occurrence.start != contract.start || occurrence.stop != contract.stop) fail();
    formals = binder_slice(packed.occurrence_binder_ids,
                           occurrence.binder_offset, occurrence.binder_count);
    source = &packed.compiled_sources->parents[splice.source_index];
    compatibility = contract.compatibility_id;
    nodes = contract.materialized_nodes; depth = contract.materialized_depth;
    nesting = contract.template_nesting;
  } else if (splice.source_kind == SpliceSourceKind::CompiledDonor) {
    if (splice.source_index < 0 || splice.source_candidate != -1 ||
        static_cast<std::size_t>(splice.source_index) >= packed.compiled_sources->donors.size() ||
        static_cast<std::size_t>(splice.source_index) >= packed.donor_contracts.size() ||
        destination.donor_offset < 0 || splice.source_index < destination.donor_offset ||
        splice.source_index - destination.donor_offset >= destination.donor_count)
      fail();
    source = &packed.compiled_sources->donors[splice.source_index];
    if (splice.source_begin != 0 || splice.source_end < 0 ||
        static_cast<std::size_t>(splice.source_end) != source->nodes.size()) fail();
    const auto& contract = packed.donor_contracts[splice.source_index];
    formals = binder_slice(packed.donor_binder_ids, contract.binder_offset, contract.binder_count);
    compatibility = contract.compatibility_id;
    nodes = contract.materialized_nodes; depth = contract.materialized_depth;
    nesting = contract.template_nesting;
  } else {
    fail();
  }
  if (!source || compatibility != destination.compatibility_id || nodes <= 0 || depth <= 0 ||
      nesting < 0 || nodes > destination.replacement_max_nodes ||
      depth > destination.replacement_max_depth || nesting > destination.remaining_template_nesting ||
      splice.source_begin < 0 || splice.source_end <= splice.source_begin ||
      splice.source_end - splice.source_begin != nodes)
    fail();
  reconstruct_splice_metadata(child, packed.compiled_sources->parents[splice.base_parent],
      *source, occurrences, static_cast<std::size_t>(splice.source_begin),
      static_cast<std::size_t>(splice.source_end), formals);
}

}  // namespace gagp::evo::repro
