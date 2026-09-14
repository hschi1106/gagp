#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "migration_snapshot.hpp"
#include "gagp/cli/json.hpp"
#include "gagp/core/bytecode_verify.hpp"
#include "gagp/core/errors.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/transition/linear_rec.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"

namespace {

using Clock = std::chrono::steady_clock;
using gagp::BytecodeProgram;
using gagp::ErrCode;
using gagp::ExecResult;
using gagp::Value;
using gagp::cli_detail::JsonParser;
using gagp::cli_detail::JsonValue;
using gagp::cli_detail::require_int;
using gagp::cli_detail::require_object_field;
using gagp::cli_detail::require_string;
using gagp::evo::NodeKind;
using gagp::evo::ProgramGenome;

constexpr int k_timing_repetitions = 7;
volatile std::uint64_t result_sink = 0;

struct PendingCompilation {
  int ordinal = -1;
  ProgramGenome genome;
  BytecodeProgram bytecode;
};

struct Fixture {
  int compilation_ordinal = -1;
  int execution_ordinal = -1;
  ProgramGenome original;
  BytecodeProgram reference;
  int archived_fuel = 0;
  int probe_cap = 0;
  int boundary = 0;
  JsonValue archived_result;
  JsonValue archived_at_cap;
  JsonValue archived_at_boundary;
  JsonValue archived_below_boundary;
  bool has_below_boundary = false;
};

struct Measurement {
  int compilation_ordinal = -1;
  int execution_ordinal = -1;
  std::size_t original_nodes = 0;
  std::size_t transformed_nodes = 0;
  std::size_t original_instructions = 0;
  std::size_t transformed_instructions = 0;
  int archived_fuel = 0;
  int probe_cap = 0;
  int minimum_fuel = 0;
  std::size_t archived_checkpoint_checks = 0;
  std::size_t differential_checks = 0;
  std::vector<double> original_ns;
  std::vector<double> transformed_ns;
  double original_median_ns = 0.0;
  double transformed_median_ns = 0.0;
};

bool contains_linear_rec(const ProgramGenome& genome) {
  for (const auto& node : genome.ast.nodes) {
    if (node.kind == NodeKind::LINEAR_REC) return true;
  }
  return false;
}

int positive_iterations(const std::string& text) {
  std::size_t consumed = 0;
  const long long value = std::stoll(text, &consumed);
  if (consumed != text.size() || value <= 0 || value > std::numeric_limits<int>::max()) {
    throw std::runtime_error("iterations must be an integer in 1..INT_MAX");
  }
  return static_cast<int>(value);
}

bool same_result(const ExecResult& left, const ExecResult& right) {
  if (left.is_error != right.is_error) return false;
  if (left.is_error) return left.err.code == right.err.code;
  return gagp::migration::encode_value(left.value) ==
         gagp::migration::encode_value(right.value);
}

void check_archived_result(const ExecResult& actual, const JsonValue& expected,
                           int ordinal, const char* checkpoint) {
  const JsonValue& error = require_object_field(expected, "error");
  if (actual.is_error) {
    if (error.kind != JsonValue::Kind::String ||
        error.string_v != gagp::err_code_name(actual.err.code)) {
      throw std::runtime_error("ordinal " + std::to_string(ordinal) +
                               " changed archived " + checkpoint + " error");
    }
    return;
  }
  if (error.kind != JsonValue::Kind::Null) {
    throw std::runtime_error("ordinal " + std::to_string(ordinal) +
                             " no longer reports archived " + checkpoint + " error");
  }
  const Value expected_value =
      gagp::migration::decode_value(require_object_field(expected, "value"));
  if (gagp::migration::encode_value(actual.value) !=
      gagp::migration::encode_value(expected_value)) {
    throw std::runtime_error("ordinal " + std::to_string(ordinal) +
                             " changed archived " + checkpoint + " value");
  }
}

void require_empty_inputs(const JsonValue& execution, int ordinal) {
  const JsonValue& inputs = require_object_field(execution, "inputs");
  if (inputs.kind != JsonValue::Kind::Array || !inputs.array_v.empty()) {
    throw std::runtime_error("ordinal " + std::to_string(ordinal) +
                             " has inputs; archived-fixture benchmark requires none");
  }
}

Fixture pair_fixture(PendingCompilation pending, const JsonValue& execution) {
  if (require_string(require_object_field(execution, "kind"), "kind") != "execution") {
    throw std::runtime_error("LinearRec compilation is not followed by an execution record");
  }
  const int execution_ordinal =
      require_int(require_object_field(execution, "ordinal"), "ordinal");
  if (execution_ordinal != pending.ordinal + 1) {
    throw std::runtime_error("LinearRec compilation/execution ordinals are not consecutive");
  }
  require_empty_inputs(execution, execution_ordinal);
  if (require_string(require_object_field(execution, "bytecode_verifier"),
                     "bytecode_verifier") != "ok") {
    throw std::runtime_error("archived LinearRec bytecode is not verifier-valid");
  }

  BytecodeProgram execution_bytecode =
      gagp::migration::decode_bytecode(require_object_field(execution, "bytecode"));
  if (gagp::migration::encode_bytecode(pending.bytecode) !=
      gagp::migration::encode_bytecode(execution_bytecode)) {
    throw std::runtime_error("compilation and following execution bytecode differ");
  }
  const auto bytecode_check = gagp::verify_bytecode(execution_bytecode);
  if (!bytecode_check.ok) {
    throw std::runtime_error("frozen LinearRec bytecode fails the current verifier");
  }

  const JsonValue& boundary_raw =
      require_object_field(execution, "first_non_timeout_fuel");
  if (boundary_raw.kind == JsonValue::Kind::Null) {
    throw std::runtime_error("archived LinearRec execution has no finite fuel boundary");
  }
  Fixture fixture;
  fixture.compilation_ordinal = pending.ordinal;
  fixture.execution_ordinal = execution_ordinal;
  fixture.original = std::move(pending.genome);
  fixture.reference = std::move(execution_bytecode);
  fixture.archived_fuel = require_int(require_object_field(execution, "fuel"), "fuel");
  fixture.probe_cap = require_int(require_object_field(execution, "probe_cap"), "probe_cap");
  fixture.boundary = require_int(boundary_raw, "first_non_timeout_fuel");
  if (fixture.archived_fuel < 0 || fixture.probe_cap < 0 || fixture.boundary < 0 ||
      fixture.boundary > fixture.probe_cap) {
    throw std::runtime_error("archived LinearRec fuel fields are invalid");
  }
  fixture.archived_result = require_object_field(execution, "result");
  fixture.archived_at_cap = require_object_field(execution, "at_probe_cap");
  fixture.archived_at_boundary = require_object_field(execution, "at_boundary");
  const auto below = execution.object_v.find("below_boundary");
  fixture.has_below_boundary = below != execution.object_v.end();
  if (fixture.has_below_boundary) fixture.archived_below_boundary = below->second;
  return fixture;
}

std::vector<Fixture> read_fixtures(const std::string& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot read " + path);
  std::vector<Fixture> fixtures;
  bool awaiting_execution = false;
  PendingCompilation pending;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty()) continue;
    const JsonValue row = JsonParser(line).parse();
    if (awaiting_execution) {
      fixtures.push_back(pair_fixture(std::move(pending), row));
      awaiting_execution = false;
      continue;
    }
    const std::string kind = require_string(require_object_field(row, "kind"), "kind");
    if (kind != "compilation") continue;
    const auto population =
        gagp::migration::decode_population(require_object_field(row, "population"));
    if (population.size() != 1) {
      throw std::runtime_error("archived compilation population must contain one AST");
    }
    if (!contains_linear_rec(population.front())) continue;
    pending.ordinal = require_int(require_object_field(row, "ordinal"), "ordinal");
    pending.genome = population.front();
    pending.bytecode =
        gagp::migration::decode_bytecode(require_object_field(row, "bytecode"));
    awaiting_execution = true;
  }
  if (awaiting_execution) {
    throw std::runtime_error("LinearRec compilation is missing its execution record");
  }
  const std::set<int> expected{12, 14, 16, 30};
  std::set<int> actual;
  for (const auto& fixture : fixtures) actual.insert(fixture.compilation_ordinal);
  if (fixtures.size() != expected.size() || actual != expected) {
    throw std::runtime_error("archive does not contain the four authoritative LinearRec pairs");
  }
  return fixtures;
}

