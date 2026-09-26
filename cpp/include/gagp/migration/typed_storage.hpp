#pragma once

#include "gagp/evolution/ast_program.hpp"
#include "gagp/evolution/input_spec.hpp"

namespace gagp::migration {

struct TypedStorageNormalization {
  evo::AstProgram ast;
  std::size_t renamed_uses = 0;
};

// Offline mapping only. Splits ordinary names by their verified source type,
// after proving that every affected read has that one reaching definition type.
// Branches and zero-or-more loop iterations are included in the proof. Rejects
// an unproven split; never falls back to sampled execution as an equivalence test.
// Preserves node count, prefix shape, constants, lexical binders and fuel charges.
// Name/local counts may grow; callers must still enforce target storage capacities.
// canonical_names additionally gives every non-input binding a name derived from
// its original spelling and exact type, consistent across a mixed population.
// A collision with an existing source name is rejected in this mode.
TypedStorageNormalization normalize_typed_storage(const evo::AstProgram& ast,
    const std::vector<evo::InputSpec>& inputs, bool canonical_names = false);

}  // namespace gagp::migration
