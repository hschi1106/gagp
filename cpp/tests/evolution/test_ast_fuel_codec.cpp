#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "gagp/cli/commands.hpp"
#include "gagp/cli/json.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/fuel_events.hpp"

namespace {

using gagp::evo::AstNode;
using gagp::evo::AstProgram;
using gagp::evo::FuelCharge;
using gagp::evo::FuelEvent;
using gagp::evo::NodeFuelSpec;
using gagp::evo::NodeKind;
using gagp::evo::VerifyCode;

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << "\n";
  return condition;
}

AstProgram constant_program() {
  AstProgram ast;
  ast.consts = {gagp::Value::from_int(7)};
  ast.nodes = {
      AstNode{NodeKind::PROGRAM, 0, 0},
      AstNode{NodeKind::BLOCK_CONS, 0, 0},
      AstNode{NodeKind::RETURN, 0, 0},
      AstNode{NodeKind::CONST, 0, 0},
      AstNode{NodeKind::BLOCK_NIL, 0, 0},
  };
  return ast;
}

std::string ast_json_with(const std::string& fields) {
  return std::string(
      R"({"version":"ast-prefix","nodes":[],"names":[],"consts":[],)"
      R"("linear_rec_binders":[],"asgp_dc_binders":[],)"
      R"("asgp_dp1d_specs":[],"asgp_dp2d_specs":[])" + fields + "}");
}

