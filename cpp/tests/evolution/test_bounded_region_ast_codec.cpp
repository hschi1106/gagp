#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include "gagp/cli/commands.hpp"
#include "gagp/cli/json.hpp"
#include "gagp/core/region_plan.hpp"
#include "gagp/evolution/ast_program.hpp"
#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/grammar/cache.hpp"

namespace {

using namespace gagp;
using namespace gagp::evo;
using gagp::cli_detail::JsonValue;

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << "\n";
  return condition;
}

template <typename Function>
bool rejects(Function&& function, const std::string& message) {
  try {
    function();
  } catch (const std::exception&) {
    return true;
  }
  std::cerr << "FAIL: " << message << "\n";
  return false;
}

JsonValue number(double value) {
  JsonValue result;
  result.kind = JsonValue::Kind::Number;
  result.number_v = value;
  return result;
}

JsonValue string(std::string value) {
  JsonValue result;
  result.kind = JsonValue::Kind::String;
  result.string_v = std::move(value);
  return result;
}

JsonValue object() {
  JsonValue result;
  result.kind = JsonValue::Kind::Object;
  return result;
}

RegionBound literal(std::int64_t value) {
  RegionBound result;
  result.literal = value;
  return result;
}

RegionBound operand(std::uint32_t index) {
  RegionBound result;
  result.kind = RegionBoundKind::Operand;
  result.operand = index;
  return result;
}

RegionPlan coordinate_plan() {
  RegionPlan plan;
  plan.state_types = {ValueTag::Int};
  plan.result_type = ValueTag::String;
  plan.parameter_types = {ValueTag::Bool, ValueTag::Char};
  plan.preparations = {{ValueTag::Int, RegionPreparationKind::Identity}};
  plan.request_expression_types = {ValueTag::Float};
  plan.bound_operand_count = 2;
  RegionStateTransition transition;
  transition.kind = RegionTransitionKind::CoordinateOffset;
  transition.offset = -1;
  plan.requests = {{{transition}}};
  plan.limits = {17, 5, 9};
  plan.memoized = true;
  plan.duplicate_policy = DuplicatePolicy::Allow;
  plan.coordinate_slots = {0};
  plan.coordinate_rank = {{0, 1}};
  plan.coordinate_domains = {{operand(1), literal(std::numeric_limits<std::int64_t>::max())}};
  plan.coordinate_endpoint = DomainEndpoint::Inclusive;
  validate_region_plan(plan);
  return plan;
}

AstProgram region_ast() {
  AstProgram ast;
  ast.names = {"captured-name"};
  ast.nodes = {{NodeKind::BOUNDED_REGION, 9, 0}};

  BoundedRegionSpec spec;
  spec.node_index = 0;
  spec.plan = coordinate_plan();
  spec.parameters = {
      {RegionCaptureKind::Lexical, 4},
      {RegionCaptureKind::Name, 0},
  };
  for (std::uint32_t argument = 3; argument <= 8; ++argument) {
    RegionAstPhase phase;
    phase.argument = argument;
    phase.bindings = {
        {{RegionSlotBank::State, 0}, static_cast<int>(10 + argument)},
        {{RegionSlotBank::Parameter, 1}, static_cast<int>(20 + argument)},
    };
    spec.phases.push_back(std::move(phase));
  }
  spec.phases.back().bindings.push_back(
      {{RegionSlotBank::Result, 0}, 40});
  ast.bounded_region_specs.push_back(std::move(spec));
  return ast;
}

bool same_spec(const BoundedRegionSpec& lhs, const BoundedRegionSpec& rhs) {
  if (lhs.node_index != rhs.node_index ||
      lhs.parameters.size() != rhs.parameters.size() ||
      lhs.phases.size() != rhs.phases.size()) return false;
  for (std::size_t i = 0; i < lhs.parameters.size(); ++i) {
    if (lhs.parameters[i].kind != rhs.parameters[i].kind ||
        lhs.parameters[i].index != rhs.parameters[i].index) return false;
  }
  for (std::size_t i = 0; i < lhs.phases.size(); ++i) {
    if (lhs.phases[i].argument != rhs.phases[i].argument ||
        lhs.phases[i].bindings.size() != rhs.phases[i].bindings.size()) return false;
    for (std::size_t j = 0; j < lhs.phases[i].bindings.size(); ++j) {
      const auto& a = lhs.phases[i].bindings[j];
      const auto& b = rhs.phases[i].bindings[j];
      if (a.source.bank != b.source.bank || a.source.slot != b.source.slot ||
          a.binder_id != b.binder_id) return false;
    }
  }
  AstProgram left;
  AstProgram right;
  left.bounded_region_specs = {lhs};
  right.bounded_region_specs = {rhs};
  return ast_cache_key(left) == ast_cache_key(right);
}

bool test_round_trip_and_legacy_omission() {
  const AstProgram ast = region_ast();
  const std::string encoded = gagp::cli_detail::encode_ast_json(ast);
  if (!check(encoded.find("\"bounded_region_specs\":[") != std::string::npos &&
                 encoded.find("\"kind\":\"lexical\"") != std::string::npos &&
                 encoded.find("\"bank\":\"result\"") != std::string::npos &&
                 encoded.find("\"literal\":\"9223372036854775807\"") !=
                     std::string::npos,
             "bounded-region JSON must use canonical names and exact int64 strings")) return false;

  const AstProgram decoded = gagp::cli_detail::decode_ast_json(
      gagp::cli_detail::JsonParser(encoded).parse());
  if (!check(decoded.bounded_region_specs.size() == 1 &&
                 same_spec(decoded.bounded_region_specs[0],
                           ast.bounded_region_specs[0]),
             "AST codec must preserve the complete bounded-region side table")) return false;
  if (!check(gagp::cli_detail::encode_ast_json(decoded) == encoded,
             "bounded-region AST JSON must round trip canonically")) return false;

  AstProgram legacy;
  return check(gagp::cli_detail::encode_ast_json(legacy).find(
                   "bounded_region_specs") == std::string::npos,
               "empty bounded-region metadata must preserve legacy JSON bytes");
}

