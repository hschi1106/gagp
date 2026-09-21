#include "gagp/migration/artifact.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

#include "gagp/cli/commands.hpp"
#include "gagp/core/bytecode_verify.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/identity.hpp"
#include "gagp/evolution/grammar/values.hpp"
#include "gagp/evolution/node_descriptor.hpp"
#include "gagp/evolution/transition/bounded_regions.hpp"
#include "gagp/migration/legacy_grammar_config.hpp"
#include "gagp/runtime/payload/payload.hpp"

namespace gagp::migration {
namespace {

using Json = cli_detail::JsonValue;
namespace grammar = evo::grammar;

Json string(std::string value) { Json out; out.kind = Json::Kind::String; out.string_v = std::move(value); return out; }
Json number(double value) { Json out; out.kind = Json::Kind::Number; out.number_v = value; return out; }
Json array() { Json out; out.kind = Json::Kind::Array; return out; }
Json object() { Json out; out.kind = Json::Kind::Object; return out; }

void only_fields(const Json& value, std::initializer_list<const char*> allowed,
                 const char* path) {
  if (value.kind != Json::Kind::Object)
    throw std::invalid_argument(std::string(path) + " must be an object");
  for (const auto& entry : value.object_v) {
    bool known = false;
    for (const char* name : allowed) known = known || entry.first == name;
    if (!known)
      throw std::invalid_argument(std::string(path) + " has unknown field: " +
                                  entry.first);
  }
}

const Json& field(const Json& value, const char* name) {
  if (value.kind != Json::Kind::Object)
    throw std::invalid_argument("migration document must be a JSON object");
  const auto found = value.object_v.find(name);
  if (found == value.object_v.end())
    throw std::invalid_argument(std::string("migration document missing field: ") + name);
  return found->second;
}

int integer(const Json& value, const char* path) {
  if (value.kind != Json::Kind::Number || !std::isfinite(value.number_v) ||
      std::trunc(value.number_v) != value.number_v ||
      value.number_v < std::numeric_limits<int>::min() ||
      value.number_v > std::numeric_limits<int>::max())
    throw std::invalid_argument(std::string(path) + " must be a signed 32-bit integer");
  return static_cast<int>(value.number_v);
}

std::int64_t exact_integer(const Json& value, const char* path) {
  if (value.kind == Json::Kind::String) {
    std::int64_t result = 0;
    const auto decoded = std::from_chars(
        value.string_v.data(), value.string_v.data() + value.string_v.size(),
        result);
    if (decoded.ec != std::errc{} ||
        decoded.ptr != value.string_v.data() + value.string_v.size() ||
        std::to_string(result) != value.string_v)
      throw std::invalid_argument(
          std::string(path) +
          " must be a canonical signed 64-bit decimal string or a safe integral JSON number");
    return result;
  }
  constexpr double kMaxSafeJsonInteger = 9007199254740991.0;
  if (value.kind != Json::Kind::Number || !std::isfinite(value.number_v) ||
      std::trunc(value.number_v) != value.number_v ||
      value.number_v < -kMaxSafeJsonInteger ||
      value.number_v > kMaxSafeJsonInteger)
    throw std::invalid_argument(
        std::string(path) +
        " must be a canonical signed 64-bit decimal string or a safe integral JSON number");
  return static_cast<std::int64_t>(value.number_v);
}

std::size_t index(const Json& value, const char* path) {
  const int result = integer(value, path);
  if (result < 0) throw std::invalid_argument(std::string(path) + " must be non-negative");
  return static_cast<std::size_t>(result);
}

std::uint32_t positive_limit(const Json& value, const char* path,
                             std::uint32_t maximum) {
  const auto decoded = index(value, path);
  if (decoded == 0 || decoded > maximum)
    throw std::invalid_argument(std::string(path) +
                                " must be a positive integer within its supported limit");
  return static_cast<std::uint32_t>(decoded);
}

bool boolean(const Json& value, const char* path) {
  if (value.kind != Json::Kind::Bool)
    throw std::invalid_argument(std::string(path) + " must be boolean");
  return value.bool_v;
}

std::string text(const Json& value, const char* path) {
  if (value.kind != Json::Kind::String)
    throw std::invalid_argument(std::string(path) + " must be a string");
  return value.string_v;
}

const std::vector<Json>& elements(const Json& value, const char* path) {
  if (value.kind != Json::Kind::Array)
    throw std::invalid_argument(std::string(path) + " must be an array");
  return value.array_v;
}

Value legacy_typed_value(const Json& value, const char* path) {
  only_fields(value, {"type", "value"}, path);
  const std::string type = text(field(value, "type"),
                                (std::string(path) + ".type").c_str());
  const Json& raw = field(value, "value");
  if (type == "int") return Value::from_int(exact_integer(raw, path));
  if (type == "float") {
    if (raw.kind != Json::Kind::Number || !std::isfinite(raw.number_v))
      throw std::invalid_argument(std::string(path) + " float value must be finite");
    return Value::from_float(raw.number_v);
  }
  if (type == "bool") return Value::from_bool(boolean(raw, path));
  if (type == "char") {
    const auto decoded = text(raw, path);
    if (decoded.size() != 1)
      throw std::invalid_argument(std::string(path) + " char value must contain one byte");
    return Value::from_char(static_cast<unsigned char>(decoded.front()));
  }
  if (type == "string") return payload::make_string_value(text(raw, path));
  if (type == "int_list") {
    std::vector<Value> result;
    for (const auto& item : elements(raw, path))
      result.push_back(Value::from_int(exact_integer(item, path)));
    return payload::make_int_list_value(result);
  }
  if (type == "float_list") {
    std::vector<Value> result;
    for (const auto& item : elements(raw, path)) {
      if (item.kind != Json::Kind::Number || !std::isfinite(item.number_v))
        throw std::invalid_argument(std::string(path) + " float_list elements must be finite numbers");
      result.push_back(Value::from_float(item.number_v));
    }
    return payload::make_float_list_value(result);
  }
  if (type == "string_list") {
    std::vector<Value> result;
    for (const auto& item : elements(raw, path))
      result.push_back(payload::make_string_value(text(item, path)));
    return payload::make_string_list_value(result);
  }
  if (type == "num_list" || type == "list")
    throw std::invalid_argument(std::string(path) + " legacy typed value '" + type +
        "' is ambiguous; materialize it with an exact int_list, float_list or string_list tag");
  throw std::invalid_argument(std::string(path) + " has unknown legacy typed value type '" + type + "'");
}

template <class Row, class Decode>
std::vector<Row> rows(const Json& ast, const char* name, Decode decode) {
  const auto found = ast.object_v.find(name);
  if (found == ast.object_v.end()) return {};
  std::vector<Row> result;
  for (const auto& value : elements(found->second, name)) {
    if (value.kind != Json::Kind::Object)
      throw std::invalid_argument(std::string(name) + " entries must be objects");
    result.push_back(decode(value));
  }
  return result;
}

legacy_v1::NodeKind legacy_kind(const Json& value, const char* path) {
  const int raw = integer(value, path);
  if (raw < 0 || raw >= static_cast<int>(legacy_v1::NodeKind::COUNT))
    throw std::invalid_argument(std::string(path) + " is not a release-1 node kind");
  return static_cast<legacy_v1::NodeKind>(raw);
}

Json parse(const std::string& source, const char* label) {
  if (source.size() > 256U * 1024U * 1024U)
    throw std::invalid_argument(std::string(label) + " exceeds 256 MiB");
  try { return cli_detail::JsonParser(source, {true, 512}).parse(); }
  catch (const std::exception& error) {
    throw std::invalid_argument(std::string("malformed ") + label + ": " + error.what());
  }
}

std::vector<evo::EvalCase> cases(const std::string& source) {
  if (source.empty())
    throw std::invalid_argument("this migration input requires --cases");
  try { return cli_detail::decode_fitness_cases_json(parse(source, "fitness cases")); }
  catch (const std::exception& error) {
    throw std::invalid_argument(std::string("invalid --cases artifact: ") + error.what());
  }
}

evo::CaseSet case_schema(const std::string& source) {
  const auto prepared = evo::prepare_case_set(cases(source));
  if (prepared.expected_return_type == evo::RType::Invalid)
    throw std::invalid_argument("--cases must have one exact expected return type");
  for (const auto& input : prepared.input_specs)
    if (input.type == evo::RType::Any || input.type == evo::RType::Invalid)
      throw std::invalid_argument("--cases must have one exact type for every input");
  return prepared;
}

Json input_json(const std::vector<evo::InputSpec>& inputs) {
  Json result = array();
  for (const auto& input : inputs) {
    Json one = object();
    one.object_v["name"] = string(input.name);
    one.object_v["type"] = string(std::string(grammar::type_name(input.type)));
    result.array_v.push_back(std::move(one));
  }
  return result;
}

std::vector<evo::InputSpec> embedded_inputs(const Json& value) {
  std::vector<evo::InputSpec> result;
  std::set<std::string> names;
  for (const auto& input : elements(value, "inputs")) {
    only_fields(input, {"name", "type"}, "inputs entry");
    const auto name = text(field(input, "name"), "inputs.name");
    if (name.empty() || !names.insert(name).second)
      throw std::invalid_argument("inputs must have distinct non-empty names");
    const auto type = grammar::parse_type(text(field(input, "type"), "inputs.type"));
    if (type == evo::RType::Any || type == evo::RType::Invalid)
      throw std::invalid_argument("inputs must use exact value types");
    result.push_back({name, type});
  }
  return result;
}

std::uint32_t prefix_depth(const evo::AstProgram& ast) {
  struct Pending { int remaining; std::uint32_t depth; };
  std::vector<Pending> stack{{1, 0}};
  std::uint32_t maximum = 0;
  for (const auto& node : ast.nodes) {
    while (!stack.empty() && stack.back().remaining == 0) stack.pop_back();
    if (stack.empty()) throw std::invalid_argument("migrated AST has trailing prefix nodes");
    --stack.back().remaining;
    const auto depth = stack.back().depth + 1;
    maximum = std::max(maximum, depth);
    const int arity = evo::node_prefix_arity(node);
    if (arity > 0) stack.push_back({arity, depth});
  }
  while (!stack.empty() && stack.back().remaining == 0) stack.pop_back();
  if (!stack.empty()) throw std::invalid_argument("migrated AST has missing prefix children");
  return maximum;
}

Json materialized(const evo::ProgramGenome& genome,
                  const std::vector<evo::InputSpec>& inputs,
                  evo::RType return_type,
                  std::uint32_t fuel, std::uint32_t max_nodes,
                  std::uint32_t max_depth, const Json& source,
                  const char* source_version) {
  Json root = object();
  root.object_v["format_version"] = string(kMaterializedVersion);
  root.object_v["semantic_version"] = string(grammar::kGrammarSemanticVersion);
  Json identity = object();
  identity.object_v["format_version"] = string(source_version);
  identity.object_v["content_sha256"] = string(
      grammar::content_sha256(grammar::canonical_json(source)));
  root.object_v["source_identity"] = std::move(identity);
  root.object_v["inputs"] = input_json(inputs);
  root.object_v["return_type"] = string(
      std::string(grammar::type_name(return_type)));
  Json search = object();
  search.object_v["max_nodes"] = number(max_nodes);
  search.object_v["max_depth"] = number(max_depth);
  root.object_v["search_limits"] = std::move(search);
  Json execution = object(); execution.object_v["fuel"] = number(fuel);
  root.object_v["execution_limits"] = std::move(execution);
  auto shape = genome.ast;
  shape.consts.clear();
  root.object_v["ast"] = cli_detail::JsonParser(
      cli_detail::encode_ast_json(shape), {true, 512}).parse();
  Json constants = array();
  for (const auto& value : genome.ast.consts)
    constants.array_v.push_back(grammar::encode_constant(value));
  root.object_v["constants"] = std::move(constants);
  return root;
}


void exact_version(const Json& root, const char* field_name,
                   const char* expected) {
  const auto actual = text(field(root, field_name), field_name);
  if (actual != expected)
    throw std::invalid_argument(std::string("grammar-generated-v1 ") +
                                field_name + " must be " + expected);
}

void sha256(const Json& value, const char* path) {
  const auto encoded = text(value, path);
  if (encoded.size() != 64)
    throw std::invalid_argument(std::string(path) +
                                " must contain 64 lowercase hexadecimal digits");
  for (const unsigned char c : encoded)
    if (!std::isdigit(c) && !(c >= 'a' && c <= 'f'))
      throw std::invalid_argument(std::string(path) +
                                  " must contain 64 lowercase hexadecimal digits");
}

void uint32_value(const Json& value, const char* path) {
  if (value.kind != Json::Kind::Number || !std::isfinite(value.number_v) ||
      std::trunc(value.number_v) != value.number_v || value.number_v < 0 ||
      value.number_v > 4294967295.0)
    throw std::invalid_argument(std::string(path) +
                                " must be an unsigned 32-bit integer");
}

void derivation_rows(const Json& value, const char* name, std::size_t width) {
  std::size_t row_number = 0;
  for (const auto& row : elements(field(value, name), name)) {
    const auto& columns = elements(row, (std::string("derivation.") + name).c_str());
    if (columns.size() != width)
      throw std::invalid_argument(std::string("derivation.") + name +
                                  " rows must have exactly " +
                                  std::to_string(width) + " columns");
    for (std::size_t column = 0; column < columns.size(); ++column)
      uint32_value(columns[column],
                   (std::string("derivation.") + name + "[" +
                    std::to_string(row_number) + "][" +
                    std::to_string(column) + "]").c_str());
    ++row_number;
  }
}

void validate_generated_provenance(const Json& root) {
  exact_version(root, "semantic_version", "gagp-native-1.0.0");
  exact_version(root, "generator_version", "typed-derivation-v1");
  exact_version(root, "rng_version", "splitmix64-rejection-v1");
  exact_version(root, "payload_seeding", "domain-only-v1");
  sha256(field(root, "grammar_hash"), "grammar_hash");
  sha256(field(root, "input_schema_hash"), "input_schema_hash");
  const auto& grammar_value = field(root, "grammar");
  if (grammar_value.kind != Json::Kind::Object)
    throw std::invalid_argument("grammar must be an object");
  if (text(field(root, "grammar_hash"), "grammar_hash") !=
      grammar::content_sha256(grammar::canonical_json(grammar_value)))
    throw std::invalid_argument("grammar-generated-v1 grammar_hash mismatch");
  const auto& input_value = field(root, "inputs");
  if (text(field(root, "input_schema_hash"), "input_schema_hash") !=
      grammar::content_sha256(grammar::canonical_json(input_value)))
    throw std::invalid_argument("grammar-generated-v1 input_schema_hash mismatch");

  const auto seed = text(field(root, "seed"), "seed");
  std::uint64_t ignored = 0;
  const auto decoded = std::from_chars(seed.data(), seed.data() + seed.size(), ignored);
  if (seed.empty() || decoded.ec != std::errc{} ||
      decoded.ptr != seed.data() + seed.size() || std::to_string(ignored) != seed)
    throw std::invalid_argument(
        "grammar-generated-v1 seed must be a canonical unsigned 64-bit decimal string");

  const auto& request = field(root, "request");
  only_fields(request, {"nonterminal", "type", "visible_environment",
                        "scope_mapping"}, "request");
  uint32_value(field(request, "nonterminal"), "request.nonterminal");
  const auto request_type = grammar::parse_type(
      text(field(request, "type"), "request.type"));
  if (request_type == evo::RType::Any || request_type == evo::RType::Invalid)
    throw std::invalid_argument("request.type must be an exact value type");
  for (const auto& binding : elements(field(request, "visible_environment"),
                                       "request.visible_environment")) {
    only_fields(binding, {"name", "type"}, "request.visible_environment entry");
    (void)text(field(binding, "name"), "request.visible_environment.name");
    const auto type = grammar::parse_type(
        text(field(binding, "type"), "request.visible_environment.type"));
    if (type == evo::RType::Any || type == evo::RType::Invalid)
      throw std::invalid_argument(
          "request.visible_environment.type must be an exact value type");
  }
  for (const auto& mapping : elements(field(request, "scope_mapping"),
                                       "request.scope_mapping"))
    uint32_value(mapping, "request.scope_mapping");

  const auto& derivation = field(root, "derivation");
  only_fields(derivation,
      {"logical_steps", "derived_nodes", "lowered_instructions", "nodes",
       "choices", "templates", "holes"}, "derivation");
  for (const char* name : {"logical_steps", "derived_nodes",
                           "lowered_instructions"})
    uint32_value(field(derivation, name),
                 (std::string("derivation.") + name).c_str());
  derivation_rows(derivation, "nodes", 7);
  derivation_rows(derivation, "choices", 5);
  derivation_rows(derivation, "templates", 2);
  derivation_rows(derivation, "holes", 4);
}

void validate_materialized(const std::string& encoded) {
  const Json root = parse(encoded, "materialized migration output");
  if (text(field(root, "format_version"), "format_version") != kMaterializedVersion)
    throw std::logic_error("migration output format changed during canonical decode");
  if (text(field(root, "semantic_version"), "semantic_version") !=
      grammar::kGrammarSemanticVersion)
    throw std::logic_error("migration output semantic version changed during canonical decode");
  auto ast = cli_detail::decode_ast_json(field(root, "ast"));
  if (!ast.consts.empty())
    throw std::logic_error(
        "canonical migration output contains duplicate constant pools");
  for (const auto& value : elements(field(root, "constants"), "constants"))
    ast.consts.push_back(grammar::decode_constant(value));
  std::vector<evo::InputSpec> inputs;
  for (const auto& one : elements(field(root, "inputs"), "inputs"))
    inputs.push_back({text(field(one, "name"), "inputs.name"),
        grammar::parse_type(text(field(one, "type"), "inputs.type"))});
  const auto verified = evo::verify_ast(ast, inputs);
  if (!verified)
    throw std::logic_error("canonical migration output failed AST verification: " +
                           verified.diagnostic.message);
  if (verified.verified.return_type !=
      grammar::parse_type(text(field(root, "return_type"), "return_type")))
    throw std::logic_error("canonical migration output return type changed");
  evo::ProgramGenome genome{ast, evo::build_genome_meta(ast)};
  const auto bytecode = evo::compile_for_eval(genome, verified.verified);
  const auto checked = verify_bytecode(bytecode);
  if (!checked)
    throw std::logic_error("canonical migration output failed bytecode verification: " +
                           checked.diagnostic.message);
}

bool config_bool(const Json& section, const char* section_name,
                 const char* name) {
  const auto found = section.object_v.find(name);
  if (found == section.object_v.end())
    throw std::invalid_argument(std::string("legacy grammar-config missing ") +
                                section_name + "." + name);
  return boolean(found->second,
                 (std::string(section_name) + "." + name).c_str());
}

struct LegacyConfig {
  LegacyGrammarConfig config;
  std::uint32_t max_nodes = 0;
  std::uint32_t max_depth = 0;
  std::uint32_t max_statements = 0;
  std::int64_t max_for_k = 0;
};

LegacyConfig decode_config(const Json& root) {
  only_fields(root,
      {"format_version", "profile", "statements", "expressions", "builtins",
       "values", "limits", "structured", "asgp", "compat"},
      "legacy grammar-config");
  const Json& statements = field(root, "statements");
  const Json& expressions = field(root, "expressions");
  const Json& builtins = field(root, "builtins");
  const Json& values = field(root, "values");
  const Json& limits = field(root, "limits");
  (void)text(field(root, "profile"), "profile");
  only_fields(statements, {"assign", "if_stmt", "for_range", "return"},
              "legacy grammar-config statements");
  only_fields(expressions,
      {"const", "var", "bound_var", "unary", "binary", "if_expr", "call",
       "map_list", "filter_list", "linear_rec", "asgp_dc", "asgp_dp1d",
       "asgp_dp2d"}, "legacy grammar-config expressions");
  only_fields(builtins,
      {"abs", "min", "max", "clip", "idiv0", "imod0", "len", "concat",
       "slice", "index", "append", "prepend", "reverse", "find",
       "contains", "singleton", "char_to_string", "string_to_char", "ord",
       "chr", "is_letter", "is_digit", "is_space", "is_vowel", "to_lower",
       "to_upper", "to_string"}, "legacy grammar-config builtins");
  only_fields(values,
      {"int", "float", "bool", "char", "string", "int_list", "float_list",
       "string_list"}, "legacy grammar-config values");
  only_fields(limits,
      {"max_stmts_per_block", "max_call_args", "max_for_k", "max_expr_depth",
       "max_total_nodes"}, "legacy grammar-config limits");
  (void)index(field(limits, "max_call_args"), "limits.max_call_args");

  const Json& structured = field(root, "structured");
  only_fields(structured,
      {"max_nested_binders", "max_map_body_depth", "max_filter_pred_depth",
       "max_linear_rec_body_depth"}, "legacy grammar-config structured");
  for (const char* name : {"max_nested_binders", "max_map_body_depth",
                           "max_filter_pred_depth", "max_linear_rec_body_depth"})
    (void)index(field(structured, name),
                (std::string("structured.") + name).c_str());

  const Json& asgp = field(root, "asgp");
  only_fields(asgp, {"max_scheme_nesting", "dc", "dp1d", "dp2d"},
              "legacy grammar-config asgp");
  (void)index(field(asgp, "max_scheme_nesting"), "asgp.max_scheme_nesting");
  const Json& dc = field(asgp, "dc");
  only_fields(dc, {"enabled_source_elems", "max_depth"},
              "legacy grammar-config asgp.dc");
  for (const auto& item : elements(field(dc, "enabled_source_elems"),
                                    "asgp.dc.enabled_source_elems"))
    (void)text(item, "asgp.dc.enabled_source_elems");
  (void)index(field(dc, "max_depth"), "asgp.dc.max_depth");
  const Json& dp1d = field(asgp, "dp1d");
  only_fields(dp1d, {"dependency_patterns", "max_states", "max_step"},
              "legacy grammar-config asgp.dp1d");
  for (const auto& item : elements(field(dp1d, "dependency_patterns"),
                                    "asgp.dp1d.dependency_patterns"))
    (void)text(item, "asgp.dp1d.dependency_patterns");
  (void)index(field(dp1d, "max_states"), "asgp.dp1d.max_states");
  (void)index(field(dp1d, "max_step"), "asgp.dp1d.max_step");
  const Json& dp2d = field(asgp, "dp2d");
  only_fields(dp2d, {"dependency_patterns", "max_cells"},
              "legacy grammar-config asgp.dp2d");
  for (const auto& item : elements(field(dp2d, "dependency_patterns"),
                                    "asgp.dp2d.dependency_patterns"))
    (void)text(item, "asgp.dp2d.dependency_patterns");
  (void)index(field(dp2d, "max_cells"), "asgp.dp2d.max_cells");
  if (field(root, "compat").kind != Json::Kind::Null)
    throw std::invalid_argument(
        "legacy grammar-config compat metadata is unsupported; migrate the original content-defined preset");
  for (const char* name : {"map_list", "filter_list", "linear_rec", "asgp_dc",
                           "asgp_dp1d", "asgp_dp2d"})
    if (config_bool(expressions, "expressions", name))
      throw std::invalid_argument(std::string("unsupported legacy grammar-config field expressions.") +
          name + "; migrate an explicit materialized AST or author the corresponding general package");
  LegacyConfig out;
  auto& c = out.config;
  c.statement_assign = config_bool(statements, "statements", "assign");
  c.statement_if_stmt = config_bool(statements, "statements", "if_stmt");
  c.statement_for_range = config_bool(statements, "statements", "for_range");
  c.statement_return = config_bool(statements, "statements", "return");
  c.expression_const = config_bool(expressions, "expressions", "const");
  c.expression_var = config_bool(expressions, "expressions", "var");
  c.expression_if_expr = config_bool(expressions, "expressions", "if_expr");
  const bool unary = config_bool(expressions, "expressions", "unary");
  c.unary_neg = unary; c.unary_not = unary;
  const bool binary = config_bool(expressions, "expressions", "binary");
  c.binary_add = binary; c.binary_sub = binary; c.binary_mul = binary;
  c.binary_div = binary; c.binary_mod = binary; c.binary_lt = binary;
  c.binary_le = binary; c.binary_gt = binary; c.binary_ge = binary;
  c.binary_eq = binary; c.binary_ne = binary; c.binary_and = binary;
  c.binary_or = binary;
  const bool call = config_bool(expressions, "expressions", "call");
#define GAGP_MIGRATE_BUILTIN(member, key) c.member = call && config_bool(builtins, "builtins", key)
  GAGP_MIGRATE_BUILTIN(builtin_abs, "abs"); GAGP_MIGRATE_BUILTIN(builtin_min, "min");
  GAGP_MIGRATE_BUILTIN(builtin_max, "max"); GAGP_MIGRATE_BUILTIN(builtin_clip, "clip");
  GAGP_MIGRATE_BUILTIN(builtin_idiv0, "idiv0"); GAGP_MIGRATE_BUILTIN(builtin_imod0, "imod0");
  GAGP_MIGRATE_BUILTIN(builtin_len, "len"); GAGP_MIGRATE_BUILTIN(builtin_concat, "concat");
  GAGP_MIGRATE_BUILTIN(builtin_slice, "slice"); GAGP_MIGRATE_BUILTIN(builtin_index, "index");
  GAGP_MIGRATE_BUILTIN(builtin_append, "append"); GAGP_MIGRATE_BUILTIN(builtin_prepend, "prepend");
  GAGP_MIGRATE_BUILTIN(builtin_reverse, "reverse"); GAGP_MIGRATE_BUILTIN(builtin_find, "find");
  GAGP_MIGRATE_BUILTIN(builtin_contains, "contains"); GAGP_MIGRATE_BUILTIN(builtin_singleton, "singleton");
  GAGP_MIGRATE_BUILTIN(builtin_char_to_string, "char_to_string");
  GAGP_MIGRATE_BUILTIN(builtin_string_to_char, "string_to_char");
  GAGP_MIGRATE_BUILTIN(builtin_ord, "ord"); GAGP_MIGRATE_BUILTIN(builtin_chr, "chr");
  GAGP_MIGRATE_BUILTIN(builtin_is_letter, "is_letter"); GAGP_MIGRATE_BUILTIN(builtin_is_digit, "is_digit");
  GAGP_MIGRATE_BUILTIN(builtin_is_space, "is_space"); GAGP_MIGRATE_BUILTIN(builtin_is_vowel, "is_vowel");
  GAGP_MIGRATE_BUILTIN(builtin_to_lower, "to_lower"); GAGP_MIGRATE_BUILTIN(builtin_to_upper, "to_upper");
  GAGP_MIGRATE_BUILTIN(builtin_to_string, "to_string");
#undef GAGP_MIGRATE_BUILTIN
#define GAGP_MIGRATE_VALUE(member, key) c.member = config_bool(values, "values", key)
  GAGP_MIGRATE_VALUE(value_int, "int"); GAGP_MIGRATE_VALUE(value_float, "float");
  GAGP_MIGRATE_VALUE(value_bool, "bool"); GAGP_MIGRATE_VALUE(value_char, "char");
  GAGP_MIGRATE_VALUE(value_string, "string"); GAGP_MIGRATE_VALUE(value_int_list, "int_list");
  GAGP_MIGRATE_VALUE(value_float_list, "float_list"); GAGP_MIGRATE_VALUE(value_string_list, "string_list");
#undef GAGP_MIGRATE_VALUE
  out.max_nodes = static_cast<std::uint32_t>(index(field(limits, "max_total_nodes"), "limits.max_total_nodes"));
  out.max_depth = static_cast<std::uint32_t>(index(field(limits, "max_expr_depth"), "limits.max_expr_depth"));
  out.max_statements = static_cast<std::uint32_t>(index(field(limits, "max_stmts_per_block"), "limits.max_stmts_per_block"));
  out.max_for_k = integer(field(limits, "max_for_k"), "limits.max_for_k");
  if (!out.max_nodes || !out.max_depth || !out.max_statements)
    throw std::invalid_argument("legacy grammar-config limits must be positive");
  c.validate();
  return out;
}

grammar::ConstantDomain defaults(evo::RType type) {
  grammar::ConstantDomain out; out.type = type;
  switch (type) {
    case evo::RType::Int: out.integer_range = true; out.minimum = -1; out.maximum = 1; break;
    case evo::RType::Float: out.values = {0.0, 1.0}; break;
    case evo::RType::Bool: out.values = {false, true}; break;
    case evo::RType::Char: out.values = {char32_t{'a'}}; break;
    case evo::RType::String: out.values = {std::string{}}; break;
    case evo::RType::IntList: out.values = {std::vector<std::int64_t>{}}; break;
    case evo::RType::FloatList: out.values = {std::vector<double>{}}; break;
    case evo::RType::StringList: out.values = {std::vector<std::string>{}}; break;
    default: throw std::logic_error("migration default requires an exact value type");
  }
  return out;
}

std::string migrate_config(const Json& root, const std::string& cases_text,
                           const std::string& conversion_profile,
                           bool explicit_limits) {
  if (explicit_limits)
    throw std::invalid_argument("grammar-config migration uses embedded limits; do not pass AST limit options");
  if (conversion_profile != "constrained-intent-v1")
    throw std::invalid_argument(
        "grammar-config migration requires --conversion-profile constrained-intent-v1; "
        "release-1 configs do not contain exact constant domains, execution fuel, "
        "or a typed assignment environment");
  const auto legacy = decode_config(root);
  const auto schema = case_schema(cases_text);
  LegacyGrammarConfigConversion conversion;
  conversion.return_type = schema.expected_return_type;
  conversion.inputs = schema.input_specs;
  conversion.search_limits = {legacy.max_nodes, legacy.max_depth};
  conversion.execution_limits = {1000000};
  conversion.max_statements_per_block = legacy.max_statements;
  conversion.max_for_k = legacy.max_for_k;
  if (legacy.config.statement_for_range) {
    std::set<std::string> names;
    for (const auto& input : conversion.inputs) names.insert(input.name);
    std::string name = "migration_loop";
    while (names.count(name)) name += "_";
    conversion.locals.push_back({name, evo::RType::Int});
  }
  for (const auto type : {evo::RType::Int, evo::RType::Float, evo::RType::Bool,
                          evo::RType::Char, evo::RType::String, evo::RType::IntList,
                          evo::RType::FloatList, evo::RType::StringList})
    if (legacy.config.allows_type(type)) conversion.constants.push_back(defaults(type));
  return convert_legacy_grammar_config(legacy.config, conversion).canonical;
}

}  // namespace

legacy_v1::AstProgram decode_legacy_ast_artifact(const Json& document) {
  if (text(field(document, "format_version"), "format_version") != "ast-prefix")
    throw std::invalid_argument("legacy AST migration requires format_version=ast-prefix");
  const auto wrapped = document.object_v.find("ast");
  const Json& ast = wrapped == document.object_v.end() ? document : wrapped->second;
  if (ast.kind != Json::Kind::Object)
    throw std::invalid_argument("legacy AST artifact ast field must be an object");
  if (wrapped == document.object_v.end()) {
    only_fields(document,
        {"format_version", "version", "nodes", "names", "consts",
         "linear_rec_binders", "asgp_dc_binders", "asgp_dp1d_specs",
         "asgp_dp2d_specs", "lexical_regions", "traversal_specs",
         "fuel_specs", "bounded_region_specs"}, "legacy AST artifact");
  } else {
    only_fields(document, {"format_version", "ast"}, "legacy AST artifact");
    only_fields(ast,
        {"version", "nodes", "names", "consts", "linear_rec_binders",
         "asgp_dc_binders", "asgp_dp1d_specs", "asgp_dp2d_specs",
         "lexical_regions", "traversal_specs", "fuel_specs",
         "bounded_region_specs"}, "legacy AST shape");
  }
  legacy_v1::AstProgram out;
  const auto version = ast.object_v.find("version");
  out.version = version == ast.object_v.end() ? "ast-prefix" : text(version->second, "ast.version");
  for (const auto& row : elements(field(ast, "nodes"), "ast.nodes")) {
    if (row.kind != Json::Kind::Object) throw std::invalid_argument("ast.nodes entries must be objects");
    only_fields(row, {"kind", "i0", "i1"}, "ast.nodes entry");
    out.nodes.push_back({legacy_kind(field(row, "kind"), "ast.nodes.kind"),
                         integer(field(row, "i0"), "ast.nodes.i0"),
                         integer(field(row, "i1"), "ast.nodes.i1")});
  }
  for (const auto& name : elements(field(ast, "names"), "ast.names"))
    out.names.push_back(text(name, "ast.names"));
  for (const auto& value : elements(field(ast, "consts"), "ast.consts"))
    out.consts.push_back(legacy_typed_value(value, "ast.consts"));
  out.linear_rec_binders = rows<legacy_v1::LinearRecBinders>(ast, "linear_rec_binders", [](const Json& row) {
    only_fields(row, {"node_index", "elem_name", "accum_name", "index_name"},
                "linear_rec_binders entry");
    return legacy_v1::LinearRecBinders{index(field(row, "node_index"), "linear_rec_binders.node_index"),
        integer(field(row, "elem_name"), "linear_rec_binders.elem_name"),
        integer(field(row, "accum_name"), "linear_rec_binders.accum_name"),
        integer(field(row, "index_name"), "linear_rec_binders.index_name")};
  });
  out.asgp_dc_binders = rows<legacy_v1::AsgpDcBinders>(ast, "asgp_dc_binders", [](const Json& row) {
    only_fields(row,
        {"node_index", "solve_xs_name", "solve_n_name", "solve_lo_name",
         "divide_n_name", "combine_left_name", "combine_right_name"},
        "asgp_dc_binders entry");
    return legacy_v1::AsgpDcBinders{index(field(row, "node_index"), "asgp_dc_binders.node_index"),
        integer(field(row, "solve_xs_name"), "asgp_dc_binders.solve_xs_name"),
        integer(field(row, "solve_n_name"), "asgp_dc_binders.solve_n_name"),
        integer(field(row, "solve_lo_name"), "asgp_dc_binders.solve_lo_name"),
        integer(field(row, "divide_n_name"), "asgp_dc_binders.divide_n_name"),
        integer(field(row, "combine_left_name"), "asgp_dc_binders.combine_left_name"),
        integer(field(row, "combine_right_name"), "asgp_dc_binders.combine_right_name")};
  });
  out.asgp_dp1d_specs = rows<legacy_v1::AsgpDp1dSpec>(ast, "asgp_dp1d_specs", [](const Json& row) {
    only_fields(row,
        {"node_index", "lo", "hi", "base_state", "boundary_const",
         "dep_kind", "dep_offsets", "solve_state_name",
         "transition_state_name", "transition_dep_names"},
        "asgp_dp1d_specs entry");
    legacy_v1::AsgpDp1dSpec value;
    value.node_index = index(field(row, "node_index"), "asgp_dp1d_specs.node_index");
    value.lo = integer(field(row, "lo"), "asgp_dp1d_specs.lo"); value.hi = integer(field(row, "hi"), "asgp_dp1d_specs.hi");
    value.base_state = integer(field(row, "base_state"), "asgp_dp1d_specs.base_state");
    value.boundary_const = integer(field(row, "boundary_const"), "asgp_dp1d_specs.boundary_const");
    value.dep_kind = legacy_kind(field(row, "dep_kind"), "asgp_dp1d_specs.dep_kind");
    for (const auto& item : elements(field(row, "dep_offsets"), "asgp_dp1d_specs.dep_offsets")) value.dep_offsets.push_back(integer(item, "dep_offsets"));
    value.solve_state_name = integer(field(row, "solve_state_name"), "asgp_dp1d_specs.solve_state_name");
    value.transition_state_name = integer(field(row, "transition_state_name"), "asgp_dp1d_specs.transition_state_name");
    for (const auto& item : elements(field(row, "transition_dep_names"), "asgp_dp1d_specs.transition_dep_names")) value.transition_dep_names.push_back(integer(item, "transition_dep_names"));
    return value;
  });
  out.asgp_dp2d_specs = rows<legacy_v1::AsgpDp2dSpec>(ast, "asgp_dp2d_specs", [](const Json& row) {
    only_fields(row,
        {"node_index", "i_lo", "i_hi", "j_lo", "j_hi", "base_i",
         "base_j", "boundary_const", "dep_kind", "solve_i_name",
         "solve_j_name", "transition_i_name", "transition_j_name",
         "transition_dep_names"}, "asgp_dp2d_specs entry");
    legacy_v1::AsgpDp2dSpec value;
    value.node_index = index(field(row, "node_index"), "asgp_dp2d_specs.node_index");
    value.i_lo = integer(field(row, "i_lo"), "asgp_dp2d_specs.i_lo"); value.i_hi = integer(field(row, "i_hi"), "asgp_dp2d_specs.i_hi");
    value.j_lo = integer(field(row, "j_lo"), "asgp_dp2d_specs.j_lo"); value.j_hi = integer(field(row, "j_hi"), "asgp_dp2d_specs.j_hi");
    value.base_i = integer(field(row, "base_i"), "asgp_dp2d_specs.base_i"); value.base_j = integer(field(row, "base_j"), "asgp_dp2d_specs.base_j");
    value.boundary_const = integer(field(row, "boundary_const"), "asgp_dp2d_specs.boundary_const");
    value.dep_kind = legacy_kind(field(row, "dep_kind"), "asgp_dp2d_specs.dep_kind");
    value.solve_i_name = integer(field(row, "solve_i_name"), "asgp_dp2d_specs.solve_i_name"); value.solve_j_name = integer(field(row, "solve_j_name"), "asgp_dp2d_specs.solve_j_name");
    value.transition_i_name = integer(field(row, "transition_i_name"), "asgp_dp2d_specs.transition_i_name"); value.transition_j_name = integer(field(row, "transition_j_name"), "asgp_dp2d_specs.transition_j_name");
    for (const auto& item : elements(field(row, "transition_dep_names"), "asgp_dp2d_specs.transition_dep_names")) value.transition_dep_names.push_back(integer(item, "transition_dep_names"));
    return value;
  });

  // The retained general-region sidecars have the current schema. Decode them
  // through a sanitized shape; legacy node values are replaced only in this
  // temporary document and never enter the production AST decoder.
  Json shadow = ast;
  shadow.object_v["version"] = string(evo::k_ast_prefix_version_current);
  // Constants were already decoded by the isolated release-1 codec above.
  // Do not feed exact signed-64 string forms into the production AST codec.
  shadow.object_v["consts"] = array();
  for (const char* name : {"linear_rec_binders", "asgp_dc_binders", "asgp_dp1d_specs", "asgp_dp2d_specs", "format_version"})
    shadow.object_v.erase(name);
  for (auto& row : shadow.object_v.at("nodes").array_v) {
    const int kind = integer(field(row, "kind"), "ast.nodes.kind");
    if (kind >= static_cast<int>(legacy_v1::NodeKind::MAP_LIST) &&
        kind <= static_cast<int>(legacy_v1::NodeKind::DP2_NEIGHBORHOOD_FORWARD3))
      row.object_v["kind"] = number(static_cast<int>(evo::NodeKind::CONST));
  }
  const auto retained = cli_detail::decode_ast_json(shadow);
  out.lexical_regions = retained.lexical_regions;
  out.traversal_specs = retained.traversal_specs;
  out.fuel_specs = retained.fuel_specs;
  out.bounded_region_specs = retained.bounded_region_specs;
  return out;
}

namespace {

std::string migrate_materialized_ast(
    legacy_v1::AstProgram legacy, const std::vector<evo::InputSpec>& inputs,
    evo::RType return_type, std::uint32_t fuel, std::uint32_t max_nodes,
    std::uint32_t max_depth, const Json& source, const char* source_version) {
  auto genome = evo::transition::lower_bounded_regions(legacy, inputs);
  if (genome.ast.nodes.size() > max_nodes)
    throw std::invalid_argument("migrated AST exceeds its recorded max_nodes");
  if (prefix_depth(genome.ast) > max_depth)
    throw std::invalid_argument("migrated AST exceeds its recorded max_depth");
  const auto verified = evo::verify_ast(genome.ast, inputs);
  if (!verified)
    throw std::logic_error("migrated AST failed verification: " +
                           verified.diagnostic.message);
  if (verified.verified.return_type != return_type)
    throw std::invalid_argument(
        "migrated AST return type differs from its declared contract");
  const auto bytecode = evo::compile_for_eval(genome, verified.verified);
  const auto checked = verify_bytecode(bytecode);
  if (!checked)
    throw std::logic_error("migrated AST failed compilation verification: " +
                           checked.diagnostic.message);
  const auto encoded = grammar::canonical_json(materialized(
      genome, inputs, return_type, fuel, max_nodes, max_depth, source,
      source_version));
  validate_materialized(encoded);
  return encoded;
}

std::string migrate_generated(const Json& root, bool explicit_limits) {
  if (explicit_limits)
    throw std::invalid_argument(
        "grammar-generated-v1 migration uses embedded limits; do not pass AST limit options");
  only_fields(root,
      {"format_version", "semantic_version", "generator_version", "rng_version",
       "grammar_hash", "grammar", "inputs", "input_schema_hash", "return_type",
       "request", "seed", "payload_seeding", "search_limits",
       "execution_limits", "ast_shape", "constants", "derivation"},
      "grammar-generated-v1 artifact");
  validate_generated_provenance(root);

  const auto inputs = embedded_inputs(field(root, "inputs"));
  const auto return_type = grammar::parse_type(
      text(field(root, "return_type"), "return_type"));
  if (return_type == evo::RType::Any || return_type == evo::RType::Invalid)
    throw std::invalid_argument("return_type must be an exact value type");
  const auto request_type = grammar::parse_type(
      text(field(field(root, "request"), "type"), "request.type"));
  if (request_type != return_type)
    throw std::invalid_argument(
        "grammar-generated-v1 request.type differs from return_type");

  const auto& search = field(root, "search_limits");
  only_fields(search, {"max_nodes", "max_depth"}, "search_limits");
  const auto max_nodes = positive_limit(field(search, "max_nodes"),
                                        "search_limits.max_nodes", 65536);
  const auto max_depth = positive_limit(field(search, "max_depth"),
                                        "search_limits.max_depth", 256);
  const auto& execution = field(root, "execution_limits");
  only_fields(execution, {"fuel"}, "execution_limits");
  const auto fuel = positive_limit(field(execution, "fuel"),
                                   "execution_limits.fuel", 2147483647);

  Json wrapped = object();
  wrapped.object_v["format_version"] = string("ast-prefix");
  wrapped.object_v["ast"] = field(root, "ast_shape");
  auto legacy = decode_legacy_ast_artifact(wrapped);
  if (!legacy.consts.empty())
    throw std::invalid_argument(
        "grammar-generated-v1 ast_shape must contain an empty consts table");
  std::size_t constant_number = 0;
  for (const auto& constant : elements(field(root, "constants"), "constants")) {
    try {
      legacy.consts.push_back(grammar::decode_constant(constant));
    } catch (const std::exception& error) {
      throw std::invalid_argument(
          "grammar-generated-v1 constants[" +
          std::to_string(constant_number) + "]: " + error.what());
    }
    ++constant_number;
  }
  return migrate_materialized_ast(std::move(legacy), inputs, return_type, fuel,
                                  max_nodes, max_depth, root,
                                  "grammar-generated-v1");
}

}  // namespace

std::string migrate_artifact(const std::string& input_text,
                             const std::string& cases_text,
                             const std::string& conversion_profile,
                             std::uint32_t fuel,
                             std::uint32_t max_nodes,
                             std::uint32_t max_depth,
                             bool explicit_ast_limits) {
  const Json root = parse(input_text, "migration input");
  const std::string version = text(field(root, "format_version"), "format_version");
  if (version == "bytecode-json" || version == "bytecode-fixture" ||
      version == "migration-bytecode-v1")
    throw std::invalid_argument("legacy bytecode migration is unsupported because AST/type provenance is absent; migrate the source ast-prefix artifact and recompile");
  if (version == "population-seeds")
    throw std::invalid_argument("legacy population-seeds cannot be migrated exactly; materialize every seed with the frozen old build, then migrate those ast-prefix artifacts");
  if (version == "grammar-population-v1")
    throw std::invalid_argument("grammar-population-v1 cannot be migrated as one artifact; migrate every materialized member independently");
  if (version == "grammar-config")
    return migrate_config(root, cases_text, conversion_profile,
                          explicit_ast_limits);
  if (!conversion_profile.empty())
    throw std::invalid_argument(
        "--conversion-profile is valid only for grammar-config migration");
  if (version == "grammar-generated-v1")
    return migrate_generated(root, explicit_ast_limits);
  if (version != "ast-prefix")
    throw std::invalid_argument("unsupported migration format_version '" + version + "'");
  if (!explicit_ast_limits)
    throw std::invalid_argument("ast-prefix migration requires explicit --fuel, --max-nodes and --max-depth");
  const auto schema = case_schema(cases_text);
  const auto legacy = decode_legacy_ast_artifact(root);
  return migrate_materialized_ast(
      legacy, schema.input_specs, schema.expected_return_type, fuel, max_nodes,
      max_depth, root, "ast-prefix-v1");
}

}  // namespace gagp::migration
