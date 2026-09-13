#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "migration_snapshot.hpp"
#include "gagp/cli/commands.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/core/bytecode_verify.hpp"
#include "gagp/core/errors.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/cpu/fitness_cpu.hpp"
#include "gagp/runtime/payload/payload.hpp"
#ifdef GAGP_HAS_CUDA
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#endif

namespace {
using namespace gagp;
using cli_detail::JsonValue;
using cli_detail::require_object_field;

bool equal_json(const JsonValue& a, const JsonValue& b) {
  if (a.kind != b.kind) return false;
  switch (a.kind) {
    case JsonValue::Kind::Null: return true;
    case JsonValue::Kind::Bool: return a.bool_v == b.bool_v;
    case JsonValue::Kind::Number: return a.number_v == b.number_v;
    case JsonValue::Kind::String: return a.string_v == b.string_v;
    case JsonValue::Kind::Array:
      if (a.array_v.size() != b.array_v.size()) return false;
      for (std::size_t i = 0; i < a.array_v.size(); ++i) if (!equal_json(a.array_v[i], b.array_v[i])) return false;
      return true;
    case JsonValue::Kind::Object:
      if (a.object_v.size() != b.object_v.size()) return false;
      for (const auto& entry : a.object_v) {
        const auto found = b.object_v.find(entry.first);
        if (found == b.object_v.end() || !equal_json(entry.second, found->second)) return false;
      }
      return true;
  }
  return false;
}

void check_result(const ExecResult& actual, const JsonValue& expected) {
  const auto& error = require_object_field(expected, "error");
  if (actual.is_error) {
    if (error.kind != JsonValue::Kind::String || error.string_v != err_code_name(actual.err.code)) {
      throw std::runtime_error("CPU error code changed");
    }
    const auto message = expected.object_v.find("message_hex");
    if (message != expected.object_v.end()) {
      std::string hex;
      const char* digits = "0123456789abcdef";
      for (unsigned char c : actual.err.message) { hex += digits[c >> 4]; hex += digits[c & 15]; }
      if (message->second.string_v != hex) throw std::runtime_error("CPU error message changed");
    }
  } else {
    if (error.kind != JsonValue::Kind::Null) throw std::runtime_error("CPU no longer reports expected error");
    const auto encoded = cli_detail::JsonParser(migration::encode_value(actual.value, false)).parse();
    if (!equal_json(encoded, require_object_field(expected, "value"))) {
      throw std::runtime_error("CPU value/tag/bits/payload changed");
    }
  }
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 3 || argc > 4 || (argc == 4 && std::string(argv[3]) != "--gpu")) {
      throw std::runtime_error("usage: replay INPUT.jsonl SUMMARY.json [--gpu]");
    }
    const bool gpu = argc == 4;
#ifndef GAGP_HAS_CUDA
    if (gpu) throw std::runtime_error("GPU replay requires CUDA");
#endif
    std::ifstream input(argv[1]);
    if (!input) throw std::runtime_error("cannot read oracle corpus");
    std::size_t executions = 0, other_rows = 0, cpu_checks = 0, gpu_checks = 0, negative_tests = 0;
    std::size_t baseline_disagreements = 0;
    std::size_t ast_rejections = 0, bytecode_rejections = 0;
    std::size_t compilation_roundtrips = 0;
    std::string line;
    while (std::getline(input, line)) {
      const auto row = cli_detail::JsonParser(line).parse();
      const auto kind = cli_detail::require_string(require_object_field(row, "kind"), "kind");
      if (kind != "execution" && kind != "ast_rejection" && kind != "bytecode_rejection" && kind != "compilation") {
        ++other_rows;
        continue;
      }
      const int ordinal = cli_detail::require_int(require_object_field(row, "ordinal"), "ordinal");
      try {
        payload::clear();
        if (kind == "compilation") {
          const auto& frozen_population = require_object_field(row, "population");
          const auto population = migration::decode_population(frozen_population);
          if (!equal_json(cli_detail::JsonParser(migration::encode_population(population)).parse(), frozen_population))
            throw std::runtime_error("compilation AST/payload roundtrip changed");
          const auto& frozen_bytecode = require_object_field(row, "bytecode");
          const auto program = migration::decode_bytecode(frozen_bytecode);
          if (!equal_json(cli_detail::JsonParser(migration::encode_bytecode(program)).parse(), frozen_bytecode))
            throw std::runtime_error("compilation bytecode roundtrip changed");
          ++compilation_roundtrips;
          continue;
        }
        if (kind == "ast_rejection") {
          // The frozen rejection sources use empty inputs and default options.
          // Decode structure without whole-population acceptance or metadata
          // construction: the rejected input itself is the object under test.
          const auto& frozen = require_object_field(row, "population");
          if (require_object_field(frozen, "format_version").string_v != "migration-population-v1")
            throw std::runtime_error("unsupported rejection population format");
          const auto& programs = require_object_field(frozen, "programs");
          if (programs.kind != JsonValue::Kind::Array || programs.array_v.size() != 1)
            throw std::runtime_error("expected one rejected AST");
          const auto& saved = programs.array_v.front();
          auto ast = cli_detail::decode_ast_json(require_object_field(saved, "structure"));
          if (!ast.consts.empty()) throw std::runtime_error("rejection duplicates constants");
          for (const auto& value : require_object_field(saved, "constants").array_v)
            ast.consts.push_back(migration::decode_value(value));
          const auto roundtrip = cli_detail::JsonParser(
              migration::encode_population({evo::ProgramGenome{ast, {}}}, false)).parse();
          if (!equal_json(roundtrip, frozen)) throw std::runtime_error("rejected AST roundtrip changed");
          const auto result = evo::verify_ast(ast, {});
          if (result.ok || require_object_field(row, "code").string_v != evo::verify_code_name(result.diagnostic.code) ||
              require_object_field(row, "node").number_v != static_cast<double>(result.diagnostic.node_index))
            throw std::runtime_error("AST verifier rejection changed");
          std::string message;
          const char* digits = "0123456789abcdef";
          for (unsigned char c : result.diagnostic.message) { message += digits[c >> 4]; message += digits[c & 15]; }
          if (require_object_field(row, "message_hex").string_v != message)
            throw std::runtime_error("AST verifier diagnostic changed");
          ++ast_rejections;
          continue;
        }
        const auto& frozen = require_object_field(row, "bytecode");
        const auto program = migration::decode_bytecode(frozen);
        if (!equal_json(cli_detail::JsonParser(migration::encode_bytecode(program, false)).parse(), frozen)) {
          throw std::runtime_error("bytecode/phase snapshot roundtrip changed");
        }
        if (kind == "bytecode_rejection") {
          const auto& raw_options = require_object_field(row, "options");
          BytecodeVerifyOptions options;
          const auto& allow_private = require_object_field(raw_options, "allow_private_opcodes");
          if (allow_private.kind != JsonValue::Kind::Bool) throw std::runtime_error("invalid verifier boolean");
          options.allow_private_opcodes = allow_private.bool_v;
          const auto limit = [&](const char* key) {
            const int value = cli_detail::require_int(require_object_field(raw_options, key), key);
            if (value < 0) throw std::runtime_error("negative verifier resource limit");
            return static_cast<std::size_t>(value);
          };
          options.max_instructions_per_code = limit("max_instructions_per_code");
          options.max_constants_per_code = limit("max_constants_per_code");
          options.max_locals_per_code = limit("max_locals_per_code");
          options.max_segments = limit("max_segments");
          options.max_stack_depth = limit("max_stack_depth");
          const auto result = verify_bytecode(program, options);
          if (result.ok || require_object_field(row, "code").string_v != bytecode_verify_code_name(result.diagnostic.code) ||
              require_object_field(row, "instruction").number_v != static_cast<double>(result.diagnostic.instruction_index))
            throw std::runtime_error("bytecode verifier rejection changed");
          const auto hex = [](const std::string& text) {
            std::string encoded;
            const char* digits = "0123456789abcdef";
            for (unsigned char c : text) { encoded += digits[c >> 4]; encoded += digits[c & 15]; }
            return encoded;
          };
          if (require_object_field(row, "path_hex").string_v != hex(result.diagnostic.path) ||
              require_object_field(row, "message_hex").string_v != hex(result.diagnostic.message))
            throw std::runtime_error("bytecode verifier diagnostic changed");
          ++bytecode_rejections;
          continue;
        }
        const auto verified = verify_bytecode(program);
        if (row.object_v.count("bytecode_verifier") &&
            require_object_field(row, "bytecode_verifier").string_v != bytecode_verify_code_name(verified.diagnostic.code)) {
          throw std::runtime_error("bytecode verifier result changed");
        }
        if (!verified.ok) {
          // Only explicitly archived runtime-negative tests may bypass the
          // verifier in this test adapter. Production ingestion is unchanged.
          const auto marker = row.object_v.find("runtime_negative_test");
          if (!row.object_v.count("bytecode_verifier") || marker == row.object_v.end() ||
              marker->second.kind != JsonValue::Kind::Bool || !marker->second.bool_v) {
            throw std::runtime_error(std::string("unmarked bytecode rejection: ") +
                                      bytecode_verify_code_name(verified.diagnostic.code));
          }
          ++negative_tests;
        }
        std::vector<std::pair<int, Value>> inputs;
        CaseBindings bindings;
        const auto& raw_inputs = require_object_field(row, "inputs");
        if (raw_inputs.kind != JsonValue::Kind::Array) throw std::runtime_error("invalid oracle inputs");
        for (const auto& item : raw_inputs.array_v) {
          if (item.kind != JsonValue::Kind::Array || item.array_v.size() != 2) throw std::runtime_error("invalid oracle binding");
          const int index = cli_detail::require_int(item.array_v[0], "local index");
          const Value value = migration::decode_value(item.array_v[1]);
          inputs.emplace_back(index, value);
          bindings.push_back({index, value});
        }
        const int fuel = cli_detail::require_int(require_object_field(row, "fuel"), "fuel");
        check_result(execute_bytecode_cpu(program, inputs, fuel), require_object_field(row, "result"));
        ++cpu_checks;
        const int cap = cli_detail::require_int(require_object_field(row, "probe_cap"), "probe_cap");
        check_result(execute_bytecode_cpu(program, inputs, cap), require_object_field(row, "at_probe_cap"));
        ++cpu_checks;
        const auto& boundary = require_object_field(row, "first_non_timeout_fuel");
        if (boundary.kind != JsonValue::Kind::Null) {
          const int bound = cli_detail::require_int(boundary, "fuel boundary");
          check_result(execute_bytecode_cpu(program, inputs, bound), require_object_field(row, "at_boundary"));
          ++cpu_checks;
          if (bound > 0) {
            check_result(execute_bytecode_cpu(program, inputs, bound - 1), require_object_field(row, "below_boundary"));
            ++cpu_checks;
          }
        }
        if (row.object_v.count("expected")) {
          const auto expected = migration::decode_value(require_object_field(row, "expected"));
          const double penalty = require_object_field(row, "penalty").number_v;
          const double expected_fitness = require_object_field(row, "fitness").number_v;
          if (row.object_v.count("gpu_fitness") &&
              require_object_field(row, "gpu_fitness").number_v != expected_fitness) ++baseline_disagreements;
          const auto scores = eval_fitness_cpu({program}, {bindings}, {expected}, fuel, penalty, 1);
          if (scores.at(0) != expected_fitness) throw std::runtime_error("CPU fitness changed");
          ++cpu_checks;
#ifdef GAGP_HAS_CUDA
          if (gpu) {
            FitnessSessionGpu session;
            const auto initialized = session.init({bindings}, {expected}, fuel, 256, penalty);
            if (!initialized.ok) throw std::runtime_error("GPU init failed: " + initialized.err.message);
            const auto measured = session.eval_programs({program});
            if (!measured.ok) throw std::runtime_error("GPU eval failed: " + measured.err.message);
            const double expected_gpu = row.object_v.count("gpu_fitness") ?
                require_object_field(row, "gpu_fitness").number_v : expected_fitness;
            if (measured.fitness.at(0) != expected_gpu) throw std::runtime_error("GPU fitness changed");
            ++gpu_checks;
          }
#endif
        }
        ++executions;
      } catch (const std::exception& error) {
        throw std::runtime_error("ordinal " + std::to_string(ordinal) + ": " + error.what());
      }
    }
    if (executions == 0 && ast_rejections == 0 && bytecode_rejections == 0 && compilation_roundtrips == 0)
      throw std::runtime_error("empty replay corpus");
    std::ofstream out(argv[2]);
    out << "{\"format_version\":\"migration-oracle-replay-v1\",\"execution_rows\":" << executions
        << ",\"bytecode_roundtrips\":" << executions << ",\"cpu_checks\":" << cpu_checks
        << ",\"gpu_fitness_checks\":" << gpu_checks << ",\"non_execution_rows\":" << other_rows
        << ",\"verifier_negative_executions\":" << negative_tests
        << ",\"ast_rejection_checks\":" << ast_rejections
        << ",\"bytecode_rejection_checks\":" << bytecode_rejections
        << ",\"compilation_roundtrips\":" << compilation_roundtrips
        << ",\"baseline_fitness_disagreements\":" << baseline_disagreements << "}\n";
    if (!out) throw std::runtime_error("cannot write replay summary");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "replay: " << error.what() << '\n';
    return 1;
  }
}
