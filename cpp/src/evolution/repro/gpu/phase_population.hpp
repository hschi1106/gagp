#pragma once
#include <memory>
#include <vector>
#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/grammar/compiled.hpp"
#include "gagp/evolution/repro/stats.hpp"
namespace gagp { class FitnessSessionGpu; struct FitnessEvalResult; }
namespace gagp::evo::repro {
// Private run owner for the experimental native GPU genotype. Import performs
// complete admission; no mutable program, device pointer or certificate escapes.
// CPU code only transports/exports artifacts. Selection and variation run on GPU.
class NativePhasePopulation final {
 public:
  NativePhasePopulation(std::shared_ptr<const grammar::CompiledGrammar> grammar,
      const std::vector<ProgramGenome>& input, const std::vector<std::string>& input_names);
  ~NativePhasePopulation();
  NativePhasePopulation(const NativePhasePopulation&) = delete;
  NativePhasePopulation& operator=(const NativePhasePopulation&) = delete;
  const std::vector<BytecodeProgram>& programs() const;
  ReproductionStats reproduce(const std::vector<double>& completed_fitness,
      int tournament_size, double mutation_probability, std::uint64_t seed);
  ProgramGenome export_member(std::size_t index) const;
  std::size_t device_bytes() const;
  FitnessEvalResult evaluate(FitnessSessionGpu& session, bool diagnostics) const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace gagp::evo::repro
