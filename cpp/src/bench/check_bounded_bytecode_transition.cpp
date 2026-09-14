#include <cstdint>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "migration_snapshot.hpp"
#include "gagp/cli/json.hpp"
#include "gagp/core/bytecode_verify.hpp"
#include "gagp/core/errors.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/transition/bounded_bytecode.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"

namespace {

using gagp::BytecodeProgram;
using gagp::ErrCode;
using gagp::ExecResult;
using gagp::Value;
using gagp::ValueTag;
using gagp::cli_detail::JsonParser;
using gagp::cli_detail::JsonValue;
using gagp::cli_detail::require_int;
using gagp::cli_detail::require_object_field;
using gagp::cli_detail::require_string;
using gagp::evo::transition::BoundedBytecodeHints;
using gagp::evo::transition::BoundedBytecodeProfile;

constexpr const char* k_named_oracle =
    "cpp/tests/fixtures/migration/reference-boundary-oracle.jsonl";
const std::set<int> k_strict_rejections{3, 4, 11, 12, 19, 20};
const std::set<int> k_capacity_timeouts{
    63, 64, 65, 66, 73, 75, 76, 78, 79, 80, 81, 83, 84, 87};
const std::set<int> k_named_ordinals{
    186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196,
    212, 213, 214, 215};
const ValueTag k_result_tags[]{
    ValueTag::Int, ValueTag::Float, ValueTag::Bool, ValueTag::Char,
    ValueTag::String, ValueTag::IntList, ValueTag::FloatList,
    ValueTag::StringList};
const ValueTag k_source_tags[]{ValueTag::String, ValueTag::IntList,
                              ValueTag::FloatList, ValueTag::StringList};

struct Record {
  int ordinal = -1;
  bool named = false;
  BytecodeProgram reference;
  int archived_fuel = 0;
  int probe_cap = 0;
  int boundary = 0;
  JsonValue result;
  JsonValue at_cap;
  JsonValue at_boundary;
  JsonValue below_boundary;
  bool has_below_boundary = false;
};

struct RowReport {
  int ordinal = -1;
  bool named = false;
  bool translated = false;
  std::string family;
  std::string source_hint;
  std::string result_hint;
  std::string rejection;
  std::size_t archived_checks = 0;
  std::size_t differential_checks = 0;
};

struct GpuCapacityRecord {
  int ordinal = -1;
  int archived_fuel = 0;
  int probe_cap = 0;
  int boundary = -1;
  JsonValue result;
  JsonValue at_cap;
  JsonValue at_boundary;
  JsonValue below_boundary;
  bool has_boundary = false;
};

struct CapacityRowReport {
  int ordinal = -1;
  bool capacity_timeout = false;
  std::size_t archived_checkpoint_checks = 0;
  std::size_t differential_checks = 0;
};

struct CapacityReport {
  std::string input;
  std::vector<CapacityRowReport> rows;
};

const char* tag_name(ValueTag tag) {
  switch (tag) {
    case ValueTag::Int: return "Int";
    case ValueTag::Float: return "Float";
    case ValueTag::Bool: return "Bool";
    case ValueTag::Char: return "Char";
    case ValueTag::String: return "String";
    case ValueTag::IntList: return "IntList";
    case ValueTag::FloatList: return "FloatList";
    case ValueTag::StringList: return "StringList";
    case ValueTag::FallbackToken: return "FallbackToken";
    case ValueTag::Invalid: return "Invalid";
  }
  return "Unknown";
}

bool same_result(const ExecResult& left, const ExecResult& right) {
  if (left.is_error != right.is_error) return false;
  if (left.is_error) return left.err.code == right.err.code;
  return gagp::migration::encode_value(left.value) ==
         gagp::migration::encode_value(right.value);
}

bool archived_error_is(const JsonValue& result, const char* name) {
  const JsonValue& error = require_object_field(result, "error");
  return error.kind == JsonValue::Kind::String && error.string_v == name;
}

void require_archived_timeout(const JsonValue& result, int ordinal,
                              const char* checkpoint) {
  if (!archived_error_is(result, "Timeout"))
    throw std::runtime_error("ordinal " + std::to_string(ordinal) +
                             " GPU " + checkpoint + " is not Timeout");
}

void require_archived_int(const JsonValue& result, int ordinal,
                          const char* checkpoint) {
  const JsonValue& error = require_object_field(result, "error");
  if (error.kind != JsonValue::Kind::Null)
    throw std::runtime_error("ordinal " + std::to_string(ordinal) +
                             " GPU " + checkpoint + " is an error");
  if (gagp::migration::decode_value(require_object_field(result, "value")).tag !=
      ValueTag::Int) {
    throw std::runtime_error("ordinal " + std::to_string(ordinal) +
                             " GPU " + checkpoint +
                             " is not the scalar Int capacity fixture");
  }
}

void check_archived(const ExecResult& actual, const JsonValue& expected,
                    int ordinal, const char* checkpoint) {
  const JsonValue& error = require_object_field(expected, "error");
  if (actual.is_error) {
    if (error.kind != JsonValue::Kind::String ||
        error.string_v != gagp::err_code_name(actual.err.code)) {
      throw std::runtime_error("ordinal " + std::to_string(ordinal) +
                               " changed archived " + checkpoint +
                               " error code");
    }
    return;
  }
  if (error.kind != JsonValue::Kind::Null) {
    throw std::runtime_error("ordinal " + std::to_string(ordinal) +
                             " lost archived " + checkpoint + " error");
  }
  const Value expected_value =
      gagp::migration::decode_value(require_object_field(expected, "value"));
  if (gagp::migration::encode_value(actual.value) !=
      gagp::migration::encode_value(expected_value)) {
    throw std::runtime_error("ordinal " + std::to_string(ordinal) +
                             " changed archived " + checkpoint + " payload");
  }
}

Record decode_record(const JsonValue& row, bool named) {
  if (require_string(require_object_field(row, "kind"), "kind") != "execution")
    throw std::runtime_error("oracle row is not an execution");
  Record record;
  record.ordinal = require_int(require_object_field(row, "ordinal"), "ordinal");
  record.named = named;
  const bool outside_base_probe = !named && (record.ordinal == 184 || record.ordinal == 185);
  const auto archived_verifier = require_string(require_object_field(row, "bytecode_verifier"),
                                                 "bytecode_verifier");
  if (archived_verifier != (outside_base_probe ? "invalid_segment_metadata" : "ok")) {
    throw std::runtime_error("ordinal " + std::to_string(record.ordinal) +
                             " is not archived as bytecode-verifier valid");
  }
  const JsonValue& inputs = require_object_field(row, "inputs");
  if (inputs.kind != JsonValue::Kind::Array || !inputs.array_v.empty()) {
    throw std::runtime_error("ordinal " + std::to_string(record.ordinal) +
                             " does not have the declared empty input fixture");
  }
  record.reference =
      gagp::migration::decode_bytecode(require_object_field(row, "bytecode"));
  const auto source_verifier = gagp::verify_bytecode(record.reference);
  if ((!outside_base_probe && !source_verifier.ok) ||
      (outside_base_probe && (source_verifier.ok ||
          source_verifier.diagnostic.code != gagp::BytecodeVerifyCode::InvalidSegmentMetadata))) {
    throw std::runtime_error("ordinal " + std::to_string(record.ordinal) +
                             " fails current bytecode verification");
  }
  const JsonValue& boundary =
      require_object_field(row, "first_non_timeout_fuel");
  if (boundary.kind == JsonValue::Kind::Null)
    throw std::runtime_error("oracle row has no finite fuel boundary");
  record.archived_fuel = require_int(require_object_field(row, "fuel"), "fuel");
  record.probe_cap = require_int(require_object_field(row, "probe_cap"), "probe_cap");
  record.boundary = require_int(boundary, "first_non_timeout_fuel");
  if (record.archived_fuel < 0 || record.probe_cap < 0 || record.boundary < 0 ||
      record.boundary > record.probe_cap) {
    throw std::runtime_error("ordinal " + std::to_string(record.ordinal) +
                             " has invalid fuel fields");
  }
  record.result = require_object_field(row, "result");
  record.at_cap = require_object_field(row, "at_probe_cap");
  record.at_boundary = require_object_field(row, "at_boundary");
  const auto below = row.object_v.find("below_boundary");
  record.has_below_boundary = below != row.object_v.end();
  if (record.has_below_boundary) record.below_boundary = below->second;
  return record;
}

std::vector<Record> read_records(const std::string& full_path) {
  std::vector<Record> records;
  std::ifstream full(full_path);
  if (!full) throw std::runtime_error("cannot read " + full_path);
  std::string line;
  while (std::getline(full, line)) {
    if (!line.empty()) records.push_back(decode_record(JsonParser(line).parse(), false));
  }
  if (records.size() != 186)
    throw std::runtime_error("full boundary oracle must contain exactly 186 rows");
  for (std::size_t i = 0; i < records.size(); ++i) {
    if (records[i].ordinal != static_cast<int>(i))
      throw std::runtime_error("full boundary oracle ordinals must be exactly 0..185");
  }

  std::ifstream named_input(k_named_oracle);
  if (!named_input) throw std::runtime_error(std::string("cannot read ") + k_named_oracle);
  std::set<int> found_named;
  while (std::getline(named_input, line)) {
    if (line.empty()) continue;
    JsonValue row = JsonParser(line).parse();
    const int ordinal = require_int(require_object_field(row, "ordinal"), "ordinal");
    if (!k_named_ordinals.count(ordinal)) continue;
    if (!found_named.insert(ordinal).second)
      throw std::runtime_error("duplicate named oracle ordinal " + std::to_string(ordinal));
    records.push_back(decode_record(row, true));
  }
  if (found_named != k_named_ordinals)
    throw std::runtime_error("tracked oracle is missing one of the 15 named ASGP rows");
  return records;
}

std::vector<GpuCapacityRecord> read_gpu_capacity_records(
    const std::string& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot read " + path);
  std::vector<GpuCapacityRecord> records;
  std::set<int> ordinals;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty()) continue;
    const JsonValue row = JsonParser(line).parse();
    if (require_string(require_object_field(row, "kind"), "kind") !=
        "gpu_execution") {
      throw std::runtime_error("GPU capacity archive contains a non-gpu_execution row");
    }
    const int ordinal =
        require_int(require_object_field(row, "ordinal"), "ordinal");
    if (ordinal < 55 || ordinal > 87) continue;
    if (!ordinals.insert(ordinal).second)
      throw std::runtime_error("duplicate GPU capacity ordinal " +
                               std::to_string(ordinal));
    GpuCapacityRecord record;
    record.ordinal = ordinal;
    record.archived_fuel =
        require_int(require_object_field(row, "fuel"), "fuel");
    record.probe_cap =
        require_int(require_object_field(row, "probe_cap"), "probe_cap");
    record.result = require_object_field(row, "result");
    record.at_cap = require_object_field(row, "at_probe_cap");
    const JsonValue& boundary =
        require_object_field(row, "first_non_timeout_fuel");
    if (boundary.kind == JsonValue::Kind::Null) {
      record.has_boundary = false;
    } else {
      record.has_boundary = true;
      record.boundary = require_int(boundary, "first_non_timeout_fuel");
      record.at_boundary = require_object_field(row, "at_boundary");
      record.below_boundary = require_object_field(row, "below_boundary");
    }
    records.push_back(std::move(record));
  }
  if (records.size() != 33)
    throw std::runtime_error("GPU capacity archive must contain exactly ordinals 55..87");
  for (int ordinal = 55; ordinal <= 87; ++ordinal) {
    if (!ordinals.count(ordinal))
      throw std::runtime_error("GPU capacity archive is missing ordinal " +
                               std::to_string(ordinal));
  }
  return records;
}

