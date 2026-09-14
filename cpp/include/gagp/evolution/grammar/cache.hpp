#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "gagp/evolution/genome.hpp"

namespace gagp::evo::grammar {

// Materialized runtime identity, independent of grammar search/provenance.
// Requires exact constant payloads and the generated native AST subset.
std::string runtime_cache_identity(const ProgramGenome& genome,
                                   const std::vector<std::string>& input_names,
                                   std::uint32_t fuel);

}  // namespace gagp::evo::grammar
