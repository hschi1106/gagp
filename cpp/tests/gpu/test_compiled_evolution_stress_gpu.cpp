#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/core/semantic_fuel.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/case_set.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/cpu/fitness_cpu.hpp"
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "gagp/runtime/payload/payload.hpp"
#include "gagp/serialization/region_plan_json.hpp"

namespace {

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

RegionBound literal(std::int64_t value) {
  RegionBound result;
  result.kind = RegionBoundKind::Literal;
  result.literal = value;
  return result;
}

RegionStateTransition offset(std::int64_t value) {
  RegionStateTransition result;
  result.kind = RegionTransitionKind::CoordinateOffset;
  result.offset = value;
  return result;
}

RegionPlan unary_plan(bool memoized, bool parameter) {
  RegionPlan plan;
  plan.state_types = {ValueTag::Int};
  plan.result_type = ValueTag::Int;
  if (parameter) plan.parameter_types = {ValueTag::Int};
  plan.requests = {{{offset(-1)}}};
  plan.limits = {16, memoized ? 16U : 0U, 1};
  plan.memoized = memoized;
  plan.coordinate_slots = {0};
  plan.coordinate_rank = {{0, 1}};
  plan.coordinate_domains = {{literal(0), literal(5)}};
  return plan;
}

std::string encoded_plan(const RegionPlan& plan) {
  return canonical_json(serialization::encode_region_plan(plan));
}

// Kept aligned with test_grammar_bounded_variation.cpp: the template hole is
// expanded twice under distinct Let binders, and each expansion owns a
// memoized one-dependency bounded region.
std::string unary_template_definition() {
  std::string text = R"JSON({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","type":"Int"},
    "search_limits":{"max_nodes":100,"max_depth":20},
    "execution_limits":{"fuel":1000},
    "templates":[{"id":"Pair","type":"Int","scope":[],
      "holes":[{"id":"recurrence","type":"Int","scope":[{"name":"x","type":"Int"}]}],
      "body":{"signature":"add(Int,Int)->Int","args":[
        {"signature":"let(Int,Int)->Int","args":[{"constant":{"type":"Int","values":["1"]}},
          {"hole":"recurrence"}],"bind":{"1":["x"]}},
        {"signature":"let(Int,Int)->Int","args":[{"constant":{"type":"Int","values":["2"]}},
          {"hole":"recurrence"}],"bind":{"1":["x"]}}]}}],
    "nonterminals":[
      {"id":"Main","type":"Int","scope":[],"alternatives":[{"id":"main","weight":1,
        "expression":{"template":"Pair","holes":{"recurrence":{"ref":"Recurrence"}}}}]},
      {"id":"Recurrence","type":"Int","scope":[{"name":"x","type":"Int"}],"alternatives":[
        {"id":"region","weight":1,"expression":{
          "structured":{"family":"bounded","plan":PLAN},
          "captures":[{"bound":"x"}],
          "phases":[
            {"argument":1,"bindings":[{"bank":"state","slot":0,"name":"n"}]},
            {"argument":2,"bindings":[{"bank":"parameter","slot":0,"name":"seed"}]},
            {"argument":3,"bindings":[{"bank":"result","slot":0,"name":"child"}]},
            {"argument":4,"bindings":[]}],
          "args":[{"constant":{"type":"Int","values":["3"]}},
            {"signature":"le(Int,Int)->Bool","args":[{"bound":"n"},{"constant":{"type":"Int","values":["0"]}}]},
            {"bound":"seed"},
            {"signature":"add(Int,Int)->Int","args":[{"bound":"child"},{"constant":{"type":"Int","values":["1"]}}]},
            {"constant":{"type":"Int","values":["0"]}}]}}]}
  ]})JSON";
  text.replace(text.find("PLAN"), 4, encoded_plan(unary_plan(true, true)));
  return text;
}

std::string unary_expression(const RegionPlan& plan,
                             const std::string& initial,
                             const std::string& base,
                             const std::string& increment) {
  return std::string(R"JSON({
    "structured":{"family":"bounded","plan":)JSON") + encoded_plan(plan) +
      R"JSON(},"captures":[],"phases":[
        {"argument":1,"bindings":[{"bank":"state","slot":0,"name":"n"}]},
        {"argument":2,"bindings":[]},
        {"argument":3,"bindings":[{"bank":"result","slot":0,"name":"previous"}]},
        {"argument":4,"bindings":[]}],"args":[)JSON" + initial +
      R"JSON(,
        {"signature":"eq(Int,Int)->Bool","args":[{"bound":"n"},{"constant":{"type":"Int","values":["0"]}}]},)JSON" +
      base + R"JSON(,
        {"signature":"add(Int,Int)->Int","args":[{"bound":"previous"},{"constant":{"type":"Int","values":[")JSON" +
      increment + R"JSON("]}}]},
        {"constant":{"type":"Int","values":["0"]}}]})JSON";
}