ValueTag full_dc_source_hint(int ordinal) {
  if (ordinal >= 23 && ordinal <= 54)
    return k_source_tags[(ordinal - 23) / 8];
  // Rows 2 and 186 deliberately supply an invalid scalar at runtime. IntList is
  // the declared valid sequence type used to exercise the generic state guard.
  if (ordinal == 1) return ValueTag::String;
  return ValueTag::IntList;
}

ValueTag full_dc_result_hint(int ordinal) {
  if (ordinal >= 23 && ordinal <= 54)
    return k_result_tags[(ordinal - 23) % 8];
  if (ordinal == 1) return ValueTag::String;
  return ValueTag::Int;
}

BoundedBytecodeHints hints_for(const Record& record, RowReport* report) {
  const BytecodeProgram& program = record.reference;
  const std::size_t families = (!program.asgp_dc_segments.empty() ? 1U : 0U) +
                               (!program.asgp_dp1d_segments.empty() ? 1U : 0U) +
                               (!program.asgp_dp2d_segments.empty() ? 1U : 0U);
  if (families != 1 || program.asgp_dc_segments.size() > 1 ||
      program.asgp_dp1d_segments.size() > 1 ||
      program.asgp_dp2d_segments.size() > 1) {
    throw std::runtime_error("ordinal " + std::to_string(record.ordinal) +
                             " does not have one declared legacy segment");
  }
  BoundedBytecodeHints hints;
  if (!program.asgp_dc_segments.empty()) {
    const ValueTag source = full_dc_source_hint(record.ordinal);
    const ValueTag result = full_dc_result_hint(record.ordinal);
    hints.dc.push_back({source, result});
    report->family = "dc";
    report->source_hint = tag_name(source);
    report->result_hint = tag_name(result);
  } else if (!program.asgp_dp1d_segments.empty()) {
    // The boundary value is the legacy DP table's declared result seed and is
    // the authoritative type information used by the frozen fixture builder.
    const ValueTag result = program.asgp_dp1d_segments.front().boundary_value.tag;
    hints.dp1d.push_back(result);
    report->family = "dp1d";
    report->result_hint = tag_name(result);
  } else {
    const ValueTag result = program.asgp_dp2d_segments.front().boundary_value.tag;
    hints.dp2d.push_back(result);
    report->family = "dp2d";
    report->result_hint = tag_name(result);
  }
  return hints;
}

