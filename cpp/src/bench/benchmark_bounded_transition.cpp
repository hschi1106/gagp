#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
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
#include "gagp/core/semantic_fuel.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/transition/bounded_regions.hpp"
#include "gagp/runtime/cpu/execution_session.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"

namespace {

using Clock = std::chrono::steady_clock;
using gagp::BytecodeProgram;
using gagp::CpuExecutionSession;
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
constexpr int k_default_iterations = 10000;
const std::set<int> k_compilation_ordinals{
    3037, 3039, 3041, 3042, 3092, 3094, 3116, 3118};
// These frozen compiler probes intentionally bypass native scope verification.
// Account for their archived runtime errors without treating them as typed ASTs.
const std::set<int> k_undefined_local_ordinals{
    3037, 3039, 3092, 3094, 3116, 3118};
volatile std::uint64_t result_sink = 0;
volatile std::uint64_t validation_sink = 0;

struct Observation {
  int ordinal = -1;
  int archived_fuel = 0;
  int probe_cap = 0;
  int boundary = 0;
  JsonValue result;
  JsonValue at_cap;
  JsonValue at_boundary;
  JsonValue below_boundary;
  bool has_below_boundary = false;
};

struct Fixture {
  int compilation_ordinal = -1;
  std::string family;
  ProgramGenome original;
  BytecodeProgram reference;
  std::vector<Observation> observations;
};

struct Measurement {
  int compilation_ordinal = -1;
  bool translated = true;
  std::size_t rejection_node = 0;
  std::string family;
  std::vector<int> execution_ordinals;
  std::size_t original_nodes = 0;
  std::size_t transformed_nodes = 0;
  std::size_t original_instructions = 0;
  std::size_t transformed_instructions = 0;
  int timing_fuel = 0;
  std::size_t archived_checkpoint_checks = 0;
  std::size_t differential_checks = 0;
  std::size_t session_checkpoint_checks = 0;
  std::size_t session_differential_checks = 0;
  std::size_t session_original_retained_region_bytes = 0;
  std::size_t session_transformed_retained_region_bytes = 0;
  std::vector<double> original_ns;
  std::vector<double> transformed_ns;
  std::vector<double> session_original_ns;
  std::vector<double> session_transformed_ns;
  std::vector<double> session_original_cold_ns;
  std::vector<double> session_transformed_cold_ns;
  std::vector<double> validation_ns;
  double original_median_ns = 0.0;
  double transformed_median_ns = 0.0;
  double session_original_median_ns = 0.0;
  double session_transformed_median_ns = 0.0;
  double session_original_cold_median_ns = 0.0;
  double session_transformed_cold_median_ns = 0.0;
  double validation_median_ns = 0.0;
};

int positive_iterations(const std::string& text) {
  std::size_t consumed = 0;
  const long long value = std::stoll(text, &consumed);
  if (consumed != text.size() || value <= 0 ||
      value > std::numeric_limits<int>::max()) {
    throw std::runtime_error("iterations must be an integer in 1..INT_MAX");
  }
  return static_cast<int>(value);
}

bool legacy_region_node(NodeKind kind) {
  return kind == NodeKind::ASGP_DC || kind == NodeKind::ASGP_DP1D ||
         kind == NodeKind::ASGP_DP2D;
}

std::string family_of(const ProgramGenome& genome, int ordinal) {
  std::string family;
  for (const auto& node : genome.ast.nodes) {
    if (!legacy_region_node(node.kind)) continue;
    const std::string current = node.kind == NodeKind::ASGP_DC
                                    ? "dc"
                                : node.kind == NodeKind::ASGP_DP1D ? "dp1d"
                                                                  : "dp2d";
    if (!family.empty() && family != current) {
      throw std::runtime_error("compilation ordinal " + std::to_string(ordinal) +
                               " mixes legacy bounded families");
    }
    family = current;
  }
  if (family.empty()) {
    throw std::runtime_error("compilation ordinal " + std::to_string(ordinal) +
                             " has no legacy bounded node");
  }
  return family;
}

bool same_result(const ExecResult& left, const ExecResult& right) {
  if (left.is_error != right.is_error) return false;
  if (left.is_error) return left.err.code == right.err.code;
  return gagp::migration::encode_value(left.value) ==
         gagp::migration::encode_value(right.value);
}

void check_archived_result(const ExecResult& actual, const JsonValue& expected,
                           int ordinal, const std::string& implementation,
                           const char* checkpoint) {
  const JsonValue& error = require_object_field(expected, "error");
  if (actual.is_error) {
    if (error.kind != JsonValue::Kind::String ||
        error.string_v != gagp::err_code_name(actual.err.code)) {
      throw std::runtime_error("execution ordinal " + std::to_string(ordinal) +
                               " " + implementation + " changed archived " +
                               checkpoint + " error code");
    }
    return;
  }
  if (error.kind != JsonValue::Kind::Null) {
    throw std::runtime_error("execution ordinal " + std::to_string(ordinal) +
                             " " + implementation + " lost archived " +
                             checkpoint + " error");
  }
  const Value expected_value =
      gagp::migration::decode_value(require_object_field(expected, "value"));
  if (gagp::migration::encode_value(actual.value) !=
      gagp::migration::encode_value(expected_value)) {
    throw std::runtime_error("execution ordinal " + std::to_string(ordinal) +
                             " " + implementation + " changed archived " +
                             checkpoint + " payload");
  }
}

void require_empty_inputs(const JsonValue& row, int ordinal) {
  const JsonValue& inputs = require_object_field(row, "inputs");
  if (inputs.kind != JsonValue::Kind::Array || !inputs.array_v.empty()) {
    throw std::runtime_error("execution ordinal " + std::to_string(ordinal) +
                             " has runtime inputs; bounded corpus rows must not");
  }
}

Observation decode_observation(const JsonValue& row) {
  Observation result;
  result.ordinal = require_int(require_object_field(row, "ordinal"), "ordinal");
  require_empty_inputs(row, result.ordinal);
  if (require_string(require_object_field(row, "bytecode_verifier"),
                     "bytecode_verifier") != "ok") {
    throw std::runtime_error("execution ordinal " +
                             std::to_string(result.ordinal) +
                             " is not archived as verifier-valid");
  }
  const JsonValue& boundary =
      require_object_field(row, "first_non_timeout_fuel");
  if (boundary.kind == JsonValue::Kind::Null) {
    throw std::runtime_error("execution ordinal " +
                             std::to_string(result.ordinal) +
                             " has no finite fuel boundary");
  }
  result.archived_fuel = require_int(require_object_field(row, "fuel"), "fuel");
  result.probe_cap = require_int(require_object_field(row, "probe_cap"), "probe_cap");
  result.boundary = require_int(boundary, "first_non_timeout_fuel");
  if (result.archived_fuel < 0 || result.probe_cap < 0 || result.boundary < 0 ||
      result.boundary > result.probe_cap) {
    throw std::runtime_error("execution ordinal " +
                             std::to_string(result.ordinal) +
                             " has invalid fuel fields");
  }
  result.result = require_object_field(row, "result");
  result.at_cap = require_object_field(row, "at_probe_cap");
  result.at_boundary = require_object_field(row, "at_boundary");
  const auto below = row.object_v.find("below_boundary");
  result.has_below_boundary = below != row.object_v.end();
  if (result.has_below_boundary) result.below_boundary = below->second;
  return result;
}

std::vector<Fixture> read_fixtures(const std::string& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot read " + path);
  std::vector<JsonValue> rows;
  std::map<std::string, std::vector<JsonValue>> executions;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty()) continue;
    JsonValue row = JsonParser(line).parse();
    const std::string kind = require_string(require_object_field(row, "kind"), "kind");
    if (kind == "execution") {
      // Unrelated negative probes can contain intentionally opaque payloads.
      // Join lossless snapshot objects before decoding only selected fixtures.
      const auto key = gagp::evo::grammar::canonical_json(
          require_object_field(row, "bytecode"));
      executions[key].push_back(std::move(row));
    } else if (kind == "compilation") {
      rows.push_back(std::move(row));
    }
  }

  std::vector<Fixture> fixtures;
  std::set<int> found;
  for (const JsonValue& row : rows) {
    const int ordinal = require_int(require_object_field(row, "ordinal"), "ordinal");
    if (k_compilation_ordinals.count(ordinal) == 0) continue;
    if (!found.insert(ordinal).second) {
      throw std::runtime_error("duplicate compilation ordinal " +
                               std::to_string(ordinal));
    }
    const auto population =
        gagp::migration::decode_population(require_object_field(row, "population"));
    if (population.size() != 1) {
      throw std::runtime_error("compilation ordinal " + std::to_string(ordinal) +
                               " population does not contain exactly one AST");
    }
    Fixture fixture;
    fixture.compilation_ordinal = ordinal;
    fixture.original = population.front();
    fixture.family = family_of(fixture.original, ordinal);
    fixture.reference =
        gagp::migration::decode_bytecode(require_object_field(row, "bytecode"));
    const auto bytecode_check = gagp::verify_bytecode(fixture.reference);
    if (!bytecode_check.ok) {
      throw std::runtime_error("compilation ordinal " + std::to_string(ordinal) +
                               " frozen bytecode fails current verification");
    }
    const std::string key = gagp::evo::grammar::canonical_json(
        require_object_field(row, "bytecode"));
    const auto joined = executions.find(key);
    if (joined == executions.end() || joined->second.empty()) {
      throw std::runtime_error("compilation ordinal " + std::to_string(ordinal) +
                               " has no canonical-bytecode execution join");
    }
    for (const JsonValue& execution : joined->second)
      fixture.observations.push_back(decode_observation(execution));
    std::sort(fixture.observations.begin(), fixture.observations.end(),
              [](const Observation& a, const Observation& b) {
                return a.ordinal < b.ordinal;
              });
    fixtures.push_back(std::move(fixture));
  }
  if (found != k_compilation_ordinals) {
    throw std::runtime_error("archive does not contain all eight authoritative "
                             "bounded compilation ordinals");
  }
  std::sort(fixtures.begin(), fixtures.end(), [](const Fixture& a, const Fixture& b) {
    return a.compilation_ordinal < b.compilation_ordinal;
  });
  return fixtures;
}

