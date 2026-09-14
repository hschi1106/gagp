#pragma once

#include <cstdint>
#include <vector>

#include "gagp/evolution/grammar/frame.hpp"
#include "gagp/evolution/grammar/variation_contract.hpp"

namespace gagp::evo::grammar {

struct ContextualDonor {
  ProgramGenome genome;
  // Ordinary inputs/locals; lexical captures require project_frame with frame.
  std::vector<InputSpec> inputs;
  GenerationFrame frame;
  VariationSpan payload;
  std::uint32_t nodes = 0;
  std::uint32_t depth = 0;
  std::uint32_t template_nesting = 0;
};

ContextualDonor generate_donor(const CompiledGrammar& grammar, std::uint64_t seed,
    const VariationSite& site);

}  // namespace gagp::evo::grammar
