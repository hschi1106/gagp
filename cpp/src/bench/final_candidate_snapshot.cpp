#include "gagp/migration/typed_storage.hpp"
#include "final_candidate_snapshot.hpp"

#include <cstring>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "gagp/cli/commands.hpp"
#include "gagp/core/bytecode_verify.hpp"
#include "gagp/evolution/transition/bounded_regions.hpp"
#include "gagp/migration/artifact.hpp"
#include "gagp/runtime/payload/payload.hpp"

namespace gagp::migration::bench {
namespace {
using cli_detail::JsonValue;

const std::vector<JsonValue>& array(const JsonValue& value, const char* name) {
  if (value.kind != JsonValue::Kind::Array)
    throw std::runtime_error(std::string(name) + " must be an array");
  return value.array_v;
}

bool boolean(const JsonValue& value, const char* name) {
  if (value.kind != JsonValue::Kind::Bool)
    throw std::runtime_error(std::string(name) + " must be boolean");
  return value.bool_v;
}

int nibble(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  throw std::runtime_error("invalid snapshot hex digit");
}

std::string unhex(const std::string& encoded) {
  if (encoded.size() % 2 != 0) throw std::runtime_error("odd snapshot hex length");
  std::string result;
  result.reserve(encoded.size() / 2);
  for (std::size_t i = 0; i < encoded.size(); i += 2)
    result.push_back(static_cast<char>((nibble(encoded[i]) << 4) | nibble(encoded[i + 1])));
  return result;
}

Value decode_value(const JsonValue& raw, unsigned depth = 0) {
  if (depth > 2) throw std::runtime_error("nested snapshot payload is unsupported");
  const int tag = cli_detail::require_int(
      cli_detail::require_object_field(raw, "tag"), "tag");
  if (tag < static_cast<int>(ValueTag::Int) ||
      tag > static_cast<int>(ValueTag::StringList))
    throw std::runtime_error("snapshot contains opaque or invalid Value tag");
  if (raw.object_v.count("materialized"))
    throw std::runtime_error("snapshot contains an unmaterialized payload");
  const std::string bits = cli_detail::require_string(
      cli_detail::require_object_field(raw, "bits"), "bits");
  if (bits.size() != 16) throw std::runtime_error("snapshot bits must have 16 hex digits");
  std::uint64_t packed = 0;
  for (char digit : bits) packed = (packed << 4) | static_cast<unsigned>(nibble(digit));
  Value result = Value::invalid();
  result.tag = static_cast<ValueTag>(tag);
  std::memcpy(&result.i, &packed, sizeof(packed));
  result.b = boolean(cli_detail::require_object_field(raw, "bool"), "bool");
  if (result.tag == ValueTag::String) {
    const auto bytes = unhex(cli_detail::require_string(
        cli_detail::require_object_field(raw, "bytes_hex"), "bytes_hex"));
    payload::register_string(result, bytes);
  } else if (is_typed_list(result)) {
    const ValueTag element_tag = result.tag == ValueTag::IntList ? ValueTag::Int :
        result.tag == ValueTag::FloatList ? ValueTag::Float : ValueTag::String;
    std::vector<Value> elements;
    for (const auto& encoded : array(
             cli_detail::require_object_field(raw, "elements"), "elements")) {
      Value element = decode_value(encoded, depth + 1);
      if (element.tag != element_tag)
        throw std::runtime_error("snapshot typed-list element mismatch");
      elements.push_back(element);
    }
    payload::register_list(result, elements);
  }
  return result;
}

Value decode_opaque_bytecode_value(const JsonValue& raw) {
  const int tag = cli_detail::require_int(
      cli_detail::require_object_field(raw, "tag"), "tag");
  if (tag < static_cast<int>(ValueTag::Int) ||
      tag > static_cast<int>(ValueTag::FallbackToken))
    throw std::runtime_error("bytecode snapshot contains an invalid Value tag");
  const std::string bits = cli_detail::require_string(
      cli_detail::require_object_field(raw, "bits"), "bits");
  if (bits.size() != 16) throw std::runtime_error("snapshot bits must have 16 hex digits");
  std::uint64_t packed = 0;
  for (char digit : bits) packed = (packed << 4) | static_cast<unsigned>(nibble(digit));
  Value result = Value::invalid();
  result.tag = static_cast<ValueTag>(tag);
  std::memcpy(&result.i, &packed, sizeof(packed));
  result.b = boolean(cli_detail::require_object_field(raw, "bool"), "bool");
  const auto materialized = raw.object_v.find("materialized");
  if (materialized != raw.object_v.end() &&
      boolean(materialized->second, "materialized"))
    throw std::runtime_error(
        "legacy bytecode materialized payload requires source provenance");
  return result;
}

void require_empty_array_field(const JsonValue& raw, const char* name) {
  if (!array(cli_detail::require_object_field(raw, name), name).empty())
    throw std::runtime_error(std::string("legacy bytecode ") + name +
        " segments cannot be replayed by the final candidate");
}

BytecodeProgram decode_ordinary_bytecode(const JsonValue& raw) {
  if (cli_detail::require_string(
          cli_detail::require_object_field(raw, "format_version"),
          "format_version") != "migration-bytecode-v1")
    throw std::runtime_error("unsupported frozen bytecode member version");
  BytecodeProgram program;
  program.n_locals = cli_detail::require_int(
      cli_detail::require_object_field(raw, "n_locals"), "n_locals");
  for (const auto& constant : array(
           cli_detail::require_object_field(raw, "consts"), "consts"))
    program.consts.push_back(decode_opaque_bytecode_value(constant));
  for (const auto& encoded : array(
           cli_detail::require_object_field(raw, "code"), "code")) {
    const auto& fields = array(encoded, "instruction");
    if (fields.size() != 5)
      throw std::runtime_error("legacy bytecode instruction must have five fields");
    const int opcode = cli_detail::require_int(fields[0], "instruction opcode");
    if (opcode < static_cast<int>(Opcode::PushConst) ||
        opcode > static_cast<int>(Opcode::BoundedRegion) ||
        (opcode >= 25 && opcode <= 27))
      throw std::runtime_error("legacy bytecode contains a removed opcode");
    Instr instruction;
    instruction.op = static_cast<Opcode>(opcode);
    instruction.a = cli_detail::require_int(fields[1], "instruction a");
    instruction.b = cli_detail::require_int(fields[2], "instruction b");
    instruction.has_a = boolean(fields[3], "instruction has_a");
    instruction.has_b = boolean(fields[4], "instruction has_b");
    program.code.push_back(instruction);
  }
  require_empty_array_field(raw, "var2idx");
  require_empty_array_field(raw, "dc");
  require_empty_array_field(raw, "dp1d");
  require_empty_array_field(raw, "dp2d");
  const auto verified = verify_bytecode(program);
  if (!verified)
    throw std::runtime_error(std::string("invalid frozen bytecode (") +
        bytecode_verify_code_name(verified.diagnostic.code) + ") at " +
        verified.diagnostic.path + ": " + verified.diagnostic.message);
  return program;
}

legacy_v1::AstProgram decode_member(const JsonValue& member) {
  JsonValue wrapped;
  wrapped.kind = JsonValue::Kind::Object;
  JsonValue version;
  version.kind = JsonValue::Kind::String;
  version.string_v = "ast-prefix";
  wrapped.object_v.emplace("format_version", std::move(version));
  wrapped.object_v.emplace("ast", cli_detail::require_object_field(member, "structure"));
  auto legacy = decode_legacy_ast_artifact(wrapped);
  if (!legacy.consts.empty())
    throw std::runtime_error("snapshot structure duplicates its detached constants");
  for (const auto& constant : array(
           cli_detail::require_object_field(member, "constants"), "constants"))
    legacy.consts.push_back(decode_value(constant));
  return legacy;
}

std::uint64_t unsigned_decimal(const JsonValue& raw, const char* name) {
  const std::string text = cli_detail::require_string(raw, name);
  if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
    throw std::runtime_error(std::string(name) + " must be unsigned decimal");
  std::size_t consumed = 0;
  const auto result = std::stoull(text, &consumed);
  if (consumed != text.size()) throw std::runtime_error(std::string("invalid ") + name);
  return result;
}

double finite_number(const JsonValue& raw, const char* name) {
  if (raw.kind != JsonValue::Kind::Number || !std::isfinite(raw.number_v))
    throw std::runtime_error(std::string(name) + " must be finite");
  return raw.number_v;
}

int field_int(const JsonValue& object, const char* name) {
  return cli_detail::require_int(cli_detail::require_object_field(object, name), name);
}

void require_legacy_capacity(const legacy_v1::AstProgram& program,
                             const FrozenReproductionConfig& config,
                             const char* label) {
  const bool exceeded =
      program.nodes.size() > static_cast<std::size_t>(config.max_nodes) ||
      program.names.size() > static_cast<std::size_t>(config.max_names) ||
      program.consts.size() > static_cast<std::size_t>(config.max_consts) ||
      program.linear_rec_binders.size() >
          static_cast<std::size_t>(config.max_linear_rec_binders) ||
      program.asgp_dc_binders.size() >
          static_cast<std::size_t>(config.max_asgp_dc_binders) ||
      program.asgp_dp1d_specs.size() >
          static_cast<std::size_t>(config.max_asgp_dp1d_specs) ||
      program.asgp_dp2d_specs.size() >
          static_cast<std::size_t>(config.max_asgp_dp2d_specs);
  if (exceeded)
    throw std::runtime_error(std::string("frozen ") + label +
        " exceeds its captured capacity");
}

}  // namespace

std::vector<evo::ProgramGenome> decode_final_candidate_population(
    const JsonValue& raw, const std::vector<evo::InputSpec>& inputs,
    std::uint32_t minimum_dc_frames, bool normalize_storage) {
  if (cli_detail::require_string(
          cli_detail::require_object_field(raw, "format_version"),
          "format_version") != "migration-population-v1")
    throw std::runtime_error("unsupported final-candidate snapshot version");
  std::vector<evo::ProgramGenome> result;
  std::size_t member_index = 0;
  for (const auto& member : array(
           cli_detail::require_object_field(raw, "programs"), "programs")) {
    auto legacy = decode_member(member);
    try {
      auto genome = evo::transition::lower_bounded_regions(legacy, inputs, minimum_dc_frames);
      if (normalize_storage) {
        genome.ast = normalize_typed_storage(genome.ast, inputs, true).ast;
        genome.meta = evo::build_genome_meta(genome.ast);
      }
      result.push_back(std::move(genome));
    } catch (const std::exception& error) {
      throw std::runtime_error("frozen member " + std::to_string(member_index) +
          " cannot be lowered: " + error.what());
    }
    ++member_index;
  }
  if (result.empty()) throw std::runtime_error("empty final-candidate population");
  return result;
}

std::vector<BytecodeProgram> decode_final_candidate_bytecode_population(
    const JsonValue& raw) {
  if (cli_detail::require_string(
          cli_detail::require_object_field(raw, "format_version"),
          "format_version") != "migration-bytecode-population-v1")
    throw std::runtime_error("unsupported final-candidate bytecode population version");
  std::vector<BytecodeProgram> result;
  for (const auto& member : array(
           cli_detail::require_object_field(raw, "programs"), "programs"))
    result.push_back(decode_ordinary_bytecode(member));
  if (result.empty()) throw std::runtime_error("empty final-candidate bytecode population");
  return result;
}

evo::CaseSet decode_final_candidate_bytecode_cases(const JsonValue& raw) {
  if (cli_detail::require_string(
          cli_detail::require_object_field(raw, "format_version"),
          "format_version") != "migration-evaluation-cases-v1")
    throw std::runtime_error("unsupported frozen bytecode cases version");
  evo::CaseSet result;
  for (const auto& encoded_case : array(
           cli_detail::require_object_field(raw, "cases"), "cases")) {
    CaseBindings bindings;
    for (const auto& encoded_binding : array(
             cli_detail::require_object_field(encoded_case, "inputs"), "inputs")) {
      const auto& fields = array(encoded_binding, "input binding");
      if (fields.size() != 2)
        throw std::runtime_error("frozen bytecode input binding must have two fields");
      const int index = cli_detail::require_int(fields[0], "input index");
      if (index < 0) throw std::runtime_error("frozen bytecode input index must be nonnegative");
      bindings.push_back({index, decode_opaque_bytecode_value(fields[1])});
    }
    result.bindings.push_back(std::move(bindings));
    result.expected_values.push_back(decode_opaque_bytecode_value(
        cli_detail::require_object_field(encoded_case, "expected")));
  }
  if (result.bindings.empty()) throw std::runtime_error("empty frozen bytecode cases");
  return result;
}

FrozenReproduction decode_frozen_reproduction(
    const JsonValue& raw, const std::vector<evo::InputSpec>& inputs,
    std::uint32_t minimum_dc_frames, bool normalize_storage) {
  if (cli_detail::require_string(
          cli_detail::require_object_field(raw, "format_version"),
          "format_version") != "migration-reproduction-v1")
    throw std::runtime_error("unsupported frozen reproduction version");

  FrozenReproduction out;
  const auto& parents = cli_detail::require_object_field(raw, "parents");
  out.parents = decode_final_candidate_population(parents, inputs, minimum_dc_frames, normalize_storage);
  for (const auto& value : array(
           cli_detail::require_object_field(raw, "fitness"), "fitness"))
    out.fitness.push_back(finite_number(value, "fitness"));

  const auto& config = cli_detail::require_object_field(raw, "config");
  out.config.population_size = field_int(config, "population_size");
  out.config.pair_count = field_int(config, "pair_count");
  out.config.candidates_per_program = field_int(config, "candidates_per_program");
  out.config.donor_pool_size_per_type = field_int(config, "donor_pool_size_per_type");
  out.config.max_nodes = field_int(config, "max_nodes");
  out.config.max_donor_nodes = field_int(config, "max_donor_nodes");
  out.config.max_names = field_int(config, "max_names");
  out.config.max_consts = field_int(config, "max_consts");
  out.config.max_linear_rec_binders = field_int(config, "max_linear_rec_binders");
  out.config.max_asgp_dc_binders = field_int(config, "max_asgp_dc_binders");
  out.config.max_asgp_dp1d_specs = field_int(config, "max_asgp_dp1d_specs");
  out.config.max_asgp_dp2d_specs = field_int(config, "max_asgp_dp2d_specs");
  out.config.tournament_k = field_int(config, "tournament_k");
  out.config.max_expr_depth = field_int(config, "max_expr_depth");
  out.config.max_for_k = field_int(config, "max_for_k");
  out.config.mutation_ratio = finite_number(
      cli_detail::require_object_field(config, "mutation_ratio"), "mutation_ratio");
  out.config.mutation_subtree_ratio = finite_number(
      cli_detail::require_object_field(config, "mutation_subtree_ratio"),
      "mutation_subtree_ratio");
  out.config.seed = unsigned_decimal(
      cli_detail::require_object_field(config, "seed"), "seed");

  const auto& parent_rows = array(
      cli_detail::require_object_field(parents, "programs"), "parent programs");
  const auto& subtree_rows = array(
      cli_detail::require_object_field(raw, "subtree_ends"), "subtree_ends");
  const auto& candidate_rows = array(
      cli_detail::require_object_field(raw, "candidates"), "candidates");
  const std::size_t count = out.parents.size();
  if (out.fitness.size() != count || parent_rows.size() != count ||
      subtree_rows.size() != count || candidate_rows.size() != count ||
      out.config.population_size != static_cast<int>(count) ||
      out.config.pair_count != (out.config.population_size + 1) / 2 ||
      out.config.candidates_per_program <= 0 ||
      out.config.donor_pool_size_per_type <= 0 || out.config.max_nodes <= 0 ||
      out.config.max_donor_nodes <= 0 || out.config.max_names <= 0 ||
      out.config.max_consts <= 0 || out.config.max_linear_rec_binders < 0 ||
      out.config.max_asgp_dc_binders < 0 || out.config.max_asgp_dp1d_specs < 0 ||
      out.config.max_asgp_dp2d_specs < 0)
    throw std::runtime_error("frozen reproduction dimensions disagree");

  for (std::size_t p = 0; p < count; ++p) {
    const auto legacy = decode_member(parent_rows[p]);
    require_legacy_capacity(legacy, out.config, "parent");
    const auto verified = legacy_v1::verify(legacy, inputs);
    const auto& ends = array(subtree_rows[p], "subtree ends");
    if (ends.size() != verified.subtree_end.size())
      throw std::runtime_error("frozen reproduction subtree dimensions disagree");
    for (std::size_t i = 0; i < ends.size(); ++i) {
      const int end = cli_detail::require_int(ends[i], "subtree end");
      if (end < 0 || static_cast<std::size_t>(end) != verified.subtree_end[i])
        throw std::runtime_error("frozen reproduction subtree identity disagrees");
    }
    const auto& candidates = array(candidate_rows[p], "candidates");
    if (candidates.size() >
        static_cast<std::size_t>(out.config.candidates_per_program))
      throw std::runtime_error("frozen candidate count exceeds its captured capacity");
    for (const auto& candidate : candidates) {
      const auto& fields = array(candidate, "candidate");
      if (fields.size() != 10)
        throw std::runtime_error("invalid frozen candidate field count");
      const int start = cli_detail::require_int(fields[0], "candidate start");
      const int stop = cli_detail::require_int(fields[1], "candidate stop");
      (void)cli_detail::require_int(fields[2], "candidate tag");
      (void)cli_detail::require_int(fields[3], "candidate aux");
      (void)unsigned_decimal(fields[4], "scope signature");
      (void)unsigned_decimal(fields[5], "binder signature");
      (void)cli_detail::require_int(fields[6], "candidate scheme");
      (void)cli_detail::require_int(fields[7], "candidate phase");
      (void)unsigned_decimal(fields[8], "environment signature");
      (void)cli_detail::require_int(fields[9], "dependency arity");
      if (start < 0 || stop < start ||
          static_cast<std::size_t>(stop) > legacy.nodes.size())
        throw std::runtime_error("frozen candidate lies outside its parent");
    }
  }

  const auto& donors = array(
      cli_detail::require_object_field(raw, "donors"), "donors");
  const std::size_t expected_donors = static_cast<std::size_t>(
      out.config.donor_pool_size_per_type * 9);
  if (donors.size() != expected_donors)
    throw std::runtime_error("frozen donor dimensions disagree");
  for (const auto& donor : donors) {
    const int type = field_int(donor, "type");
    if (type < 0 || type >= static_cast<int>(evo::RType::Invalid))
      throw std::runtime_error("invalid frozen donor type");
    const auto& fragment = cli_detail::require_object_field(donor, "fragment");
    if (cli_detail::require_string(
            cli_detail::require_object_field(fragment, "format_version"),
            "fragment format_version") != "migration-population-v1")
      throw std::runtime_error("invalid frozen donor fragment version");
    const auto& programs = array(
        cli_detail::require_object_field(fragment, "programs"), "donor programs");
    if (programs.size() != 1)
      throw std::runtime_error("frozen donor must contain one expression");
    // Decoding all values is part of tape validation. The expression is not
    // lowered: it has no standalone lexical environment in the retired tape.
    const auto legacy = decode_member(programs.front());
    if (legacy.nodes.size() > static_cast<std::size_t>(out.config.max_donor_nodes) ||
        legacy.names.size() > static_cast<std::size_t>(out.config.max_names) ||
        legacy.consts.size() > static_cast<std::size_t>(out.config.max_consts))
      throw std::runtime_error("frozen donor exceeds its captured capacity");
    require_legacy_capacity(legacy, out.config, "donor");
  }
  return out;
}

}  // namespace gagp::migration::bench
