#include <algorithm>
#include <cstdint>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/core/semantic_fuel.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/frame.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/payload/payload.hpp"
#include "gagp/serialization/region_plan_json.hpp"

namespace {

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;

void check(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

CompiledGrammar compile_text(const std::string& text) {
  return compile_grammar(parse_definition(text));
}

ExecResult execute(const CompiledGrammar& grammar,
                   const GeneratedDerivation& generated,
                   const std::vector<std::pair<int, Value>>& inputs = {}) {
  return execute_bytecode_cpu(compile_for_eval(generated.genome), inputs,
                              grammar.execution_limits().fuel);
}

void check_generated(const CompiledGrammar& grammar,
                     const GeneratedDerivation& generated) {
  require_membership(grammar, generated.genome);
  const AstVerifyResult verified = verify_ast(generated.genome.ast, [&] {
    std::vector<InputSpec> inputs;
    for (const auto& input : grammar.inputs())
      inputs.push_back({input.name, input.type});
    return inputs;
  }());
  check(verified.ok, "generated bounded AST failed verification: " +
                         verified.diagnostic.message);
  const BytecodeProgram lowered = compile_for_eval(generated.genome);
  check(!lowered.bounded_region_segments.empty(),
        "generated bounded AST did not lower a region segment");
  check(generated.derivation.lowered_instructions ==
            bytecode_instruction_count(lowered),
        "derivation omitted bounded phase instructions");
}

void check_phase_binders(const AstProgram& ast) {
  std::set<int> declared;
  std::set<int> referenced;
  for (const AstNode& node : ast.nodes)
    if (node.kind == NodeKind::REGION_VAR) referenced.insert(node.i0);
  for (const BoundedRegionSpec& spec : ast.bounded_region_specs) {
    check(spec.node_index < ast.nodes.size() &&
              ast.nodes[spec.node_index].kind == NodeKind::BOUNDED_REGION &&
              ast.nodes[spec.node_index].i0 ==
                  static_cast<int>(bounded_region_arity(spec.plan)),
          "bounded metadata lost its owner or dynamic arity");
    for (const RegionAstPhase& phase : spec.phases) {
      for (const RegionAstBinding& binding : phase.bindings) {
        check(binding.binder_id >= 0 && declared.insert(binding.binder_id).second,
              "bounded phase binder IDs are not fresh and global");
        check(referenced.count(binding.binder_id) != 0,
              "fixture phase binder is not visible in its generated body");
      }
    }
  }
}

bool same_list(const Value& left, const Value& right) {
  std::vector<Value> a;
  std::vector<Value> b;
  if (left.tag != ValueTag::StringList || right.tag != ValueTag::StringList ||
      !payload::lookup_list(left, &a) || !payload::lookup_list(right, &b) ||
      a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    std::string x;
    std::string y;
    if (!payload::lookup_string(a[i], &x) ||
        !payload::lookup_string(b[i], &y) || x != y) return false;
  }
  return true;
}

void test_standalone_examples() {
  const std::string root = GAGP_REPOSITORY_ROOT;
  const CompiledGrammar sequence = compile_grammar(load_definition(
      root + "/configs/grammar_definitions/bounded_sequence.json"));
  const GeneratedDerivation generated_sequence =
      generate_derivation(sequence, 17);
  check_generated(sequence, generated_sequence);
  check(generated_sequence.genome.ast.bounded_region_specs.size() == 1,
        "sequence grammar did not materialize one bounded region");
  const auto& sequence_spec =
      generated_sequence.genome.ast.bounded_region_specs.front();
  check(sequence_spec.plan.progress == RegionProgressKind::SequenceWindows &&
            sequence_spec.plan.requests.size() == 3 &&
            sequence_spec.phases.size() == 5,
        "three-way sequence plan or phase layout changed");
  check_phase_binders(generated_sequence.genome.ast);

  const Value source = payload::make_string_list_value({
      payload::make_string_value("a"), payload::make_string_value("b"),
      payload::make_string_value("c"), payload::make_string_value("d")});
  const ExecResult sequence_result =
      execute(sequence, generated_sequence, {{0, source}});
  check(!sequence_result.is_error && same_list(sequence_result.value, source),
        "generated three-way sequence region changed its source");

  const CompiledGrammar memo = compile_grammar(load_definition(
      root + "/configs/grammar_definitions/bounded_memo.json"));
  const GeneratedDerivation generated_memo = generate_derivation(memo, 29);
  check_generated(memo, generated_memo);
  const auto& memo_spec = generated_memo.genome.ast.bounded_region_specs.front();
  check(memo_spec.plan.memoized && memo_spec.plan.state_types.size() == 2 &&
            memo_spec.plan.requests.size() == 2 &&
            memo_spec.parameters.size() == 1 &&
            memo_spec.parameters[0].kind == RegionCaptureKind::Name &&
            memo_spec.parameters[0].index == 4,
        "2D memo plan or input capture changed during generation");
  const ExecResult memo_result = execute(
      memo, generated_memo,
      {{0, Value::from_int(2)}, {1, Value::from_int(2)},
       {2, Value::from_int(3)}, {3, Value::from_int(4)},
       {4, Value::from_int(1)}});
  check(!memo_result.is_error && memo_result.value.tag == ValueTag::Int &&
            memo_result.value.i == 8,
        "generated sparse 2D memo region returned the wrong result");
}

RegionBound literal(std::int64_t value) {
  RegionBound result;
  result.literal = value;
  return result;
}

RegionStateTransition offset(std::int64_t value) {
  RegionStateTransition result;
  result.kind = RegionTransitionKind::CoordinateOffset;
  result.offset = value;
  return result;
}

RegionPlan counter_plan(bool parameter) {
  RegionPlan plan;
  plan.state_types = {ValueTag::Int};
  plan.result_type = ValueTag::Int;
  if (parameter) plan.parameter_types = {ValueTag::Int};
  plan.requests = {{{offset(-1)}}};
  plan.limits = {16, 0, 1};
  plan.coordinate_slots = {0};
  plan.coordinate_rank = {{0, 1}};
  plan.coordinate_domains = {{literal(0), literal(5)}};
  return plan;
}

std::string plan_json(const RegionPlan& plan) {
  return canonical_json(serialization::encode_region_plan(plan));
}

std::string counter_expression(const std::string& capture,
                               bool read_parameter) {
  const std::string base_phase = read_parameter
      ? R"({"argument":2,"bindings":[{"bank":"parameter","slot":0,"name":"base"}]})"
      : R"({"argument":2,"bindings":[]})";
  const std::string base_expression = read_parameter
      ? R"({"bound":"base"})"
      : R"({"constant":{"type":"Int","values":["0"]}})";
  return R"({"structured":{"family":"bounded","plan":)" +
      plan_json(counter_plan(true)) + R"(},"captures":[)" + capture +
      R"(],"phases":[
        {"argument":1,"bindings":[{"bank":"state","slot":0,"name":"n"}]},)" +
      base_phase + R"(,
        {"argument":3,"bindings":[{"bank":"result","slot":0,"name":"previous"}]},
        {"argument":4,"bindings":[]}],
      "args":[
        {"constant":{"type":"Int","values":["3"]}},
        {"signature":"eq(Int,Int)->Bool","args":[{"bound":"n"},{"constant":{"type":"Int","values":["0"]}}]},)" +
      base_expression + R"(,
        {"signature":"add(Int,Int)->Int","args":[{"bound":"previous"},{"constant":{"type":"Int","values":["1"]}}]},
        {"constant":{"type":"Int","values":["0"]}}
      ]})";
}

