#pragma once
#include "gagp/evolution/grammar/membership.hpp"

namespace gagp::evo::grammar::detail {
// One synchronous read of a caller-owned genome. The key is computed here, never
// supplied as proof by a caller. Returned bytecode is immutable; no reference to
// the mutable genome is retained. This is executable reuse, not grammar admission.
struct EvaluationIdentity {
  std::string key;
  std::shared_ptr<const BytecodeProgram> executable;
};
EvaluationIdentity evaluation_identity(const ProgramGenome& genome,
    const std::vector<std::string>& input_names, std::uint32_t fuel);
}  // namespace gagp::evo::grammar::detail
