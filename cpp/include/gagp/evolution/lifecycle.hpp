#pragma once

#include <cstdint>
#include <vector>

#include "gagp/core/value.hpp"
#include "gagp/evolution/case_set.hpp"
#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/selection.hpp"

namespace gagp::evo {

class PayloadLifetimeManager {
 public:
  explicit PayloadLifetimeManager(const std::vector<EvalCase>& cases);

  void retain(const std::vector<ProgramGenome>& population,
              const std::vector<ScoredGenome>& history_best,
              const ScoredGenome* best = nullptr,
              const std::vector<ScoredGenome>* final_population = nullptr) const;

 private:
  std::vector<Value> case_roots_;
};

}  // namespace gagp::evo
