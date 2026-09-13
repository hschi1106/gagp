#include "migration_snapshot.hpp"

#include <cstring>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>

#include "gagp/cli/commands.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/runtime/payload/payload.hpp"

namespace gagp::migration {
namespace {
using cli_detail::JsonValue;
using cli_detail::require_object_field;
using cli_detail::require_string;

std::string hex_bytes(const std::string& bytes) {
  const char* digits = "0123456789abcdef";
  std::string result;
  for (unsigned char c : bytes) {
    result += digits[c >> 4];
    result += digits[c & 15];
  }
  return result;
}

int nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  throw std::runtime_error("invalid snapshot hex digit");
}

std::string unhex(const std::string& hex) {
  if (hex.size() % 2) throw std::runtime_error("odd snapshot hex length");
  std::string result;
  for (std::size_t i = 0; i < hex.size(); i += 2) {
    result += static_cast<char>(nibble(hex[i]) * 16 + nibble(hex[i + 1]));
  }
  return result;
}

const std::vector<JsonValue>& array(const JsonValue& raw) {
  if (raw.kind != JsonValue::Kind::Array) throw std::runtime_error("expected snapshot array");
  return raw.array_v;
}

template <class Program>
void phase_fields(std::ostream& out, const Program& program, bool require_payload) {
  out << "\"n_locals\":" << program.n_locals << ",\"consts\":[";
  for (std::size_t i = 0; i < program.consts.size(); ++i) {
    if (i) out << ',';
    out << encode_value(program.consts[i], require_payload);
  }
  out << "],\"code\":[";
  for (std::size_t i = 0; i < program.code.size(); ++i) {
    if (i) out << ',';
    const auto& ins = program.code[i];
    out << '[' << static_cast<int>(ins.op) << ',' << ins.a << ',' << ins.b << ','
        << (ins.has_a ? "true" : "false") << ',' << (ins.has_b ? "true" : "false") << ']';
  }
  out << "],\"var2idx\":[";
  bool first = true;
  for (const auto& entry : std::map<std::string, int>(program.var2idx.begin(), program.var2idx.end())) {
    if (!first) out << ',';
    first = false;
    out << "[\"" << hex_bytes(entry.first) << "\"," << entry.second << ']';
  }
  out << ']';
}

void phase(std::ostream& out, const PhaseProgram& program, bool require_payload) {
  out << '{';
  phase_fields(out, program, require_payload);
  out << ",\"binder_locals\":[";
  bool first = true;
  for (const auto& entry : std::map<int, int>(program.binder_locals.begin(), program.binder_locals.end())) {
    if (!first) out << ',';
    first = false;
    out << '[' << entry.first << ',' << entry.second << ']';
  }
  out << "]}";
}

void ints(std::ostream& out, const std::vector<int>& values) {
  out << '[';
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i) out << ',';
    out << values[i];
  }
  out << ']';
}

std::vector<int> read_ints(const JsonValue& raw, std::size_t size = 0) {
  std::vector<int> result;
  for (const auto& value : array(raw)) result.push_back(cli_detail::require_int(value, "integer array"));
  if (size && result.size() != size) throw std::runtime_error("snapshot metadata arity mismatch");
  return result;
}

bool read_bool(const JsonValue& raw) {
  if (raw.kind != JsonValue::Kind::Bool) throw std::runtime_error("expected snapshot boolean");
  return raw.bool_v;
}

template <class Program>
void read_phase_fields(const JsonValue& raw, Program* program) {
  program->n_locals = cli_detail::require_int(require_object_field(raw, "n_locals"), "n_locals");
  for (const auto& value : array(require_object_field(raw, "consts"))) program->consts.push_back(decode_value(value));
  for (const auto& value : array(require_object_field(raw, "code"))) {
    const auto& fields = array(value);
    if (fields.size() != 5) throw std::runtime_error("snapshot instruction arity mismatch");
    program->code.push_back({static_cast<Opcode>(cli_detail::require_int(fields[0], "opcode")),
        cli_detail::require_int(fields[1], "a"), cli_detail::require_int(fields[2], "b"),
        read_bool(fields[3]), read_bool(fields[4])});
  }
  for (const auto& value : array(require_object_field(raw, "var2idx"))) {
    const auto& fields = array(value);
    if (fields.size() != 2) throw std::runtime_error("snapshot variable mapping arity mismatch");
    const auto name = unhex(require_string(fields[0], "name"));
    if (!program->var2idx.emplace(name, cli_detail::require_int(fields[1], "index")).second) {
      throw std::runtime_error("duplicate snapshot variable mapping");
    }
  }
}