int first_non_timeout(const BytecodeProgram& program, int cap) {
  for (int fuel = 0; fuel <= cap; ++fuel) {
    const ExecResult result = gagp::execute_bytecode_cpu(program, {}, fuel);
    if (!result.is_error || result.err.code != ErrCode::Timeout) return fuel;
  }
  return -1;
}

std::uint64_t result_token(const ExecResult& result) {
  return result.is_error
             ? 0x8000000000000000ULL ^ static_cast<std::uint64_t>(result.err.code)
             : Value::shallow_hash64(result.value);
}

double time_program(const BytecodeProgram& program, int fuel, int iterations) {
  const auto start = Clock::now();
  std::uint64_t batch = 0;
  for (int i = 0; i < iterations; ++i) {
    batch ^= result_token(gagp::execute_bytecode_cpu(program, {}, fuel)) +
             static_cast<std::uint64_t>(i);
  }
  result_sink = result_sink ^ batch;
  return std::chrono::duration<double, std::nano>(Clock::now() - start).count() /
         static_cast<double>(iterations);
}

double time_session(CpuExecutionSession& session, int fuel, int iterations) {
  const auto start = Clock::now();
  std::uint64_t batch = 0;
  for (int i = 0; i < iterations; ++i) {
    batch ^= result_token(session.execute({}, fuel)) + static_cast<std::uint64_t>(i);
  }
  result_sink = result_sink ^ batch;
  return std::chrono::duration<double, std::nano>(Clock::now() - start).count() /
         static_cast<double>(iterations);
}

