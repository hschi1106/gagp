#include "gagp/evolution/grammar/config_adapter.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

#include "gagp/evolution/grammar/values.hpp"

namespace gagp::evo::grammar {
namespace {
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
Json domain_json(const ConstantDomain& domain) {
  Json out = object({{"type", string(type(domain.type))}});
  if (domain.integer_range) {
    if (!domain.values.empty()) throw std::invalid_argument("conversion constant range must not also contain values");
    out.object_v["range"] = array({string(std::to_string(domain.minimum)), string(std::to_string(domain.maximum))});
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
  (void)parse_constant_domain(out);
  return out;
}
}  // namespace

ResolvedDefinition convert_grammar_config(const GrammarConfig& config,
    const GrammarConfigConversion& options) {
  config.validate();
  if (config.compat_legacy_num_list_inputs_as_any)
    throw std::invalid_argument("conversion requires exact typed inputs; disable compat_legacy_num_list_inputs_as_any");
  const std::pair<bool, const char*> unsupported[] = {
      {config.expression_map_list, "expression_map_list"}, {config.expression_filter_list, "expression_filter_list"},
      {config.expression_linear_rec, "expression_linear_rec"}, {config.expression_asgp_dc, "expression_asgp_dc"},
      {config.expression_asgp_dp1d, "expression_asgp_dp1d"}, {config.expression_asgp_dp2d, "expression_asgp_dp2d"}};
  for (const auto& flag : unsupported)
    if (flag.first) throw std::invalid_argument(std::string("conversion does not support ") + flag.second + "; disable this flag or author an explicit grammar definition");
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
    rules.array_v.push_back(object({{"id", string(std::move(id))}, {"type", string(type(value))},
        {"category", string(category)}, {"scope", array()}, {"alternatives", array(std::move(alternatives))}}));
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

}  // namespace gagp::evo::grammar