PhaseProgram read_phase(const JsonValue& raw) {
  PhaseProgram program;
  read_phase_fields(raw, &program);
  for (const auto& value : array(require_object_field(raw, "binder_locals"))) {
    const auto fields = read_ints(value, 2);
    if (!program.binder_locals.emplace(fields[0], fields[1]).second) {
      throw std::runtime_error("duplicate snapshot binder mapping");
    }
  }
  return program;
}
}  // namespace

BytecodeProgram decode_bytecode(const JsonValue& raw) {
  if (require_string(require_object_field(raw, "format_version"), "format_version") != "migration-bytecode-v1") {
    throw std::runtime_error("unsupported migration bytecode version");
  }
  BytecodeProgram program;
  read_phase_fields(raw, &program);
  for (const auto& row : array(require_object_field(raw, "dc"))) {
    const auto names = read_ints(require_object_field(row, "names"), 6);
    AsgpDcSegment segment;
    segment.solve_xs_name = names[0]; segment.solve_n_name = names[1]; segment.solve_lo_name = names[2];
    segment.divide_n_name = names[3]; segment.combine_left_name = names[4]; segment.combine_right_name = names[5];
    segment.solve = read_phase(require_object_field(row, "solve"));
    segment.divide = read_phase(require_object_field(row, "divide"));
    segment.combine = read_phase(require_object_field(row, "combine"));
    program.asgp_dc_segments.push_back(std::move(segment));
  }
  for (const auto& row : array(require_object_field(raw, "dp1d"))) {
    const auto bounds = read_ints(require_object_field(row, "bounds"), 3);
    const auto names = read_ints(require_object_field(row, "names"), 2);
    AsgpDp1dSegment segment;
    segment.lo = bounds[0]; segment.hi = bounds[1]; segment.base_state = bounds[2];
    segment.boundary_value = decode_value(require_object_field(row, "boundary_value"));
    segment.dep_kind = cli_detail::require_int(require_object_field(row, "dep_kind"), "dep_kind");
    segment.dep_offsets = read_ints(require_object_field(row, "dep_offsets"));
    segment.solve_state_name = names[0]; segment.transition_state_name = names[1];
    segment.transition_dep_names = read_ints(require_object_field(row, "transition_dep_names"));
    segment.solve = read_phase(require_object_field(row, "solve"));
    segment.transition = read_phase(require_object_field(row, "transition"));
    program.asgp_dp1d_segments.push_back(std::move(segment));
  }
  for (const auto& row : array(require_object_field(raw, "dp2d"))) {
    const auto bounds = read_ints(require_object_field(row, "bounds"), 6);
    const auto names = read_ints(require_object_field(row, "names"), 4);
    AsgpDp2dSegment segment;
    segment.i_lo = bounds[0]; segment.i_hi = bounds[1]; segment.j_lo = bounds[2]; segment.j_hi = bounds[3];
    segment.base_i = bounds[4]; segment.base_j = bounds[5];
    segment.boundary_value = decode_value(require_object_field(row, "boundary_value"));
    segment.dep_kind = cli_detail::require_int(require_object_field(row, "dep_kind"), "dep_kind");
    segment.solve_i_name = names[0]; segment.solve_j_name = names[1];
    segment.transition_i_name = names[2]; segment.transition_j_name = names[3];
    segment.transition_dep_names = read_ints(require_object_field(row, "transition_dep_names"));
    segment.solve = read_phase(require_object_field(row, "solve"));
    segment.transition = read_phase(require_object_field(row, "transition"));
    program.asgp_dp2d_segments.push_back(std::move(segment));
  }
  // Deliberately preserve verifier-negative programs for the migration oracle.
  // Callers must run the production verifier before executing untrusted artifacts.
  return program;
}