bool test_text_and_runtime_cache_identity() {
  const AstProgram ast = region_ast();
  const std::string text = ast_to_string(ast);
  const std::string key = ast_cache_key(ast);
  ProgramGenome genome;
  genome.ast = ast;
  const std::string runtime =
      grammar::runtime_cache_identity(genome, {"input"}, 100);

  const auto differs = [&](AstProgram changed, const std::string& field) {
    ProgramGenome changed_genome;
    changed_genome.ast = changed;
    return check(ast_to_string(changed) != text && ast_cache_key(changed) != key &&
                     grammar::runtime_cache_identity(changed_genome, {"input"}, 100) != runtime,
                 field + " must participate in AST and runtime-cache identity");
  };

  AstProgram changed = ast;
  changed.bounded_region_specs[0].plan.coordinate_domains[0].upper.literal--;
  if (!differs(changed, "region plan")) return false;
  changed = ast;
  changed.bounded_region_specs[0].parameters[0].kind = RegionCaptureKind::Name;
  if (!differs(changed, "capture kind")) return false;
  changed = ast;
  changed.bounded_region_specs[0].parameters[0].index++;
  if (!differs(changed, "capture index")) return false;
  changed = ast;
  changed.bounded_region_specs[0].phases[0].argument++;
  if (!differs(changed, "phase argument")) return false;
  changed = ast;
  changed.bounded_region_specs[0].phases[0].bindings[0].source.bank =
      RegionSlotBank::Measure;
  if (!differs(changed, "binding bank")) return false;
  changed = ast;
  changed.bounded_region_specs[0].phases[0].bindings[0].source.slot++;
  if (!differs(changed, "binding slot")) return false;
  changed = ast;
  changed.bounded_region_specs[0].phases[0].bindings[0].binder_id++;
  return differs(changed, "binding id");
}

bool test_decoder_caps_and_malformed_rows() {
  const std::string encoded = gagp::cli_detail::encode_ast_json(region_ast());
  const auto rejects_changed = [&](auto mutate, const std::string& message) {
    JsonValue root = gagp::cli_detail::JsonParser(encoded).parse();
    mutate(root.object_v.at("bounded_region_specs").array_v[0]);
    return rejects([&] { (void)gagp::cli_detail::decode_ast_json(root); }, message);
  };

  if (!rejects_changed([](JsonValue& row) { row.object_v.erase("plan"); },
                       "missing plan must be rejected")) return false;
  if (!rejects_changed([](JsonValue& row) { row.object_v["extra"] = number(0); },
                       "unknown row fields must be rejected")) return false;
  if (!rejects_changed([](JsonValue& row) {
        row.object_v.at("parameters").array_v.push_back(object());
      }, "parameter count must be capped before allocation")) return false;
  if (!rejects_changed([](JsonValue& row) {
        row.object_v.at("phases").array_v.pop_back();
      }, "phase count must match the plan")) return false;
  if (!rejects_changed([](JsonValue& row) {
        row.object_v.at("parameters").array_v[0].object_v["kind"] = string("global");
      }, "unknown capture kind must be rejected")) return false;
  if (!rejects_changed([](JsonValue& row) {
        row.object_v.at("parameters").array_v[0].object_v["extra"] = number(0);
      }, "unknown capture fields must be rejected")) return false;
  if (!rejects_changed([](JsonValue& row) {
        row.object_v.at("parameters").array_v[0].object_v["index"] = number(-1);
      }, "negative capture index must be rejected")) return false;
  if (!rejects_changed([](JsonValue& row) {
        auto& binding = row.object_v.at("phases").array_v[0]
                            .object_v.at("bindings").array_v[0];
        binding.object_v["bank"] = string("local");
      }, "unknown binding bank must be rejected")) return false;
  if (!rejects_changed([](JsonValue& row) {
        row.object_v.at("phases").array_v[0].object_v["extra"] = number(0);
      }, "unknown phase fields must be rejected")) return false;
  if (!rejects_changed([](JsonValue& row) {
        auto& binding = row.object_v.at("phases").array_v[0]
                            .object_v.at("bindings").array_v[0];
        binding.object_v["extra"] = number(0);
      }, "unknown binding fields must be rejected")) return false;
  if (!rejects_changed([](JsonValue& row) {
        auto& binding = row.object_v.at("phases").array_v[0]
                            .object_v.at("bindings").array_v[0];
        binding.object_v["binder_id"] = number(2147483647.0);
      }, "reserved binder id must be rejected")) return false;
  return rejects_changed([](JsonValue& row) {
    auto& bindings = row.object_v.at("phases").array_v[0]
                         .object_v.at("bindings").array_v;
    while (bindings.size() <= 49) bindings.push_back(bindings.front());
  }, "phase bindings must be capped before allocation");
}

}  // namespace

int main() {
  bool ok = true;
  ok = test_round_trip_and_legacy_omission() && ok;
  ok = test_text_and_runtime_cache_identity() && ok;
  ok = test_decoder_caps_and_malformed_rows() && ok;
  if (!ok) return 1;
  std::cout << "bounded-region AST codec and identities passed\n";
  return 0;
}
