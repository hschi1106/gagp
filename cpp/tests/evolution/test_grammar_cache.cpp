#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "gagp/evolution/grammar/cache.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/values.hpp"
#include "gagp/runtime/payload/payload.hpp"

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;
namespace {
void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void rejects(const std::function<void()>& action) {
  try { action(); } catch (const std::invalid_argument&) { return; }
  throw std::runtime_error("invalid runtime cache identity accepted");
}
ProgramGenome constant(Value value) {
  ProgramGenome genome;
  genome.ast.nodes = {{NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS},
      {NodeKind::RETURN}, {NodeKind::CONST, 0}, {NodeKind::BLOCK_NIL}};
  genome.ast.consts = {value};
  return genome;
}
std::string key(const ProgramGenome& genome) {
  return runtime_cache_identity(genome, {"x", "y"}, 100);
}
void test_identity() {
  auto genome = constant(Value::from_int(7));
  const auto original = key(genome);
  check(original.size() == 64, "runtime identity is not SHA-256");
  check(runtime_cache_identity(genome, {"x", "y"}, 101) != original, "fuel omitted");
  check(runtime_cache_identity(genome, {"y", "x"}, 100) != original, "input order omitted");
  check(runtime_cache_identity(genome, {"x", "z"}, 100) != original, "input name omitted");
  check(runtime_cache_identity(genome, {"x|y"}, 100) != original, "input boundaries omitted");
  genome.meta.program_key = "stale";
  genome.meta.node_count = 100;
  auto metadata = std::make_shared<DerivationMetadata>();
  metadata->grammar_hash = "changed search-space hash";
  metadata->seed = 123;
  metadata->semantic_version = "untrusted attached metadata";
  metadata->generator_version = "another generator";
  metadata->execution_limits.fuel = 999;
  genome.derivation = metadata;
  check(key(genome) == original, "provenance changed runtime identity");
  genome.ast.consts[0] = Value::from_int(8);
  check(key(genome) != original, "constant omitted");
  genome.ast.consts = {Value::from_int(7), Value::from_bool(false)};
  check(key(genome) != original, "unused constant pool entry omitted");
  genome = constant(Value::from_int(7));
  genome.ast.nodes[3].i0 = 1;
  check(key(genome) != original, "AST node omitted");
  genome = constant(Value::from_int(7));
  genome.ast.names = {"local"};
  check(key(genome) != original, "AST names omitted");
  check(key(constant(Value::from_float(0.0))) != key(constant(Value::from_float(-0.0))),
        "Float signed zero collapsed");
  check(key(constant(payload::make_float_list_value({Value::from_float(0.0)}))) !=
        key(constant(payload::make_float_list_value({Value::from_float(-0.0)}))),
        "FloatList signed zero collapsed");

  const auto fixture = [](int weight) {
    return compile_grammar(parse_definition(
        R"({"format_version":"grammar-definition-v2","entry":{"nonterminal":"Value","type":"Int"},
        "search_limits":{"max_nodes":5,"max_depth":4},"execution_limits":{"fuel":100},
        "nonterminals":[{"id":"Value","type":"Int","scope":[],"alternatives":[
        {"id":"constant","weight":)" + std::to_string(weight) +
        R"(,"expression":{"constant":{"type":"Int","values":["7"]}}}]}]})"));
  };
  const auto first = generate_derivation(fixture(1), 1);
  const auto second = generate_derivation(fixture(9), 999);
  check(first.derivation.grammar_hash != second.derivation.grammar_hash, "weight fixture hash unchanged");
  check(key(first.genome) == key(second.genome), "production weight changed runtime identity");
}
void test_materialized_payloads() {
  auto genome = constant(payload::make_string_list_value({
      payload::make_string_value("first"), payload::make_string_value("second")}));
  const auto encoded = encode_constant(genome.ast.consts[0]);
  const auto original = key(genome);
  payload::clear();
  rejects([&] { key(genome); });
  genome.ast.consts[0] = decode_constant(encoded);
  check(key(genome) == original, "cold payload reload changed runtime identity");

  // Equal decoded contents with different transport tokens have equal identity.
  const auto a = Value::from_string_hash_len(1234, 5);
  const auto b = Value::from_string_hash_len(5678, 5);
  payload::register_string(a, "hello");
  payload::register_string(b, "hello");
  check(key(constant(a)) == key(constant(b)), "transport token leaked into identity");
  payload::register_string(b, "world");
  check(key(constant(a)) != key(constant(b)), "decoded payload omitted");
}
void test_structure_identity_without_copy() {
  auto ast = constant(Value::from_int(7)).ast;
  ast.names = {"a|b", "c:d"};
  ast.lexical_regions.emplace_back();
  ast.traversal_specs.emplace_back();
  ast.fuel_specs.emplace_back();
  ast.bounded_region_specs.emplace_back();
  const auto original = ast_cache_key(ast);
  auto structure = ast;
  structure.consts.clear();
  check(ast_structure_cache_key(ast) == ast_cache_key(structure),
        "direct structure encoding differs from copied AST with empty pool");
  check(ast_cache_key(ast) == original,
        "structure identity modified its input constant pool");
  ast.consts = {Value::invalid()};
  check(ast_structure_cache_key(ast) == ast_cache_key(structure),
        "structure identity unexpectedly encoded constant transport values");
}
void test_direct_constant_encoding() {
  const std::vector<Value> values{
      Value::from_int(std::numeric_limits<std::int64_t>::min()),
      Value::from_int(0), Value::from_int(std::numeric_limits<std::int64_t>::max()),
      Value::from_float(0.0), Value::from_float(-0.0), Value::from_float(1.25),
      Value::from_float(std::numeric_limits<double>::denorm_min()),
      Value::from_float(std::numeric_limits<double>::max()),
      Value::from_bool(false), Value::from_bool(true),
      Value::from_char(0), Value::from_char('"'), Value::from_char(0x10ffff),
      payload::make_string_value("quoted\"\\\n\t"),
      payload::make_int_list_value({Value::from_int(-1), Value::from_int(2)}),
      payload::make_float_list_value({Value::from_float(-0.0)}),
      payload::make_string_list_value({payload::make_string_value("x")})};
  for (const auto& value : values)
    check(canonical_constant_encoding(value) == canonical_json(encode_constant(value)),
          "direct constant encoding changed canonical bytes");
  for (unsigned count = 0; count < 32; ++count) {
    std::vector<Value> ints, floats, strings;
    for (unsigned i = 0; i < count; ++i) {
      ints.push_back(Value::from_int(i % 2 ? std::numeric_limits<std::int64_t>::min() : i));
      floats.push_back(Value::from_float(i % 2 ? -0.0 : std::numeric_limits<double>::denorm_min()));
      strings.push_back(payload::make_string_value(std::string(i, '\0') + "\"\\\n\t\xF0\x9F\x98\x80"));
    }
    for (const auto& value : {payload::make_int_list_value(ints),
         payload::make_float_list_value(floats), payload::make_string_list_value(strings)})
      check(canonical_constant_encoding(value) == canonical_json(encode_constant(value)),
            "direct list encoding differs from canonical artifact serialization");
  }
  rejects([] { canonical_constant_encoding(Value::from_char(0xd800)); });
  rejects([] { canonical_constant_encoding(Value::from_float(
      std::numeric_limits<double>::quiet_NaN())); });
}
void test_rejections() {
  const auto genome = constant(Value::from_int(1));
  rejects([&] { runtime_cache_identity(genome, {}, 0); });
  rejects([&] { runtime_cache_identity(genome, {}, std::numeric_limits<std::uint32_t>::max()); });
  rejects([&] { key(constant(Value::invalid())); });
  rejects([&] { key(constant(Value::from_fallback_token(42))); });
  rejects([&] { key(constant(Value::from_float(std::numeric_limits<double>::infinity()))); });
  rejects([&] { key(constant(Value::from_string_hash_len(999999, 12))); });
}
}  // namespace
int main() {
  try {
    test_identity();
    test_structure_identity_without_copy();
    test_direct_constant_encoding();
    test_materialized_payloads();
    test_rejections();
    std::cout << "grammar runtime cache identity: materialization and search independence passed\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
