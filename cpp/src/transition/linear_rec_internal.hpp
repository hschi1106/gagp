#pragma once

#include "gagp/evolution/input_spec.hpp"
#include "gagp/migration/legacy_ast_v1.hpp"

namespace gagp::evo::transition::detail {

migration::legacy_v1::AstProgram lower_linear_rec_legacy_ast(
    const migration::legacy_v1::AstProgram& source,
    const std::vector<InputSpec>& inputs);

}  // namespace gagp::evo::transition::detail
