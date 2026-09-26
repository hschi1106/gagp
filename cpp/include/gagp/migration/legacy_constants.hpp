#pragma once

#include <cstddef>
#include <limits>
#include <vector>

#include "gagp/evolution/genome.hpp"
#include "gagp/migration/legacy_ast_v1.hpp"

namespace gagp::migration {

inline constexpr std::size_t kNoLegacyConstant = std::numeric_limits<std::size_t>::max();

struct LegacyConstantSite {
  std::size_t source_node = 0;
  std::size_t target_node = 0;
};

struct LegacyConstantProjection {
  evo::ProgramGenome genome;
  // Source prefix order, including selected no-op constants. Different source
  // nodes sharing a value-table entry remain distinct candidates.
  std::vector<LegacyConstantSite> sites;
  // Source node ID for each lowered node; administrative constants and all
  // non-constants carry kNoLegacyConstant. IDs do not follow target emission order.
  std::vector<std::size_t> source_by_target;
};

// Offline mapping boundary. Produces the lowered program itself and checks its
// constant sites against the independent expansion layout. This is not a grammar
// derivation certificate and does not authorize wider production search limits.
LegacyConstantProjection lower_with_constant_origins(const legacy_v1::AstProgram& source,
    const std::vector<evo::InputSpec>& inputs = {});

}  // namespace gagp::migration
