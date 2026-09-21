#pragma once

#include <cstdint>

#include "atomic_splice.cuh"

namespace gagp::evo::repro {

__device__ inline bool d_compiled_node_uses_name(NodeKind kind) {
  return kind == NodeKind::VAR || kind == NodeKind::BOUND_VAR ||
         kind == NodeKind::ASSIGN || kind == NodeKind::FOR_RANGE;
}

__device__ inline std::uint64_t d_float_bits(double value) {
  union {
    double floating;
    std::uint64_t bits;
  } converted;
  converted.floating = value;
  return converted.bits;
}

__device__ inline bool d_compiled_const_equal(const Value& left,
                                               const Value& right) {
  if (left.tag != right.tag) return false;
  if (left.tag == ValueTag::Float)
    return d_float_bits(left.f) == d_float_bits(right.f);
  if (left.tag == ValueTag::Bool) return left.b == right.b;
  return left.i == right.i;
}

__device__ inline int d_find_or_append_compiled_name(
    std::uint64_t value, std::uint64_t* child_names, int* child_name_count,
    int name_capacity) {
  for (int i = 0; i < *child_name_count; ++i)
    if (child_names[i] == value) return i;
  if (*child_name_count >= name_capacity) return -1;
  const int result = *child_name_count;
  child_names[result] = value;
  *child_name_count += 1;
  return result;
}

__device__ inline int d_find_or_append_compiled_const(
    const Value& value, Value* child_consts, int* child_const_count,
    int const_capacity) {
  for (int i = 0; i < *child_const_count; ++i)
    if (d_compiled_const_equal(child_consts[i], value)) return i;
  if (*child_const_count >= const_capacity) return -1;
  const int result = *child_const_count;
  child_consts[result] = value;
  *child_const_count += 1;
  return result;
}

// Caller-owned work buffers may contain partial data after failure and must be
// discarded. Published lengths and provenance change only after full success.
__device__ inline bool d_prepare_compiled_splice(
    const DPlainNode* base_nodes, int base_len,
    const std::uint64_t* base_name_ids, int base_name_count,
    const Value* base_consts, int base_const_count,
    const CandidateOccurrence* occurrences, int occurrence_count,
    const DPlainNode* source_nodes, int source_len, int source_begin,
    int source_end, const std::uint64_t* source_name_ids,
    int source_name_count, const Value* source_consts, int source_const_count,
    DPlainNode* child_nodes, AtomicSpliceOrigin* child_origins,
    int node_capacity, std::uint64_t* child_name_ids, int name_capacity,
    Value* child_consts, int const_capacity, int* child_len,
    int* child_name_count, int* child_const_count,
    PackedChildSplice descriptor, PackedChildSplice* child_splice) {
  if (base_nodes == nullptr || (base_name_count > 0 && base_name_ids == nullptr) ||
      (base_const_count > 0 && base_consts == nullptr) || source_nodes == nullptr ||
      (source_name_count > 0 && source_name_ids == nullptr) ||
      (source_const_count > 0 && source_consts == nullptr) ||
      child_nodes == nullptr || child_origins == nullptr ||
      (name_capacity > 0 && child_name_ids == nullptr) ||
      (const_capacity > 0 && child_consts == nullptr) ||
      child_len == nullptr || child_name_count == nullptr ||
      child_const_count == nullptr || child_splice == nullptr ||
      base_name_count < 0 || base_name_count > name_capacity ||
      source_name_count < 0 || base_const_count < 0 ||
      base_const_count > const_capacity || source_const_count < 0 ||
      node_capacity < 0 || name_capacity < 0 || const_capacity < 0) {
    return false;
  }

  int work_len = 0;
  if (!d_atomic_splice(base_nodes, base_len, occurrences, occurrence_count,
                       source_nodes, source_len, source_begin, source_end,
                       child_nodes, child_origins, node_capacity, &work_len)) {
    return false;
  }

  int work_name_count = base_name_count;
  for (int i = 0; i < base_name_count; ++i) child_name_ids[i] = base_name_ids[i];
  int work_const_count = base_const_count;
  for (int i = 0; i < base_const_count; ++i) child_consts[i] = base_consts[i];

  for (int i = 0; i < work_len; ++i) {
    DPlainNode& node = child_nodes[i];
    const AtomicSpliceOrigin origin = child_origins[i];
    const bool from_source = origin.occurrence >= 0;
    const int source_index = node.i0;
    const NodeKind kind = static_cast<NodeKind>(node.kind);
    if (kind == NodeKind::CONST) {
      const int available = from_source ? source_const_count : base_const_count;
      if (source_index < 0 || source_index >= available) return false;
      if (from_source) {
        const int mapped = d_find_or_append_compiled_const(
            source_consts[source_index], child_consts, &work_const_count,
            const_capacity);
        if (mapped < 0) return false;
        node.i0 = mapped;
      }
    } else if (d_compiled_node_uses_name(kind)) {
      const int available = from_source ? source_name_count : base_name_count;
      if (source_index < 0 || source_index >= available) return false;
      if (from_source) {
        const int mapped = d_find_or_append_compiled_name(
            source_name_ids[source_index], child_name_ids, &work_name_count,
            name_capacity);
        if (mapped < 0) return false;
        node.i0 = mapped;
      }
    }
    // REGION_VAR carries a lexical binder ID rather than a table index. Host
    // metadata reconstruction remaps it for each physical occurrence.
  }

  descriptor.applied = 1;
  descriptor.occurrence_count = occurrence_count;
  *child_len = work_len;
  *child_name_count = work_name_count;
  *child_const_count = work_const_count;
  *child_splice = descriptor;
  return true;
}

}  // namespace gagp::evo::repro
