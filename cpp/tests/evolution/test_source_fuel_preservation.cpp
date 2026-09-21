#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/cli/commands.hpp"
#include "gagp/cli/json.hpp"
#include "gagp/core/errors.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/grammar/cache.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/repro/pack.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "subtree_utils.hpp"

namespace {
using gagp::ErrCode;
using gagp::ExecResult;
using gagp::Value;
using gagp::ValueTag;
using gagp::evo::AstNode;
using gagp::evo::AstProgram;
using gagp::evo::FuelEvent;
using gagp::evo::NodeKind;
using gagp::evo::ProgramGenome;

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << '\n';
  return condition;
}

AstProgram profiled_add() {
  AstProgram ast;
  ast.consts = {Value::from_int(4), Value::from_int(10)};
  ast.nodes = {
      AstNode{NodeKind::PROGRAM, 0, 0},
      AstNode{NodeKind::BLOCK_CONS, 0, 0},
      AstNode{NodeKind::RETURN, 0, 0},
      AstNode{NodeKind::ADD, 0, 0},
      AstNode{NodeKind::CONST, 0, 0},
      AstNode{NodeKind::CONST, 1, 0},
      AstNode{NodeKind::BLOCK_NIL, 0, 0},
  };
  ast.fuel_specs = {
      {3, {{FuelEvent::Operation, 7}}},
      {4, {{FuelEvent::Operation, 2}}},
      {5, {{FuelEvent::Operation, 3}}},
  };
  return ast;
}

ProgramGenome genome(const AstProgram& ast) {
  ProgramGenome out;
  out.ast = ast;
  out.meta = gagp::evo::build_genome_meta(ast);
  return out;
}

ExecResult execute(const AstProgram& ast, int fuel) {
  const auto verified = gagp::evo::verify_ast(ast, {});
  if (!verified.ok) throw std::runtime_error("profile preservation fixture failed verification");
  return gagp::execute_bytecode_cpu(
      gagp::evo::compile_for_eval(genome(ast), verified.verified), {}, fuel);
}

bool is_int(const ExecResult& result, std::int64_t value) {
  return !result.is_error && result.value.tag == ValueTag::Int && result.value.i == value;
}

bool times_out(const ExecResult& result) {
  return result.is_error && result.err.code == ErrCode::Timeout;
}

bool same_profiles(const AstProgram& left, const AstProgram& right) {
  if (left.fuel_specs.size() != right.fuel_specs.size()) return false;
  for (std::size_t i = 0; i < left.fuel_specs.size(); ++i) {
    const auto& a = left.fuel_specs[i];
    const auto& b = right.fuel_specs[i];
    if (a.node_index != b.node_index || a.charges.size() != b.charges.size()) return false;
    for (std::size_t j = 0; j < a.charges.size(); ++j)
      if (a.charges[j].event != b.charges[j].event ||
          a.charges[j].cost != b.charges[j].cost) return false;
  }
  return true;
}

bool rejects_invalid(const std::function<void()>& action) {
  try {
    action();
  } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
}

bool test_codec_and_identities() {
  const AstProgram ast = profiled_add();
  const std::string encoded = gagp::cli_detail::encode_ast_json(ast);
  const AstProgram decoded = gagp::cli_detail::decode_ast_json(
      gagp::cli_detail::JsonParser(encoded).parse());
  if (!check(same_profiles(decoded, ast) &&
                 gagp::cli_detail::encode_ast_json(decoded) == encoded,
             "materialized AST codec must preserve every fuel profile field") ||
      !check(gagp::evo::ast_cache_key(decoded) == gagp::evo::ast_cache_key(ast),
             "materialized AST codec changed profile cache identity") ||
      !check(is_int(execute(ast, 13), 14) && times_out(execute(ast, 12)) &&
                 is_int(execute(decoded, 13), 14) && times_out(execute(decoded, 12)),
             "materialized AST codec changed the exact fuel boundary")) {
    return false;
  }

  AstProgram changed = ast;
  changed.fuel_specs[0].charges[0].cost = 8;
  if (!check(gagp::evo::ast_to_string(changed) != gagp::evo::ast_to_string(ast) &&
                 gagp::evo::ast_cache_key(changed) != gagp::evo::ast_cache_key(ast),
             "fuel event costs must participate in AST text and cache identity")) {
    return false;
  }
  const std::string runtime = gagp::evo::grammar::runtime_cache_identity(genome(ast), {}, 100);
  return check(gagp::evo::grammar::runtime_cache_identity(genome(changed), {}, 100) != runtime,
               "fuel event costs must participate in runtime cache identity");
}

