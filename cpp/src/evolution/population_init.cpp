#include "gagp/evolution/population_init.hpp"

#include <cstdint>
#include <stdexcept>
#include <unordered_map>

#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/genome_generation.hpp"
#include "gagp/evolution/grammar/compiled.hpp"
#include "gagp/evolution/grammar/request.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/grammar/variation.hpp"

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

RType exact_expected_type(const Value& value) {
  switch (value.tag) {
    case ValueTag::Int: return RType::Int;
    case ValueTag::Float: return RType::Float;
    case ValueTag::Bool: return RType::Bool;
    case ValueTag::Char: return RType::Char;
    case ValueTag::String: return RType::String;
    case ValueTag::IntList: return RType::IntList;
    case ValueTag::FloatList: return RType::FloatList;
    case ValueTag::StringList: return RType::StringList;
    default: return RType::Invalid;
  }
}
}  // namespace

std::vector<grammar::GenerationRequest> population_requests(const EvolutionConfig& config) {
  if (!config.generation_request)
    throw std::invalid_argument("evolution requires an explicit generation request");
  std::vector<grammar::GenerationRequest> requests{*config.generation_request};
  requests.insert(requests.end(), config.additional_generation_requests.begin(),
                  config.additional_generation_requests.end());
  return requests;
}

void validate_grammar_case_set(const EvolutionConfig& config, const CaseSet& case_set) {
  if (!config.compiled_grammar || !config.generation_request)
    throw std::invalid_argument("evolution case validation requires a grammar and request");
  if (config.additional_generation_requests.empty()) {
    validate_grammar_case_set(*config.compiled_grammar, case_set, *config.generation_request);
    return;
  }
  const auto requests = population_requests(config);
  grammar::validate_population_requests(*config.compiled_grammar, requests);
  validate_grammar_inputs(*config.compiled_grammar, case_set);
  if (case_set.expected_values.empty())
    throw std::invalid_argument("mixed evolution requires nonempty fitness cases");
  for (const auto& expected : case_set.expected_values) {
    const auto type = exact_expected_type(expected);
    bool admitted = false;
    for (const auto& request : requests) admitted |= request.type == type;
    if (!admitted)
      throw std::invalid_argument("case expected type must match an admitted population root type");
  }
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
  const auto generate = [&](std::uint64_t seed, const grammar::GenerationRequest& root) {
    if (config.initial_resource_budget)
      return grammar::generate_derivation(grammar, seed, root, *config.initial_resource_budget).genome;
    return generate_random_genome(seed, grammar, root);
  };
  const auto require_initial_budget = [&](const grammar::DerivationMetadata& witness) {
    if (config.initial_resource_budget &&
        !config.initial_resource_budget->accepts(witness.resources->subtree()))
      throw std::invalid_argument("initial population exceeds projected resource budget");
  };
  if (!config.additional_generation_requests.empty()) {
    const auto requests = population_requests(config);
    if (!replay_population) {
      out.population.reserve(static_cast<std::size_t>(config.population_size));
      for (int i = 0; i < config.population_size; ++i)
        out.population.push_back(generate(
            config.seed + static_cast<std::uint64_t>(i),
            requests[static_cast<std::size_t>(i) % requests.size()]));
    } else {
      if (replay_population->size() != static_cast<std::size_t>(config.population_size))
        throw std::invalid_argument("initial_population size must match population_size");
      grammar::VariationContext context(config.compiled_grammar, requests);
      out.population = *replay_population;
      out.replayed = true;
      for (auto& genome : out.population) {
        const auto analysis = context.analyze(genome);
        require_initial_budget(analysis->witness);
        genome.meta = build_genome_meta(genome.ast);
        genome.derivation = std::make_shared<const grammar::DerivationMetadata>(analysis->witness);
      }
    }
    return out;
  }
  if (!replay_population) {
    out.population.reserve(static_cast<std::size_t>(config.population_size));
    for (int i = 0; i < config.population_size; ++i)
      out.population.push_back(generate(config.seed + static_cast<std::uint64_t>(i), request));
    return out;
  }
  if (replay_population->size() != static_cast<std::size_t>(config.population_size))
    throw std::invalid_argument("initial_population size must match population_size");
  out.population = *replay_population;
  out.replayed = true;
  for (auto& genome : out.population) {
    auto witness = grammar::reconstruct_derivation(grammar, genome, request);
    require_initial_budget(witness);
    genome.meta = build_genome_meta(genome.ast);
    genome.derivation = std::make_shared<const grammar::DerivationMetadata>(std::move(witness));
  }
  return out;
}

}  // namespace gagp::evo
