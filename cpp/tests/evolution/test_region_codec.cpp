#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/cli/commands.hpp"
#include "gagp/cli/json.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/grammar/cache.hpp"
#include "gagp/evolution/repro/pack.hpp"

namespace {

using gagp::evo::AstNode;
using gagp::evo::AstProgram;
using gagp::evo::LexicalBinding;
using gagp::evo::LexicalRegion;
using gagp::evo::NodeKind;
using gagp::evo::RType;
using gagp::evo::TraversalDirection;
using gagp::evo::TraversalSpec;

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << "\n";
  return condition;
}

AstProgram region_program() {
  AstProgram ast;
  ast.names = {"xs"};
  ast.consts = {gagp::Value::from_int(0)};
  ast.nodes = {
      AstNode{NodeKind::PROGRAM, 0, 0},
      AstNode{NodeKind::BLOCK_CONS, 0, 0},
      AstNode{NodeKind::RETURN, 0, 0},
      AstNode{NodeKind::LET_REGION, 0, 0},
      AstNode{NodeKind::CONST, 0, 0},
      AstNode{NodeKind::TRAVERSE, 0, 0},
      AstNode{NodeKind::VAR, 0, 0},
      AstNode{NodeKind::CONST, 0, 0},
      AstNode{NodeKind::REGION_VAR, 0, 0},
      AstNode{NodeKind::TRAVERSE_RANGE, 0, 0},
      AstNode{NodeKind::VAR, 0, 0},
      AstNode{NodeKind::CONST, 0, 0},
      AstNode{NodeKind::CONST, 0, 0},
      AstNode{NodeKind::CONST, 0, 0},
      AstNode{NodeKind::REGION_VAR, 3, 0},
      AstNode{NodeKind::REGION_VAR, 6, 0},
      AstNode{NodeKind::BLOCK_NIL, 0, 0},
  };
  ast.lexical_regions = {
      LexicalRegion{3, 1, {LexicalBinding{0, RType::Int}}},
      LexicalRegion{5, 3, {
          LexicalBinding{1, RType::Int},
          LexicalBinding{2, RType::Int},
          LexicalBinding{3, RType::Int},
      }},
      LexicalRegion{9, 5, {
          LexicalBinding{4, RType::Int},
          LexicalBinding{5, RType::Int},
          LexicalBinding{6, RType::Int},
      }},
  };
  ast.traversal_specs = {
      TraversalSpec{5, TraversalDirection::Forward},
      TraversalSpec{9, TraversalDirection::Reverse},
  };
  return ast;
}

std::string ast_json_with(const std::string& fields) {
  return std::string(
      R"({"version":"ast-prefix","nodes":[],"names":[],"consts":[],)"
      R"("linear_rec_binders":[],"asgp_dc_binders":[],)"
      R"("asgp_dp1d_specs":[],"asgp_dp2d_specs":[])") + fields + "}";
}

