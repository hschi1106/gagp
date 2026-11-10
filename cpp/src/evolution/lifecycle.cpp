#include "gagp/evolution/lifecycle.hpp"

#include <stdexcept>

#include "gagp/runtime/payload/payload.hpp"

namespace gagp::evo {
namespace {

void append_payload_root(const Value& value, std::vector<Value>* roots) {
  if (value.tag == ValueTag::String || value.tag == ValueTag::IntList ||
      value.tag == ValueTag::FloatList || value.tag == ValueTag::StringList) {
    roots->push_back(value);
  }
}

void append_genome_roots(const ProgramGenome& genome, std::vector<Value>* roots) {
  for (const Value& value : genome.ast.consts) append_payload_root(value, roots);
}

void append_population_roots(const std::vector<ProgramGenome>& population,
                             std::vector<Value>* roots) {
  for (const ProgramGenome& genome : population) append_genome_roots(genome, roots);
}

void append_scored_roots(const std::vector<ScoredGenome>& scored,
                         std::vector<Value>* roots) {
  for (const ScoredGenome& one : scored) append_genome_roots(one.genome, roots);
}

}  // namespace

PayloadLifetimeManager::PayloadLifetimeManager(const std::vector<EvalCase>& cases) {
  for (const EvalCase& one_case : cases) {
    for (const auto& entry : one_case.inputs) append_payload_root(entry.second, &case_roots_);
    append_payload_root(one_case.expected, &case_roots_);
  }
}

void PayloadLifetimeManager::retain(
    const std::vector<ProgramGenome>& population,
    const std::vector<ScoredGenome>& history_best,
    const ScoredGenome* best,
    const std::vector<ScoredGenome>* final_population) const {
  std::vector<Value> roots = case_roots_;
  roots.reserve(case_roots_.size() + population.size() * 8U +
                history_best.size() * 8U + 16U);
  append_population_roots(population, &roots);
  append_scored_roots(history_best, &roots);
  if (best != nullptr) append_genome_roots(best->genome, &roots);
  if (final_population != nullptr) append_scored_roots(*final_population, &roots);
  gagp::payload::retain_only(roots);
}

}  // namespace gagp::evo
