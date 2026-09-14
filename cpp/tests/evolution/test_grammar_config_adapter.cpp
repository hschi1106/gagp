#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/grammar/config_adapter.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;
namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void rejects(const std::function<void()>& action, const char* message) {
  try { action(); } catch (const std::invalid_argument&) { return; }
  throw std::runtime_error(message);
}
void disable_specialized(GrammarConfig& config) {
  config.expression_map_list = config.expression_filter_list = config.expression_linear_rec = false;
  config.expression_asgp_dc = config.expression_asgp_dp1d = config.expression_asgp_dp2d = false;
}
GrammarConfigConversion scalar_options() {
  GrammarConfigConversion out;
  out.return_type = RType::Float;
  out.inputs = {{"x", RType::Float}};
  out.locals = {{"i", RType::Int}};
  out.constants = {{RType::Int, {}, true, -8, 8},
      {RType::Float, {0.0, -0.0, 0.5, -2.25}}, {RType::Bool, {true, false}}};
  out.search_limits = {100, 20};
  out.execution_limits = {10000};
  out.max_statements_per_block = 4;
  out.max_for_k = 3;
  return out;
}
void verify_sample(const CompiledGrammar& grammar, const GrammarConfig& config,
    const GrammarConfigConversion& options, std::uint64_t seed) {
  const auto generated = generate_derivation(grammar, seed);
  require_membership(grammar, generated.genome);
  VerifyOptions verify_options;
  verify_options.grammar_config = &config;
  const auto verified = verify_ast(generated.genome.ast, options.inputs, verify_options);
  if (!verified.ok) throw std::runtime_error("converted AST fails native verification: " + verified.diagnostic.message);
  check(verified.verified.return_type == options.return_type, "conversion changed exact return type");
  const auto& ast = generated.genome.ast;
  for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
    const auto& node = ast.nodes[i];
    if (node.kind == NodeKind::BLOCK_CONS) {
      std::size_t cursor = i, count = 0;
      while (ast.nodes.at(cursor).kind == NodeKind::BLOCK_CONS) {
        ++count;
        cursor = verified.verified.subtree_end.at(cursor + 1);
      }
      check(ast.nodes.at(cursor).kind == NodeKind::BLOCK_NIL && count <= options.max_statements_per_block,
          "converted block exceeds statement bound");
    }
    if (node.kind == NodeKind::FOR_RANGE) {
      const auto& bound = ast.nodes.at(i + 1);
      check(bound.kind == NodeKind::CONST, "converted loop bound is not a literal");
      const auto& value = ast.consts.at(bound.i0);
      check(value.tag == ValueTag::Int && value.i >= 0 && value.i <= options.max_for_k,
          "converted loop bound escaped explicit range");
    }
  }
  // Every native local has a root assignment whose RHS cannot read a local.
  std::size_t cursor = 1;
  for (const auto& local : options.locals) {
    check(ast.nodes.at(cursor).kind == NodeKind::BLOCK_CONS, "missing initialization block");
    const auto& assign = ast.nodes.at(cursor + 1);
    check(assign.kind == NodeKind::ASSIGN && ast.names.at(assign.i0) == local.name &&
          ast.nodes.at(cursor + 2).kind == NodeKind::CONST, "local initialization can read undefined locals");
    cursor = verified.verified.subtree_end.at(cursor + 1);
  }
}
}  // namespace