bool decode_rejects(const std::string& json) {
  try {
    (void)gagp::cli_detail::decode_ast_json(gagp::cli_detail::JsonParser(json).parse());
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

bool test_round_trip_and_identity() {
  const AstProgram ast = region_program();
  const auto structural = gagp::evo::verify_ast_structure(ast);
  if (!check(structural.ok, "region fixture must be structurally valid")) return false;

  const std::string encoded = gagp::cli_detail::encode_ast_json(ast);
  if (!check(encoded.find("\"lexical_regions\"") != std::string::npos &&
                 encoded.find("\"direction\":\"forward\"") != std::string::npos &&
                 encoded.find("\"direction\":\"reverse\"") != std::string::npos,
             "codec must write lexical and traversal metadata")) return false;
  const AstProgram decoded = gagp::cli_detail::decode_ast_json(
      gagp::cli_detail::JsonParser(encoded).parse());
  if (!check(gagp::cli_detail::encode_ast_json(decoded) == encoded,
             "region AST JSON round trip must be canonical and lossless")) return false;
  if (!check(decoded.lexical_regions.size() == 3 && decoded.traversal_specs.size() == 2 &&
                 decoded.lexical_regions[2].bindings[2].id == 6 &&
                 decoded.lexical_regions[2].bindings[2].type == RType::Int &&
                 decoded.traversal_specs[1].direction == TraversalDirection::Reverse,
             "round trip must retain every region field")) return false;

  const RType public_types[] = {
      RType::Int, RType::Float, RType::Bool, RType::Char,
      RType::String, RType::IntList, RType::FloatList, RType::StringList,
  };
  for (RType type : public_types) {
    AstProgram typed = ast;
    typed.lexical_regions[0].bindings[0].type = type;
    const AstProgram typed_decoded = gagp::cli_detail::decode_ast_json(
        gagp::cli_detail::JsonParser(gagp::cli_detail::encode_ast_json(typed)).parse());
    if (!check(typed_decoded.lexical_regions[0].bindings[0].type == type,
               "every public lexical binding type must round trip exactly")) return false;
  }

  const std::string base_repr = gagp::evo::ast_to_string(ast);
  const std::string base_key = gagp::evo::ast_cache_key(ast);
  AstProgram changed = ast;
  changed.traversal_specs[0].direction = TraversalDirection::Reverse;
  if (!check(gagp::evo::ast_to_string(changed) != base_repr &&
                 gagp::evo::ast_cache_key(changed) != base_key,
             "direction must participate in AST identity")) return false;
  changed = ast;
  changed.lexical_regions[0].bindings[0].type = RType::Float;
  if (!check(gagp::evo::ast_to_string(changed) != base_repr &&
                 gagp::evo::ast_cache_key(changed) != base_key,
             "binding type must participate in AST identity")) return false;
  changed = ast;
  changed.lexical_regions[0].bindings[0].id = 7;
  return check(gagp::evo::ast_to_string(changed) != base_repr &&
                   gagp::evo::ast_cache_key(changed) != base_key,
               "binding id must participate in AST identity");
}

bool test_legacy_output_is_unchanged() {
  const AstProgram ast;
  const std::string expected_json =
      R"({"version":"ast-prefix","nodes":[],"names":[],"consts":[],"linear_rec_binders":[],"asgp_dc_binders":[],"asgp_dp1d_specs":[],"asgp_dp2d_specs":[]})";
  const std::string expected_repr =
      "AstPrefix(;LinearRec=;AsgpDC=;AsgpDP1D=;AsgpDP2D=)";
  const std::string expected_key =
      "AstCache(version:10:ast-prefix;names:0;consts:0;nodes:0;linear_rec:0;"
      "asgp_dc:0;asgp_dp1d:0;asgp_dp2d:0)";
  return check(gagp::cli_detail::encode_ast_json(ast) == expected_json,
               "empty metadata arrays must be omitted from legacy JSON") &&
         check(gagp::evo::ast_to_string(ast) == expected_repr,
               "empty metadata must preserve legacy AST text") &&
         check(gagp::evo::ast_cache_key(ast) == expected_key,
               "empty metadata must preserve legacy cache identity");
}

bool test_runtime_cache_identity_and_compaction() {
  gagp::evo::ProgramGenome genome;
  genome.ast = region_program();
  const std::vector<std::string> inputs = {"xs"};
  const std::string base =
      gagp::evo::grammar::runtime_cache_identity(genome, inputs, 100);

  auto changed = genome;
  changed.ast.traversal_specs[0].direction = TraversalDirection::Reverse;
  if (!check(gagp::evo::grammar::runtime_cache_identity(changed, inputs, 100) != base,
             "runtime cache identity must include traversal direction")) return false;

  changed = genome;
  changed.ast.lexical_regions[0].bindings[0].id = 7;
  changed.ast.nodes[8].i0 = 7;
  if (!check(gagp::evo::verify_ast_structure(changed.ast).ok,
             "binder id/reference identity fixture must remain structurally valid") ||
      !check(gagp::evo::grammar::runtime_cache_identity(changed, inputs, 100) != base,
             "runtime cache identity must include binder ids and references")) return false;

  changed = genome;
  changed.ast.lexical_regions[0].bindings[0].type = RType::Float;
  if (!check(gagp::evo::grammar::runtime_cache_identity(changed, inputs, 100) != base,
             "runtime cache identity must include exact binding types")) return false;

  const gagp::evo::ProgramGenome compacted =
      gagp::evo::repro::compact_genome_tables(genome);
  return check(gagp::evo::ast_cache_key(compacted.ast) ==
                   gagp::evo::ast_cache_key(genome.ast) &&
                   compacted.ast.lexical_regions.size() == 3 &&
                   compacted.ast.traversal_specs.size() == 2,
               "table compaction must retain lexical and traversal metadata");
}

bool test_decode_rejects_invalid_fields() {
  const std::string binding_prefix =
      R"(,"lexical_regions":[{"node_index":0,"body_argument":0,"bindings":[{"id":)";
  const std::string binding_suffix = R"(,"type":"Int"}]}])";
  for (const std::string& id : {"-1", "2147483647", "2147483648", "1.5", "1e100"}) {
    if (!check(decode_rejects(ast_json_with(binding_prefix + id + binding_suffix)),
               "invalid binder id must be rejected: " + id)) return false;
  }

  for (const std::string& node_index : {"-1", "1.5", "1e100"}) {
    const std::string fields =
        R"(,"lexical_regions":[{"node_index":)" + node_index +
        R"(,"body_argument":0,"bindings":[]}])";
    if (!check(decode_rejects(ast_json_with(fields)),
               "invalid node index must be rejected: " + node_index)) return false;
    const std::string traversal_fields =
        R"(,"traversal_specs":[{"node_index":)" + node_index +
        R"(,"direction":"forward"}])";
    if (!check(decode_rejects(ast_json_with(traversal_fields)),
               "invalid traversal node index must be rejected: " + node_index)) return false;
  }
  for (const std::string& slot : {"-1", "1.5", "2147483648", "1e100"}) {
    const std::string fields =
        R"(,"lexical_regions":[{"node_index":0,"body_argument":)" + slot +
        R"(,"bindings":[]}])";
    if (!check(decode_rejects(ast_json_with(fields)),
               "invalid body argument must be rejected: " + slot)) return false;
  }

  for (const std::string& type : {"Any", "Invalid", "int", "Unknown"}) {
    const std::string fields = binding_prefix + "0,\"type\":\"" + type + "\"}]}]";
    if (!check(decode_rejects(ast_json_with(fields)),
               "invalid binding type must be rejected: " + type)) return false;
  }
  for (const std::string& direction : {"sideways", "Forward", "", "Any"}) {
    const std::string fields =
        R"(,"traversal_specs":[{"node_index":0,"direction":")" + direction + R"("}])";
    if (!check(decode_rejects(ast_json_with(fields)),
               "invalid traversal direction must be rejected: " + direction)) return false;
  }

  for (const std::string& fields : {
           R"(,"lexical_regions":null)",
           R"(,"traversal_specs":{})",
           R"(,"lexical_regions":[null])",
           R"(,"lexical_regions":[{"node_index":0,"body_argument":0,"bindings":null}])",
           R"(,"lexical_regions":[{"node_index":0,"body_argument":0,"bindings":[null]}])",
           R"(,"traversal_specs":[{"node_index":0,"direction":null}])",
       }) {
    if (!check(decode_rejects(ast_json_with(fields)),
               "null or wrong metadata structure must be rejected")) return false;
  }
  return true;
}

bool test_encode_and_structural_boundaries() {
  AstProgram invalid = region_program();
  invalid.lexical_regions[0].bindings[0].id = -1;
  try {
    (void)gagp::cli_detail::encode_ast_json(invalid);
    return check(false, "encoder must reject negative binder ids");
  } catch (const std::runtime_error&) {
  }

  invalid = region_program();
  invalid.lexical_regions[0].bindings[0].id = std::numeric_limits<int>::max();
  try {
    (void)gagp::cli_detail::encode_ast_json(invalid);
    return check(false, "encoder must reject the reserved maximum binder id");
  } catch (const std::runtime_error&) {
  }

  invalid = region_program();
  invalid.lexical_regions[0].body_argument = -1;
  try {
    (void)gagp::cli_detail::encode_ast_json(invalid);
    return check(false, "encoder must reject negative body argument slots");
  } catch (const std::runtime_error&) {
  }

  invalid = region_program();
  invalid.lexical_regions[0].bindings[0].type = RType::Any;
  try {
    (void)gagp::cli_detail::encode_ast_json(invalid);
    return check(false, "encoder must reject non-public binding types");
  } catch (const std::runtime_error&) {
  }

  invalid = region_program();
  invalid.traversal_specs[0].direction = static_cast<TraversalDirection>(2);
  try {
    (void)gagp::cli_detail::encode_ast_json(invalid);
    return check(false, "encoder must reject unknown traversal directions");
  } catch (const std::runtime_error&) {
  }

  invalid = region_program();
  invalid.lexical_regions[0].node_index = 4;
  if (!check(!gagp::evo::verify_ast_structure(invalid).ok,
             "structural boundary must reject metadata on a non-region node")) return false;
  const AstProgram decoded = gagp::cli_detail::decode_ast_json(
      gagp::cli_detail::JsonParser(gagp::cli_detail::encode_ast_json(invalid)).parse());
  return check(!gagp::evo::verify_ast_structure(decoded).ok,
               "structural boundary must still reject malformed decoded metadata");
}

}  // namespace

int main() {
  if (!test_round_trip_and_identity()) return 1;
  if (!test_legacy_output_is_unchanged()) return 1;
  if (!test_runtime_cache_identity_and_compaction()) return 1;
  if (!test_decode_rejects_invalid_fields()) return 1;
  if (!test_encode_and_structural_boundaries()) return 1;
  std::cout << "gagp_test_region_codec: OK\n";
  return 0;
}
