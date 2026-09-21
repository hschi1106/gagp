#define GAGP_CAPTURE_LIBRARY_ONLY 1
#define main gagp_capture_tool_main
#include "gpu_result_probe.cu"
#undef main

#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"

namespace {
using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;
using namespace gagp::cli_detail;

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

void compare_boundary(const BytecodeProgram& program,
                      const std::vector<std::pair<int, Value>>& inputs,
                      bool expect_error, const std::string& label) {
  int boundary = -1;
  ExecResult terminal;
  for (int fuel = 0; fuel <= 10000; ++fuel) {
    terminal = execute_bytecode_cpu(program, inputs, fuel);
    if (!terminal.is_error || terminal.err.code != ErrCode::Timeout) {
      boundary = fuel;
      break;
    }
  }
  require(boundary > 0, label + ": no positive finite CPU fuel boundary");
  require(terminal.is_error == expect_error, label + ": unexpected CPU fixture outcome");
  if (expect_error)
    require(terminal.err.code == ErrCode::Type, label + ": invalid input did not produce TypeError");
  CaseBindings bindings;
  for (const auto& input : inputs) bindings.push_back({input.first, input.second});
  for (int fuel : {boundary - 1, boundary, boundary + 1}) {
    const auto cpu = execute_bytecode_cpu(program, inputs, fuel);
    bool timeout = false;
    const auto gpu = JsonParser(migration::probe(program, bindings, fuel, &timeout)).parse();
    const auto& error = require_object_field(gpu, "error");
    const std::string at = label + " fuel=" + std::to_string(fuel);
    require((error.kind != JsonValue::Kind::Null) == cpu.is_error, at + ": error status differs");
    if (cpu.is_error) {
      require(require_string(error, "error") == err_code_name(cpu.err.code), at + ": error code differs");
    } else {
      const auto expected = JsonParser(migration::encode_value(cpu.value, false)).parse();
      require(canonical_json(require_object_field(gpu, "value")) == canonical_json(expected),
              at + ": exact GPU tag/value differs");
      require(require_int(require_object_field(gpu, "fuel_left"), "fuel_left") == fuel - boundary,
              at + ": GPU fuel consumption differs from CPU boundary");
    }
    require(timeout == (fuel < boundary), at + ": GPU first-non-timeout boundary differs");
  }
}

void exercise(const std::string& file, std::uint64_t seed,
              const std::vector<std::pair<std::string, Value>>& values,
              const std::string& invalid_name) {
  const auto grammar = compile_grammar(load_definition(
      std::string(GAGP_REPOSITORY_ROOT) + "/configs/grammar/compat/" + file));
  const auto generated = generate_derivation(grammar, seed);
  std::vector<InputSpec> specs;
  std::vector<std::string> names;
  std::vector<std::pair<int, Value>> inputs;
  int invalid_index = -1;
  for (const auto& input : grammar.inputs()) {
    specs.push_back({input.name, input.type});
    names.push_back(input.name);
    const auto value = std::find_if(values.begin(), values.end(),
        [&](const auto& item) { return item.first == input.name; });
    require(value != values.end(), file + ": fixture input missing");
    const int index = static_cast<int>(inputs.size());
    inputs.push_back({index, value->second});
    if (input.name == invalid_name) invalid_index = index;
  }
  const auto verified = verify_ast(generated.genome.ast, specs);
  require(static_cast<bool>(verified), file + ": generated AST failed verification");
  const auto program = compile_for_eval(generated.genome, verified.verified, names);
  compare_boundary(program, inputs, false, file + " recursive input");
  require(invalid_index >= 0, file + ": invalid-input slot missing");
  inputs[static_cast<std::size_t>(invalid_index)].second = Value::from_bool(true);
  compare_boundary(program, inputs, true, file + " invalid input");
}
}  // namespace

int main() {
  try {
    cudaDeviceProp properties{};
    std::string error;
    require(select_gpu_device(properties, error), "GPU selection failed: " + error);
    const auto source = payload::make_int_list_value(
        {Value::from_int(1), Value::from_int(2), Value::from_int(3), Value::from_int(4)});
    exercise("linear_rec_intlist_int.json", 17,
             {{"source", source}, {"start", Value::from_int(0)}}, "source");
    exercise("dc_intlist_int.json", 19, {{"source", source}}, "source");
    exercise("dp1d_int.json", 23, {{"state", Value::from_int(4)}}, "state");
    exercise("dp2d_int.json", 29,
             {{"row", Value::from_int(2)}, {"column", Value::from_int(2)}}, "row");
    std::cout << "package GPU results: four families, exact values/errors and CPU fuel boundaries passed\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
