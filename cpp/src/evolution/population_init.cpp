#include "gagp/evolution/population_init.hpp"

#include <cstdint>
#include <stdexcept>
#include <unordered_map>

#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/genome_generation.hpp"
#include "gagp/evolution/grammar/compiled.hpp"
#include "gagp/evolution/grammar/request.hpp"

namespace gagp::evo {
namespace {

bool should_seed_for_expected_return_type(RType type) {
  return type == RType::String || type == RType::IntList ||
         type == RType::FloatList || type == RType::StringList;
}

}  // namespace

PopulationInitialization initialize_population(
    const grammar::CompiledGrammar& grammar,
    const CaseSet& case_set,
    int population_size,
    std::uint64_t seed) {
  return initialize_population(grammar, case_set, population_size, seed, grammar::entry_request(grammar));
}

PopulationInitialization initialize_population(
    const grammar::CompiledGrammar& grammar,
    const CaseSet& case_set,
    int population_size,
    std::uint64_t seed,
    const grammar::GenerationRequest& request) {
  if (population_size <= 0) {
    throw std::invalid_argument("population_size must be positive");
  }
  (void)grammar::validate_request(grammar, request);
  grammar.require_executable(request.nonterminal);
  std::unordered_map<std::string, RType> inputs;
  for (const auto& input : case_set.input_specs) {
    if (input.type == RType::Any || input.type == RType::Invalid ||
        !inputs.emplace(input.name, input.type).second) {
      throw std::invalid_argument("case input schema requires unique names and exact types");
    }
  }
  if (inputs.size() != grammar.inputs().size()) {
    throw std::invalid_argument("case input schema must match grammar inputs");
  }
  for (const auto& input : grammar.inputs()) {
    const auto found = inputs.find(input.name);
    if (found == inputs.end() || found->second != input.type) {
      throw std::invalid_argument("case input schema must match grammar inputs by name and exact type");
    }
  }
  if (case_set.expected_return_type != request.type) {
    throw std::invalid_argument("case expected_return_type must match requested nonterminal type");
  }
  PopulationInitialization out;
  out.population.reserve(static_cast<std::size_t>(population_size));
  for (int i = 0; i < population_size; ++i) {
    out.population.push_back(generate_random_genome(seed + static_cast<std::uint64_t>(i), grammar, request));
  }
  return out;
}

PopulationInitialization initialize_population(
    const EvolutionConfig& config,
    const CaseSet& case_set,
    const std::vector<ProgramGenome>* replay_population) {
  PopulationInitialization out;
  if (replay_population != nullptr) {
    out.population = *replay_population;
    out.replayed = true;
  } else {
    out.population.reserve(static_cast<std::size_t>(config.population_size));
    const bool seed_for_return_type =
        should_seed_for_expected_return_type(case_set.expected_return_type) &&
        config.grammar.allows_type(case_set.expected_return_type);
    for (int i = 0; i < config.population_size; ++i) {
      const std::uint64_t seed = config.seed + static_cast<std::uint64_t>(i);
      if (seed_for_return_type) {
        out.population.push_back(generate_random_genome_for_return_type(
            seed, case_set.expected_return_type, config.limits,
            case_set.input_specs, config.grammar));
      } else {
        out.population.push_back(generate_random_genome(
            seed, config.limits, case_set.input_specs, config.grammar));
      }
    }
  }
  if (static_cast<int>(out.population.size()) != config.population_size) {
    throw std::invalid_argument("initial_population size must match population_size");
  }
  return out;
}

}  // namespace gagp::evo