double time_cold_session(const BytecodeProgram& program, int fuel,
                         int iterations) {
  const auto start = Clock::now();
  std::uint64_t batch = 0;
  for (int i = 0; i < iterations; ++i) {
    {
      CpuExecutionSession session(program);
      batch ^= result_token(session.execute({}, fuel)) +
               static_cast<std::uint64_t>(i);
    }
  }
  result_sink = result_sink ^ batch;
  return std::chrono::duration<double, std::nano>(Clock::now() - start).count() /
         static_cast<double>(iterations);
}

double time_segment_validation(const BytecodeProgram& program, int iterations) {
  if (program.bounded_region_segments.empty()) {
    throw std::runtime_error("translated bytecode has no bounded segment to validate");
  }
  const auto start = Clock::now();
  std::uint64_t batch = 0;
  for (int i = 0; i < iterations; ++i) {
    for (std::size_t segment_index = 0;
         segment_index < program.bounded_region_segments.size(); ++segment_index) {
      const auto checked = gagp::verify_bounded_region_segment(
          program.bounded_region_segments[segment_index], program.n_locals);
      if (!checked.ok) {
        throw std::runtime_error(
            "bounded segment " + std::to_string(segment_index) +
            " failed diagnostic validation during timing: " +
            checked.diagnostic.message);
      }
      batch ^= static_cast<std::uint64_t>(checked.verified.max_stack_depth) +
               (static_cast<std::uint64_t>(checked.verified.phase_program_count)
                << 32U) + static_cast<std::uint64_t>(i + segment_index);
    }
  }
  validation_sink = validation_sink ^ batch;
  return std::chrono::duration<double, std::nano>(Clock::now() - start).count() /
         static_cast<double>(iterations);
}