int first_non_timeout(const BytecodeProgram& program, int cap) {
  for (int fuel = 0; fuel <= cap; ++fuel) {
    const ExecResult result = gagp::execute_bytecode_cpu(program, {}, fuel);
    if (!result.is_error || result.err.code != ErrCode::Timeout) return fuel;
  }
  return -1;
}

void check_frozen(const Record& record, RowReport* report) {
  check_archived(gagp::execute_bytecode_cpu(record.reference, {}, record.archived_fuel),
                 record.result, record.ordinal, "fuel/result");
  check_archived(gagp::execute_bytecode_cpu(record.reference, {}, record.probe_cap),
                 record.at_cap, record.ordinal, "probe_cap");
  check_archived(gagp::execute_bytecode_cpu(record.reference, {}, record.boundary),
                 record.at_boundary, record.ordinal, "boundary");
  report->archived_checks += 3;
  if (record.boundary > 0) {
    if (!record.has_below_boundary)
      throw std::runtime_error("positive boundary is missing below_boundary result");
    check_archived(gagp::execute_bytecode_cpu(record.reference, {}, record.boundary - 1),
                   record.below_boundary, record.ordinal, "below_boundary");
    ++report->archived_checks;
  }
  if (first_non_timeout(record.reference, record.probe_cap) != record.boundary)
    throw std::runtime_error("ordinal " + std::to_string(record.ordinal) +
                             " changed archived minimum fuel");
}

