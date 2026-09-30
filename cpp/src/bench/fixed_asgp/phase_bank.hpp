#pragma once
#include "adapter.hpp"
#include "gagp/evolution/evolve.hpp"
namespace fixed_asgp {
// Experimental finite phase-bank profile. No task identifiers or answers enter
// capability detection or variation. Unsupported grammar structures throw.
void phase_bank_probe(const std::vector<gagp::evo::ProgramGenome>& population,
    const std::vector<gagp::evo::EvalCase>& cases,
    const std::shared_ptr<const gg::CompiledGrammar>& grammar, const std::string& output);
}