double median(std::vector<double> samples) {
  std::sort(samples.begin(), samples.end());
  return samples[samples.size() / 2];
}

void check_observation(const BytecodeProgram& program,
                       const Observation& observation,
                       const std::string& implementation,
                       std::size_t* checks) {
  check_archived_result(
      gagp::execute_bytecode_cpu(program, {}, observation.archived_fuel),
      observation.result, observation.ordinal, implementation, "fuel/result");
  check_archived_result(gagp::execute_bytecode_cpu(program, {}, observation.probe_cap),
                        observation.at_cap, observation.ordinal, implementation,
                        "probe_cap");
  check_archived_result(gagp::execute_bytecode_cpu(program, {}, observation.boundary),
                        observation.at_boundary, observation.ordinal, implementation,
                        "boundary");
  *checks += 3;
  if (observation.boundary > 0) {
    if (!observation.has_below_boundary) {
      throw std::runtime_error("execution ordinal " +
                               std::to_string(observation.ordinal) +
                               " has no below_boundary checkpoint");
    }
    check_archived_result(
        gagp::execute_bytecode_cpu(program, {}, observation.boundary - 1),
        observation.below_boundary, observation.ordinal, implementation,
        "below_boundary");
    ++*checks;
  }
}

void check_session_observation(CpuExecutionSession& session,
                               const Observation& observation,
                               const std::string& implementation,
                               std::size_t* checks) {
  check_archived_result(session.execute({}, observation.archived_fuel),
                        observation.result, observation.ordinal,
                        implementation, "fuel/result");
  check_archived_result(session.execute({}, observation.probe_cap),
                        observation.at_cap, observation.ordinal,
                        implementation, "probe_cap");
  check_archived_result(session.execute({}, observation.boundary),
                        observation.at_boundary, observation.ordinal,
                        implementation, "boundary");
  *checks += 3;
  if (observation.boundary > 0) {
    if (!observation.has_below_boundary) {
      throw std::runtime_error("execution ordinal " +
                               std::to_string(observation.ordinal) +
                               " has no below_boundary checkpoint");
    }
    check_archived_result(session.execute({}, observation.boundary - 1),
                          observation.below_boundary, observation.ordinal,
                          implementation, "below_boundary");
    ++*checks;
  }
}

