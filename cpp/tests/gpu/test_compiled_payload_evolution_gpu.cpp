#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/cpu/fitness_cpu.hpp"
#include "gagp/runtime/gpu/fitness_gpu.hpp"

namespace {
using namespace gagp;
using namespace gagp::evo;

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

struct DomainCase {
  const char* type;
  ValueTag tag;
  const char* values;
};

std::shared_ptr<const grammar::CompiledGrammar> make_grammar(const DomainCase& row) {
  const std::string type = row.type;
  const std::string source = R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","type":")" + type + R"("},
    "search_limits":{"max_nodes":12,"max_depth":8},
    "execution_limits":{"fuel":100},
    "nonterminals":[{"id":"Main","type":")" + type + R"(","scope":[],
      "alternatives":[{"id":"captured","weight":1,"expression":{
        "signature":"let()" + type + "," + type + ")->" + type + R"(",
        "args":[{"constant":{"type":")" + type + R"(","values":)" + row.values + R"(}},
                {"bound":"x"}],"bind":{"1":["x"]}}}]}]})";
  return std::make_shared<const grammar::CompiledGrammar>(
      grammar::compile_grammar(grammar::parse_definition(source)));
}

BytecodeProgram compile_checked(const ProgramGenome& genome) {
  const auto verified = verify_ast(genome.ast, {});
  require(verified.ok, "evolved AST failed native verification");
  return compile_for_eval(genome, verified.verified);
}

void exercise(const DomainCase& row, int population_size, int mode) {
  const auto grammar_owner = make_grammar(row);
  EvolutionConfig config;
  config.compiled_grammar = grammar_owner;
  config.generation_request = grammar::entry_request(*grammar_owner);
  config.population_size = population_size;
  config.generations = 4;
  config.seed = 381;
  config.fuel = 100;
  config.selection_pressure = population_size;
  config.mutation_rate = 1.0;
  config.mutation_subtree_prob = 0.5;
  config.eval_engine = (mode == 0 || mode == 4) ? EvalEngine::CPU : EvalEngine::GPU;
  config.reproduction_backend = mode <= 1 ? repro::ReproductionBackend::Cpu
                                        : repro::ReproductionBackend::Gpu;
  config.repro_overlap = mode == 3;
  config.gpu_blocksize = population_size == 1 ? 128 : 1024;

  std::vector<ProgramGenome> population;
  for (int i = 0; i < population_size; ++i)
    population.push_back(grammar::generate_derivation(*grammar_owner, 91 + i).genome);
  const auto expected = execute_bytecode_cpu(compile_checked(population.front()), {}, config.fuel);
  require(!expected.is_error && expected.value.tag == row.tag,
          "typed capture fixture failed before evolution");
  const EvalCase one_case{{}, expected.value};
  const auto result = evolve_population({one_case}, config, &population);
  require(result.history_best.size() == static_cast<std::size_t>(config.generations) &&
              result.final_population.size() == population.size(),
          "typed evolution did not complete all generations");

  std::vector<BytecodeProgram> programs;
  for (const auto& scored : result.final_population) {
    require(scored.genome.derivation != nullptr, "evolved child lost grammar certification");
    grammar::require_membership(*grammar_owner, scored.genome, *config.generation_request);
    programs.push_back(compile_checked(scored.genome));
    const auto actual = execute_bytecode_cpu(programs.back(), {}, config.fuel);
    require(!actual.is_error && actual.value.tag == row.tag,
            "evolution changed a capture's runtime type or produced an error");
  }
  const std::vector<CaseBindings> bindings(1);
  const std::vector<Value> answers{one_case.expected};
  const auto cpu = eval_fitness_cpu(programs, bindings, answers, config.fuel, config.penalty);
  FitnessSessionGpu session;
  const auto init = session.init(bindings, answers, config.fuel, config.gpu_blocksize, config.penalty);
  require(init.ok, "final parity session initialization failed: " + init.err.message);
  const auto gpu = session.eval_programs(programs);
  require(gpu.ok && cpu == gpu.fitness, "final typed population CPU/GPU fitness mismatch");
  for (std::size_t i = 0; i < cpu.size(); ++i)
    require(cpu[i] == result.final_population[i].fitness,
            "evolution final fitness differs from independent evaluation");
}
}  // namespace

int main() {
  const DomainCase cases[] = {
      {"Int", ValueTag::Int, R"(["-3","8"])"},
      {"Float", ValueTag::Float, "[-1.25,2.5]"},
      {"Bool", ValueTag::Bool, "[false,true]"},
      {"Char", ValueTag::Char, R"(["a","z"])"},
      {"String", ValueTag::String, R"(["alpha","beta"])"},
      {"IntList", ValueTag::IntList, R"([["1","-2"],["7"]])"},
      {"FloatList", ValueTag::FloatList, "[[1.25,-2.5],[7.5]]"},
      {"StringList", ValueTag::StringList, R"([["alpha","beta"],["gamma"]])"},
  };
  for (const auto& row : cases) for (int size : {1, 5}) for (int mode = 0; mode < 5; ++mode) {
    try {
      exercise(row, size, mode);
    } catch (const std::exception& error) {
      std::cerr << "FAIL: " << row.type << " population=" << size << " mode=" << mode
                << ": " << error.what() << '\n';
      return 1;
    }
  }
  return 0;
}
