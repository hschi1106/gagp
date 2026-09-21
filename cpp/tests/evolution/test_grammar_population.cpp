#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "gagp/evolution/genome_generation.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/population_init.hpp"

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;

namespace {
void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void rejects(const std::function<void()>& action) {
  try { action(); }
  catch (const std::invalid_argument&) { return; }
  throw std::runtime_error("incompatible population initialization was accepted");
}
}  // namespace

int main() {
  try {
    const auto grammar = compile_grammar(parse_definition(R"({
      "format_version":"grammar-definition-v2",
      "entry":{"nonterminal":"Expr","type":"Int"},
      "search_limits":{"max_nodes":5,"max_depth":4},
      "execution_limits":{"fuel":100},
      "inputs":[{"name":"x","type":"Int"},{"name":"items","type":"IntList"}],
      "nonterminals":[{"id":"Expr","type":"Int","scope":[],"alternatives":[
        {"id":"constant","weight":1,"expression":{"constant":{"type":"Int","range":["0","3"]}}}
      ]}]
    })"));
    CaseSet cases;
    cases.input_specs = {{"items", RType::IntList}, {"x", RType::Int}};
    cases.input_names = {"items", "x"};
    cases.expected_return_type = RType::Int;
    cases.expected_values = {Value::from_int(987654321)};
    const std::uint64_t seed = std::numeric_limits<std::uint64_t>::max() - 1;
    const auto initialized = initialize_population(grammar, cases, 16, seed);
    check(initialized.population.size() == 16 && !initialized.replayed,
          "grammar population size or replay status changed");
    auto changed_outputs = cases;
    changed_outputs.expected_values = {Value::from_int(-987654321)};
    const auto independent = initialize_population(grammar, changed_outputs, 16, seed);
    for (std::size_t i = 0; i < initialized.population.size(); ++i) {
      const auto& genome = initialized.population[i];
      const auto individual_seed = seed + static_cast<std::uint64_t>(i);
      const auto direct = generate_derivation(grammar, individual_seed);
      const auto overload = generate_random_genome(individual_seed, grammar);
      check(ast_cache_key(genome.ast) == ast_cache_key(direct.genome.ast) &&
            ast_cache_key(genome.ast) == ast_cache_key(overload.ast),
            "population generation changed seed+i replay");
      check(ast_cache_key(genome.ast) == ast_cache_key(independent.population[i].ast),
            "expected outputs influenced domain-only population generation");
      check(genome.derivation && genome.derivation->seed == individual_seed &&
            genome.derivation->grammar_hash == grammar.content_hash() &&
            genome.derivation->nodes.size() == genome.ast.nodes.size(),
            "population genome lost attached derivation metadata");
      for (const auto& value : genome.ast.consts) {
        check(value.tag == ValueTag::Int && value.i >= 0 && value.i <= 3,
              "population constant escaped declared domain");
      }
    }
    rejects([&] { initialize_population(grammar, cases, 0, 0); });
    rejects([&] { initialize_population(grammar, cases, -1, 0); });
    auto invalid = cases;
    invalid.input_specs.pop_back();
    rejects([&] { initialize_population(grammar, invalid, 1, 0); });
    invalid = cases;
    invalid.input_specs[0].name = "other";
    rejects([&] { initialize_population(grammar, invalid, 1, 0); });
    invalid = cases;
    invalid.input_specs[0].type = RType::FloatList;
    rejects([&] { initialize_population(grammar, invalid, 1, 0); });
    invalid = cases;
    invalid.input_specs[1].type = RType::Float;
    rejects([&] { initialize_population(grammar, invalid, 1, 0); });
    for (const auto type : {RType::Any, RType::Invalid}) {
      invalid = cases;
      invalid.input_specs[0].type = type;
      rejects([&] { initialize_population(grammar, invalid, 1, 0); });
    }
    invalid = cases;
    invalid.input_specs[1] = invalid.input_specs[0];
    rejects([&] { initialize_population(grammar, invalid, 1, 0); });
    invalid = cases;
    invalid.input_specs.push_back({"extra", RType::Int});
    rejects([&] { initialize_population(grammar, invalid, 1, 0); });
    invalid = cases;
    invalid.expected_return_type = RType::Float;
    rejects([&] { initialize_population(grammar, invalid, 1, 0); });
    std::cout << "grammar population initialization passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