Measurement measure_fixture(const Fixture& fixture, int iterations) {
  const auto source_verified = gagp::evo::verify_ast(fixture.original.ast, {});
  if (!source_verified.ok) {
    if (k_undefined_local_ordinals.count(fixture.compilation_ordinal) &&
        source_verified.diagnostic.code == gagp::evo::VerifyCode::UndefinedLocal) {
      Measurement rejected;
      rejected.compilation_ordinal = fixture.compilation_ordinal;
      rejected.family = fixture.family;
      rejected.translated = false;
      rejected.rejection_node = source_verified.diagnostic.node_index;
      rejected.original_nodes = fixture.original.ast.nodes.size();
      rejected.original_instructions = gagp::bytecode_instruction_count(fixture.reference);
      for (const auto& observation : fixture.observations) {
        rejected.execution_ordinals.push_back(observation.ordinal);
        check_observation(fixture.reference, observation, "frozen",
                          &rejected.archived_checkpoint_checks);
        if (first_non_timeout(fixture.reference, observation.probe_cap) != observation.boundary)
          throw std::runtime_error("rejected source changed archived runtime fuel boundary");
      }
      return rejected;
    }
    throw std::runtime_error(
        "compilation ordinal " + std::to_string(fixture.compilation_ordinal) +
        " source AST verification failed: " +
        gagp::evo::verify_code_name(source_verified.diagnostic.code) + " " +
        source_verified.diagnostic.message);
  }
  if (k_undefined_local_ordinals.count(fixture.compilation_ordinal))
    throw std::runtime_error("intentional undefined-local source was accepted by native verification");
  ProgramGenome transformed =
      gagp::evo::transition::lower_bounded_regions(fixture.original, {});
  for (const auto& node : transformed.ast.nodes) {
    if (legacy_region_node(node.kind)) {
      throw std::runtime_error("compilation ordinal " +
                               std::to_string(fixture.compilation_ordinal) +
                               " transition left a legacy AST node");
    }
  }
  if (!transformed.ast.asgp_dc_binders.empty() ||
      !transformed.ast.asgp_dp1d_specs.empty() ||
      !transformed.ast.asgp_dp2d_specs.empty() ||
      transformed.ast.bounded_region_specs.empty()) {
    throw std::runtime_error("compilation ordinal " +
                             std::to_string(fixture.compilation_ordinal) +
                             " transition emitted invalid metadata shape");
  }
  const auto transformed_verified = gagp::evo::verify_ast(transformed.ast, {});
  if (!transformed_verified.ok) {
    throw std::runtime_error(
        "compilation ordinal " + std::to_string(fixture.compilation_ordinal) +
        " transformed AST verification failed: " +
        gagp::evo::verify_code_name(transformed_verified.diagnostic.code) + " " +
        transformed_verified.diagnostic.message);
  }
  const BytecodeProgram candidate =
      gagp::evo::compile_for_eval(transformed, transformed_verified.verified);
  const auto candidate_check = gagp::verify_bytecode(candidate);
  if (!candidate_check.ok) {
    throw std::runtime_error("compilation ordinal " +
                             std::to_string(fixture.compilation_ordinal) +
                             " transformed bytecode fails verification");
  }

  Measurement result;
  result.compilation_ordinal = fixture.compilation_ordinal;
  result.family = fixture.family;
  result.original_nodes = fixture.original.ast.nodes.size();
  result.transformed_nodes = transformed.ast.nodes.size();
  result.original_instructions = gagp::bytecode_instruction_count(fixture.reference);
  result.transformed_instructions = gagp::bytecode_instruction_count(candidate);
  CpuExecutionSession original_session(fixture.reference);
  CpuExecutionSession transformed_session(candidate);
  for (const Observation& observation : fixture.observations) {
    result.execution_ordinals.push_back(observation.ordinal);
    check_observation(fixture.reference, observation, "frozen", &result.archived_checkpoint_checks);
    check_observation(candidate, observation, "transformed", &result.archived_checkpoint_checks);
    check_session_observation(original_session, observation, "session/frozen",
                              &result.session_checkpoint_checks);
    check_session_observation(transformed_session, observation,
                              "session/transformed",
                              &result.session_checkpoint_checks);
    const int computed_boundary =
        first_non_timeout(fixture.reference, observation.probe_cap);
    if (computed_boundary != observation.boundary) {
      throw std::runtime_error("execution ordinal " +
                               std::to_string(observation.ordinal) +
                               " changed archived minimum fuel");
    }
    for (int fuel = 0; fuel <= observation.boundary + 3; ++fuel) {
      const ExecResult frozen =
          gagp::execute_bytecode_cpu(fixture.reference, {}, fuel);
      const ExecResult translated = gagp::execute_bytecode_cpu(candidate, {}, fuel);
      if (!same_result(frozen, translated)) {
        throw std::runtime_error("compilation ordinal " +
                                 std::to_string(fixture.compilation_ordinal) +
                                 " differs at fuel " + std::to_string(fuel) +
                                 " for execution ordinal " +
                                 std::to_string(observation.ordinal));
      }
      ++result.differential_checks;
      if (!same_result(original_session.execute({}, fuel), frozen) ||
          !same_result(transformed_session.execute({}, fuel), frozen)) {
        throw std::runtime_error("compilation ordinal " +
                                 std::to_string(fixture.compilation_ordinal) +
                                 " session differs from frozen result at fuel " +
                                 std::to_string(fuel) + " for execution ordinal " +
                                 std::to_string(observation.ordinal));
      }
      result.session_differential_checks += 2;
    }
    const ExecResult reference_cap =
        gagp::execute_bytecode_cpu(fixture.reference, {}, observation.probe_cap);
    if (!same_result(reference_cap,
                     gagp::execute_bytecode_cpu(candidate, {}, observation.probe_cap))) {
      throw std::runtime_error("compilation ordinal " +
                               std::to_string(fixture.compilation_ordinal) +
                               " differs at probe_cap for execution ordinal " +
                               std::to_string(observation.ordinal));
    }
    ++result.differential_checks;
    if (!same_result(original_session.execute({}, observation.probe_cap),
                     reference_cap) ||
        !same_result(transformed_session.execute({}, observation.probe_cap),
                     reference_cap)) {
      throw std::runtime_error("compilation ordinal " +
                               std::to_string(fixture.compilation_ordinal) +
                               " session differs at probe_cap for execution ordinal " +
                               std::to_string(observation.ordinal));
    }
    result.session_differential_checks += 2;
  }

  result.timing_fuel = fixture.observations.front().probe_cap;
  const int warmup = std::min(iterations, 1000);
  (void)time_program(fixture.reference, result.timing_fuel, warmup);
  (void)time_program(candidate, result.timing_fuel, warmup);
  (void)time_session(original_session, result.timing_fuel, warmup);
  (void)time_session(transformed_session, result.timing_fuel, warmup);
  (void)time_segment_validation(candidate, warmup);
  result.session_original_retained_region_bytes =
      original_session.retained_region_bytes();
  result.session_transformed_retained_region_bytes =
      transformed_session.retained_region_bytes();
  for (int repetition = 0; repetition < k_timing_repetitions; ++repetition) {
    if ((repetition & 1) == 0) {
      result.original_ns.push_back(time_program(fixture.reference, result.timing_fuel, iterations));
      result.transformed_ns.push_back(time_program(candidate, result.timing_fuel, iterations));
    } else {
      result.transformed_ns.push_back(time_program(candidate, result.timing_fuel, iterations));
      result.original_ns.push_back(time_program(fixture.reference, result.timing_fuel, iterations));
    }
  }
  result.session_original_ns.reserve(k_timing_repetitions);
  result.session_transformed_ns.reserve(k_timing_repetitions);
  result.session_original_cold_ns.reserve(k_timing_repetitions);
  result.session_transformed_cold_ns.reserve(k_timing_repetitions);
  for (int repetition = 0; repetition < k_timing_repetitions; ++repetition) {
    if ((repetition & 1) == 0) {
      result.session_original_ns.push_back(
          time_session(original_session, result.timing_fuel, iterations));
      result.session_transformed_ns.push_back(
          time_session(transformed_session, result.timing_fuel, iterations));
      result.session_original_cold_ns.push_back(
          time_cold_session(fixture.reference, result.timing_fuel, iterations));
      result.session_transformed_cold_ns.push_back(
          time_cold_session(candidate, result.timing_fuel, iterations));
    } else {
      result.session_transformed_ns.push_back(
          time_session(transformed_session, result.timing_fuel, iterations));
      result.session_original_ns.push_back(
          time_session(original_session, result.timing_fuel, iterations));
      result.session_transformed_cold_ns.push_back(
          time_cold_session(candidate, result.timing_fuel, iterations));
      result.session_original_cold_ns.push_back(
          time_cold_session(fixture.reference, result.timing_fuel, iterations));
    }
  }
  result.validation_ns.reserve(k_timing_repetitions);
  for (int repetition = 0; repetition < k_timing_repetitions; ++repetition)
    result.validation_ns.push_back(time_segment_validation(candidate, iterations));
  result.original_median_ns = median(result.original_ns);
  result.transformed_median_ns = median(result.transformed_ns);
  result.session_original_median_ns = median(result.session_original_ns);
  result.session_transformed_median_ns = median(result.session_transformed_ns);
  result.session_original_cold_median_ns = median(result.session_original_cold_ns);
  result.session_transformed_cold_median_ns =
      median(result.session_transformed_cold_ns);
  result.validation_median_ns = median(result.validation_ns);
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

std::string report_json(const std::string& input,
                        const std::vector<Measurement>& rows, int iterations) {
  std::ostringstream out;
  out << std::setprecision(17)
      << "{\"format_version\":\"bounded-transition-benchmark-v1\",\"input\":\"";
  for (char c : input) {
    if (c == '\\' || c == '"') out << '\\';
    out << c;
  }
  out << "\",\"iterations\":" << iterations
      << ",\"timing_repetitions\":" << k_timing_repetitions
      << ",\"fixtures\":[";
  std::size_t total_archived = 0;
  std::size_t total_differential = 0;
  std::size_t total_session_checkpoints = 0;
  std::size_t total_session_differential = 0;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (i) out << ',';
    const Measurement& row = rows[i];
    total_archived += row.archived_checkpoint_checks;
    total_differential += row.differential_checks;
    total_session_checkpoints += row.session_checkpoint_checks;
    total_session_differential += row.session_differential_checks;
    out << "{\"compilation_ordinal\":" << row.compilation_ordinal
        << ",\"family\":\"" << row.family << "\",\"execution_ordinals\":[";
    for (std::size_t j = 0; j < row.execution_ordinals.size(); ++j) {
      if (j) out << ',';
      out << row.execution_ordinals[j];
    }
    out << "],\"source_status\":\""
        << (row.translated ? "verified" : "rejected_undefined_local")
        << "\",\"translated\":" << (row.translated ? "true" : "false");
    if (!row.translated) {
      out << ",\"rejection_node\":" << row.rejection_node
          << ",\"original_ast_nodes\":" << row.original_nodes
          << ",\"original_instructions\":" << row.original_instructions
          << ",\"archived_checkpoint_checks\":" << row.archived_checkpoint_checks
          << ",\"differential_checks\":0}";
      continue;
    }
    out << ",\"original_ast_nodes\":" << row.original_nodes
        << ",\"transformed_ast_nodes\":" << row.transformed_nodes
        << ",\"original_instructions\":" << row.original_instructions
        << ",\"transformed_instructions\":" << row.transformed_instructions
        << ",\"timing_fuel\":" << row.timing_fuel
        << ",\"archived_checkpoint_checks\":" << row.archived_checkpoint_checks
        << ",\"differential_checks\":" << row.differential_checks
        << ",\"session_checkpoint_checks\":" << row.session_checkpoint_checks
        << ",\"session_differential_checks\":"
        << row.session_differential_checks
        << ",\"session_original_retained_region_bytes\":"
        << row.session_original_retained_region_bytes
        << ",\"session_transformed_retained_region_bytes\":"
        << row.session_transformed_retained_region_bytes
        << ",\"original_ns_per_run_samples\":";
    emit_samples(out, row.original_ns);
    out << ",\"transformed_ns_per_run_samples\":";
    emit_samples(out, row.transformed_ns);
    out << ",\"session_original_ns_per_run_samples\":";
    emit_samples(out, row.session_original_ns);
    out << ",\"session_transformed_ns_per_run_samples\":";
    emit_samples(out, row.session_transformed_ns);
    out << ",\"session_original_cold_ns_per_run_samples\":";
    emit_samples(out, row.session_original_cold_ns);
    out << ",\"session_transformed_cold_ns_per_run_samples\":";
    emit_samples(out, row.session_transformed_cold_ns);
    out << ",\"validation_ns_per_run_samples\":";
    emit_samples(out, row.validation_ns);
    out << ",\"original_ns_per_run_median\":" << row.original_median_ns
        << ",\"transformed_ns_per_run_median\":" << row.transformed_median_ns
        << ",\"session_original_ns_per_run_median\":"
        << row.session_original_median_ns
        << ",\"session_transformed_ns_per_run_median\":"
        << row.session_transformed_median_ns
        << ",\"session_original_cold_ns_per_run_median\":"
        << row.session_original_cold_median_ns
        << ",\"session_transformed_cold_ns_per_run_median\":"
        << row.session_transformed_cold_median_ns
        << ",\"validation_ns_per_run_median\":" << row.validation_median_ns
        << '}';
  }
  out << "],\"archived_checkpoint_checks\":" << total_archived
      << ",\"differential_checks\":" << total_differential
      << ",\"session_checkpoint_checks\":" << total_session_checkpoints
      << ",\"session_differential_checks\":" << total_session_differential
      << '}';
  return out.str();
}

void write_file(const std::string& path, const std::string& text) {
  std::ofstream output(path);
  if (!output || !(output << text << '\n'))
    throw std::runtime_error("cannot write " + path);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 3 || argc > 4) {
      throw std::runtime_error(
          "usage: benchmark_bounded_transition INPUT.jsonl OUTPUT.json [iterations]");
    }
    const int iterations = argc == 4 ? positive_iterations(argv[3])
                                     : k_default_iterations;
    const std::vector<Fixture> fixtures = read_fixtures(argv[1]);
    std::vector<Measurement> measurements;
    measurements.reserve(fixtures.size());
    for (const Fixture& fixture : fixtures)
      measurements.push_back(measure_fixture(fixture, iterations));
    const std::string report = report_json(argv[1], measurements, iterations);
    write_file(argv[2], report);
    std::cout << report << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "benchmark_bounded_transition: " << error.what() << '\n';
    return 1;
  }
}
