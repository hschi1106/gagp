// Built by the migration capture tool against the immutable reference archives.
// The included test source remains unchanged; wrappers observe its production
// compiler/runtime calls and preserve the original test assertions and exit code.
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "migration_snapshot.hpp"
#include "gagp/core/bytecode_verify.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/cpu/fitness_cpu.hpp"
#include "gagp/runtime/payload/payload.hpp"
#ifdef GAGP_CAPTURE_GPU
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#endif

namespace gagp::migration {
std::ofstream capture_output;
std::size_t ordinal = 0;
std::string observation_name;

struct PreservePayloadRegistry {
  std::vector<payload::StringSnapshot> strings = payload::snapshot_strings();
  std::vector<payload::ListSnapshot> lists = payload::snapshot_lists();
  ~PreservePayloadRegistry() {
    payload::clear();
    for (const auto& item : strings) payload::register_string(item.key, item.data);
    for (const auto& item : lists) payload::register_list(item.key, item.elems);
  }
};

std::string text_hex(const std::string& text) {
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (unsigned char c : text) out << std::setw(2) << static_cast<unsigned>(c);
  return out.str();
}

std::string encode_result(const ExecResult& result) {
  if (result.is_error) return std::string("{\"error\":\"") + err_code_name(result.err.code) +
      "\",\"message_hex\":\"" + text_hex(result.err.message) + "\"}";
  return "{\"error\":null,\"value\":" + encode_value(result.value, false) + "}";
}

void record_execution(const BytecodeProgram& program,
                      const std::vector<std::pair<int, Value>>& inputs, int fuel,
                      const ExecResult& result, const Value* expected = nullptr,
                      double penalty = 1.0) {
  PreservePayloadRegistry registry;
  const auto verified = verify_bytecode(program);
  capture_output << "{\"kind\":\"execution\",\"ordinal\":" << ordinal++;
  if (!observation_name.empty()) capture_output << ",\"observation_name_hex\":\"" << text_hex(observation_name) << '"';
  capture_output << ",\"bytecode_verifier\":\"" << bytecode_verify_code_name(verified.diagnostic.code)
      << "\",\"runtime_negative_test\":" << (verified.ok ? "false" : "true")
      << ",\"bytecode\":" << encode_bytecode(program, false) << ",\"inputs\":[";
  for (std::size_t i = 0; i < inputs.size(); ++i) {
    if (i) capture_output << ',';
    capture_output << '[' << inputs[i].first << ',' << encode_value(inputs[i].second, false) << ']';
  }
  capture_output << "],\"fuel\":" << fuel << ",\"result\":" << encode_result(result);
  const Value derived_expected = result.is_error ? Value::from_int(0) : result.value;
  if (!expected) expected = &derived_expected;
  if (expected) {
    CaseBindings bindings;
    for (const auto& input : inputs) bindings.push_back({input.first, input.second});
    const auto fitness = eval_fitness_cpu({program}, {bindings}, {*expected}, fuel, penalty, 1);
    capture_output << ",\"expected\":" << encode_value(*expected, false)
        << ",\"penalty\":" << std::setprecision(17) << penalty
        << ",\"fitness\":" << fitness.at(0);
#ifdef GAGP_CAPTURE_GPU
    FitnessSessionGpu session;
    const auto init = session.init({bindings}, {*expected}, fuel, 256, penalty);
    if (!init.ok) capture_output << ",\"gpu_error_hex\":\"" << text_hex(init.err.message) << '"';
    else {
      const auto gpu_result = session.eval_programs({program});
      if (!gpu_result.ok) capture_output << ",\"gpu_error_hex\":\"" << text_hex(gpu_result.err.message) << '"';
      else capture_output << ",\"gpu_fitness\":" << gpu_result.fitness.at(0);
    }
#endif
  }
  // Probe beyond a deliberately low-fuel test, retaining the original outcome.
  // The cap is explicit; nontermination is not relabelled as a successful boundary.
  constexpr int probe_cap = 20000;
  const auto terminal = execute_bytecode_cpu(program, inputs, probe_cap);
  capture_output << ",\"probe_cap\":" << probe_cap << ",\"at_probe_cap\":" << encode_result(terminal)
      << ",\"first_non_timeout_fuel\":";
  if (terminal.is_error && terminal.err.code == ErrCode::Timeout) capture_output << "null";
  else {
    int lo = 0, hi = probe_cap;
    while (lo < hi) {
      const int mid = lo + (hi - lo) / 2;
      const auto probe = execute_bytecode_cpu(program, inputs, mid);
      if (probe.is_error && probe.err.code == ErrCode::Timeout) lo = mid + 1;
      else hi = mid;
    }
    capture_output << lo << ",\"at_boundary\":" << encode_result(execute_bytecode_cpu(program, inputs, lo));
    if (lo > 0) capture_output << ",\"below_boundary\":"
        << encode_result(execute_bytecode_cpu(program, inputs, lo - 1));
  }
  capture_output << "}\n";
  if (!capture_output) throw std::runtime_error("oracle capture write failed");
}
}  // namespace gagp::migration