int main() {
  try {
    auto config = GrammarConfig::scalar();
    disable_specialized(config);
    const auto options = scalar_options();
    const auto definition = convert_grammar_config(config, options);
    check(definition.canonical == convert_grammar_config(config, options).canonical,
          "conversion is not deterministic");
    check(definition.content_hash == convert_grammar_config(config, options).content_hash,
          "conversion hash is not deterministic");
    check(definition.canonical.find("Gct1.Program") != std::string::npos, "conversion identity is not versioned");
    const auto grammar = compile_grammar(definition);
    bool positive_zero = false, negative_zero = false;
    for (const auto& domain : grammar.constants()) if (domain.type == RType::Float)
      for (const auto& data : domain.values) {
        const auto value = std::get<double>(data);
        if (value == 0) (std::signbit(value) ? negative_zero : positive_zero) = true;
      }
    check(positive_zero && negative_zero, "conversion lost signed Float zeros");
    for (std::uint64_t seed = 0; seed < 160; ++seed) verify_sample(grammar, config, options, seed);
    check(ast_cache_key(generate_derivation(grammar, 29).genome.ast) ==
          ast_cache_key(generate_derivation(grammar, 29).genome.ast), "converted seed replay changed");

    auto changed = options;
    changed.search_limits.max_nodes++;
    check(convert_grammar_config(config, changed).content_hash != definition.content_hash, "search limit omitted from hash");
    changed = options; changed.execution_limits.fuel++;
    check(convert_grammar_config(config, changed).content_hash != definition.content_hash, "execution limit omitted from hash");
    changed = options; changed.constants[0].maximum++;
    check(convert_grammar_config(config, changed).content_hash != definition.content_hash, "constant domain omitted from hash");
    auto restricted = config;
    restricted.binary_add = restricted.binary_mul = restricted.expression_var = false;
    const auto restricted_definition = convert_grammar_config(restricted, options);
    check(restricted_definition.content_hash != definition.content_hash, "operation mask omitted from hash");
    const auto restricted_grammar = compile_grammar(restricted_definition);
    for (const auto& expression : restricted_grammar.expressions()) {
      check(expression.kind != ExpressionKind::Input && expression.kind != ExpressionKind::Local,
            "disabled variable production emitted");
      if (expression.kind == ExpressionKind::Primitive) {
        const auto node = PrimitiveCatalog::standard().at(expression.target).lowering_node;
        check(node != NodeKind::ADD && node != NodeKind::MUL, "disabled arithmetic production emitted");
      }
    }
    for (std::uint64_t seed = 0; seed < 16; ++seed) verify_sample(restricted_grammar, restricted, options, seed);

    auto all = GrammarConfig::all_enabled();
    disable_specialized(all);
    auto typed = options;
    typed.constants.push_back({RType::Char, {char32_t(U'😀'), char32_t(U'a')}});
    typed.constants.push_back({RType::String, {std::string("a\0b", 3), std::string()}});
    typed.constants.push_back({RType::IntList, {std::vector<std::int64_t>{-8, 0, 8}}});
    typed.constants.push_back({RType::FloatList, {std::vector<double>{0.0, -0.0, 0.5}}});
    typed.constants.push_back({RType::StringList, {std::vector<std::string>{"", std::string("a\0b", 3)}}});
    for (auto result_type : value_types()) {
      typed.return_type = result_type;
      const auto typed_grammar = compile_grammar(convert_grammar_config(all, typed));
      for (std::uint64_t seed = 0; seed < 16; ++seed) verify_sample(typed_grammar, all, typed, seed);
    }

    const std::vector<bool GrammarConfig::*> unsupported = {
        &GrammarConfig::expression_map_list, &GrammarConfig::expression_filter_list,
        &GrammarConfig::expression_linear_rec, &GrammarConfig::expression_asgp_dc,
        &GrammarConfig::expression_asgp_dp1d, &GrammarConfig::expression_asgp_dp2d,
        &GrammarConfig::compat_legacy_num_list_inputs_as_any};
    for (auto flag : unsupported) {
      auto invalid = config; invalid.*flag = true;
      rejects([&] { convert_grammar_config(invalid, options); }, "unsupported config flag accepted");
    }
    auto bad = options; bad.constants.pop_back();
    rejects([&] { convert_grammar_config(config, bad); }, "missing enabled domain accepted");
    bad = options; bad.constants.push_back(bad.constants.front());
    rejects([&] { convert_grammar_config(config, bad); }, "duplicate domain accepted");
    bad = options; bad.constants.push_back({RType::String, {std::string("extra")}});
    rejects([&] { convert_grammar_config(config, bad); }, "disabled domain accepted");
    bad = options; bad.constants[0].integer_range = false; bad.constants[0].values = {true};
    rejects([&] { convert_grammar_config(config, bad); }, "mismatched constant data accepted");
    for (auto invalid_type : {RType::Any, RType::Invalid, RType::String}) {
      bad = options; bad.return_type = invalid_type;
      rejects([&] { convert_grammar_config(config, bad); }, "invalid return type accepted");
      bad = options; bad.inputs[0].type = invalid_type;
      rejects([&] { convert_grammar_config(config, bad); }, "invalid input type accepted");
      bad = options; bad.locals[0].type = invalid_type;
      rejects([&] { convert_grammar_config(config, bad); }, "invalid local type accepted");
    }
    bad = options; bad.max_statements_per_block = 1;
    rejects([&] { convert_grammar_config(config, bad); }, "initialization without final return capacity accepted");
    for (auto capacity : {0u, 65u}) {
      bad = options; bad.max_statements_per_block = capacity;
      rejects([&] { convert_grammar_config(config, bad); }, "invalid statement capacity accepted");
    }
    bad = options; bad.max_for_k = -1;
    rejects([&] { convert_grammar_config(config, bad); }, "negative loop bound accepted");
    bad = options; bad.locals.clear();
    rejects([&] { convert_grammar_config(config, bad); }, "loop without Int local accepted");
    auto invalid = config; invalid.value_bool = false;
    bad = options; bad.constants.pop_back();
    rejects([&] { convert_grammar_config(invalid, bad); }, "if statement without Bool accepted");
    invalid = config; invalid.statement_assign = false;
    rejects([&] { convert_grammar_config(invalid, options); }, "uninitialized declared local accepted");
    bad = options; bad.locals[0].name = "x";
    rejects([&] { convert_grammar_config(config, bad); }, "conflicting local/input names accepted");
    std::cout << "grammar-config conversion: typed domains, bounded controls, 304 seeds, and rejection cases passed\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