std::string encode_bytecode(const BytecodeProgram& program, bool require_payload) {
  std::ostringstream out;
  out << "{\"format_version\":\"migration-bytecode-v1\",";
  phase_fields(out, program, require_payload);
  out << ",\"dc\":[";
  for (std::size_t i = 0; i < program.asgp_dc_segments.size(); ++i) {
    if (i) out << ',';
    const auto& s = program.asgp_dc_segments[i];
    out << "{\"names\":";
    ints(out, {s.solve_xs_name, s.solve_n_name, s.solve_lo_name, s.divide_n_name,
               s.combine_left_name, s.combine_right_name});
    out << ",\"solve\":"; phase(out, s.solve, require_payload);
    out << ",\"divide\":"; phase(out, s.divide, require_payload);
    out << ",\"combine\":"; phase(out, s.combine, require_payload);
    out << '}';
  }
  out << "],\"dp1d\":[";
  for (std::size_t i = 0; i < program.asgp_dp1d_segments.size(); ++i) {
    if (i) out << ',';
    const auto& s = program.asgp_dp1d_segments[i];
    out << "{\"bounds\":"; ints(out, {s.lo, s.hi, s.base_state});
    out << ",\"boundary_value\":" << encode_value(s.boundary_value, require_payload) << ",\"dep_kind\":" << s.dep_kind;
    out << ",\"dep_offsets\":"; ints(out, s.dep_offsets);
    out << ",\"names\":"; ints(out, {s.solve_state_name, s.transition_state_name});
    out << ",\"transition_dep_names\":"; ints(out, s.transition_dep_names);
    out << ",\"solve\":"; phase(out, s.solve, require_payload);
    out << ",\"transition\":"; phase(out, s.transition, require_payload);
    out << '}';
  }
  out << "],\"dp2d\":[";
  for (std::size_t i = 0; i < program.asgp_dp2d_segments.size(); ++i) {
    if (i) out << ',';
    const auto& s = program.asgp_dp2d_segments[i];
    out << "{\"bounds\":"; ints(out, {s.i_lo, s.i_hi, s.j_lo, s.j_hi, s.base_i, s.base_j});
    out << ",\"boundary_value\":" << encode_value(s.boundary_value, require_payload) << ",\"dep_kind\":" << s.dep_kind;
    out << ",\"names\":"; ints(out, {s.solve_i_name, s.solve_j_name, s.transition_i_name, s.transition_j_name});
    out << ",\"transition_dep_names\":"; ints(out, s.transition_dep_names);
    out << ",\"solve\":"; phase(out, s.solve, require_payload);
    out << ",\"transition\":"; phase(out, s.transition, require_payload);
    out << '}';
  }
  out << "]}";
  return out.str();
}

std::string encode_value(const Value& value, bool require_payload) {
  std::uint64_t bits = 0;
  if (value.tag != ValueTag::Bool && value.tag != ValueTag::Invalid) {
    std::memcpy(&bits, &value.i, sizeof(bits));
  }
  std::ostringstream out;
  out << "{\"tag\":" << static_cast<int>(value.tag)
      << ",\"bits\":\"" << std::hex << std::setw(16) << std::setfill('0') << bits
      << "\",\"bool\":" << (value.b ? "true" : "false");
  if (value.tag == ValueTag::String) {
    std::string data;
    if (!payload::lookup_string(value, &data)) {
      if (require_payload) throw std::runtime_error("missing snapshot string payload");
      return out.str() + ",\"materialized\":false}";
    }
    out << ",\"bytes_hex\":\"" << hex_bytes(data) << "\"";
  } else if (value.tag == ValueTag::IntList || value.tag == ValueTag::FloatList ||
             value.tag == ValueTag::StringList) {
    std::vector<Value> elements;
    if (!payload::lookup_list(value, &elements)) {
      if (require_payload) throw std::runtime_error("missing snapshot list payload");
      return out.str() + ",\"materialized\":false}";
    }
    out << ",\"elements\":[";
    for (std::size_t i = 0; i < elements.size(); ++i) {
      if (i) out << ',';
      out << encode_value(elements[i], require_payload);
    }
    out << ']';
  }
  out << '}';
  return out.str();
}

