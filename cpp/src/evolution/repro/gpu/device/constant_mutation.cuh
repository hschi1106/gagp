#pragma once

#include <cstdint>

#include "gagp/core/value.hpp"
#include "gagp/evolution/ast_program.hpp"
#include "gagp/evolution/repro/constant_types.hpp"
#include "atomic_splice.cuh"
#include "grammar_random.cuh"
#include "pack_types.cuh"

namespace gagp::evo::repro {

enum class DConstantMutationResult : int {
  Applied = 0,
  NoGroups = 1,
  Invalid = 2,
  Capacity = 3,
};

struct DConstantMutationTableView {
  const ConstantMutationDomain* domains = nullptr;
  int domain_count = 0;
  const Value* values = nullptr;
  int value_count = 0;
  const ConstantMutationGroup* groups = nullptr;
  int group_count = 0;
  bool domains_validated = false;
};

struct DConstantMutationStreamView {
  const int* node_group_origins = nullptr;
  int node_count = 0;
};

// `indices` are roots in the input constant table. On success their compacted
// indices are written to `remapped_indices`. The arrays must not alias.
struct DConstantMutationPinnedRoots {
  const int* indices = nullptr;
  int* remapped_indices = nullptr;
  int count = 0;
};

namespace constant_mutation_detail {

struct LogicalGroup {
  int source_namespace = 0;
  int prepared_group = kNoConstantMutationGroup;
};

__device__ inline std::uint64_t float_bits(double value) {
  union {
    double floating;
    std::uint64_t bits;
  } converted;
  converted.floating = value;
  return converted.bits;
}

__device__ inline bool equal_value(const Value& a, const Value& b) {
  if (a.tag != b.tag) return false;
  if (a.tag == ValueTag::Invalid) return true;
  if (a.tag == ValueTag::Bool) return a.b == b.b;
  if (a.tag == ValueTag::Float) return float_bits(a.f) == float_bits(b.f);
  return a.i == b.i;
}

__host__ __device__ inline bool expected_tag(int type, ValueTag* tag) {
  switch (static_cast<RType>(type)) {
    case RType::Int: *tag = ValueTag::Int; return true;
    case RType::Float: *tag = ValueTag::Float; return true;
    case RType::Bool: *tag = ValueTag::Bool; return true;
    case RType::Char: *tag = ValueTag::Char; return true;
    case RType::String: *tag = ValueTag::String; return true;
    case RType::IntList: *tag = ValueTag::IntList; return true;
    case RType::FloatList: *tag = ValueTag::FloatList; return true;
    case RType::StringList: *tag = ValueTag::StringList; return true;
    default: return false;
  }
}

__device__ inline bool same_group(const LogicalGroup& a,
                                  const LogicalGroup& b) {
  return a.source_namespace == b.source_namespace &&
         a.prepared_group == b.prepared_group;
}

__device__ inline bool resolve_group(
    const AtomicSpliceOrigin& origin, DConstantMutationStreamView base,
    DConstantMutationStreamView source, LogicalGroup* result) {
  if (origin.original_index < 0 || origin.occurrence < -1 || result == nullptr)
    return false;
  const bool from_source = origin.occurrence >= 0;
  const DConstantMutationStreamView stream = from_source ? source : base;
  if (origin.original_index >= stream.node_count ||
      (stream.node_count > 0 && stream.node_group_origins == nullptr)) {
    return false;
  }
  result->source_namespace = from_source ? 1 : 0;
  result->prepared_group = stream.node_group_origins[origin.original_index];
  return result->prepared_group >= kNoConstantMutationGroup;
}

__device__ inline bool valid_domain(const ConstantMutationDomain& domain,
                                    DConstantMutationTableView table) {
  ValueTag expected = ValueTag::Invalid;
  if (!expected_tag(domain.type, &expected) ||
      (domain.integer_range != 0 && domain.integer_range != 1)) {
    return false;
  }
  if (domain.integer_range != 0)
    return expected == ValueTag::Int && domain.minimum <= domain.maximum;
  if (domain.value_offset < 0 || domain.value_count <= 0 ||
      domain.value_offset > table.value_count ||
      domain.value_count > table.value_count - domain.value_offset ||
      table.values == nullptr) {
    return false;
  }
  if (table.domains_validated) return true;
  for (int i = 0; i < domain.value_count; ++i) {
    if (table.values[domain.value_offset + i].tag != expected) return false;
  }
  return true;
}

__device__ inline int find_value(const Value* values, int count,
                                 const Value& value) {
  for (int i = 0; i < count; ++i) {
    if (equal_value(values[i], value)) return i;
  }
  return -1;
}

__device__ inline bool append_unique(Value* work, int capacity, int* count,
                                     const Value& value) {
  if (find_value(work, *count, value) >= 0) return true;
  if (*count >= capacity) return false;
  work[*count] = value;
  ++*count;
  return true;
}

}  // namespace constant_mutation_detail

// Serial device helper for a control thread. `constant_work` must not alias
// `child_consts`; it is scratch used to make all failure paths transactional.
// Source occurrence numbers intentionally do not participate in logical group
// identity, so repeated insertions of one source group mutate together.
__device__ inline DConstantMutationResult d_mutate_compiled_constants(
    DPlainNode* child_nodes, int child_node_count,
    const AtomicSpliceOrigin* child_origins, Value* child_consts,
    int* child_const_count, int child_const_capacity, Value* constant_work,
    DConstantMutationTableView table, DConstantMutationStreamView base,
    DConstantMutationStreamView source, DConstantMutationPinnedRoots pinned,
    std::uint64_t seed) {
  using namespace constant_mutation_detail;
  if (child_node_count < 0 || child_const_count == nullptr ||
      child_const_capacity < 0 || table.domain_count < 0 ||
      table.value_count < 0 || table.group_count < 0 || base.node_count < 0 ||
      source.node_count < 0 || pinned.count < 0 ||
      (child_node_count > 0 && (child_nodes == nullptr || child_origins == nullptr)) ||
      *child_const_count < 0 || *child_const_count > child_const_capacity ||
      (*child_const_count > 0 && child_consts == nullptr) ||
      (table.domain_count > 0 && table.domains == nullptr) ||
      (table.group_count > 0 && table.groups == nullptr) ||
      (pinned.count > 0 &&
       (pinned.indices == nullptr || pinned.remapped_indices == nullptr ||
        pinned.indices == pinned.remapped_indices))) {
    return DConstantMutationResult::Invalid;
  }

  int logical_group_count = 0;
  for (int i = 0; i < child_node_count; ++i) {
    LogicalGroup current;
    if (!resolve_group(child_origins[i], base, source, &current))
      return DConstantMutationResult::Invalid;
    if (static_cast<NodeKind>(child_nodes[i].kind) != NodeKind::CONST) {
      if (current.prepared_group != kNoConstantMutationGroup)
        return DConstantMutationResult::Invalid;
      continue;
    }
    if (child_nodes[i].i0 < 0 || child_nodes[i].i0 >= *child_const_count)
      return DConstantMutationResult::Invalid;
    if (current.prepared_group == kNoConstantMutationGroup) continue;
    if (current.prepared_group >= table.group_count)
      return DConstantMutationResult::Invalid;
    const int domain_index = table.groups[current.prepared_group].domain;
    if (domain_index < 0 || domain_index >= table.domain_count ||
        !valid_domain(table.domains[domain_index], table)) {
      return DConstantMutationResult::Invalid;
    }

    bool appeared = false;
    for (int j = 0; j < i; ++j) {
      if (static_cast<NodeKind>(child_nodes[j].kind) != NodeKind::CONST)
        continue;
      LogicalGroup previous;
      if (!resolve_group(child_origins[j], base, source, &previous))
        return DConstantMutationResult::Invalid;
      if (previous.prepared_group != kNoConstantMutationGroup &&
          same_group(current, previous)) {
        appeared = true;
        break;
      }
    }
    if (!appeared) ++logical_group_count;
  }

  for (int i = 0; i < pinned.count; ++i) {
    if (pinned.indices[i] < 0 || pinned.indices[i] >= *child_const_count)
      return DConstantMutationResult::Invalid;
  }
  if (logical_group_count == 0) return DConstantMutationResult::NoGroups;
  if (constant_work == nullptr || constant_work == child_consts)
    return DConstantMutationResult::Invalid;

  DGrammarRandom random(seed);
  const int selected_rank = static_cast<int>(
      random.bounded(static_cast<std::uint64_t>(logical_group_count)));
  LogicalGroup selected;
  int seen_groups = 0;
  bool found_selected = false;
  for (int i = 0; i < child_node_count && !found_selected; ++i) {
    if (static_cast<NodeKind>(child_nodes[i].kind) != NodeKind::CONST) continue;
    LogicalGroup current;
    if (!resolve_group(child_origins[i], base, source, &current) ||
        current.prepared_group == kNoConstantMutationGroup) {
      continue;
    }
    bool appeared = false;
    for (int j = 0; j < i; ++j) {
      if (static_cast<NodeKind>(child_nodes[j].kind) != NodeKind::CONST)
        continue;
      LogicalGroup previous;
      if (resolve_group(child_origins[j], base, source, &previous) &&
          previous.prepared_group != kNoConstantMutationGroup &&
          same_group(current, previous)) {
        appeared = true;
        break;
      }
    }
    if (!appeared) {
      if (seen_groups == selected_rank) {
        selected = current;
        found_selected = true;
      }
      ++seen_groups;
    }
  }
  if (!found_selected) return DConstantMutationResult::Invalid;

  const int domain_index = table.groups[selected.prepared_group].domain;
  const ConstantMutationDomain domain = table.domains[domain_index];
  Value sampled;
  if (domain.integer_range != 0) {
    sampled = Value::from_int(random.integer(domain.minimum, domain.maximum));
  } else {
    const int value_index = domain.value_offset + static_cast<int>(
        random.bounded(static_cast<std::uint64_t>(domain.value_count)));
    sampled = table.values[value_index];
  }

  int compact_count = 0;
  for (int i = 0; i < child_node_count; ++i) {
    if (static_cast<NodeKind>(child_nodes[i].kind) != NodeKind::CONST) continue;
    LogicalGroup current;
    if (!resolve_group(child_origins[i], base, source, &current))
      return DConstantMutationResult::Invalid;
    const Value desired = same_group(current, selected)
                              ? sampled
                              : child_consts[child_nodes[i].i0];
    if (!append_unique(constant_work, child_const_capacity, &compact_count,
                       desired)) {
      return DConstantMutationResult::Capacity;
    }
  }
  for (int i = 0; i < pinned.count; ++i) {
    if (!append_unique(constant_work, child_const_capacity, &compact_count,
                       child_consts[pinned.indices[i]])) {
      return DConstantMutationResult::Capacity;
    }
  }

  for (int i = 0; i < child_node_count; ++i) {
    if (static_cast<NodeKind>(child_nodes[i].kind) != NodeKind::CONST) continue;
    LogicalGroup current;
    (void)resolve_group(child_origins[i], base, source, &current);
    const Value desired = same_group(current, selected)
                              ? sampled
                              : child_consts[child_nodes[i].i0];
    child_nodes[i].i0 = find_value(constant_work, compact_count, desired);
  }
  for (int i = 0; i < pinned.count; ++i) {
    pinned.remapped_indices[i] =
        find_value(constant_work, compact_count, child_consts[pinned.indices[i]]);
  }
  for (int i = 0; i < compact_count; ++i) child_consts[i] = constant_work[i];
  *child_const_count = compact_count;
  return DConstantMutationResult::Applied;
}

}  // namespace gagp::evo::repro
