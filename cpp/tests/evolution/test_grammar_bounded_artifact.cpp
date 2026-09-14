#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/cli/grammar_artifact.hpp"
#include "gagp/core/semantic_fuel.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/payload/payload.hpp"
#include "gagp/serialization/region_plan_json.hpp"

namespace {

using namespace gagp;
using namespace gagp::cli_detail;
using namespace gagp::evo;
using namespace gagp::evo::grammar;

void check(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

JsonValue parse(const std::string& text) {
  return JsonParser(text, {true, 512}).parse();
}

CompiledGrammar load(const char* name) {
  return compile_grammar(load_definition(
      std::string(GAGP_REPOSITORY_ROOT) +
      "/configs/grammar_definitions/" + name));
}

std::string plan_json(const RegionPlan& plan) {
  return canonical_json(serialization::encode_region_plan(plan));
}

bool same_spec(const BoundedRegionSpec& left,
               const BoundedRegionSpec& right) {
  if (left.node_index != right.node_index ||
      plan_json(left.plan) != plan_json(right.plan) ||
      left.parameters.size() != right.parameters.size() ||
      left.phases.size() != right.phases.size()) return false;
  for (std::size_t i = 0; i < left.parameters.size(); ++i)
    if (left.parameters[i].kind != right.parameters[i].kind ||
        left.parameters[i].index != right.parameters[i].index) return false;
  for (std::size_t i = 0; i < left.phases.size(); ++i) {
    const auto& a = left.phases[i];
    const auto& b = right.phases[i];
    if (a.argument != b.argument || a.bindings.size() != b.bindings.size())
      return false;
    for (std::size_t j = 0; j < a.bindings.size(); ++j)
      if (a.bindings[j].source.bank != b.bindings[j].source.bank ||
          a.bindings[j].source.slot != b.bindings[j].source.slot ||
          a.bindings[j].binder_id != b.bindings[j].binder_id) return false;
  }
  return true;
}

void check_witness(const CompiledGrammar& grammar,
                   const ProgramGenome& genome) {
  require_membership(grammar, genome);
  VerifiedAst verified;
  std::vector<std::vector<int>> scopes;
  const DerivationMetadata witness = reconstruct_derivation(
      grammar, genome, entry_request(grammar), &verified, &scopes);
  check(witness.choices.size() == scopes.size(),
        "bounded artifact witness lost scope sidecar alignment");
  const BytecodeProgram bytecode = compile_for_eval(genome);
  check(witness.lowered_instructions == bytecode_instruction_count(bytecode),
        "bounded artifact witness omitted phase instructions");
}

void rejects_replay(const std::string& artifact,
                    const CompiledGrammar& grammar,
                    const std::function<void(JsonValue&)>& mutation,
                    const std::string& message) {
  JsonValue changed = parse(artifact);
  mutation(changed);
  const std::string encoded = canonical_json(changed);
  try {
    (void)decode_materialized_program(encoded);
  } catch (const std::exception& error) {
    throw std::runtime_error(message +
                             ": altered artifact is not independently "
                             "materialization-valid: " + error.what());
  }
  try {
    (void)replay_generated_artifact(encoded, &grammar);
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error(message);
}

bool same_string_list(const Value& left, const Value& right) {
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

void test_sequence_artifact() {
  const CompiledGrammar grammar = load("bounded_sequence.json");
  const GeneratedDerivation generated = generate_derivation(grammar, 17);
  check(generated.genome.ast.bounded_region_specs.size() == 1,
        "bounded sequence config did not generate one descriptor");
  const std::string artifact = encode_generated_artifact(grammar, generated);
  const JsonValue root = parse(artifact);
  const auto& encoded_specs = root.object_v.at("ast_shape").object_v.at(
      "bounded_region_specs").array_v;
  check(encoded_specs.size() == 1 &&
            encoded_specs[0].object_v.at("plan").object_v.at("progress")
                    .string_v == "sequence_windows" &&
            encoded_specs[0].object_v.at("phases").array_v.size() == 5,
        "artifact shape omitted the bounded sequence plan or phases");

  payload::clear();
  const MaterializedGrammarArtifact materialized =
      decode_materialized_program(artifact);
  check(materialized.genome.ast.bounded_region_specs.size() == 1 &&
            same_spec(generated.genome.ast.bounded_region_specs[0],
                      materialized.genome.ast.bounded_region_specs[0]) &&
            ast_cache_key(generated.genome.ast) ==
                ast_cache_key(materialized.genome.ast),
        "materialized artifact changed bounded sequence metadata");
  check_witness(grammar, materialized.genome);

  const Value source = payload::make_string_list_value({
      payload::make_string_value(std::string("a\0", 2)),
      payload::make_string_value("beta"),
      payload::make_string_value(std::string(1, static_cast<char>(0xff))),
      payload::make_string_value("")});
  const BytecodeProgram bytecode = compile_for_eval(materialized.genome);
  const ExecResult result = execute_bytecode_cpu(
      bytecode, {{bytecode.var2idx.at("source"), source}},
      materialized.execution_limits.fuel);
  check(!result.is_error && same_string_list(result.value, source),
        "materialized bounded sequence artifact changed runtime payloads");

  const GeneratedDerivation replayed = replay_generated_artifact(artifact,
                                                                   &grammar);
  check(replayed.genome.derivation &&
            encode_generated_artifact(grammar, replayed) == artifact &&
            same_spec(replayed.genome.ast.bounded_region_specs[0],
                      generated.genome.ast.bounded_region_specs[0]),
        "bounded sequence artifact failed exact seed replay");
}

void test_memo_artifact_and_tampering() {
  const CompiledGrammar grammar = load("bounded_memo.json");
  const GeneratedDerivation generated = generate_derivation(grammar, 29);
  const std::string artifact = encode_generated_artifact(grammar, generated);
  const MaterializedGrammarArtifact materialized =
      decode_materialized_program(artifact);
  check(materialized.genome.ast.bounded_region_specs.size() == 1 &&
            same_spec(generated.genome.ast.bounded_region_specs[0],
                      materialized.genome.ast.bounded_region_specs[0]),
        "materialized artifact changed memo plan, capture, or phase metadata");
  const auto& spec = materialized.genome.ast.bounded_region_specs[0];
  check(spec.plan.memoized && spec.parameters.size() == 1 &&
            spec.parameters[0].kind == RegionCaptureKind::Name &&
            materialized.genome.ast.names.at(spec.parameters[0].index) ==
                "base_value",
        "memo artifact lost its named capture ownership");
  check_witness(grammar, materialized.genome);

  const BytecodeProgram bytecode = compile_for_eval(materialized.genome);
  const auto input = [&](const char* name, std::int64_t value) {
    return std::make_pair(bytecode.var2idx.at(name), Value::from_int(value));
  };
  const ExecResult result = execute_bytecode_cpu(
      bytecode,
      {input("row", 2), input("column", 2), input("rows", 3),
       input("columns", 4), input("base_value", 1)},
      materialized.execution_limits.fuel);
  check(!result.is_error && result.value.tag == ValueTag::Int &&
            result.value.i == 8,
        "materialized memo artifact changed runtime semantics");

  const auto shape_spec = [](JsonValue& root) -> JsonValue& {
    return root.object_v.at("ast_shape").object_v.at(
        "bounded_region_specs").array_v[0];
  };
  rejects_replay(artifact, grammar, [&](JsonValue& root) {
    shape_spec(root).object_v.at("plan").object_v.at("limits")
        .object_v.at("frames").number_v += 1;
  }, "seed replay accepted a changed bounded plan");
  rejects_replay(artifact, grammar, [&](JsonValue& root) {
    shape_spec(root).object_v.at("parameters").array_v[0]
        .object_v.at("index").number_v -= 1;
  }, "seed replay accepted a changed bounded capture");
  rejects_replay(artifact, grammar, [&](JsonValue& root) {
    shape_spec(root).object_v.at("phases").array_v[1]
        .object_v.at("bindings").array_v[0].object_v.at("bank").string_v =
        "state";
  }, "seed replay accepted a changed bounded phase source");
}

}  // namespace

int main() {
  try {
    test_sequence_artifact();
    test_memo_artifact_and_tampering();
    std::cout << "grammar bounded artifact: materialization, witness, replay, "
                 "and runtime semantics passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