// Mirrors the allowed nesting boundary in test_bounded_region_typing.cpp:
// the inner region is an initial-state operand, never an isolated phase body.
std::string nested_template_definition() {
  const std::string inner = unary_expression(
      unary_plan(true, false),
      R"JSON({"constant":{"type":"Int","values":["2"]}})JSON",
      R"JSON({"constant":{"type":"Int","values":["1"]}})JSON", "1");
  const std::string outer = unary_expression(
      unary_plan(false, false), R"JSON({"hole":"initial"})JSON",
      R"JSON({"constant":{"type":"Int","values":["0"]}})JSON", "1");
  return std::string(R"JSON({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","type":"Int"},
    "search_limits":{"max_nodes":64,"max_depth":20},
    "execution_limits":{"fuel":1000},
    "templates":[{"id":"Outer","type":"Int","scope":[],
      "holes":[{"id":"initial","type":"Int","scope":[]}],"body":)JSON") +
      outer + R"JSON(}],
    "nonterminals":[{"id":"Main","type":"Int","scope":[],"alternatives":[
      {"id":"nested","weight":1,"expression":{"template":"Outer","holes":{"initial":)JSON" +
      inner + R"JSON(}}}]}]
  })JSON";
}

std::shared_ptr<const CompiledGrammar> compile_shared(
    const std::string& definition) {
  auto grammar = std::make_shared<const CompiledGrammar>(
      compile_grammar(parse_definition(definition)));
  grammar->require_executable();
  return grammar;
}

std::shared_ptr<const CompiledGrammar> load_shared(const std::string& path) {
  auto grammar = std::make_shared<const CompiledGrammar>(
      compile_grammar(load_definition(path)));
  grammar->require_executable();
  return grammar;
}

std::vector<InputSpec> grammar_inputs(const CompiledGrammar& grammar) {
  std::vector<InputSpec> result;
  result.reserve(grammar.inputs().size());
  for (const auto& input : grammar.inputs())
    result.push_back({input.name, input.type});
  return result;
}

enum class Shape {
  UnaryTemplate,
  BinaryMemo,
  Nested,
  PackageLinear,
  PackageDc,
  PackageDp1,
  PackageDp2,
};

void require_shape(const ProgramGenome& genome, Shape shape,
                   const std::string& label) {
  const auto& regions = genome.ast.bounded_region_specs;
  if (shape == Shape::PackageLinear) {
    require(regions.empty() &&
                std::count_if(genome.ast.nodes.begin(), genome.ast.nodes.end(),
                    [](const AstNode& node) {
                      return node.kind == NodeKind::TRAVERSE_RANGE;
                    }) == 1,
            label + ": LinearRec package shape changed");
    return;
  }
  if (shape == Shape::PackageDc) {
    require(regions.size() == 1 && !regions.front().plan.memoized &&
                regions.front().plan.progress ==
                    RegionProgressKind::SequenceWindows &&
                regions.front().plan.requests.size() == 2,
            label + ": DC package shape changed");
    return;
  }
  if (shape == Shape::PackageDp1) {
    require(regions.size() == 1 && regions.front().plan.memoized &&
                regions.front().plan.state_types.size() == 1 &&
                regions.front().plan.requests.size() >= 1 &&
                regions.front().plan.requests.size() <= 3,
            label + ": DP1D package shape changed");
    return;
  }
  if (shape == Shape::PackageDp2) {
    require(regions.size() == 1 && regions.front().plan.memoized &&
                regions.front().plan.state_types.size() == 2 &&
                regions.front().plan.requests.size() >= 1 &&
                regions.front().plan.requests.size() <= 3,
            label + ": DP2D package shape changed");
    return;
  }
  if (shape == Shape::UnaryTemplate) {
    require(regions.size() == 2,
            label + ": repeated template hole lost a bounded occurrence");
    for (const auto& region : regions) {
      require(region.plan.memoized && region.plan.requests.size() == 1 &&
                  region.plan.requests.front().states.size() == 1,
              label + ": unary memo dependency arity changed");
    }
    return;
  }
  if (shape == Shape::BinaryMemo) {
    require(regions.size() == 1 && regions.front().plan.memoized &&
                regions.front().plan.requests.size() == 2,
            label + ": two-dimensional memo region shape changed");
    for (const auto& request : regions.front().plan.requests) {
      require(request.states.size() == 2,
              label + ": two-dimensional dependency arity changed");
    }
    return;
  }
  require(regions.size() == 2,
          label + ": nested grammar did not retain both bounded regions");
  const auto outer = std::min_element(
      regions.begin(), regions.end(), [](const auto& left, const auto& right) {
        return left.node_index < right.node_index;
      });
  const auto inner = std::max_element(
      regions.begin(), regions.end(), [](const auto& left, const auto& right) {
        return left.node_index < right.node_index;
      });
  require(inner->node_index == outer->node_index + 1 && inner->plan.memoized &&
              !outer->plan.memoized,
          label + ": nested initial-state ownership or memo distinction changed");
}

std::vector<BytecodeProgram> validate_and_compile(
    const CompiledGrammar& grammar, const GenerationRequest& request,
    const std::vector<ProgramGenome>& population, const CaseSet& case_set,
    Shape shape, std::int64_t expected, const std::string& label,
    bool exact_expected = true) {
  require(population.size() == 7, label + ": final population size changed");
  const std::vector<InputSpec> inputs = grammar_inputs(grammar);
  std::vector<BytecodeProgram> programs;
  programs.reserve(population.size());
  for (const auto& child : population) {
    require(child.derivation != nullptr,
            label + ": final child lost derivation certification");
    const AstVerifyResult verified = verify_ast(child.ast, inputs);
    require(verified.ok, label + ": final child failed native verification: " +
                             verified.diagnostic.message);
    require_membership(grammar, child, request);
    require_shape(child, shape, label);
    BytecodeProgram program =
        compile_for_eval(child, verified.verified, case_set.input_names);
    std::vector<std::pair<int, Value>> bindings;
    for (const auto& binding : case_set.bindings.front())
      bindings.push_back({binding.idx, binding.value});
    const ExecResult execution =
        execute_bytecode_cpu(program, bindings, grammar.execution_limits().fuel);
    require(!execution.is_error && execution.value.tag == ValueTag::Int &&
                (!exact_expected || execution.value.i == expected),
            label + ": final child changed the fixture's result contract");
    programs.push_back(std::move(program));
  }
  return programs;
}

void require_cpu_gpu_parity(const std::vector<BytecodeProgram>& programs,
                            const CaseSet& cases, int fuel, double penalty,
                            const std::string& label,
                            bool require_zero_fitness = true) {
  const std::vector<double> cpu = eval_fitness_cpu(
      programs, cases.bindings, cases.expected_values, fuel, penalty, 32);
  FitnessSessionGpu session;
  const FitnessSessionInitResult initialized = session.init(
      cases.bindings, cases.expected_values, fuel, 128, penalty);
  require(initialized.ok,
          label + ": GPU parity session initialization failed: " +
              initialized.err.message);
  const FitnessEvalResult gpu = session.eval_programs(programs);
  require(gpu.ok, label + ": GPU parity evaluation failed: " + gpu.err.message);
  require(cpu == gpu.fitness,
          label + ": final population CPU/GPU fitness differs");
  if (require_zero_fitness) {
    require(std::all_of(cpu.begin(), cpu.end(),
                        [](double score) { return score == 0.0; }),
            label + ": fixed numeric fixture did not retain zero error");
  }
}

void exercise_mode(const std::shared_ptr<const CompiledGrammar>& grammar,
                   const std::vector<EvalCase>& cases, Shape shape,
                   std::int64_t expected, EvalEngine engine,
                   repro::ReproductionBackend reproduction_backend,
                   bool overlap,
                   const std::string& label,
                   bool exact_expected = true) {
  EvolutionConfig config;
  config.population_size = 7;
  config.generations = 3;
  config.mutation_rate = 1.0;
  config.mutation_subtree_prob = 0.75;
  config.penalty = 7.0;
  config.eval_engine = engine;
  config.reproduction_backend = reproduction_backend;
  config.repro_overlap = overlap;
  config.gpu_blocksize = 128;
  config.selection_pressure = 3;
  config.seed = UINT64_C(0x4f7319a2dcb608e5);
  config.fuel = grammar->execution_limits().fuel;
  config.compiled_grammar = grammar;
  config.generation_request = entry_request(*grammar);
  config.retain_final_population = true;

  const EvolutionResult evolved = evolve_population(cases, config);
  require(evolved.history_best.size() ==
              static_cast<std::size_t>(config.generations) &&
              evolved.timing.generations.size() ==
                  static_cast<std::size_t>(config.generations),
          label + ": public evolution did not complete every generation");
  for (const auto& generation : evolved.timing.generations) {
    require(generation.reproduction.variation.crossover_attempts == 4 &&
                generation.reproduction.variation.mutation_attempts == 7,
            label + ": public reproduction did not exercise every child");
  }
  std::vector<ProgramGenome> population;
  population.reserve(evolved.final_population.size());
  for (const auto& scored : evolved.final_population)
    population.push_back(scored.genome);
  const CaseSet case_set = prepare_case_set(cases);
  const auto programs = validate_and_compile(
      *grammar, *config.generation_request, population, case_set, shape,
      expected, label, exact_expected);
  require_cpu_gpu_parity(programs, case_set, config.fuel, config.penalty, label,
                         exact_expected);
}

void exercise_fixture(const std::shared_ptr<const CompiledGrammar>& grammar,
                      const std::vector<EvalCase>& cases, Shape shape,
                      std::int64_t expected, const std::string& label,
                      bool exact_expected = true) {
  exercise_mode(grammar, cases, shape, expected, EvalEngine::CPU,
                repro::ReproductionBackend::Cpu, false,
                label + " cpu-eval/cpu-repro", exact_expected);
  exercise_mode(grammar, cases, shape, expected, EvalEngine::GPU,
                repro::ReproductionBackend::Cpu, false,
                label + " gpu-eval/cpu-repro", exact_expected);
  exercise_mode(grammar, cases, shape, expected, EvalEngine::GPU,
                repro::ReproductionBackend::Gpu, false,
                label + " gpu-eval/gpu-repro/sync", exact_expected);
  exercise_mode(grammar, cases, shape, expected, EvalEngine::GPU,
                repro::ReproductionBackend::Gpu, true,
                label + " gpu-eval/gpu-repro/overlap", exact_expected);
  exercise_mode(grammar, cases, shape, expected, EvalEngine::CPU,
                repro::ReproductionBackend::Gpu, false,
                label + " cpu-eval/gpu-repro", exact_expected);
}

bool cuda_unavailable(const std::string& message) {
  return message.find("cuda device unavailable") != std::string::npos ||
         message.find("CUDA is unavailable") != std::string::npos ||
         message.find("no CUDA-capable device") != std::string::npos;
}

}  // namespace

int main() {
  try {
    exercise_fixture(
        compile_shared(unary_template_definition()),
        {{{}, Value::from_int(9)}}, Shape::UnaryTemplate, 9,
        "repeated unary memo template");

    const std::string root = GAGP_REPOSITORY_ROOT;
    auto binary_document = load_definition(
        root + "/configs/grammar_definitions/bounded_memo.json").document;
    // Define a GPU-capable test grammar explicitly; production packing still
    // rejects the catalog fixture's 1024-cell declaration rather than clamping it.
    auto& cells = binary_document.object_v.at("nonterminals").array_v.at(0)
        .object_v.at("alternatives").array_v.at(0).object_v.at("expression")
        .object_v.at("structured").object_v.at("plan").object_v.at("limits")
        .object_v.at("cells");
    require(gagp::cli_detail::require_int(cells, "cells") == 1024,
            "two-dimensional memo fixture cell declaration changed");
    cells.number_v = 128;
    auto binary_memo = std::make_shared<const CompiledGrammar>(
        compile_grammar(parse_definition(canonical_json(binary_document))));
    binary_memo->require_executable();
    exercise_fixture(
        binary_memo,
        {{{{"row", Value::from_int(2)},
           {"column", Value::from_int(2)},
           {"rows", Value::from_int(3)},
           {"columns", Value::from_int(4)},
           {"base_value", Value::from_int(1)}},
          Value::from_int(8)}},
        Shape::BinaryMemo, 8, "two-dimensional memo fixture");

    exercise_fixture(compile_shared(nested_template_definition()),
                     {{{}, Value::from_int(3)}}, Shape::Nested, 3,
                     "nested initial-state template");

    const Value package_source = payload::make_int_list_value(
        {Value::from_int(1), Value::from_int(2), Value::from_int(3),
         Value::from_int(4)});
    exercise_fixture(
        load_shared(root + "/configs/grammar/compat/linear_rec_intlist_int.json"),
        {{{{"source", package_source}, {"start", Value::from_int(0)}},
          Value::from_int(10)}},
        Shape::PackageLinear, 0, "LinearRec compatibility package", false);
    exercise_fixture(
        load_shared(root + "/configs/grammar/compat/dc_intlist_int.json"),
        {{{{"source", package_source}}, Value::from_int(4)}},
        Shape::PackageDc, 0, "DC compatibility package", false);
    exercise_fixture(
        load_shared(root + "/configs/grammar/compat/dp1d_int.json"),
        {{{{"state", Value::from_int(4)}}, Value::from_int(5)}},
        Shape::PackageDp1, 0, "DP1D compatibility package", false);
    exercise_fixture(
        load_shared(root + "/configs/grammar/compat/dp2d_int.json"),
        {{{{"row", Value::from_int(2)}, {"column", Value::from_int(2)}},
          Value::from_int(6)}},
        Shape::PackageDp2, 0, "DP2D compatibility package", false);
  } catch (const std::runtime_error& error) {
    if (cuda_unavailable(error.what())) {
      std::cout << "gagp_test_compiled_evolution_stress_gpu: SKIP ("
                << error.what() << ")\n";
      return 0;
    }
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "compiled evolution GPU stress: arity-1/2 memo, nested "
               "initial-state regions, overlap, and final CPU/GPU parity OK\n";
  return 0;
}