void check_translated(const Record& record, const BytecodeProgram& translated,
                      RowReport* report) {
  check_archived(gagp::execute_bytecode_cpu(translated, {}, record.archived_fuel),
                 record.result, record.ordinal, "translated fuel/result");
  check_archived(gagp::execute_bytecode_cpu(translated, {}, record.probe_cap),
                 record.at_cap, record.ordinal, "translated probe_cap");
  check_archived(gagp::execute_bytecode_cpu(translated, {}, record.boundary),
                 record.at_boundary, record.ordinal, "translated boundary");
  report->archived_checks += 3;
  if (record.boundary > 0) {
    check_archived(gagp::execute_bytecode_cpu(translated, {}, record.boundary - 1),
                   record.below_boundary, record.ordinal,
                   "translated below_boundary");
    ++report->archived_checks;
  }
  for (int fuel = 0; fuel <= record.boundary + 3; ++fuel) {
    if (!same_result(gagp::execute_bytecode_cpu(record.reference, {}, fuel),
                     gagp::execute_bytecode_cpu(translated, {}, fuel))) {
      throw std::runtime_error("ordinal " + std::to_string(record.ordinal) +
                               " differs at fuel " + std::to_string(fuel));
    }
    ++report->differential_checks;
  }
  if (!same_result(gagp::execute_bytecode_cpu(record.reference, {}, record.probe_cap),
                   gagp::execute_bytecode_cpu(translated, {}, record.probe_cap))) {
    throw std::runtime_error("ordinal " + std::to_string(record.ordinal) +
                             " differs at probe_cap");
  }
  ++report->differential_checks;
}