bool test_compaction_and_splice() {
  ProgramGenome padded = genome(profiled_add());
  padded.ast.names = {"dead"};
  padded.ast.consts.insert(padded.ast.consts.begin(), Value::from_int(99));
  ++padded.ast.nodes[4].i0;
  ++padded.ast.nodes[5].i0;
  const ProgramGenome compacted = gagp::evo::repro::compact_genome_tables(padded);
  if (!check(same_profiles(compacted.ast, padded.ast),
             "table compaction must preserve fuel profiles") ||
      !check(is_int(execute(compacted.ast, 13), 14) && times_out(execute(compacted.ast, 12)),
             "table compaction changed the exact fuel boundary")) {
    return false;
  }

  AstProgram donor;
  donor.consts = {Value::from_int(4)};
  donor.nodes = {
      AstNode{NodeKind::NEG, 0, 0},
      AstNode{NodeKind::CONST, 0, 0},
  };
  donor.fuel_specs = {
      {0, {{FuelEvent::Operation, 4}}},
      {1, {{FuelEvent::Operation, 5}}},
  };
  const AstProgram spliced = gagp::evo::subtree::replace_subtree(
      profiled_add(), 4, 5, donor, 0, donor.nodes.size());
  if (!check(spliced.fuel_specs.size() == 4,
             "splice must remove the replaced profile and retain/copy the others") ||
      !check(spliced.fuel_specs[0].node_index == 3 &&
                 spliced.fuel_specs[1].node_index == 6 &&
                 spliced.fuel_specs[2].node_index == 4 &&
                 spliced.fuel_specs[3].node_index == 5,
             "splice must retain, shift, and copy profile node indexes") ||
      !check(spliced.fuel_specs[0].charges[0].cost == 7 &&
                 spliced.fuel_specs[1].charges[0].cost == 3 &&
                 spliced.fuel_specs[2].charges[0].cost == 4 &&
                 spliced.fuel_specs[3].charges[0].cost == 5,
             "splice changed retained or donor fuel charges") ||
      !check(is_int(execute(spliced, 20), 6) && times_out(execute(spliced, 19)),
             "splice changed the composed exact fuel boundary")) {
    return false;
  }
  const std::string encoded = gagp::cli_detail::encode_ast_json(spliced);
  const AstProgram decoded = gagp::cli_detail::decode_ast_json(
      gagp::cli_detail::JsonParser(encoded).parse());
  return check(is_int(execute(decoded, 20), 6) && times_out(execute(decoded, 19)),
               "post-splice materialized codec changed the exact fuel boundary");
}

bool test_membership_and_gpu_rejections() {
  const auto grammar = gagp::evo::grammar::compile_grammar(
      gagp::evo::grammar::parse_definition(R"({
        "format_version":"grammar-definition-v2",
        "entry":{"nonterminal":"Value","type":"Int"},
        "search_limits":{"max_nodes":5,"max_depth":4},
        "execution_limits":{"fuel":100},
        "nonterminals":[{"id":"Value","type":"Int","scope":[],"alternatives":[
          {"id":"constant","weight":1,"expression":{"constant":{"type":"Int","values":["7"]}}}
        ]}]
      })"));
  auto generated = gagp::evo::grammar::generate_derivation(grammar, 9).genome;
  generated.ast.fuel_specs.push_back({3, {{FuelEvent::Operation, 1}}});
  if (!check(rejects_invalid([&] {
        gagp::evo::grammar::require_membership(grammar, generated);
      }), "grammar membership accepted an unsolicited source fuel profile")) {
    return false;
  }

  try {
    (void)gagp::evo::repro::pack_population(
        {genome(profiled_add())}, gagp::evo::repro::PreprocessOutput{},
        gagp::evo::repro::GpuReproConfig{});
  } catch (const std::invalid_argument& error) {
    return check(std::string(error.what()).find("complete preparation metadata") !=
                     std::string::npos,
                 "GPU reproduction reported the wrong compiled-preparation rejection");
  }
  return check(false, "GPU reproduction accepted missing compiled preparation");
}
}  // namespace

int main() {
  if (!test_codec_and_identities()) return 1;
  if (!test_compaction_and_splice()) return 1;
  if (!test_membership_and_gpu_rejections()) return 1;
  std::cout << "gagp_test_source_fuel_preservation: OK\n";
  return 0;
}