int first_non_timeout(const BytecodeProgram& program, int probe_cap) {
  for (int fuel = 0; fuel <= probe_cap; ++fuel) {
    const ExecResult result = gagp::execute_bytecode_cpu(program, {}, fuel);
    if (!result.is_error || result.err.code != ErrCode::Timeout) return fuel;
  }
  return -1;
}

std::uint64_t result_token(const ExecResult& result) {
  if (result.is_error) {
    return 0x8000000000000000ULL ^ static_cast<std::uint64_t>(result.err.code);
  }
  return Value::shallow_hash64(result.value);
}

double time_program(const BytecodeProgram& program, int fuel, int iterations) {
  const auto start = Clock::now();
  std::uint64_t batch = 0;
  for (int i = 0; i < iterations; ++i) {
    batch ^= result_token(gagp::execute_bytecode_cpu(program, {}, fuel)) +
             static_cast<std::uint64_t>(i);
  }
  result_sink = result_sink ^ batch;
  const auto elapsed =
      std::chrono::duration<double, std::nano>(Clock::now() - start).count();
  return elapsed / static_cast<double>(iterations);
}

double median(std::vector<double> samples) {
  if (samples.size() != k_timing_repetitions) {
    throw std::logic_error("timing sample count changed");
  }
  std::sort(samples.begin(), samples.end());
  return samples[samples.size() / 2];
}

