#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <iostream>
#include <string>
#include <stdexcept>
#include <utility>

#include "gagp/cli/commands.hpp"
#include "gagp/cli/json.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/repro/pack.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"

namespace {

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << "\n";
  return condition;
}

gagp::evo::ProgramGenome bloated_genome() {
  using gagp::evo::AstNode;
  using gagp::evo::NodeKind;
  gagp::evo::ProgramGenome genome;
  genome.ast.names = {"unused", "also_unused"};
  genome.ast.consts = {
      gagp::Value::from_int(111),
      gagp::Value::from_int(42),
      gagp::Value::from_int(222),
  };
  genome.ast.nodes = {
      AstNode{NodeKind::PROGRAM, 0, 0},
      AstNode{NodeKind::BLOCK_CONS, 0, 0},
      AstNode{NodeKind::RETURN, 0, 0},
      AstNode{NodeKind::CONST, 1, 0},
      AstNode{NodeKind::BLOCK_NIL, 0, 0},
  };
  genome.meta = gagp::evo::build_genome_meta(genome.ast);
  return genome;
}

bool check_constant_key_encoding() {
  using gagp::Value;
  using gagp::ValueTag;
  std::vector<Value> values;
  for (const auto tag : {ValueTag::Int, ValueTag::Char, ValueTag::String,
           ValueTag::IntList, ValueTag::FloatList, ValueTag::StringList, ValueTag::FallbackToken}) {
    for (const auto integer : {std::int64_t{0}, std::int64_t{-1}, std::int64_t{123456789},
             std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max()}) {
      auto value = Value::from_int(integer); value.tag = tag; values.push_back(value);
    }
  }
  for (const auto bits : {UINT64_C(0), UINT64_C(0x8000000000000000), UINT64_C(1),
           UINT64_C(0x3ff0000000000000), UINT64_C(0x7ff0000000000000),
           UINT64_C(0xfff0000000000000), UINT64_C(0x7ff8000000000001), UINT64_C(0xffffffffffffffff)}) {
    auto value = Value::from_float(0.0); std::memcpy(&value.f, &bits, sizeof(bits)); values.push_back(value);
  }
  values.push_back(Value::from_bool(false)); values.push_back(Value::from_bool(true));
  auto invalid = Value::from_int(999); invalid.tag = ValueTag::Invalid; values.push_back(invalid);
  for (const auto& value : values) {
    std::ostringstream oracle;
    oracle << static_cast<int>(value.tag) << ':';
    if (value.tag == ValueTag::Float) {
      std::uint64_t bits; std::memcpy(&bits, &value.f, sizeof(bits));
      oracle << std::hex << std::setfill('0') << std::setw(16) << bits;
    } else if (value.tag == ValueTag::Bool) oracle << (value.b ? 1 : 0);
    else if (value.tag == ValueTag::Invalid) oracle << "invalid";
    else oracle << value.i;
    gagp::evo::AstProgram ast; ast.consts.push_back(value);
    const auto expected = "AstCache(version:" + std::to_string(ast.version.size()) + ":" + ast.version +
        ";names:0;consts:1|" + std::to_string(oracle.str().size()) + ":" + oracle.str() + ";nodes:0)";
    if (!check(gagp::evo::ast_cache_key(ast) == expected,
            "native constant key differs from original stream encoding")) return false;
  }
  return true;
}

long long execute_int(const gagp::evo::ProgramGenome& genome) {
  const auto verified = gagp::evo::verify_ast(genome.ast, {});
  if (!verified.ok) return -1;
  const auto result = gagp::execute_bytecode_cpu(
      gagp::evo::compile_for_eval(genome, verified.verified), {}, 100);
  return (!result.is_error && result.value.tag == gagp::ValueTag::Int)
             ? result.value.i
             : -1;
}

}  // namespace

int main() {
  if (!check_constant_key_encoding()) return 1;
  const auto original = bloated_genome();
  const std::string encoded = gagp::cli_detail::encode_ast_json(original.ast);
  const auto decoded = gagp::cli_detail::decode_ast_json(
      gagp::cli_detail::JsonParser(encoded).parse());
  if (!check(gagp::evo::ast_cache_key(decoded) ==
                 gagp::evo::ast_cache_key(original.ast),
             "AST JSON round trip must preserve canonical key")) return 1;
  if (!check(gagp::cli_detail::encode_ast_json(decoded) == encoded,
             "AST JSON codec must be canonical")) return 1;

  const auto compacted = gagp::evo::repro::compact_genome_tables(original);
  const auto compacted_twice = gagp::evo::repro::compact_genome_tables(compacted);
  if (!check(compacted.ast.names.empty() && compacted.ast.consts.size() == 1 &&
                 compacted.ast.nodes[3].i0 == 0,
             "table compaction must discard and remap dead entries")) return 1;
  if (!check(execute_int(original) == 42 && execute_int(compacted) == 42,
             "table compaction must preserve execution")) return 1;
  if (!check(compacted.meta.program_key == compacted_twice.meta.program_key &&
                 gagp::cli_detail::encode_ast_json(compacted.ast) ==
                     gagp::cli_detail::encode_ast_json(compacted_twice.ast),
             "table compaction must be idempotent in representation and key")) return 1;

  auto stale = compacted;
  stale.meta.program_key = "stale";
  stale.meta.node_count = 999;
  const auto refreshed = gagp::evo::repro::compact_genome_tables(std::move(stale));
  if (!check(refreshed.meta.program_key == compacted.meta.program_key &&
                 refreshed.meta.node_count == compacted.meta.node_count && execute_int(refreshed) == 42,
             "already compact tables must still refresh untrusted metadata")) return 1;
  auto malformed = compacted;
  malformed.ast.nodes[3].i0 = 1;
  bool rejected = false;
  try { (void)gagp::evo::repro::compact_genome_tables(std::move(malformed)); }
  catch (const std::invalid_argument&) { rejected = true; }
  if (!check(rejected, "already compact tables must not bypass index validation")) return 1;

  std::cout << "gagp_test_ast_codec_compaction: OK\n";
  return 0;
}