std::string repeated_hole_grammar() {
  return R"({"format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","type":"Int"},
    "search_limits":{"max_nodes":40,"max_depth":14},
    "execution_limits":{"fuel":10000},
    "templates":[{"id":"Pair","type":"Int","scope":[],
      "holes":[{"id":"value","type":"Int","scope":[{"name":"x","type":"Int"}]}],
      "body":{"signature":"add(Int,Int)->Int","args":[
        {"signature":"let(Int,Int)->Int","args":[{"constant":{"type":"Int","values":["1"]}},{"hole":"value"}],"bind":{"1":["x"]}},
        {"signature":"let(Int,Int)->Int","args":[{"constant":{"type":"Int","values":["2"]}},{"hole":"value"}],"bind":{"1":["x"]}}
      ]}}],
    "nonterminals":[{"id":"Main","type":"Int","scope":[],"alternatives":[{
      "id":"pair","weight":1,"expression":{"template":"Pair","holes":{"value":)" +
      counter_expression(R"({"bound":"x"})", true) +
      R"(}}}]}]})";
}

void test_repeated_hole_alpha_mapping() {
  const CompiledGrammar grammar = compile_text(repeated_hole_grammar());
  const GeneratedDerivation generated = generate_derivation(grammar, 7);
  check_generated(grammar, generated);
  const AstProgram& ast = generated.genome.ast;
  check(ast.bounded_region_specs.size() == 2 && ast.lexical_regions.size() == 2,
        "repeated hole did not copy bounded and lexical metadata");
  const auto& first = ast.bounded_region_specs[0];
  const auto& second = ast.bounded_region_specs[1];
  check(first.parameters.size() == 1 && second.parameters.size() == 1 &&
            first.parameters[0].kind == RegionCaptureKind::Lexical &&
            second.parameters[0].kind == RegionCaptureKind::Lexical &&
            first.parameters[0].index != second.parameters[0].index,
        "repeated hole did not remap its free lexical capture");
  std::set<int> let_binders;
  for (const LexicalRegion& region : ast.lexical_regions)
    let_binders.insert(region.bindings.front().id);
  check(let_binders.count(first.parameters[0].index) != 0 &&
            let_binders.count(second.parameters[0].index) != 0,
        "bounded capture does not name its physical LET occurrence");
  check_phase_binders(ast);
  const ExecResult result = execute(grammar, generated);
  check(!result.is_error && result.value.tag == ValueTag::Int &&
            result.value.i == 9,
        "alpha-renamed repeated counter holes returned the wrong result");
}

void test_unused_local_capture_in_empty_frame() {
  const std::string grammar_text =
      R"({"format_version":"grammar-definition-v2",
        "entry":{"nonterminal":"Main","type":"Int"},
        "locals":[{"name":"optional_seed","type":"Int"}],
        "search_limits":{"max_nodes":20,"max_depth":10},
        "execution_limits":{"fuel":1000},
        "nonterminals":[{"id":"Main","type":"Int","scope":[],"alternatives":[{
          "id":"unused","weight":1,"expression":)" +
      counter_expression(R"({"local":"optional_seed"})", false) +
      R"(}]}]})";
  const CompiledGrammar grammar = compile_text(grammar_text);
  const GeneratedDerivation generated = generate_derivation_in_frame(
      grammar, 11, entry_request(grammar), GenerationFrame{});
  check(generated.genome.ast.bounded_region_specs.size() == 1 &&
            generated.genome.ast.bounded_region_specs[0].parameters[0].kind ==
                RegionCaptureKind::Name,
        "unset local declaration was not materialized as a name capture");
  const ExecResult result = execute(grammar, generated);
  check(!result.is_error && result.value.tag == ValueTag::Int &&
            result.value.i == 3,
        "unused unset local capture affected bounded execution");
}

}  // namespace

int main() {
  try {
    test_standalone_examples();
    test_repeated_hole_alpha_mapping();
    test_unused_local_capture_in_empty_frame();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
