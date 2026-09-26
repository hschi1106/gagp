#include "gagp/migration/legacy_grammar_config.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

#include "gagp/evolution/grammar/values.hpp"

namespace gagp::migration {
namespace {
using namespace evo;
using namespace evo::grammar;
using Json = cli_detail::JsonValue;
Json string(std::string value) { Json out; out.kind = Json::Kind::String; out.string_v = std::move(value); return out; }
Json number(double value) { Json out; out.kind = Json::Kind::Number; out.number_v = value; return out; }
Json array(std::vector<Json> values = {}) { Json out; out.kind = Json::Kind::Array; out.array_v = std::move(values); return out; }
Json object(std::map<std::string, Json> values) { Json out; out.kind = Json::Kind::Object; out.object_v = std::move(values); return out; }
std::string type(RType value) { return std::string(type_name(value)); }
std::string expr(RType value) { return "Gct1.Expr." + type(value); }
Json reference_expression(const std::string& name) { return object({{"ref", string(name)}}); }
Json alternative(std::string id, Json expression) {
  return object({{"id", string(std::move(id))}, {"weight", number(1)}, {"expression", std::move(expression)}});
}
Json domain_json(const ConstantDomain& domain, unsigned depth = 0) {
  if (domain.mutation != ConstantMutationPolicy::Resample &&
      domain.mutation != ConstantMutationPolicy::Keep && domain.mutation != ConstantMutationPolicy::Flip &&
      domain.mutation != ConstantMutationPolicy::Add)
    throw std::invalid_argument("conversion constant mutation policy is invalid");
  if (depth > 3) throw std::invalid_argument("conversion constant domain nesting exceeds capacity");
  Json out = object({{"type", string(type(domain.type))}});
  if (int(domain.integer_range) + int(domain.float_range) + int(bool(domain.elements)) +
      int(!domain.values.empty()) != 1)
    throw std::invalid_argument("conversion constant domain requires exactly one domain representation");
  if (domain.integer_range) {
    out.object_v["range"] = array({string(std::to_string(domain.minimum)), string(std::to_string(domain.maximum))});
  } else if (domain.float_range) {
    out.object_v["range"] = array({number(domain.float_minimum), number(domain.float_maximum)});
    if (domain.float_quantization_scale != 0)
      out.object_v["quantization_scale"] = number(domain.float_quantization_scale);
  } else if (domain.elements) {
    out.object_v["sequence"] = object({{"length", array({number(domain.minimum_length), number(domain.maximum_length)})},
        {"element", domain_json(*domain.elements,depth + 1)}});
  } else {
    Json values = array();
    for (const auto& value : domain.values) {
      try {
        auto singleton = encode_constant(materialize_constant(domain.type, value));
        values.array_v.push_back(singleton.object_v.at("values").array_v.at(0));
      } catch (const std::bad_variant_access&) {
        throw std::invalid_argument("conversion constant data does not match domain type " + type(domain.type));
      }
    }
    out.object_v["values"] = std::move(values);
  }
  if (domain.sampling) out.object_v["sample_from"] = domain_json(*domain.sampling,depth + 1);
  if (domain.mutation == ConstantMutationPolicy::Add) {
    Json mutation = object({{"kind",string("add")}});
    mutation.object_v["range"] = domain.type == RType::Int ?
        array({string(std::to_string(domain.delta.integer_minimum)),string(std::to_string(domain.delta.integer_maximum))}) :
        array({number(domain.delta.float_minimum),number(domain.delta.float_maximum)});
    if (domain.delta.gpu_grid_steps) mutation.object_v["gpu_grid_steps"] = number(domain.delta.gpu_grid_steps);
    out.object_v["mutation"] = std::move(mutation);
  } else if (domain.mutation != ConstantMutationPolicy::Resample)
    out.object_v["mutation"] = string(domain.mutation == ConstantMutationPolicy::Keep ? "keep" : "flip");
  (void)parse_constant_domain(out);
  return out;
}
}  // namespace

void LegacyGrammarConfig::validate() const {
  if (!statement_return || !expression_const)
    throw std::invalid_argument("legacy grammar-config requires statements.return and expressions.const");
  if (!value_int && !value_float && !value_bool && !value_char &&
      !value_string && !value_int_list && !value_float_list &&
      !value_string_list)
    throw std::invalid_argument("legacy grammar-config enables no value type");
}

bool LegacyGrammarConfig::allows_type(evo::RType type) const {
  switch (type) {
    case evo::RType::Int: return value_int;
    case evo::RType::Float: return value_float;
    case evo::RType::Bool: return value_bool;
    case evo::RType::Char: return value_char;
    case evo::RType::String: return value_string;
    case evo::RType::IntList: return value_int_list;
    case evo::RType::FloatList: return value_float_list;
    case evo::RType::StringList: return value_string_list;
    case evo::RType::Any: case evo::RType::Invalid: return false;
  }
  return false;
}

bool LegacyGrammarConfig::allows_node_kind(evo::NodeKind kind) const {
  using K = evo::NodeKind;
  switch (kind) {
    case K::CONST: return expression_const;
    case K::VAR: return expression_var;
    case K::NEG: return unary_neg;
    case K::NOT: return unary_not;
    case K::ADD: return binary_add; case K::SUB: return binary_sub;
    case K::MUL: return binary_mul; case K::DIV: return binary_div;
    case K::MOD: return binary_mod; case K::LT: return binary_lt;
    case K::LE: return binary_le; case K::GT: return binary_gt;
    case K::GE: return binary_ge; case K::EQ: return binary_eq;
    case K::NE: return binary_ne; case K::AND: return binary_and;
    case K::OR: return binary_or; case K::IF_EXPR: return expression_if_expr;
    case K::CALL_ABS: return builtin_abs; case K::CALL_MIN: return builtin_min;
    case K::CALL_MAX: return builtin_max; case K::CALL_CLIP: return builtin_clip;
    case K::CALL_IDIV0: return builtin_idiv0; case K::CALL_IMOD0: return builtin_imod0;
    case K::CALL_LEN: return builtin_len; case K::CALL_CONCAT: return builtin_concat;
    case K::CALL_SLICE: return builtin_slice; case K::CALL_INDEX: return builtin_index;
    case K::CALL_APPEND: return builtin_append; case K::CALL_PREPEND: return builtin_prepend;
    case K::CALL_REVERSE: return builtin_reverse; case K::CALL_FIND: return builtin_find;
    case K::CALL_CONTAINS: return builtin_contains; case K::CALL_SINGLETON: return builtin_singleton;
    case K::CALL_CHAR_TO_STRING: return builtin_char_to_string;
    case K::CALL_STRING_TO_CHAR: return builtin_string_to_char;
    case K::CALL_ORD: return builtin_ord; case K::CALL_CHR: return builtin_chr;
    case K::CALL_IS_LETTER: return builtin_is_letter; case K::CALL_IS_DIGIT: return builtin_is_digit;
    case K::CALL_IS_SPACE: return builtin_is_space; case K::CALL_IS_VOWEL: return builtin_is_vowel;
    case K::CALL_TO_LOWER: return builtin_to_lower; case K::CALL_TO_UPPER: return builtin_to_upper;
    case K::CALL_TO_STRING: return builtin_to_string;
    default: return true;
  }
}

evo::grammar::ResolvedDefinition convert_legacy_grammar_config(
    const LegacyGrammarConfig& config,
    const LegacyGrammarConfigConversion& options) {
  config.validate();
  if (!config.expression_const || !config.statement_return)
    throw std::invalid_argument("conversion requires expression_const and statement_return");
  if (!options.max_statements_per_block || options.max_statements_per_block > 64 ||
      options.locals.size() > 16 || options.max_statements_per_block <= options.locals.size())
    throw std::invalid_argument("conversion requires 1..64 statements, at most 16 locals, and room for initialization plus final return");
  if (options.max_for_k < 0) throw std::invalid_argument("conversion max_for_k must be non-negative");
  const auto enabled = [&](RType value) {
    (void)type_name(value);
    if (!config.allows_type(value)) throw std::invalid_argument("conversion declares disabled type " + type(value));
  };
  enabled(options.return_type);
  std::set<std::string> names;
  const auto declaration = [&](const auto& binding) {
    enabled(binding.type);
    if (binding.name.empty() || !names.insert(binding.name).second)
      throw std::invalid_argument("conversion input/local names must be nonempty and unique");
    return object({{"name", string(binding.name)}, {"type", string(type(binding.type))}});
  };
  Json inputs = array(), locals = array();
  for (const auto& input : options.inputs) inputs.array_v.push_back(declaration(input));
  for (const auto& local : options.locals) locals.array_v.push_back(declaration(local));
  if (!options.locals.empty() && !config.statement_assign)
    throw std::invalid_argument("conversion local initialization requires statement_assign");
  if (config.statement_if_stmt && !config.value_bool)
    throw std::invalid_argument("conversion statement_if_stmt requires value_bool; enable Bool or disable if statements");
  const auto loop_local = std::find_if(options.locals.begin(), options.locals.end(),
      [](const auto& local) { return local.type == RType::Int; });
  if (config.statement_for_range && loop_local == options.locals.end())
    throw std::invalid_argument("conversion statement_for_range requires an explicit Int local; declare one or disable loops");
  std::map<RType, Json> domains;
  for (const auto& domain : options.constants) {
    enabled(domain.type);
    if (!domains.emplace(domain.type, domain_json(domain)).second)
      throw std::invalid_argument("conversion requires exactly one constant domain per enabled type: duplicate " + type(domain.type));
  }
  for (auto value : value_types())
    if (config.allows_type(value) && !domains.count(value))
      throw std::invalid_argument("conversion requires explicit constant domain for enabled type " + type(value));

  Json rules = array();
  const auto rule = [&](std::string id, RType value, const char* category, std::vector<Json> alternatives) {
    auto definition = object({{"id", string(std::move(id))}, {"type", string(type(value))},
        {"category", string(category)}, {"scope", array()}, {"alternatives", array(std::move(alternatives))}});
    if (std::string(category) == "Program") {
      Json disabled; disabled.kind = Json::Kind::Bool; disabled.bool_v = false;
      definition.object_v["variation"] = std::move(disabled);
    }
    rules.array_v.push_back(std::move(definition));
  };
  const auto& catalog = PrimitiveCatalog::standard();
  for (auto value : value_types()) {
    if (!config.allows_type(value)) continue;
    std::vector<Json> alternatives{alternative("constant", object({{"constant", domains.at(value)}}))};
    if (config.expression_var) {
      for (const auto& input : options.inputs) if (input.type == value)
        alternatives.push_back(alternative("input." + input.name, object({{"input", string(input.name)}})));
      for (const auto& local : options.locals) if (local.type == value)
        alternatives.push_back(alternative("local." + local.name, object({{"local", string(local.name)}})));
    }
    for (const auto& signature : catalog.signatures()) {
      if (signature.result != value || !signature.executable() ||
          signature.operation == "constant" || signature.operation == "input" || signature.operation == "bound" ||
          !config.allows_node_kind(*signature.lowering_node) || !signature.regions.empty()) continue;
      if (!std::all_of(signature.arguments.begin(), signature.arguments.end(),
          [&](RType argument) { return config.allows_type(argument); })) continue;
      std::vector<Json> args;
      for (auto argument : signature.arguments) args.push_back(reference_expression(expr(argument)));
      alternatives.push_back(alternative("primitive." + std::to_string(signature.id), object({{"signature", string(signature.key)}, {"args", array(std::move(args))}})));
    }
    rule(expr(value), value, "Expression", std::move(alternatives));
  }
  const auto control = [&](const std::string& key, std::vector<Json> args, const std::string& name = "") {
    (void)catalog.resolve_control(key);
    auto out = object({{"control", string(key)}, {"type", string(type(options.return_type))}, {"args", array(std::move(args))}});
    // Source statement/block ancestry contributes nodes, but restarts expression
    // depth. Keep physical construction limits separate from these charges.
    Json reset; reset.kind = Json::Kind::Bool; reset.bool_v = true;
    out.object_v["resource_charge"] = object({{"nodes", number(1)},
        {"depth", number(0)}, {"resets_depth", std::move(reset)}});
    if (!name.empty()) out.object_v["name"] = string(name);
    return out;
  };
  const auto nil = [&]() { return control("block_nil()->Block", {}); };
  const auto cons = [&](Json head, Json tail) { return control("block_cons(Statement,Block)->Block", {std::move(head), std::move(tail)}); };
  const auto ret = [&]() { return control("return(" + type(options.return_type) + ")->Statement", {reference_expression(expr(options.return_type))}); };
  std::vector<Json> statements{alternative("return", ret())};
  if (config.statement_assign) for (const auto& local : options.locals)
    statements.push_back(alternative("assign." + local.name, control("assign(" + type(local.type) + ")->Statement", {reference_expression(expr(local.type))}, local.name)));
  const auto block_name = [](std::uint32_t count) { return "Gct1.Block" + std::to_string(count); };
  const auto tail_name = [](std::uint32_t count) { return "Gct1.Tail" + std::to_string(count); };
  if (config.statement_if_stmt)
    statements.push_back(alternative("if", control("if_stmt(Bool,Block,Block)->Statement",
        {reference_expression(expr(RType::Bool)), reference_expression(block_name(options.max_statements_per_block)), reference_expression(block_name(options.max_statements_per_block))})));
  if (config.statement_for_range) {
    ConstantDomain bound; bound.type = RType::Int; bound.integer_range = true; bound.maximum = options.max_for_k;
    statements.push_back(alternative("for", control("for_range(Int,Block)->Statement",
        {object({{"constant", domain_json(bound)}}), reference_expression(block_name(options.max_statements_per_block))}, loop_local->name)));
  }
  rule("Gct1.Stmt", options.return_type, "Statement", std::move(statements));
  for (std::uint32_t count = 0; count <= options.max_statements_per_block; ++count) {
    std::vector<Json> alternatives{alternative("nil", nil())};
    if (count) alternatives.push_back(alternative("cons", cons(reference_expression("Gct1.Stmt"), reference_expression(block_name(count - 1)))));
    rule(block_name(count), options.return_type, "Block", std::move(alternatives));
  }
  const auto remaining = options.max_statements_per_block - static_cast<std::uint32_t>(options.locals.size());
  for (std::uint32_t count = 1; count <= remaining; ++count) {
    std::vector<Json> alternatives{alternative("final", cons(ret(), nil()))};
    if (count > 1) alternatives.push_back(alternative("cons", cons(reference_expression("Gct1.Stmt"), reference_expression(tail_name(count - 1)))));
    rule(tail_name(count), options.return_type, "Block", std::move(alternatives));
  }
  Json body = reference_expression(tail_name(remaining));
  for (auto it = options.locals.rbegin(); it != options.locals.rend(); ++it)
    body = cons(control("assign(" + type(it->type) + ")->Statement", {object({{"constant", domains.at(it->type)}})}, it->name), std::move(body));
  rule("Gct1.Program", options.return_type, "Program", {alternative("program", control("program(Block)->Program", {std::move(body)}))});
  Json root = object({{"format_version", string(kDefinitionVersion)},
      {"entry", object({{"nonterminal", string("Gct1.Program")}, {"type", string(type(options.return_type))}, {"category", string("Program")}})},
      {"inputs", std::move(inputs)}, {"locals", std::move(locals)}, {"nonterminals", std::move(rules)},
      {"search_limits", object({{"max_nodes", number(options.search_limits.max_nodes)}, {"max_depth", number(options.search_limits.max_depth)}})},
      {"execution_limits", object({{"fuel", number(options.execution_limits.fuel)}})}});
  auto result = parse_definition(canonical_json(root));
  (void)compile_grammar(result);
  return result;
}

}  // namespace gagp::migration