Measurement measure_fixture(const Fixture& fixture, int iterations) {
  check_archived_result(
      gagp::execute_bytecode_cpu(fixture.reference, {}, fixture.archived_fuel),
      fixture.archived_result, fixture.execution_ordinal, "fuel/result");
  check_archived_result(
      gagp::execute_bytecode_cpu(fixture.reference, {}, fixture.probe_cap),
      fixture.archived_at_cap, fixture.execution_ordinal, "probe_cap");
  check_archived_result(
      gagp::execute_bytecode_cpu(fixture.reference, {}, fixture.boundary),
      fixture.archived_at_boundary, fixture.execution_ordinal, "boundary");
  if (fixture.boundary > 0) {
    if (!fixture.has_below_boundary) {
      throw std::runtime_error("archived positive fuel boundary has no below_boundary result");
    }
    check_archived_result(
        gagp::execute_bytecode_cpu(fixture.reference, {}, fixture.boundary - 1),
        fixture.archived_below_boundary, fixture.execution_ordinal, "below_boundary");
  }
  const int computed_boundary = first_non_timeout(fixture.reference, fixture.probe_cap);
  if (computed_boundary != fixture.boundary) {
    throw std::runtime_error("ordinal " + std::to_string(fixture.execution_ordinal) +
                             " changed archived minimum fuel");
  }

  ProgramGenome transformed =
      gagp::evo::transition::lower_linear_rec(fixture.original, {});
  if (contains_linear_rec(transformed)) {
    throw std::runtime_error("LinearRec transition left a legacy source node");
  }
  const auto verified = gagp::evo::verify_ast(transformed.ast, {});
  if (!verified.ok) {
    throw std::runtime_error(std::string("transformed AST verification failed: ") +
                             gagp::evo::verify_code_name(verified.diagnostic.code) +
                             " " + verified.diagnostic.message);
  }
  const BytecodeProgram candidate =
      gagp::evo::compile_for_eval(transformed, verified.verified);

  Measurement result;
  result.compilation_ordinal = fixture.compilation_ordinal;
  result.execution_ordinal = fixture.execution_ordinal;
  result.original_nodes = fixture.original.ast.nodes.size();
  result.transformed_nodes = transformed.ast.nodes.size();
  result.original_instructions = fixture.reference.code.size();
  result.transformed_instructions = candidate.code.size();
  result.archived_fuel = fixture.archived_fuel;
  result.probe_cap = fixture.probe_cap;
  result.minimum_fuel = computed_boundary;
  result.archived_checkpoint_checks = fixture.boundary > 0 ? 4 : 3;
  for (int fuel = 0; fuel <= computed_boundary + 3; ++fuel) {
    const ExecResult reference_result =
        gagp::execute_bytecode_cpu(fixture.reference, {}, fuel);
    const ExecResult candidate_result =
        gagp::execute_bytecode_cpu(candidate, {}, fuel);
    if (!same_result(reference_result, candidate_result)) {
      throw std::runtime_error("ordinal " + std::to_string(fixture.execution_ordinal) +
                               " transition differs at fuel " + std::to_string(fuel));
    }
    ++result.differential_checks;
  }
  const ExecResult reference_cap =
      gagp::execute_bytecode_cpu(fixture.reference, {}, fixture.probe_cap);
  const ExecResult candidate_cap =
      gagp::execute_bytecode_cpu(candidate, {}, fixture.probe_cap);
  if (!same_result(reference_cap, candidate_cap)) {
    throw std::runtime_error("ordinal " + std::to_string(fixture.execution_ordinal) +
                             " transition differs at probe_cap");
  }
  ++result.differential_checks;

  const int warmup_iterations = std::min(iterations, 1000);
  (void)time_program(fixture.reference, fixture.probe_cap, warmup_iterations);
  (void)time_program(candidate, fixture.probe_cap, warmup_iterations);
  result.original_ns.reserve(k_timing_repetitions);
  result.transformed_ns.reserve(k_timing_repetitions);
  for (int repetition = 0; repetition < k_timing_repetitions; ++repetition) {
    // Alternate order to reduce a stable first-run bias between implementations.
    if ((repetition & 1) == 0) {
      result.original_ns.push_back(
          time_program(fixture.reference, fixture.probe_cap, iterations));
      result.transformed_ns.push_back(
          time_program(candidate, fixture.probe_cap, iterations));
    } else {
      result.transformed_ns.push_back(
          time_program(candidate, fixture.probe_cap, iterations));
      result.original_ns.push_back(
          time_program(fixture.reference, fixture.probe_cap, iterations));
    }
  }
  result.original_median_ns = median(result.original_ns);
  result.transformed_median_ns = median(result.transformed_ns);
  return result;
}