bool decode_rejects(const std::string& json) {
  try {
    (void)gagp::cli_detail::decode_ast_json(gagp::cli_detail::JsonParser(json).parse());
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

bool test_event_names_and_support() {
  const FuelEvent events[] = {
      FuelEvent::Operation, FuelEvent::Bind, FuelEvent::BranchTest,
      FuelEvent::BranchMerge, FuelEvent::StoreSequence, FuelEvent::StoreStart,
      FuelEvent::StoreBegin, FuelEvent::StoreEnd, FuelEvent::CheckStart,
      FuelEvent::CheckBegin, FuelEvent::CheckEnd, FuelEvent::ObserveSequence,
      FuelEvent::ClampBegin, FuelEvent::ClampEnd, FuelEvent::SetBegin,
      FuelEvent::SetEnd, FuelEvent::InitializeState, FuelEvent::InitializeCursor,
      FuelEvent::TestCursor, FuelEvent::ReadElement, FuelEvent::BindElement,
      FuelEvent::ComputeIndex, FuelEvent::UpdateState, FuelEvent::AdvanceCursor,
      FuelEvent::Repeat, FuelEvent::Result,
  };
  for (FuelEvent event : events) {
    FuelEvent parsed = FuelEvent::Operation;
    const char* name = gagp::evo::fuel_event_name(event);
    if (!check(gagp::evo::parse_fuel_event(name, &parsed) && parsed == event,
               std::string("fuel event must round trip: ") + name)) return false;
  }
  FuelEvent parsed = FuelEvent::Operation;
  if (!check(!gagp::evo::parse_fuel_event("branch-test", &parsed) &&
                 !gagp::evo::parse_fuel_event("operation", nullptr),
             "fuel event parser must reject unknown names and null output")) return false;

  return check(gagp::evo::supports_fuel_event(NodeKind::CONST, FuelEvent::Operation) &&
                   gagp::evo::supports_fuel_event(NodeKind::CHECK_LIST, FuelEvent::Operation) &&
                   gagp::evo::supports_fuel_event(NodeKind::CALL_TO_STRING, FuelEvent::Operation) &&
                   !gagp::evo::supports_fuel_event(NodeKind::AND, FuelEvent::Operation),
               "operation support must match scalar and builtin nodes") &&
         check(gagp::evo::supports_fuel_event(NodeKind::LET_REGION, FuelEvent::Bind) &&
                   !gagp::evo::supports_fuel_event(NodeKind::LET_REGION, FuelEvent::Operation) &&
                   gagp::evo::supports_fuel_event(NodeKind::IF_EXPR, FuelEvent::BranchTest) &&
                   gagp::evo::supports_fuel_event(NodeKind::IF_EXPR, FuelEvent::BranchMerge),
               "let and conditional event support must be exact") &&
         check(gagp::evo::supports_fuel_event(NodeKind::TRAVERSE, FuelEvent::SetBegin) &&
                   gagp::evo::supports_fuel_event(NodeKind::TRAVERSE, FuelEvent::Result) &&
                   !gagp::evo::supports_fuel_event(NodeKind::TRAVERSE, FuelEvent::StoreBegin) &&
                   gagp::evo::supports_fuel_event(NodeKind::TRAVERSE_RANGE, FuelEvent::StoreBegin) &&
                   gagp::evo::supports_fuel_event(NodeKind::TRAVERSE_RANGE, FuelEvent::CheckEnd) &&
                   gagp::evo::supports_fuel_event(NodeKind::TRAVERSE_RANGE, FuelEvent::ClampEnd) &&
                   !gagp::evo::supports_fuel_event(NodeKind::TRAVERSE_RANGE, FuelEvent::SetBegin),
               "plain and ranged traversal event sets must differ at bounds handling");
}

bool test_codec_round_trip_and_legacy_omission() {
  AstProgram ast = constant_program();
  ast.fuel_specs = {NodeFuelSpec{
      3,
      {FuelCharge{FuelEvent::Operation,
                  static_cast<std::uint32_t>(std::numeric_limits<int>::max())}},
  }};
  if (!check(gagp::evo::verify_ast_structure(ast).ok,
             "fuel profile fixture must be structurally valid")) return false;

  const std::string encoded = gagp::cli_detail::encode_ast_json(ast);
  if (!check(encoded.find(
          R"("fuel_specs":[{"node_index":3,"charges":[{"event":"operation","cost":2147483647}]}])") !=
                 std::string::npos,
             "codec must emit canonical fuel metadata")) return false;
  const AstProgram decoded = gagp::cli_detail::decode_ast_json(
      gagp::cli_detail::JsonParser(encoded).parse());
  if (!check(decoded.fuel_specs.size() == 1 &&
                 decoded.fuel_specs[0].node_index == 3 &&
                 decoded.fuel_specs[0].charges.size() == 1 &&
                 decoded.fuel_specs[0].charges[0].event == FuelEvent::Operation &&
                 decoded.fuel_specs[0].charges[0].cost ==
                     static_cast<std::uint32_t>(std::numeric_limits<int>::max()),
             "codec must preserve every fuel profile field")) return false;
  if (!check(gagp::cli_detail::encode_ast_json(decoded) == encoded,
             "fuel metadata round trip must be canonical")) return false;

  AstProgram legacy = constant_program();
  return check(gagp::cli_detail::encode_ast_json(legacy).find("\"fuel_specs\"") ==
                   std::string::npos,
               "codec must omit absent fuel metadata");
}

bool test_decoder_rejects_malformed_profiles() {
  const std::string prefix = R"(,"fuel_specs":)";
  return check(decode_rejects(ast_json_with(prefix + R"({}))")),
               "fuel_specs must be an array") &&
         check(decode_rejects(ast_json_with(prefix + R"([0]))")),
               "fuel_specs rows must be objects") &&
         check(decode_rejects(ast_json_with(prefix + R"([{"node_index":-1,"charges":[]}]))")),
               "fuel node indices must be non-negative") &&
         check(decode_rejects(ast_json_with(prefix + R"([{"node_index":0.5,"charges":[]}]))")),
               "fuel node indices must be integral") &&
         check(decode_rejects(ast_json_with(prefix + R"([{"node_index":0}]))")),
               "fuel rows must contain a charges array") &&
         check(decode_rejects(ast_json_with(prefix + R"([{"node_index":0,"charges":{}}]))")),
               "fuel charges must be an array") &&
         check(decode_rejects(ast_json_with(prefix + R"([{"node_index":0,"charges":[0]}]))")),
               "fuel charge rows must be objects") &&
         check(decode_rejects(ast_json_with(
                   prefix + R"([{"node_index":0,"charges":[{"event":0,"cost":1}]}]))")),
               "fuel event must be a string") &&
         check(decode_rejects(ast_json_with(
                   prefix + R"([{"node_index":0,"charges":[{"event":"bogus","cost":1}]}]))")),
               "unknown fuel events must be rejected") &&
         check(decode_rejects(ast_json_with(
                   prefix + R"([{"node_index":0,"charges":[{"event":"operation","cost":-1}]}]))")),
               "negative fuel costs must be rejected") &&
         check(decode_rejects(ast_json_with(
                   prefix + R"([{"node_index":0,"charges":[{"event":"operation","cost":1.5}]}]))")),
               "fractional fuel costs must be rejected") &&
         check(decode_rejects(ast_json_with(
                   prefix + R"([{"node_index":0,"charges":[{"event":"operation","cost":2147483648}]}]))")),
               "fuel costs above INT_MAX must be rejected") &&
         check(decode_rejects(ast_json_with(
                   prefix + R"([{"node_index":0,"charges":[{"event":"operation","cost":1e309}]}]))")),
               "non-finite fuel costs must be rejected");
}

bool test_structural_validation() {
  AstProgram valid = constant_program();
  valid.fuel_specs = {NodeFuelSpec{3, {FuelCharge{FuelEvent::Operation, 0}}}};
  gagp::evo::VerifyOptions limits;
  limits.max_metadata_entries = 2;
  if (!check(gagp::evo::verify_ast_structure(valid, limits).ok,
             "metadata limit must count a fuel row and charge")) return false;
  limits.max_metadata_entries = 1;
  if (!check(gagp::evo::verify_ast_structure(valid, limits).diagnostic.code ==
                 VerifyCode::ResourceLimit,
             "fuel charges must participate in metadata limits")) return false;

  AstProgram invalid = valid;
  invalid.fuel_specs.push_back(invalid.fuel_specs.front());
  if (!check(gagp::evo::verify_ast_structure(invalid).diagnostic.code ==
                 VerifyCode::DuplicateMetadata,
             "fuel profile owners must be unique")) return false;
  invalid = valid;
  invalid.fuel_specs[0].charges.clear();
  if (!check(!gagp::evo::verify_ast_structure(invalid).ok,
             "fuel profiles must not be empty")) return false;
  invalid = valid;
  invalid.fuel_specs[0].charges.push_back(FuelCharge{FuelEvent::Operation, 1});
  if (!check(gagp::evo::verify_ast_structure(invalid).diagnostic.code ==
                 VerifyCode::DuplicateMetadata,
             "fuel events must be unique within a profile")) return false;
  invalid = valid;
  invalid.fuel_specs[0].charges[0].event = FuelEvent::Bind;
  if (!check(gagp::evo::verify_ast_structure(invalid).diagnostic.code ==
                 VerifyCode::MetadataNodeMismatch,
             "fuel events must be supported by their owner")) return false;
  invalid = valid;
  invalid.fuel_specs[0].node_index = invalid.nodes.size();
  if (!check(gagp::evo::verify_ast_structure(invalid).diagnostic.code ==
                 VerifyCode::MetadataNodeMismatch,
             "fuel profile owners must be in bounds")) return false;
  invalid = valid;
  invalid.fuel_specs[0].charges[0].cost =
      static_cast<std::uint32_t>(std::numeric_limits<int>::max()) + 1U;
  return check(gagp::evo::verify_ast_structure(invalid).diagnostic.code ==
                   VerifyCode::InvalidIndexField,
               "fuel costs must not exceed INT_MAX");
}

}  // namespace

int main() {
  bool ok = true;
  ok = test_event_names_and_support() && ok;
  ok = test_codec_round_trip_and_legacy_omission() && ok;
  ok = test_decoder_rejects_malformed_profiles() && ok;
  ok = test_structural_validation() && ok;
  if (!ok) return 1;
  std::cout << "test_ast_fuel_codec: OK\n";
  return 0;
}
