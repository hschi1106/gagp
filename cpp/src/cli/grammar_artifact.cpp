#include "gagp/cli/grammar_artifact.hpp"
#include "gagp/cli/commands.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/node_descriptor.hpp"
#include "gagp/evolution/grammar/identity.hpp"
#include "gagp/evolution/grammar/values.hpp"

#include <charconv>
#include <cmath>
#include <cctype>
#include <limits>
#include <stdexcept>

namespace gagp::cli_detail {
namespace {
using namespace evo::grammar;
using Json = JsonValue;
Json object() { Json out; out.kind = Json::Kind::Object; return out; }
Json array() { Json out; out.kind = Json::Kind::Array; return out; }
Json string(std::string value) { Json out; out.kind = Json::Kind::String; out.string_v = std::move(value); return out; }
Json integer(std::uint32_t value) { Json out; out.kind = Json::Kind::Number; out.number_v = value; return out; }
Json row(std::initializer_list<std::uint32_t> fields) {
  Json out = array(); for (auto value : fields) out.array_v.push_back(integer(value)); return out;
}
const std::vector<Json>& elements(const Json& value) {
  if (value.kind != Json::Kind::Array) throw std::invalid_argument("artifact field must be an array");
  return value.array_v;
}
std::uint32_t positive_limit(const Json& value, const char* field, std::uint32_t maximum) {
  const auto& number = require_object_field(value, field);
  if (number.kind != Json::Kind::Number || !std::isfinite(number.number_v) ||
      number.number_v < 1 || number.number_v > maximum || std::trunc(number.number_v) != number.number_v)
    throw std::invalid_argument(std::string("artifact ") + field + " must be a positive integer within its supported limit");
  return static_cast<std::uint32_t>(number.number_v);
}
std::uint32_t index_value(const Json& value) {
  if (value.kind != Json::Kind::Number || !std::isfinite(value.number_v) || value.number_v < 0 ||
      value.number_v > 4294967295.0 || std::trunc(value.number_v) != value.number_v)
    throw std::invalid_argument("artifact request ID must be an unsigned 32-bit integer");
  return static_cast<std::uint32_t>(value.number_v);
}
Json parse(const std::string& text) {
  if (text.size() > 256 * 1024 * 1024) throw std::invalid_argument("grammar artifact exceeds 256 MiB");
  Json value;
  try { value = JsonParser(text, {true, 512}).parse(); }
  catch (const std::runtime_error& error) {
    throw std::invalid_argument(std::string("invalid grammar artifact JSON: ") + error.what());
  }
  if (require_string(require_object_field(value, "format_version"), "format_version") != kGeneratedGrammarArtifactVersion)
    throw std::invalid_argument("unsupported generated grammar artifact version");
  return value;
}
Json parse_materialized(const std::string& text) {
  if (text.size() > 256 * 1024 * 1024)
    throw std::invalid_argument("grammar artifact exceeds 256 MiB");
  Json value;
  try { value = JsonParser(text, {true, 512}).parse(); }
  catch (const std::runtime_error& error) {
    throw std::invalid_argument(std::string("invalid grammar artifact JSON: ") + error.what());
  }
  if (require_string(require_object_field(value, "format_version"), "format_version") !=
      kMaterializedGrammarArtifactVersion)
    throw std::invalid_argument("unsupported materialized grammar artifact version");
  return value;
}
void require_only_fields(const Json& value,
                         std::initializer_list<const char*> allowed,
                         const char* section) {
  if (value.kind != Json::Kind::Object)
    throw std::invalid_argument(std::string(section) + " must be an object");
  for (const auto& field : value.object_v) {
    bool found = false;
    for (const char* name : allowed) found = found || field.first == name;
    if (!found)
      throw std::invalid_argument(std::string(section) + " has unknown field: " +
                                  field.first);
  }
}
void require_sha256(const std::string& value) {
  if (value.size() != 64)
    throw std::invalid_argument("materialized source identity SHA-256 must have 64 lowercase hex digits");
  for (unsigned char c : value)
    if (!std::isdigit(c) && !(c >= 'a' && c <= 'f'))
      throw std::invalid_argument("materialized source identity SHA-256 must have 64 lowercase hex digits");
}
// The AST codec narrows structural numeric fields to int. Migrated constants
// are always detached and use the separate lossless singleton-domain codec.
void check_shape_numbers(const Json& value) {
  if (value.kind == Json::Kind::Number &&
      (!std::isfinite(value.number_v) || std::trunc(value.number_v) != value.number_v ||
       value.number_v < std::numeric_limits<int>::min() || value.number_v > std::numeric_limits<int>::max()))
    throw std::invalid_argument("artifact AST shape requires signed 32-bit integer fields");
  for (const auto& child : value.array_v) check_shape_numbers(child);
  for (const auto& field : value.object_v) check_shape_numbers(field.second);
}
void check_shape_capacity(const evo::AstProgram& ast, const GrammarLimits& limits) {
  if (ast.nodes.empty() || ast.nodes.size() > limits.max_nodes)
    throw std::invalid_argument("artifact AST is empty or exceeds its recorded node limit");
  std::vector<int> remaining{1};
  for (const auto& node : ast.nodes) {
    while (!remaining.empty() && remaining.back() == 0) remaining.pop_back();
    if (remaining.empty()) throw std::invalid_argument("artifact AST has trailing prefix nodes");
    if (remaining.size() > limits.max_depth) throw std::invalid_argument("artifact AST exceeds its recorded prefix depth limit");
    --remaining.back();
    if (!evo::is_known_node_kind(static_cast<int>(node.kind)))
      throw std::invalid_argument("artifact AST contains an unknown node kind");
    const auto arity = evo::node_prefix_arity(node);
    if (arity > 0) remaining.push_back(arity);
  }
  for (const auto count : remaining)
    if (count != 0) throw std::invalid_argument("artifact AST has missing prefix children");
}
std::vector<evo::InputSpec> inputs(const Json& value) {
  std::vector<evo::InputSpec> out;
  for (const auto& input : elements(value)) {
    out.push_back({require_string(require_object_field(input, "name"), "name"),
        parse_type(require_string(require_object_field(input, "type"), "type"))});
  }
  return out;
}
}  // namespace

std::string encode_generated_artifact(const evo::grammar::CompiledGrammar& grammar,
    const evo::grammar::GeneratedDerivation& generated) {
  using namespace evo::grammar;
  const auto& metadata = generated.derivation;
  if (!metadata.seed_replayable)
    throw std::invalid_argument("reconstructed grammar provenance is not an original seed replay; execute the materialized AST; seed replay requires original generation provenance");
  if (metadata.grammar_hash != grammar.content_hash()) throw std::invalid_argument("artifact grammar identity mismatch");
  Json root = object();
  root.object_v["format_version"] = string(kGeneratedGrammarArtifactVersion);
  root.object_v["semantic_version"] = string(metadata.semantic_version);
  root.object_v["generator_version"] = string(metadata.generator_version);
  root.object_v["rng_version"] = string(metadata.rng_version);
  root.object_v["grammar_hash"] = string(grammar.content_hash());
  root.object_v["grammar"] = JsonParser(grammar.canonical_definition(), {true, 512}).parse();
  root.object_v["inputs"] = root.object_v["grammar"].object_v.at("inputs");
  root.object_v["input_schema_hash"] = string(content_sha256(canonical_json(root.object_v["inputs"])));
  root.object_v["return_type"] = string(std::string(type_name(metadata.request.type)));
  Json request = object();
  request.object_v["nonterminal"] = integer(metadata.request.nonterminal);
  request.object_v["type"] = string(std::string(type_name(metadata.request.type)));
  request.object_v["visible_environment"] = array();
  for (const auto& binding : metadata.request.visible_environment) {
    Json value = object();
    value.object_v["name"] = string(binding.name);
    value.object_v["type"] = string(std::string(type_name(binding.type)));
    request.object_v["visible_environment"].array_v.push_back(std::move(value));
  }
  request.object_v["scope_mapping"] = array();
  for (auto index : metadata.request_scope_mapping)
    request.object_v["scope_mapping"].array_v.push_back(integer(index));
  root.object_v["request"] = std::move(request);
  root.object_v["seed"] = string(std::to_string(metadata.seed));
  root.object_v["payload_seeding"] = string("domain-only-v1");
  root.object_v["search_limits"] = object();
  root.object_v["search_limits"].object_v["max_nodes"] = integer(metadata.search_limits.max_nodes);
  root.object_v["search_limits"].object_v["max_depth"] = integer(metadata.search_limits.max_depth);
  root.object_v["execution_limits"] = object();
  root.object_v["execution_limits"].object_v["fuel"] = integer(metadata.execution_limits.fuel);
  auto shape = generated.genome.ast;
  shape.consts.clear();
  root.object_v["ast_shape"] = JsonParser(encode_ast_json(shape), {true, 512}).parse();
  Json constants = array();
  for (const auto& value : generated.genome.ast.consts) constants.array_v.push_back(encode_constant(value));
  root.object_v["constants"] = std::move(constants);
  Json provenance = object();
  provenance.object_v["logical_steps"] = integer(metadata.logical_steps);
  provenance.object_v["derived_nodes"] = integer(metadata.derived_nodes);
  provenance.object_v["lowered_instructions"] = integer(metadata.lowered_instructions);
  for (const char* field : {"nodes", "choices", "templates", "holes"}) provenance.object_v[field] = array();
  for (const auto& node : metadata.nodes)
    provenance.object_v["nodes"].array_v.push_back(row({node.expression, node.production, node.nonterminal,
        node.logical_instance, node.template_instance, node.slot, node.fixed ? 1u : 0u}));
  for (const auto& choice : metadata.choices)
    provenance.object_v["choices"].array_v.push_back(row({choice.nonterminal, choice.production, choice.parent, choice.ast_begin, choice.ast_end}));
  for (const auto& instance : metadata.templates)
    provenance.object_v["templates"].array_v.push_back(row({instance.template_id, instance.parent}));
  for (const auto& hole : metadata.holes)
    provenance.object_v["holes"].array_v.push_back(row({hole.template_instance, hole.slot, hole.ast_begin, hole.ast_end}));
  root.object_v["derivation"] = std::move(provenance);
  const auto result = canonical_json(root);
  if (result.size() > 256 * 1024 * 1024) throw std::invalid_argument("grammar artifact exceeds 256 MiB");
  return result;
}

MaterializedGrammarArtifact decode_materialized_program(const std::string& artifact) {
  using namespace evo::grammar;
  const auto root = parse(artifact);
  if (require_string(require_object_field(root, "semantic_version"), "semantic_version") != kGrammarSemanticVersion)
    throw std::invalid_argument("artifact semantic version mismatch; use the recorded runtime");
  MaterializedGrammarArtifact out;
  const auto& search = require_object_field(root, "search_limits");
  out.search_limits = {positive_limit(search, "max_nodes", 65536), positive_limit(search, "max_depth", 256)};
  out.execution_limits.fuel = positive_limit(require_object_field(root, "execution_limits"), "fuel", 2147483647);
  const auto& shape = require_object_field(root, "ast_shape");
  check_shape_numbers(shape);
  auto ast = decode_ast_json(shape);
  check_shape_capacity(ast, out.search_limits);
  if (!ast.consts.empty()) throw std::invalid_argument("artifact AST shape must not contain a second constant pool");
  for (const auto& value : elements(require_object_field(root, "constants"))) ast.consts.push_back(decode_constant(value));
  const auto& schema = require_object_field(root, "inputs");
  if (require_string(require_object_field(root, "input_schema_hash"), "input_schema_hash") != content_sha256(canonical_json(schema)))
    throw std::invalid_argument("artifact input schema hash mismatch");
  out.inputs = inputs(schema);
  const auto verified = evo::verify_ast(ast, out.inputs);
  if (!verified) throw std::invalid_argument("materialized artifact fails native verification: " + verified.diagnostic.message);
  out.return_type = parse_type(require_string(require_object_field(root, "return_type"), "return_type"));
  if (verified.verified.return_type != out.return_type)
    throw std::invalid_argument("materialized artifact return type differs from its declared contract");
  out.genome.ast = std::move(ast);
  out.genome.meta = evo::build_genome_meta(out.genome.ast);
  return out;
}

MaterializedGrammarArtifact decode_migrated_materialized_program(
    const std::string& artifact) {
  using namespace evo::grammar;
  const auto root = parse_materialized(artifact);
  require_only_fields(root,
      {"format_version", "semantic_version", "source_identity", "inputs", "return_type",
       "search_limits", "execution_limits", "ast", "constants"},
      "materialized artifact");
  if (require_string(require_object_field(root, "semantic_version"),
                     "semantic_version") != kGrammarSemanticVersion)
    throw std::invalid_argument(
        "materialized artifact semantic version mismatch; use the recorded runtime");

  const auto& identity = require_object_field(root, "source_identity");
  require_only_fields(identity, {"format_version", "content_sha256"},
                      "materialized source_identity");
  const auto source_version = require_string(
      require_object_field(identity, "format_version"),
      "source_identity.format_version");
  if (source_version != "ast-prefix-v1" &&
      source_version != "grammar-generated-v1")
    throw std::invalid_argument(
        "materialized source identity must name ast-prefix-v1 or grammar-generated-v1");
  require_sha256(require_string(require_object_field(identity, "content_sha256"),
                                "source_identity.content_sha256"));

  MaterializedGrammarArtifact out;
  const auto& search = require_object_field(root, "search_limits");
  require_only_fields(search, {"max_nodes", "max_depth"},
                      "materialized search_limits");
  out.search_limits = {
      positive_limit(search, "max_nodes", 65536),
      positive_limit(search, "max_depth", 256),
  };
  const auto& execution = require_object_field(root, "execution_limits");
  require_only_fields(execution, {"fuel"}, "materialized execution_limits");
  out.execution_limits.fuel = positive_limit(execution, "fuel", 2147483647);

  const auto& schema = require_object_field(root, "inputs");
  out.inputs = inputs(schema);
  const auto& shape = require_object_field(root, "ast");
  check_shape_numbers(shape);
  auto ast = decode_ast_json(shape);
  if (!ast.consts.empty())
    throw std::invalid_argument(
        "materialized artifact must not contain two constant pools");
  for (const auto& value : elements(require_object_field(root, "constants")))
    ast.consts.push_back(decode_constant(value));
  check_shape_capacity(ast, out.search_limits);
  const auto verified = evo::verify_ast(ast, out.inputs);
  if (!verified)
    throw std::invalid_argument("materialized artifact fails native verification: " +
                                verified.diagnostic.message);
  out.return_type = parse_type(require_string(
      require_object_field(root, "return_type"), "return_type"));
  if (verified.verified.return_type != out.return_type)
    throw std::invalid_argument(
        "materialized artifact return type differs from its declared contract");
  out.genome.ast = std::move(ast);
  out.genome.meta = evo::build_genome_meta(out.genome.ast);
  return out;
}

evo::ProgramGenome decode_materialized_artifact(const std::string& artifact) {
  return decode_materialized_program(artifact).genome;
}

evo::grammar::GeneratedDerivation replay_generated_artifact(const std::string& artifact,
    const evo::grammar::CompiledGrammar* required_grammar) {
  using namespace evo::grammar;
  const auto root = parse(artifact);
  if (require_string(require_object_field(root, "semantic_version"), "semantic_version") != kGrammarSemanticVersion ||
      require_string(require_object_field(root, "generator_version"), "generator_version") != kGrammarGeneratorVersion ||
      require_string(require_object_field(root, "rng_version"), "rng_version") != kGrammarRngVersion)
    throw std::invalid_argument("artifact semantic/generator/RNG version mismatch; restore its recorded runtime and generator; materialized decoding requires matching runtime semantics");
  const auto grammar = compile_grammar(parse_definition(canonical_json(require_object_field(root, "grammar"))));
  if (require_string(require_object_field(root, "grammar_hash"), "grammar_hash") != grammar.content_hash() ||
      (required_grammar && required_grammar->content_hash() != grammar.content_hash()))
    throw std::invalid_argument("artifact grammar hash mismatch; restore the recorded resolved grammar and imports");
  const auto seed_text = require_string(require_object_field(root, "seed"), "seed");
  std::uint64_t seed = 0;
  const auto decoded = std::from_chars(seed_text.data(), seed_text.data() + seed_text.size(), seed);
  if (decoded.ec != std::errc{} || decoded.ptr != seed_text.data() + seed_text.size() || std::to_string(seed) != seed_text)
    throw std::invalid_argument("artifact seed must be a canonical unsigned 64-bit decimal string");
  const auto& recorded_request = require_object_field(root, "request");
  GenerationRequest request;
  request.nonterminal = index_value(require_object_field(recorded_request, "nonterminal"));
  request.type = parse_type(require_string(require_object_field(recorded_request, "type"), "request type"));
  for (const auto& binding : elements(require_object_field(recorded_request, "visible_environment")))
    request.visible_environment.push_back({require_string(require_object_field(binding, "name"), "binding name"),
        parse_type(require_string(require_object_field(binding, "type"), "binding type"))});
  const auto& limits = require_object_field(root, "search_limits");
  request.budget = {positive_limit(limits, "max_nodes", 65536), positive_limit(limits, "max_depth", 256)};
  auto generated = generate_derivation(grammar, seed, request);
  if (encode_generated_artifact(grammar, generated) != canonical_json(root))
    throw std::invalid_argument("artifact materialization, limits, input schema or provenance differs from same-version replay");
  return generated;
}

}  // namespace gagp::cli_detail
