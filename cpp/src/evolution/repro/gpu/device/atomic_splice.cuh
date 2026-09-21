#pragma once

#include <cstdint>

#include "pack_types.cuh"

namespace gagp::evo::repro {

struct AtomicSpliceOrigin {
  int original_index = -1;
  // -1 identifies a base node. Nonnegative values identify the destination
  // occurrence that received a copy of the source node.
  int occurrence = -1;
};

__device__ inline bool d_preflight_atomic_splice(
    int base_len, const CandidateOccurrence* occurrences, int occurrence_count,
    int source_len, int source_start, int source_stop, int output_capacity,
    int* output_len) {
  if (base_len < 0 || occurrences == nullptr || occurrence_count <= 0 ||
      source_len < 0 || source_start < 0 || source_stop <= source_start ||
      source_stop > source_len || output_capacity < 0 || output_len == nullptr) {
    return false;
  }

  const std::int64_t inserted =
      static_cast<std::int64_t>(source_stop) - source_start;
  std::int64_t computed_len = base_len;
  int previous_stop = 0;
  for (int i = 0; i < occurrence_count; ++i) {
    const CandidateOccurrence occurrence = occurrences[i];
    if (occurrence.start < previous_stop || occurrence.stop <= occurrence.start ||
        occurrence.stop > base_len) {
      return false;
    }
    computed_len += inserted -
        (static_cast<std::int64_t>(occurrence.stop) - occurrence.start);
    previous_stop = occurrence.stop;
  }
  if (computed_len < 0 || computed_len > output_capacity) {
    return false;
  }
  *output_len = static_cast<int>(computed_len);
  return true;
}

// Input streams and output storage must not alias. The helper is intentionally
// serial so a caller can run it from one control thread without synchronization.
__device__ inline bool d_atomic_splice(
    const DPlainNode* base, int base_len,
    const CandidateOccurrence* occurrences, int occurrence_count,
    const DPlainNode* source, int source_len, int source_start, int source_stop,
    DPlainNode* output, AtomicSpliceOrigin* origins, int output_capacity,
    int* output_len) {
  if (base == nullptr || source == nullptr || output == nullptr ||
      origins == nullptr || output_len == nullptr) {
    return false;
  }
  int checked_len = 0;
  if (!d_preflight_atomic_splice(
          base_len, occurrences, occurrence_count, source_len, source_start,
          source_stop, output_capacity, &checked_len)) {
    return false;
  }

  int base_cursor = 0;
  int output_cursor = 0;
  for (int occurrence_index = 0; occurrence_index < occurrence_count;
       ++occurrence_index) {
    const CandidateOccurrence occurrence = occurrences[occurrence_index];
    for (; base_cursor < occurrence.start; ++base_cursor, ++output_cursor) {
      output[output_cursor] = base[base_cursor];
      origins[output_cursor] = AtomicSpliceOrigin{base_cursor, -1};
    }
    for (int source_index = source_start; source_index < source_stop;
         ++source_index, ++output_cursor) {
      output[output_cursor] = source[source_index];
      origins[output_cursor] =
          AtomicSpliceOrigin{source_index, occurrence_index};
    }
    base_cursor = occurrence.stop;
  }
  for (; base_cursor < base_len; ++base_cursor, ++output_cursor) {
    output[output_cursor] = base[base_cursor];
    origins[output_cursor] = AtomicSpliceOrigin{base_cursor, -1};
  }
  *output_len = checked_len;
  return true;
}

}  // namespace gagp::evo::repro
