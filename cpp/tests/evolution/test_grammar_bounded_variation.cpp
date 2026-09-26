#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>

#include "gagp/core/semantic_fuel.hpp"
#include "../fixtures/bounded_capture.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/crossover.hpp"
#include "gagp/evolution/mutation.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/serialization/region_plan_json.hpp"

namespace {
using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

std::int64_t check(const CompiledGrammar& grammar, const ProgramGenome& genome) {
  require_membership(grammar, genome);
  const auto verified = verify_ast(genome.ast, {});
  require(verified.ok, "bounded variation escaped native lexical scope");
  std::set<int> ids;
  for (const auto& region : genome.ast.lexical_regions)
    for (const auto& binding : region.bindings)
      require(ids.insert(binding.id).second, "repeated lexical declaration");
  require(genome.ast.bounded_region_specs.size() == 2, "repeated hole lost region metadata");
  for (const auto& region : genome.ast.bounded_region_specs)
    for (const auto& phase : region.phases)
      for (const auto& binding : phase.bindings)
        require(ids.insert(binding.binder_id).second, "repeated phase binder declaration");
  const auto code = compile_for_eval(genome, verified.verified);
  const auto result = execute_bytecode_cpu(code, {}, 1000);
  require(!result.is_error && result.value.tag == ValueTag::Int, "bounded variation execution failed");
  require(result.value.i == 9 || result.value.i == 29, "repeated hole or capture mapping changed");
  const auto witness = reconstruct_derivation(grammar, genome);
  require(witness.lowered_instructions == bytecode_instruction_count(code) &&
      witness.lowered_instructions > code.code.size(), "phase instruction budget omitted");
  return result.value.i;
}
}  // namespace
int main() {
  try {
    auto grammar = std::make_shared<const CompiledGrammar>(compile_grammar(parse_definition(gagp::test::bounded_capture_definition())));
    grammar->require_executable();
    VariationContext context(grammar);
    std::set<std::int64_t> values;
    auto first = generate_derivation(*grammar, 0).genome;
    auto second = generate_derivation(*grammar, 1).genome;
    for (std::uint64_t seed = 0; seed < 32; ++seed) {
      auto generated = generate_derivation(*grammar, seed).genome;
      values.insert(check(*grammar, generated));
      const auto children = crossover(first, generated, seed, context);
      check(*grammar, children.first); check(*grammar, children.second);
      first = mutate(children.first, seed, context, 1.0);
      check(*grammar, first);
      second = generated;
    }
    require(values.size() == 2, "grammar lost a base alternative");
    require(context.counters().changed_children > 0, "bounded variation only fell back");
    std::cout << "bounded grammar variation: OK\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
