#include "gagp/evolution/bounded_region.hpp"

#include <stdexcept>

namespace gagp::evo {
const BoundedRegionSpec* lookup_bounded_region_spec(
    const AstProgram& ast, std::size_t owner) noexcept {
  for (const BoundedRegionSpec& spec : ast.bounded_region_specs) {
    if (spec.node_index == owner) return &spec;
  }
  return nullptr;
}

}  // namespace gagp::evo