void emit_samples(std::ostream& out, const std::vector<double>& samples) {
  out << '[';
  for (std::size_t i = 0; i < samples.size(); ++i) {
    if (i) out << ',';
    out << samples[i];
  }
  out << ']';
}

std::string encode_report(const std::string& input_path,
                          const std::vector<Measurement>& measurements,
                          int iterations) {
  std::ostringstream out;
  out << std::setprecision(17)
      << "{\"format_version\":\"linear-rec-transition-benchmark-v1\""
      << ",\"input\":\"";
  for (char c : input_path) {
    if (c == '\\' || c == '"') out << '\\';
    out << c;
  }
  out << "\",\"iterations\":" << iterations
      << ",\"timing_repetitions\":" << k_timing_repetitions
      << ",\"fixtures\":[";
  std::size_t total_checks = 0;
  for (std::size_t i = 0; i < measurements.size(); ++i) {
    if (i) out << ',';
    const Measurement& row = measurements[i];
    total_checks += row.differential_checks;
    out << "{\"compilation_ordinal\":" << row.compilation_ordinal
        << ",\"execution_ordinal\":" << row.execution_ordinal
        << ",\"original_ast_nodes\":" << row.original_nodes
        << ",\"transformed_ast_nodes\":" << row.transformed_nodes
        << ",\"original_instructions\":" << row.original_instructions
        << ",\"transformed_instructions\":" << row.transformed_instructions
        << ",\"archived_fuel\":" << row.archived_fuel
        << ",\"probe_cap\":" << row.probe_cap
        << ",\"minimum_fuel\":" << row.minimum_fuel
        << ",\"archived_checkpoint_checks\":" << row.archived_checkpoint_checks
        << ",\"differential_checks\":" << row.differential_checks
        << ",\"original_ns_per_run_samples\":";
    emit_samples(out, row.original_ns);
    out << ",\"transformed_ns_per_run_samples\":";
    emit_samples(out, row.transformed_ns);
    out << ",\"original_ns_per_run_median\":" << row.original_median_ns
        << ",\"transformed_ns_per_run_median\":" << row.transformed_median_ns
        << '}';
  }
  out << "],\"differential_checks\":" << total_checks << '}';
  return out.str();
}

void write_file(const std::string& path, const std::string& text) {
  std::ofstream output(path);
  if (!output || !(output << text << '\n')) {
    throw std::runtime_error("cannot write " + path);
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 3 || argc > 4) {
      throw std::runtime_error(
          "usage: benchmark_linear_transition INPUT.jsonl OUTPUT.json [iterations]");
    }
    const int iterations = argc == 4 ? positive_iterations(argv[3]) : 10000;
    const std::vector<Fixture> fixtures = read_fixtures(argv[1]);
    std::vector<Measurement> measurements;
    measurements.reserve(fixtures.size());
    for (const Fixture& fixture : fixtures) {
      measurements.push_back(measure_fixture(fixture, iterations));
    }
    const std::string report = encode_report(argv[1], measurements, iterations);
    write_file(argv[2], report);
    std::cout << report << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "benchmark_linear_transition: " << error.what() << '\n';
    return 1;
  }
}