RowReport check_record(const Record& record) {
  RowReport report;
  report.ordinal = record.ordinal;
  report.named = record.named;
  const BoundedBytecodeHints hints = hints_for(record, &report);
  check_frozen(record, &report);
  if (!record.named && k_strict_rejections.count(record.ordinal)) {
    bool rejected = false;
    try {
      (void)gagp::evo::transition::lower_bounded_bytecode(record.reference, hints);
    } catch (const std::invalid_argument& error) {
      report.rejection = error.what();
      if (report.rejection.find("lowered bytecode is invalid") == std::string::npos)
        throw std::runtime_error("strict-output probe failed before lowered descriptor verification: " +
                                 report.rejection);
      rejected = true;
    }
    if (!rejected)
      throw std::runtime_error("strict-output ordinal " +
                               std::to_string(record.ordinal) +
                               " was not rejected with invalid_argument");
    return report;
  }
  const BytecodeProgram translated =
      gagp::evo::transition::lower_bounded_bytecode(record.reference, hints);
  if (!gagp::verify_bytecode(translated).ok)
    throw std::runtime_error("translated ordinal " + std::to_string(record.ordinal) +
                             " fails bytecode verification");
  report.translated = true;
  check_translated(record, translated, &report);
  return report;
}

CapacityReport check_capacity_profile(const std::string& input,
                                      const std::vector<Record>& sources) {
  CapacityReport report;
  report.input = input;
  const auto archived = read_gpu_capacity_records(input);
  const BoundedBytecodeProfile profile{64, 128, 128};
  for (const GpuCapacityRecord& gpu : archived) {
    const bool expected_timeout = k_capacity_timeouts.count(gpu.ordinal) != 0;
    if (expected_timeout != !gpu.has_boundary)
      throw std::runtime_error("ordinal " + std::to_string(gpu.ordinal) +
                               " changed the frozen GPU capacity failure set");
    CapacityRowReport row;
    row.ordinal = gpu.ordinal;
    row.capacity_timeout = expected_timeout;
    RowReport hint_report;
    const Record& source = sources.at(static_cast<std::size_t>(gpu.ordinal));
    if (gpu.archived_fuel != 20000 || gpu.probe_cap != 20000 ||
        gpu.archived_fuel != source.archived_fuel ||
        gpu.probe_cap != source.probe_cap) {
      throw std::runtime_error("ordinal " + std::to_string(gpu.ordinal) +
                               " changed the frozen capacity probe fuel");
    }
    const BytecodeProgram translated = gagp::evo::transition::lower_bounded_bytecode(
        source.reference, hints_for(source, &hint_report), profile);
    if (!gagp::verify_bytecode(translated).ok)
      throw std::runtime_error("capacity-profile translation is invalid at ordinal " +
                               std::to_string(gpu.ordinal));

    if (expected_timeout) {
      require_archived_timeout(gpu.result, gpu.ordinal, "result");
      require_archived_timeout(gpu.at_cap, gpu.ordinal, "probe_cap");
      check_archived(gagp::execute_bytecode_cpu(
                         translated, {}, gpu.archived_fuel),
                     gpu.result, gpu.ordinal, "capacity-profile fuel/result");
      check_archived(gagp::execute_bytecode_cpu(translated, {}, gpu.probe_cap),
                     gpu.at_cap, gpu.ordinal, "capacity-profile probe_cap");
      row.archived_checkpoint_checks = 2;
    } else {
      if (gpu.boundary <= 0 || gpu.boundary > gpu.probe_cap)
        throw std::runtime_error("ordinal " + std::to_string(gpu.ordinal) +
                                 " has an invalid GPU success boundary");
      require_archived_int(gpu.result, gpu.ordinal, "result");
      require_archived_int(gpu.at_cap, gpu.ordinal, "probe_cap");
      require_archived_int(gpu.at_boundary, gpu.ordinal, "boundary");
      require_archived_timeout(gpu.below_boundary, gpu.ordinal,
                               "below_boundary");
      check_archived(gagp::execute_bytecode_cpu(
                         translated, {}, gpu.archived_fuel),
                     gpu.result, gpu.ordinal, "capacity-profile fuel/result");
      check_archived(gagp::execute_bytecode_cpu(translated, {}, gpu.probe_cap),
                     gpu.at_cap, gpu.ordinal, "capacity-profile probe_cap");
      check_archived(gagp::execute_bytecode_cpu(translated, {}, gpu.boundary),
                     gpu.at_boundary, gpu.ordinal,
                     "capacity-profile boundary");
      check_archived(gagp::execute_bytecode_cpu(
                         translated, {}, gpu.boundary - 1),
                     gpu.below_boundary, gpu.ordinal,
                     "capacity-profile below_boundary");
      row.archived_checkpoint_checks = 4;
      for (int fuel = 0; fuel <= gpu.boundary + 3; ++fuel) {
        if (!same_result(gagp::execute_bytecode_cpu(source.reference, {}, fuel),
                         gagp::execute_bytecode_cpu(translated, {}, fuel))) {
          throw std::runtime_error("capacity-profile ordinal " +
                                   std::to_string(gpu.ordinal) +
                                   " differs from the frozen CPU source at fuel " +
                                   std::to_string(fuel));
        }
        ++row.differential_checks;
      }
    }
    report.rows.push_back(row);
  }
  return report;
}

