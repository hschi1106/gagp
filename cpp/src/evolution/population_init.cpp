#include "gagp/evolution/population_init.hpp"

#include <cstdint>
#include <stdexcept>
#include <unordered_map>

#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/genome_generation.hpp"
#include "gagp/evolution/grammar/compiled.hpp"
#include "gagp/evolution/grammar/request.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "grammar/cold_admission.hpp"

namespace gagp::evo {
namespace {
void validate_grammar_inputs(const grammar::CompiledGrammar& grammar, const CaseSet& case_set) {
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
}
}  // namespace

std::vector<grammar::GenerationRequest> population_requests(const EvolutionConfig& config) {
  if (!config.generation_request)
    throw std::invalid_argument("evolution requires an explicit generation request");
  return {*config.generation_request};
}

void validate_grammar_case_set(const EvolutionConfig& config, const CaseSet& case_set) {
  if (!config.compiled_grammar || !config.generation_request)
    throw std::invalid_argument("evolution case validation requires a grammar and request");
  validate_grammar_case_set(*config.compiled_grammar, case_set, *config.generation_request);
}

void validate_grammar_case_set(const grammar::CompiledGrammar& grammar,
    const CaseSet& case_set, const grammar::GenerationRequest& request) {
  (void)grammar::validate_request(grammar, request);
  grammar.require_executable(request.nonterminal);
  validate_grammar_inputs(grammar, case_set);
  if (case_set.expected_return_type != request.type) {
    throw std::invalid_argument("case expected_return_type must match requested nonterminal type");
  }
}

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
  validate_grammar_case_set(grammar, case_set, request);
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
  if (config.population_size <= 0)
    throw std::invalid_argument("population_size must be positive");
  repro::require_reproduction_mode_supported(config);
  const auto& grammar = *config.compiled_grammar;
  const auto& request = *config.generation_request;
  validate_grammar_case_set(config, case_set);
  if (!replay_population) {
    out.population.reserve(static_cast<std::size_t>(config.population_size));
    for (int i = 0; i < config.population_size; ++i)
      out.population.push_back(generate_random_genome(config.seed + static_cast<std::uint64_t>(i), grammar, request));
    return out;
  }
  if (replay_population->size() != static_cast<std::size_t>(config.population_size))
    throw std::invalid_argument("initial_population size must match population_size");
  out.population = *replay_population;
  out.replayed = true;
  for (auto& genome : out.population) {
    auto witness = grammar::detail::reconstruct_cold_derivation(grammar, genome, request);
    genome.meta = build_genome_meta(genome.ast);
    genome.derivation = std::make_shared<const grammar::DerivationMetadata>(std::move(witness));
  }
  return out;
}

}  // namespace gagp::evo