namespace gagp {
BytecodeVerifyResult capture_verify_bytecode(const BytecodeProgram& program,
                                             const BytecodeVerifyOptions& options = {}) {
  const auto result = verify_bytecode(program, options);
  if (!result.ok) {
    migration::capture_output << "{\"kind\":\"bytecode_rejection\",\"ordinal\":" << migration::ordinal++
        << ",\"bytecode\":" << migration::encode_bytecode(program, false)
        << ",\"options\":{\"allow_private_opcodes\":" << (options.allow_private_opcodes ? "true" : "false")
        << ",\"max_instructions_per_code\":" << options.max_instructions_per_code
        << ",\"max_constants_per_code\":" << options.max_constants_per_code
        << ",\"max_locals_per_code\":" << options.max_locals_per_code
        << ",\"max_segments\":" << options.max_segments
        << ",\"max_stack_depth\":" << options.max_stack_depth << '}'
        << ",\"code\":\"" << bytecode_verify_code_name(result.diagnostic.code)
        << "\",\"instruction\":" << result.diagnostic.instruction_index
        << ",\"path_hex\":\"" << migration::text_hex(result.diagnostic.path)
        << "\",\"message_hex\":\"" << migration::text_hex(result.diagnostic.message) << "\"}\n";
  }
  return result;
}

ExecResult capture_execute_bytecode_cpu(const BytecodeProgram& program,
    const std::vector<std::pair<int, Value>>& inputs, int fuel = 10000) {
  const auto result = execute_bytecode_cpu(program, inputs, fuel);
  migration::record_execution(program, inputs, fuel, result);
  return result;
}

std::vector<double> capture_eval_fitness_cpu(const std::vector<BytecodeProgram>& programs,
    const std::vector<CaseBindings>& cases, const std::vector<Value>& expected,
    int fuel = 10000, double penalty = 1.0, int lanes = 1) {
  const auto fitness = eval_fitness_cpu(programs, cases, expected, fuel, penalty, lanes);
  for (const auto& program : programs) {
    for (std::size_t c = 0; c < cases.size(); ++c) {
      std::vector<std::pair<int, Value>> inputs;
      for (const auto& binding : cases[c]) inputs.emplace_back(binding.idx, binding.value);
      migration::record_execution(program, inputs, fuel, execute_bytecode_cpu(program, inputs, fuel),
                                  &expected.at(c), penalty);
    }
  }
  return fitness;
}
}  // namespace gagp

namespace gagp::evo {
BytecodeProgram capture_compile_for_eval(const ProgramGenome& genome,
                                         const std::vector<std::string>& locals = {}) {
  const auto program = compile_for_eval(genome, locals);
  migration::capture_output << "{\"kind\":\"compilation\",\"ordinal\":" << migration::ordinal++
      << ",\"population\":" << migration::encode_population({genome})
      << ",\"bytecode\":" << migration::encode_bytecode(program) << "}\n";
  return program;
}

AstVerifyResult capture_verify_ast(const AstProgram& ast, const std::vector<InputSpec>& inputs,
                                   const VerifyOptions& options = {}) {
  const auto result = verify_ast(ast, inputs, options);
  if (!result.ok) {
    if (!inputs.empty() || options.max_nodes || options.max_expression_depth || options.max_statements ||
        options.max_metadata_entries || options.grammar_config != nullptr) {
      throw std::runtime_error("AST rejection capture requires empty inputs and default verifier options");
    }
    migration::capture_output << "{\"kind\":\"ast_rejection\",\"ordinal\":" << migration::ordinal++
        << ",\"population\":" << migration::encode_population({ProgramGenome{ast, {}}}, false)
        << ",\"code\":\"" << verify_code_name(result.diagnostic.code) << "\",\"node\":"
        << result.diagnostic.node_index << ",\"message_hex\":\""
        << migration::text_hex(result.diagnostic.message) << "\"}\n";
  }
  return result;
}
}  // namespace gagp::evo

// The reference headers above are already parsed before these names are remapped.
#define execute_bytecode_cpu capture_execute_bytecode_cpu
#define eval_fitness_cpu capture_eval_fitness_cpu
#define compile_for_eval capture_compile_for_eval
#define verify_ast capture_verify_ast
#define verify_bytecode capture_verify_bytecode
#define main baseline_test_main
#include GAGP_ORACLE_SOURCE
#ifdef GAGP_EXTRA_BOUNDARIES
#include "migration_boundary_cases.hpp"
#endif
#undef main
#undef verify_ast
#undef verify_bytecode
#undef compile_for_eval
#undef eval_fitness_cpu
#undef execute_bytecode_cpu

int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("expected oracle JSONL output path");
    gagp::migration::capture_output.open(argv[1]);
    if (!gagp::migration::capture_output) throw std::runtime_error("cannot open oracle output");
    const int tested = baseline_test_main();
    if (tested) return tested;
#ifdef GAGP_EXTRA_BOUNDARIES
    return run_extra_boundary_cases();
#else
    return 0;
#endif
  } catch (const std::exception& error) {
    std::cerr << "capture: " << error.what() << '\n';
    return 1;
  }
}