std::string encode_report(const std::string& input,
                          const std::vector<RowReport>& rows,
                          const CapacityReport* capacity = nullptr) {
  std::ostringstream out;
  out << "{\"format_version\":\"bounded-bytecode-transition-check-v1\""
      << ",\"profile\":\"exact-cpu-default\",\"input\":\"";
  for (char c : input) {
    if (c == '\\' || c == '"') out << '\\';
    out << c;
  }
  out << "\",\"named_input\":\"" << k_named_oracle << "\",\"rows\":[";
  std::size_t translated = 0;
  std::size_t rejected = 0;
  std::size_t archived = 0;
  std::size_t differential = 0;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (i) out << ',';
    const RowReport& row = rows[i];
    translated += row.translated ? 1 : 0;
    rejected += row.translated ? 0 : 1;
    archived += row.archived_checks;
    differential += row.differential_checks;
    out << "{\"ordinal\":" << row.ordinal
        << ",\"corpus\":\"" << (row.named ? "named" : "boundary-r64")
        << "\",\"family\":\"" << row.family << "\"";
    if (!row.source_hint.empty())
      out << ",\"source_hint\":\"" << row.source_hint << "\"";
    out << ",\"result_hint\":\"" << row.result_hint << "\""
        << ",\"status\":\""
        << (row.translated ? "translated" : "rejected_strict_output") << "\"";
    if (!row.named && (row.ordinal == 184 || row.ordinal == 185))
      out << ",\"source_status\":\"rejected_outside_base_metadata\"";
    if (!row.rejection.empty()) {
      JsonValue reason;
      reason.kind = JsonValue::Kind::String;
      reason.string_v = row.rejection;
      out << ",\"rejection_reason\":" << gagp::evo::grammar::canonical_json(reason);
    }
    out << ",\"archived_checkpoint_checks\":" << row.archived_checks
        << ",\"differential_checks\":" << row.differential_checks << '}';
  }
  out << "],\"row_count\":" << rows.size()
      << ",\"translated_rows\":" << translated
      << ",\"strict_output_rejections\":" << rejected
      << ",\"archived_checkpoint_checks\":" << archived
      << ",\"differential_checks\":" << differential;
  if (capacity) {
    std::size_t capacity_checkpoints = 0;
    std::size_t capacity_differential = 0;
    std::size_t capacity_timeouts = 0;
    out << ",\"capacity_profile\":{\"name\":\"frozen-gpu-dc64-dp128\""
        << ",\"limits\":{\"dc_frames\":64,\"dp_frames\":128,\"dp_cells\":128}"
        << ",\"input\":";
    JsonValue input_value;
    input_value.kind = JsonValue::Kind::String;
    input_value.string_v = capacity->input;
    out << gagp::evo::grammar::canonical_json(input_value) << ",\"rows\":[";
    for (std::size_t i = 0; i < capacity->rows.size(); ++i) {
      if (i) out << ',';
      const CapacityRowReport& row = capacity->rows[i];
      capacity_checkpoints += row.archived_checkpoint_checks;
      capacity_differential += row.differential_checks;
      capacity_timeouts += row.capacity_timeout ? 1U : 0U;
      out << "{\"ordinal\":" << row.ordinal << ",\"status\":\""
          << (row.capacity_timeout ? "capacity_timeout" : "success")
          << "\",\"archived_checkpoint_checks\":"
          << row.archived_checkpoint_checks << ",\"differential_checks\":"
          << row.differential_checks << '}';
    }
    out << "],\"row_count\":" << capacity->rows.size()
        << ",\"capacity_timeouts\":" << capacity_timeouts
        << ",\"successes\":" << capacity->rows.size() - capacity_timeouts
        << ",\"archived_checkpoint_checks\":" << capacity_checkpoints
        << ",\"differential_checks\":" << capacity_differential
        << ",\"limitations\":["
        << "\"Capacity failures are compared as archived Timeout results; they have no finite fuel boundary.\","
        << "\"Successful capacity controls have scalar Int outputs. Internal GPU typed-list slice fallback is not claimed as exact payload parity.\","
        << "\"The bounded CPU profile is compared with frozen direct-GPU observations; it does not execute the current GPU backend.\"]}";
  }
  out << '}';
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
    if (argc != 3 && argc != 4)
      throw std::runtime_error(
          "usage: check_bounded_bytecode_transition INPUT.jsonl OUTPUT.json [GPU_ARCHIVE.jsonl]");
    const std::vector<Record> records = read_records(argv[1]);
    std::vector<RowReport> reports;
    reports.reserve(records.size());
    for (const Record& record : records) {
      try {
        reports.push_back(check_record(record));
      } catch (const std::exception& error) {
        throw std::runtime_error("ordinal " + std::to_string(record.ordinal) +
                                 ": " + error.what());
      }
    }
    std::size_t translated = 0;
    std::size_t rejected = 0;
    for (const RowReport& report : reports) {
      translated += report.translated ? 1U : 0U;
      rejected += report.translated ? 0U : 1U;
    }
    if (reports.size() != 201 || translated != 195 || rejected != 6)
      throw std::runtime_error("expected 195 translated rows and six strict-output rejections");
    CapacityReport capacity;
    const CapacityReport* capacity_ptr = nullptr;
    if (argc == 4) {
      capacity = check_capacity_profile(argv[3], records);
      capacity_ptr = &capacity;
    }
    const std::string report = encode_report(argv[1], reports, capacity_ptr);
    write_file(argv[2], report);
    std::cout << report << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "check_bounded_bytecode_transition: " << error.what() << '\n';
    return 1;
  }
}
