#pragma once

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "gagp/core/bytecode.hpp"

namespace gagp::evo::transition {

// Exact type information absent from the legacy structured bytecode tables.
// Entries correspond positionally to the respective legacy segment vectors.
struct BoundedBytecodeHints {
  std::vector<std::pair<ValueTag, ValueTag>> dc;
  std::vector<ValueTag> dp1d;
  std::vector<ValueTag> dp2d;
};

struct BoundedBytecodeProfile {
  std::uint32_t dc_frames =
      static_cast<std::uint32_t>(std::numeric_limits<int>::max());
  std::uint32_t dp_frames =
      static_cast<std::uint32_t>(std::numeric_limits<int>::max());
  std::uint32_t dp_cells =
      static_cast<std::uint32_t>(std::numeric_limits<int>::max());
};

// Migration-oracle adapter for decoded legacy bytecode. Existing bounded
// segments are retained; converted DC/DP segments are appended in legacy-table
// order and all root control-flow targets are remapped after expansion.
// Out-of-domain legacy base predicates are retained: boundary wins first. All
// other source structure and the complete lowered descriptor are verified.
BytecodeProgram lower_bounded_bytecode(
    const BytecodeProgram& source, const BoundedBytecodeHints& hints,
    const BoundedBytecodeProfile& profile = {});

}  // namespace gagp::evo::transition
