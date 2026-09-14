#pragma once

#include <cstddef>
#include <cstdint>

#include "gagp/evolution/ast_program.hpp"

namespace gagp::evo {

using gagp::kBoundedRegionArityCapacity;
using gagp::kBoundedRegionPhaseBindingCapacity;
using gagp::bounded_region_arity;
using gagp::bounded_region_phase_kind;
using gagp::bounded_region_phase_type;
using gagp::bounded_region_preparation_ordinal;

const BoundedRegionSpec* lookup_bounded_region_spec(
    const AstProgram& ast, std::size_t owner) noexcept;

}  // namespace gagp::evo