Value decode_value(const JsonValue& raw) {
  const int tag = cli_detail::require_int(require_object_field(raw, "tag"), "tag");
  if (tag < 0 || tag > static_cast<int>(ValueTag::Invalid)) throw std::runtime_error("invalid snapshot tag");
  const std::string hex = require_string(require_object_field(raw, "bits"), "bits");
  if (hex.size() != 16) throw std::runtime_error("snapshot bits must have 16 hex digits");
  std::uint64_t bits = 0;
  for (char c : hex) bits = (bits << 4) | static_cast<unsigned>(nibble(c));
  Value result = Value::invalid();
  result.tag = static_cast<ValueTag>(tag);
  std::memcpy(&result.i, &bits, sizeof(bits));
  const auto& boolean = require_object_field(raw, "bool");
  if (boolean.kind != JsonValue::Kind::Bool) throw std::runtime_error("invalid snapshot bool");
  result.b = boolean.bool_v;
  const auto materialized = raw.object_v.find("materialized");
  if (materialized != raw.object_v.end()) {
    if (read_bool(materialized->second) ||
        (result.tag != ValueTag::String && result.tag != ValueTag::IntList &&
         result.tag != ValueTag::FloatList && result.tag != ValueTag::StringList) ||
        raw.object_v.count("bytes_hex") || raw.object_v.count("elements")) {
      throw std::runtime_error("invalid absent-payload snapshot marker");
    }
    return result;
  }
  if (result.tag == ValueTag::String) {
    payload::register_string(result, unhex(require_string(require_object_field(raw, "bytes_hex"), "bytes_hex")));
  } else if (result.tag == ValueTag::IntList || result.tag == ValueTag::FloatList ||
             result.tag == ValueTag::StringList) {
    std::vector<Value> elements;
    const ValueTag expected = result.tag == ValueTag::IntList ? ValueTag::Int :
                             result.tag == ValueTag::FloatList ? ValueTag::Float : ValueTag::String;
    for (const auto& entry : array(require_object_field(raw, "elements"))) {
      // Only StringList elements can themselves own a payload; reject nesting
      // before recursion so malformed snapshots cannot grow an unbounded stack.
      if (cli_detail::require_int(require_object_field(entry, "tag"), "tag") != static_cast<int>(expected)) {
        throw std::runtime_error("snapshot typed-list element mismatch");
      }
      elements.push_back(decode_value(entry));
    }
    payload::register_list(result, elements);
  }
  return result;
}

std::string encode_population(const std::vector<evo::ProgramGenome>& population, bool require_payload) {
  if (population.empty()) throw std::runtime_error("cannot snapshot empty population");
  std::ostringstream out;
  out << "{\"format_version\":\"migration-population-v1\",\"programs\":[";
  for (std::size_t i = 0; i < population.size(); ++i) {
    if (i) out << ',';
    auto structure = population[i].ast;
    structure.consts.clear();
    out << "{\"structure\":" << cli_detail::encode_ast_json(structure) << ",\"constants\":[";
    for (std::size_t j = 0; j < population[i].ast.consts.size(); ++j) {
      if (j) out << ',';
      out << encode_value(population[i].ast.consts[j], require_payload);
    }
    out << "]}";
  }
  out << "]}";
  return out.str();
}

std::vector<evo::ProgramGenome> decode_population(const JsonValue& raw) {
  if (require_string(require_object_field(raw, "format_version"), "format_version") != "migration-population-v1") {
    throw std::runtime_error("unsupported migration population version");
  }
  std::vector<evo::ProgramGenome> result;
  for (const auto& program : array(require_object_field(raw, "programs"))) {
    evo::ProgramGenome genome;
    genome.ast = cli_detail::decode_ast_json(require_object_field(program, "structure"));
    if (!genome.ast.consts.empty()) throw std::runtime_error("snapshot structure must not duplicate constants");
    for (const auto& value : array(require_object_field(program, "constants"))) {
      genome.ast.consts.push_back(decode_value(value));
    }
    const auto checked = evo::verify_ast_structure(genome.ast);
    if (!checked.ok) throw std::runtime_error("invalid snapshot AST structure");
    genome.meta = evo::build_genome_meta(genome.ast);
    result.push_back(std::move(genome));
  }
  if (result.empty()) throw std::runtime_error("empty snapshot population");
  return result;
}
}  // namespace gagp::migration
