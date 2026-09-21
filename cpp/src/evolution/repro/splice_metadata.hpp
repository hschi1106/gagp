#pragma once

#include <cstddef>
#include <vector>

#include "gagp/evolution/ast_program.hpp"

namespace gagp::evo::repro {

struct PackedHostData;
struct PackedChildSplice;

struct SpliceOccurrence {
  std::size_t begin = 0;
  std::size_t end = 0;
  std::vector<int> binder_ids;
};

// Decode-only operation: child already contains the device-spliced node stream
// and its remapped name/constant tables. Rebuild sidecars and rename lexical IDs;
// never choose an operator or insert/delete/reorder nodes on the host.
// Empty occurrences with a zero donor range restore metadata for identity/constant
// mutation. Failure leaves child unchanged. Inputs must come from verified source ASTs.
void reconstruct_splice_metadata(
    AstProgram& child, const AstProgram& base, const AstProgram& donor,
    const std::vector<SpliceOccurrence>& occurrences,
    std::size_t donor_begin, std::size_t donor_end,
    const std::vector<int>& donor_binder_ids);

// Resolve only device-published provenance against the immutable prepared tables.
// Reject stale/out-of-contract descriptors before touching the child.
void reconstruct_compiled_child_metadata(
    AstProgram& child, const PackedHostData& packed, const PackedChildSplice& splice);

}  // namespace gagp::evo::repro
