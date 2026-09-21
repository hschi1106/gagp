#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/evolution/ast_program.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/cli/json.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/crossover.hpp"
#include "gagp/evolution/grammar/compiled.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/evolution/mutation.hpp"
#include "gagp/evolution/node_descriptor.hpp"

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;

namespace {

const std::filesystem::path kRoot = GAGP_REPOSITORY_ROOT;

void check(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

enum class Family { Linear, Dc, Dp1, Dp2 };

struct Loaded {
  ResolvedDefinition resolved;
  std::shared_ptr<const CompiledGrammar> grammar;
};

Loaded load(const std::filesystem::path& relative) {
  Loaded out;
  out.resolved = load_definition(kRoot / relative);
  out.grammar = std::make_shared<const CompiledGrammar>(
      compile_grammar(out.resolved));
  out.grammar->require_executable();
  const auto roundtrip = parse_definition(out.resolved.canonical);
  check(roundtrip.canonical == out.resolved.canonical &&
            roundtrip.content_hash == out.resolved.content_hash,
        relative.string() + ": resolved export did not round-trip");
  return out;
}

std::vector<InputSpec> inputs(const CompiledGrammar& grammar) {
  std::vector<InputSpec> result;
  for (const auto& input : grammar.inputs())
    result.push_back({input.name, input.type});
  return result;
}

const BoundedRegionSpec& only_region(const ProgramGenome& genome,
                                     const std::string& label) {
  check(genome.ast.bounded_region_specs.size() == 1,
        label + ": expected exactly one bounded region");
  return genome.ast.bounded_region_specs.front();
}

const NodeFuelSpec* fuel_at(const AstProgram& ast, std::size_t node) {
  for (const auto& profile : ast.fuel_specs)
    if (profile.node_index == node) return &profile;
  return nullptr;
}

std::map<FuelEvent, std::uint32_t> charges(const NodeFuelSpec& profile) {
  std::map<FuelEvent, std::uint32_t> result;
  for (const auto& charge : profile.charges)
    result.emplace(charge.event, charge.cost);
  return result;
}

std::vector<std::size_t> child_roots(const AstProgram& ast,
                                     const VerifiedAst& verified,
                                     std::size_t owner) {
  std::vector<std::size_t> result;
  auto child = owner + 1;
  for (int i = 0; i < node_prefix_arity(ast.nodes.at(owner)); ++i) {
    result.push_back(child);
    child = verified.subtree_end.at(child);
  }
  return result;
}

void check_dp1_plan(const RegionPlan& plan) {
  check(plan.memoized && plan.progress == RegionProgressKind::Coordinates &&
            plan.duplicate_policy == DuplicatePolicy::Allow &&
            plan.coordinate_rank.size() == 1 &&
            plan.coordinate_slots == std::vector<std::uint32_t>{0} &&
            plan.requests.size() >= 1 && plan.requests.size() <= 3,
        "DP1D plan contract changed");
  const int direction = plan.coordinate_rank[0].direction;
  check(direction == 1 || direction == -1, "DP1D rank direction is invalid");
  for (std::size_t request = 0; request < plan.requests.size(); ++request) {
    const auto& states = plan.requests[request].states;
    check(states.size() == 1 &&
              states[0].kind == RegionTransitionKind::CoordinateOffset &&
              states[0].source_state == 0 &&
              states[0].offset == (direction == 1 ?
                  -static_cast<std::int64_t>(request + 1) :
                   static_cast<std::int64_t>(request + 1)),
          "DP1D dependency offsets/order changed");
  }
}

std::string dp1_key(const RegionPlan& plan) {
  check_dp1_plan(plan);
  return std::string(plan.coordinate_rank[0].direction == 1 ? "backward" :
                                                           "forward") +
      std::to_string(plan.requests.size());
}

void check_dp2_plan(const RegionPlan& plan) {
  check(plan.memoized && plan.progress == RegionProgressKind::Coordinates &&
            plan.duplicate_policy == DuplicatePolicy::Reject &&
            plan.coordinate_rank.size() == 2 &&
            plan.coordinate_slots == std::vector<std::uint32_t>({0, 1}) &&
            plan.coordinate_rank[0].direction ==
                plan.coordinate_rank[1].direction,
        "DP2D plan contract changed");
  const int direction = plan.coordinate_rank[0].direction;
  const std::int64_t sign = direction == 1 ? -1 : 1;
  std::vector<std::pair<std::int64_t, std::int64_t>> offsets;
  for (const auto& request : plan.requests) {
    check(request.states.size() == 2 &&
              request.states[0].kind ==
                  RegionTransitionKind::CoordinateOffset &&
              request.states[1].kind ==
                  RegionTransitionKind::CoordinateOffset,
          "DP2D request shape changed");
    offsets.push_back({request.states[0].offset, request.states[1].offset});
  }
  const std::vector<std::pair<std::int64_t, std::int64_t>> diagonal{
      {sign, sign}};
  const std::vector<std::pair<std::int64_t, std::int64_t>> cross{
      {sign, 0}, {0, sign}};
  const std::vector<std::pair<std::int64_t, std::int64_t>> neighborhood{
      {sign, 0}, {0, sign}, {sign, sign}};
  check(offsets == diagonal || offsets == cross || offsets == neighborhood,
        "DP2D dependency offsets/order changed");
}

std::string dp2_key(const RegionPlan& plan) {
  check_dp2_plan(plan);
  const auto direction = plan.coordinate_rank[0].direction == 1 ?
      "backward" : "forward";
  const auto shape = plan.requests.size() == 1 ? "diagonal" :
      (plan.requests.size() == 2 ? "cross" : "neighborhood");
  return std::string(shape) + "_" + direction;
}

void check_family(const ProgramGenome& genome, Family family,
                  const std::string& label) {
  if (family == Family::Linear) {
    check(genome.ast.bounded_region_specs.empty(),
          label + ": LinearRec package emitted a bounded region");
    check(std::count_if(genome.ast.nodes.begin(), genome.ast.nodes.end(),
              [](const AstNode& node) {
                return node.kind == NodeKind::TRAVERSE_RANGE;
              }) == 1,
          label + ": LinearRec package lost its single ordered traversal");
    return;
  }
  const auto& plan = only_region(genome, label).plan;
  if (family == Family::Dc) {
    check(!plan.memoized &&
              plan.progress == RegionProgressKind::SequenceWindows &&
              plan.duplicate_policy == DuplicatePolicy::Reject &&
              plan.preparations.size() == 1 && plan.requests.size() == 2,
          label + ": DC plan contract changed");
  } else if (family == Family::Dp1) {
    check_dp1_plan(plan);
  } else {
    check_dp2_plan(plan);
  }
}

ProgramGenome generate_checked(const CompiledGrammar& grammar,
                               std::uint64_t seed,
                               const std::string& label) {
  auto generated = generate_derivation(grammar, seed).genome;
  require_membership(grammar, generated);
  return generated;
}

void smoke_packages(const std::vector<std::pair<std::string, Loaded>>& all) {
  for (const auto& item : all) {
    for (std::uint64_t seed = 0; seed < 8; ++seed)
      (void)generate_checked(*item.second.grammar, seed, item.first);
  }
}

void check_relocation(const std::string& root_name,
                      const std::string& package_name,
                      const Loaded& original) {
  const auto unique = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto temp = std::filesystem::temp_directory_path() /
      ("gagp-package-relocation-" + unique);
  std::filesystem::create_directories(temp / "compat");
  std::filesystem::create_directories(temp / "packages");
  const auto cleanup = [&] {
    std::error_code error;
    std::filesystem::remove_all(temp, error);
  };
  try {
    const auto source_root = kRoot / "configs/grammar/compat" / root_name;
    const auto source_package =
        kRoot / "configs/grammar/packages" / package_name;
    std::ifstream root_in(source_root), package_in(source_package);
    check(static_cast<bool>(root_in) && static_cast<bool>(package_in),
          "failed to read relocation fixture");
    std::string root_text((std::istreambuf_iterator<char>(root_in)), {});
    const std::string package_text((std::istreambuf_iterator<char>(package_in)), {});
    const std::string renamed = "renamed_" + package_name;
    const auto position = root_text.find("../packages/" + package_name);
    check(position != std::string::npos,
          "compat root did not contain its expected package import");
    root_text.replace(position, std::string("../packages/" + package_name).size(),
                      "../packages/" + renamed);
    std::ofstream(temp / "compat" / root_name) << root_text;
    std::ofstream(temp / "packages" / renamed) << package_text;
    const auto relocated_definition =
        load_definition(temp / "compat" / root_name);
    check(relocated_definition.canonical == original.resolved.canonical &&
              relocated_definition.content_hash == original.resolved.content_hash,
          root_name + ": path/package filename affected canonical identity");
    const auto relocated = compile_grammar(relocated_definition);
    for (std::uint64_t seed = 0; seed < 16; ++seed) {
      const auto first = generate_derivation(*original.grammar, seed).genome;
      const auto second = generate_derivation(relocated, seed).genome;
      check(ast_cache_key(first.ast) == ast_cache_key(second.ast),
            root_name + ": relocated package changed deterministic generation");
      require_membership(relocated, second);
    }
  } catch (...) {
    cleanup();
    throw;
  }
  cleanup();
}

void cover_dp_patterns(const Loaded& dp1, const Loaded& dp2) {
  std::set<std::string> one_dimensional, two_dimensional;
  for (std::uint64_t seed = 0; seed < 512 &&
       (one_dimensional.size() < 6 || two_dimensional.size() < 6); ++seed) {
    if (one_dimensional.size() < 6) {
      const auto genome = generate_checked(*dp1.grammar, seed, "DP1D");
      one_dimensional.insert(dp1_key(only_region(genome, "DP1D").plan));
    }
    if (two_dimensional.size() < 6) {
      const auto genome = generate_checked(*dp2.grammar, seed, "DP2D");
      two_dimensional.insert(dp2_key(only_region(genome, "DP2D").plan));
    }
  }
  check(one_dimensional == std::set<std::string>({
            "backward1", "backward2", "backward3",
            "forward1", "forward2", "forward3"}),
        "deterministic seeds did not cover all six DP1D patterns");
  check(two_dimensional == std::set<std::string>({
            "cross_backward", "cross_forward", "diagonal_backward",
            "diagonal_forward", "neighborhood_backward",
            "neighborhood_forward"}),
        "deterministic seeds did not cover all six DP2D patterns");
}

void check_linear_contract(const Loaded& loaded) {
  const auto genome = generate_checked(*loaded.grammar, 11, "LinearRec");
  check_family(genome, Family::Linear, "LinearRec");
  std::size_t lets = 0, traversals = 0, bind_zero = 0, bind_one = 0;
  std::size_t checked_int = 0, checked_list = 0, branches = 0;
  for (std::size_t i = 0; i < genome.ast.nodes.size(); ++i) {
    if (genome.ast.nodes[i].kind == NodeKind::LET_REGION) {
      ++lets;
      const auto* profile = fuel_at(genome.ast, i);
      check(profile && profile->charges.size() == 1 &&
                profile->charges[0].event == FuelEvent::Bind,
            "LinearRec LET owner lost its authored bind profile");
      bind_zero += profile->charges[0].cost == 0;
      bind_one += profile->charges[0].cost == 1;
    }
    if (genome.ast.nodes[i].kind == NodeKind::TRAVERSE_RANGE) {
      ++traversals;
      const auto* profile = fuel_at(genome.ast, i);
      check(profile && profile->charges.size() == 20,
            "LinearRec traversal lost its complete authored profile");
      const auto map = charges(*profile);
      check(map.at(FuelEvent::TestCursor) == 4 &&
                map.at(FuelEvent::ReadElement) == 7 &&
                map.at(FuelEvent::ComputeIndex) == 4 &&
                map.at(FuelEvent::StoreSequence) == 0,
            "LinearRec traversal fuel charges changed");
    }
    if (genome.ast.nodes[i].kind == NodeKind::CHECK_INT ||
        genome.ast.nodes[i].kind == NodeKind::CHECK_LIST) {
      const auto* profile = fuel_at(genome.ast, i);
      check(profile && charges(*profile) ==
              std::map<FuelEvent, std::uint32_t>{{FuelEvent::Operation, 1}},
            "LinearRec checked owner lost its exact operation profile");
      checked_int += genome.ast.nodes[i].kind == NodeKind::CHECK_INT;
      checked_list += genome.ast.nodes[i].kind == NodeKind::CHECK_LIST;
    }
    if (genome.ast.nodes[i].kind == NodeKind::IF_EXPR) {
      const auto* profile = fuel_at(genome.ast, i);
      check(profile && charges(*profile) ==
              std::map<FuelEvent, std::uint32_t>{
                  {FuelEvent::BranchTest, 1}, {FuelEvent::BranchMerge, 1}},
            "LinearRec conditional lost its exact branch profile");
      ++branches;
    }
  }
  check(lets == 7 && bind_zero == 1 && bind_one == 6 &&
            checked_int == 1 && checked_list == 1 && branches == 1 &&
            traversals == 1 &&
            genome.ast.lexical_regions.size() == 8,
        "LinearRec administrative shape/scopes changed");
  for (const auto& region : genome.ast.lexical_regions) {
    const auto kind = genome.ast.nodes.at(region.node_index).kind;
    check((kind == NodeKind::LET_REGION && region.bindings.size() == 1) ||
              (kind == NodeKind::TRAVERSE_RANGE &&
               region.bindings.size() == 3),
          "LinearRec lexical scope layout changed");
  }
}

void check_dc_contract(const Loaded& loaded) {
  const auto genome = generate_checked(*loaded.grammar, 19, "DC");
  check_family(genome, Family::Dc, "DC");
  const auto& spec = only_region(genome, "DC");
  const std::vector<std::size_t> binding_counts{0, 3, 1, 2, 2};
  check(spec.phases.size() == binding_counts.size(),
        "DC phase count changed");
  for (std::size_t i = 0; i < binding_counts.size(); ++i)
    check(spec.phases[i].bindings.size() == binding_counts[i],
          "DC explicit phase scope changed");
  const std::vector<std::vector<RegionValueSlot>> expected_sources{
      {},
      {{RegionSlotBank::State, 0}, {RegionSlotBank::Measure, 0},
       {RegionSlotBank::State, 1}},
      {{RegionSlotBank::Measure, 0}},
      {{RegionSlotBank::State, 1}, {RegionSlotBank::Prepared, 0}},
      {{RegionSlotBank::Result, 0}, {RegionSlotBank::Result, 1}}};
  for (std::size_t phase = 0; phase < expected_sources.size(); ++phase)
    for (std::size_t binding = 0;
         binding < expected_sources[phase].size(); ++binding) {
      const auto& actual = spec.phases[phase].bindings[binding].source;
      const auto& expected = expected_sources[phase][binding];
      check(actual.bank == expected.bank && actual.slot == expected.slot,
            "DC phase source bank/slot changed");
    }
  check(fuel_at(genome.ast, spec.node_index) == nullptr,
        "DC bounded owner conflated node fuel with RegionPlan entry_fuel");
  const auto verified = verify_ast(genome.ast, inputs(*loaded.grammar));
  check(static_cast<bool>(verified), "DC package failed native verification");
  const auto children = child_roots(genome.ast, verified.verified,
                                    spec.node_index);
  const auto operation = [&](std::size_t node, std::uint32_t cost) {
    const auto* profile = fuel_at(genome.ast, node);
    return profile && charges(*profile) ==
        std::map<FuelEvent, std::uint32_t>{{FuelEvent::Operation, cost}};
  };
  check(children.size() == 7 &&
            genome.ast.nodes[children[0]].kind == NodeKind::VAR &&
            operation(children[0], 1) && operation(children[1], 0) &&
            operation(children[2], 0) &&
            genome.ast.nodes[children[5]].kind == NodeKind::ADD &&
            operation(children[5], 0),
        "DC initial/base/request administrative profiles changed");
  const auto request_children = child_roots(genome.ast, verified.verified,
                                            children[5]);
  check(request_children.size() == 2 &&
            operation(request_children[0], 0) &&
            operation(request_children[1], 0) &&
            genome.ast.fuel_specs.size() == 6,
        "DC request-offset operands lost their exact zero-cost profiles");
}

void check_dp_fuel_contracts(const Loaded& dp1, const Loaded& dp2) {
  {
    const auto genome = generate_checked(*dp1.grammar, 23, "DP1D fuel");
    check(genome.ast.fuel_specs.size() == 6,
          "DP1D administrative fuel profile count changed");
    std::size_t zero_operations = 0;
    std::size_t checked_operations = 0;
    for (const auto& profile : genome.ast.fuel_specs) {
      const auto profile_charges = charges(profile);
      check(profile_charges.size() == 1 &&
                profile_charges.count(FuelEvent::Operation) == 1,
            "DP1D administrative owner uses an unexpected fuel event");
      if (profile_charges.at(FuelEvent::Operation) == 0)
        ++zero_operations;
      else if (profile_charges.at(FuelEvent::Operation) == 1 &&
               genome.ast.nodes.at(profile.node_index).kind == NodeKind::CHECK_INT)
        ++checked_operations;
    }
    check(zero_operations == 5 && checked_operations == 1,
          "DP1D checked-state or zero-cost administrative schedule changed");
  }
  {
    const auto genome = generate_checked(*dp2.grammar, 29, "DP2D fuel");
    check(genome.ast.fuel_specs.size() == 16,
          "DP2D administrative fuel profile count changed");
    std::size_t zero_binds = 0;
    std::size_t zero_operations = 0;
    std::size_t checked_operations = 0;
    std::size_t zero_branches = 0;
    for (const auto& profile : genome.ast.fuel_specs) {
      const auto profile_charges = charges(profile);
      if (profile_charges.count(FuelEvent::Bind)) {
        zero_binds += profile_charges.size() == 1 &&
                      profile_charges.at(FuelEvent::Bind) == 0;
      } else if (profile_charges.count(FuelEvent::BranchTest)) {
        zero_branches += profile_charges.size() == 2 &&
                         profile_charges.at(FuelEvent::BranchTest) == 0 &&
                         profile_charges.at(FuelEvent::BranchMerge) == 0;
      } else {
        check(profile_charges.size() == 1 &&
                  profile_charges.count(FuelEvent::Operation) == 1,
              "DP2D administrative owner uses an unexpected fuel event");
        if (profile_charges.at(FuelEvent::Operation) == 0)
          ++zero_operations;
        else if (profile_charges.at(FuelEvent::Operation) == 1 &&
                 genome.ast.nodes.at(profile.node_index).kind == NodeKind::CHECK_INT)
          ++checked_operations;
      }
    }
    check(zero_binds == 2 && zero_operations == 12 &&
              checked_operations == 1 && zero_branches == 1,
          "DP2D evaluation-once, checked-coordinate, or predicate fuel schedule changed");
  }
}

void exercise_variation(const Loaded& loaded, Family family,
                        const std::string& label) {
  VariationContext context(loaded.grammar);
  std::vector<ProgramGenome> population;
  for (std::uint64_t seed = 0; seed < 4; ++seed)
    population.push_back(generate_checked(*loaded.grammar, 100 + seed, label));
  for (std::uint64_t round = 0; round < 12; ++round) {
    for (std::size_t i = 0; i < population.size(); ++i) {
      population[i] = mutate(population[i], 1000 + round * 17 + i,
                             context, 0.8);
      require_membership(*loaded.grammar, population[i]);
      check_family(population[i], family, label + " mutation");
    }
    const auto children = crossover(population[round % population.size()],
        population[(round + 1) % population.size()], 5000 + round, context);
    require_membership(*loaded.grammar, children.first);
    require_membership(*loaded.grammar, children.second);
    check_family(children.first, family, label + " crossover");
    check_family(children.second, family, label + " crossover");
    population[round % population.size()] = children.first;
    population[(round + 1) % population.size()] = children.second;
  }
}

using cli_detail::JsonValue;

const JsonValue& matrix_field(const JsonValue& object, const char* name) {
  return cli_detail::require_object_field(object, name);
}

const std::vector<JsonValue>& matrix_array(const JsonValue& value,
                                           const std::string& label) {
  check(value.kind == JsonValue::Kind::Array, label + " must be an array");
  return value.array_v;
}

std::string matrix_string(const JsonValue& value, const std::string& label) {
  check(value.kind == JsonValue::Kind::String, label + " must be a string");
  return value.string_v;
}

int matrix_int(const JsonValue& object, const char* name) {
  return cli_detail::require_int(matrix_field(object, name), name);
}

std::set<std::string> matrix_string_set(const JsonValue& value,
                                        const std::string& label) {
  std::set<std::string> result;
  const auto& values = matrix_array(value, label);
  for (const auto& item : values)
    result.insert(matrix_string(item, label + " item"));
  check(result.size() == values.size(), label + " contains duplicates");
  return result;
}

std::string singleton_constant(const std::string& type) {
  if (type == "Int") return R"({"constant":{"type":"Int","values":["0"]}})";
  if (type == "Float") return R"({"constant":{"type":"Float","values":[0.0]}})";
  if (type == "Bool") return R"({"constant":{"type":"Bool","values":[false]}})";
  if (type == "Char") return R"({"constant":{"type":"Char","values":["a"]}})";
  if (type == "String") return R"({"constant":{"type":"String","values":[""]}})";
  if (type == "IntList") return R"({"constant":{"type":"IntList","values":[[]]}})";
  if (type == "FloatList") return R"({"constant":{"type":"FloatList","values":[[]]}})";
  if (type == "StringList") return R"({"constant":{"type":"StringList","values":[[]]}})";
  throw std::runtime_error("matrix contains unsupported exact type " + type);
}

std::string sequence_element_type(const std::string& type) {
  if (type == "IntList") return "Int";
  if (type == "FloatList") return "Float";
  if (type == "StringList") return "String";
  throw std::runtime_error("LinearRec matrix source is not a typed list: " + type);
}

std::string linear_matrix_root(const std::string& source,
                               const std::string& result,
                               const std::string& resource,
                               const std::string& package) {
  const std::string prefix = "Matrix.LinearRec." + source + "." + result;
  std::ostringstream out;
  out << R"({"format_version":"grammar-definition-v2","imports":[")"
      << package << R"("],"entry":{"nonterminal":")" << prefix
      << R"(.Main","type":")" << result
      << R"("},"inputs":[{"name":"source","type":")" << source
      << R"("},{"name":"start","type":"Int"}],)"
         R"("search_limits":{"max_nodes":128,"max_depth":40},)"
         R"("execution_limits":{"fuel":20000},"nonterminals":[)"
      << R"({"id":")" << prefix << R"(.Main","type":")" << result
      << R"(","scope":[],"alternatives":[{"id":"only","weight":1,)"
         R"("expression":{"template":")" << resource
      << R"(","holes":{"source":{"input":"source"},"start":{"input":"start"},"empty":)"
      << singleton_constant(result) << R"(,"step":{"ref":")" << prefix
      << R"(.Step"},"last":{"ref":")" << prefix
      << R"(.Last"}}}}]},)"
      << R"({"id":")" << prefix << R"(.Step","type":")" << result
      << R"(","scope":[{"name":"element","type":")"
      << sequence_element_type(source)
      << R"("},{"name":"accumulator","type":")" << result
      << R"("},{"name":"index","type":"Int"}],)"
         R"("alternatives":[{"id":"keep","weight":1,"expression":{"bound":"accumulator"}}]},)"
      << R"({"id":")" << prefix << R"(.Last","type":")" << result
      << R"(","scope":[{"name":"last_element","type":")"
      << sequence_element_type(source)
      << R"("},{"name":"last_index","type":"Int"}],)"
         R"("alternatives":[{"id":"seed","weight":1,"expression":)"
      << singleton_constant(result) << R"(}]}]})";
  return out.str();
}

std::string dc_matrix_root(const std::string& source,
                           const std::string& result,
                           const std::string& resource,
                           const std::string& package) {
  const std::string prefix = "Matrix.DC." + source + "." + result;
  std::ostringstream out;
  out << R"({"format_version":"grammar-definition-v2","imports":[")"
      << package << R"("],"entry":{"nonterminal":")" << prefix
      << R"(.Main","type":")" << result
      << R"("},"inputs":[{"name":"source","type":")" << source
      << R"("}],"search_limits":{"max_nodes":128,"max_depth":40},)"
         R"("execution_limits":{"fuel":20000},"nonterminals":[)"
      << R"({"id":")" << prefix << R"(.Main","type":")" << result
      << R"(","scope":[],"alternatives":[{"id":"only","weight":1,)"
         R"("expression":{"template":")" << resource
      << R"(","holes":{"source":{"input":"source"},"solve":{"ref":")"
      << prefix << R"(.Solve"},"divide":{"ref":")" << prefix
      << R"(.Divide"},"combine":{"ref":")" << prefix
      << R"(.Combine"}}}}]},)"
      << R"({"id":")" << prefix << R"(.Solve","type":")" << result
      << R"(","scope":[{"name":"xs","type":")" << source
      << R"("},{"name":"n","type":"Int"},{"name":"lo","type":"Int"}],)"
         R"("alternatives":[{"id":"base","weight":1,"expression":)"
      << singleton_constant(result) << R"(}]},)"
      << R"({"id":")" << prefix
      << R"(.Divide","type":"Int","scope":[{"name":"n","type":"Int"}],)"
         R"("alternatives":[{"id":"zero","weight":1,"expression":)"
      << singleton_constant("Int") << R"(}]},)"
      << R"({"id":")" << prefix << R"(.Combine","type":")" << result
      << R"(","scope":[{"name":"left","type":")" << result
      << R"("},{"name":"right","type":")" << result
      << R"("}],"alternatives":[{"id":"left","weight":1,)"
         R"("expression":{"bound":"left"}}]}]})";
  return out.str();
}

std::string dp_matrix_root(const std::string& family,
                           const std::string& result,
                           const std::string& resource,
                           const std::string& package) {
  std::ostringstream out;
  out << R"({"format_version":"grammar-definition-v2","imports":[")"
      << package << R"("],"entry":{"nonterminal":")" << resource
      << R"(","type":")" << result << R"("},"inputs":[)";
  if (family == "dp1d") {
    out << R"({"name":"state","type":"Int"})";
  } else {
    out << R"({"name":"row","type":"Int"},{"name":"column","type":"Int"})";
  }
  out << R"(],"search_limits":{"max_nodes":128,"max_depth":40},)"
         R"("execution_limits":{"fuel":20000}})";
  return out.str();
}

class MatrixDirectory {
 public:
  MatrixDirectory() {
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    root = std::filesystem::temp_directory_path() /
        ("gagp-package-matrix-" + unique);
    std::filesystem::create_directories(root / "compat");
    std::filesystem::create_directories(root / "packages");
  }
  ~MatrixDirectory() {
    std::error_code error;
    std::filesystem::remove_all(root, error);
  }
  std::filesystem::path root;
};

void compile_matrix_variant(const MatrixDirectory& directory,
                            const std::string& family,
                            const std::string& source,
                            const std::string& result,
                            const std::string& resource,
                            const std::string& package,
                            std::uint64_t seed,
                            const std::set<std::string>& expected_patterns = {}) {
  std::string definition;
  if (family == "linear_rec")
    definition = linear_matrix_root(source, result, resource, package);
  else if (family == "dc")
    definition = dc_matrix_root(source, result, resource, package);
  else
    definition = dp_matrix_root(family, result, resource, package);

  const auto root_path = directory.root / "compat" /
      (family + "_" + (source.empty() ? result : source + "_" + result) + ".json");
  std::ofstream output(root_path);
  output << definition;
  output.close();
  check(static_cast<bool>(output), "failed to write matrix variant " + root_path.string());

  const auto resolved = load_definition(root_path);
  const auto grammar = compile_grammar(resolved);
  grammar.require_executable();
  const auto expected_type = parse_type(result);
  check(grammar.nonterminals().at(grammar.entry()).type == expected_type,
        resource + ": compiled entry result type changed");
  if (family == "linear_rec" || family == "dc") {
    const auto found = std::find_if(grammar.templates().begin(), grammar.templates().end(),
        [&](const CompiledTemplate& item) {
          return item.stable_id == resource && item.type == expected_type;
        });
    check(found != grammar.templates().end(),
          resource + ": referenced template is absent from resolved package");
  } else {
    const auto found = std::find_if(grammar.nonterminals().begin(), grammar.nonterminals().end(),
        [&](const CompiledNonterminal& item) {
          return item.stable_id == resource && item.type == expected_type;
        });
    check(found != grammar.nonterminals().end(),
          resource + ": referenced nonterminal is absent from resolved package");
    std::set<std::string> actual_patterns;
    for (const auto production : found->productions) {
      const auto& stable_id = grammar.productions().at(production).stable_id;
      const auto separator = stable_id.rfind('/');
      check(separator != std::string::npos,
            resource + ": production has no stable alternative ID");
      actual_patterns.insert(stable_id.substr(separator + 1));
    }
    check(actual_patterns == expected_patterns,
          resource + ": compiled pattern alternatives differ from the matrix");
  }

  auto genome = generate_derivation(grammar, seed).genome;
  require_membership(grammar, genome);
  const auto verified = verify_ast(genome.ast, inputs(grammar));
  check(static_cast<bool>(verified), resource + ": generated AST failed native verification");
  check(verified.verified.return_type == expected_type,
        resource + ": generated AST has the wrong result type");
  std::vector<std::string> input_names;
  for (const auto& input : grammar.inputs()) input_names.push_back(input.name);
  (void)compile_for_eval(genome, verified.verified, input_names);
}

void check_matrix_integrity() {
  std::ifstream input(kRoot / "configs/grammar/compat/matrix.json");
  check(static_cast<bool>(input), "failed to open compatibility matrix");
  const std::string text((std::istreambuf_iterator<char>(input)), {});
  const auto matrix = cli_detail::JsonParser(
      text, cli_detail::JsonParseOptions{true, 128}).parse();
  check(matrix.kind == JsonValue::Kind::Object,
        "compatibility matrix root must be an object");
  check(matrix_string(matrix_field(matrix, "format_version"), "format_version") ==
            "gagp-grammar-compat-matrix-v1",
        "compatibility matrix format version changed");

  const std::set<std::string> exact_types{
      "Int", "Float", "Bool", "Char", "String",
      "IntList", "FloatList", "StringList"};
  check(matrix_string_set(matrix_field(matrix, "exact_types"), "exact_types") ==
            exact_types,
        "compatibility matrix exact type set changed");
  const std::set<std::string> linear_sources{
      "IntList", "FloatList", "StringList"};
  const std::set<std::string> dc_sources{
      "String", "IntList", "FloatList", "StringList"};
  const std::set<std::string> dp1_patterns{
      "backward1", "backward2", "backward3",
      "forward1", "forward2", "forward3"};
  const std::set<std::string> dp2_patterns{
      "cross_backward", "cross_forward", "diagonal_backward",
      "diagonal_forward", "neighborhood_backward3", "neighborhood_forward3"};

  MatrixDirectory directory;
  std::set<std::string> copied_packages;
  std::set<std::string> resource_ids;
  std::uint64_t seed = 0;
  const auto copy_package = [&](const std::string& relative) {
    const auto name = std::filesystem::path(relative).filename().string();
    if (copied_packages.insert(name).second) {
      const auto source =
          (kRoot / "configs/grammar/compat" / relative).lexically_normal();
      std::filesystem::copy_file(source,
                                 directory.root / "packages" / name);
    }
    return std::string("../packages/") + name;
  };

  const auto check_template_family = [&](const char* family_name,
                                         int expected_count,
                                         const std::set<std::string>& sources,
                                         const std::string& id_prefix) {
    const auto& family = matrix_field(matrix, family_name);
    check(matrix_int(family, "count") == expected_count,
          std::string(family_name) + " count field changed");
    const auto package = copy_package(matrix_string(
        matrix_field(family, "package"), std::string(family_name) + ".package"));
    const auto& variants = matrix_array(matrix_field(family, "variants"),
                                        std::string(family_name) + ".variants");
    check(variants.size() == static_cast<std::size_t>(expected_count),
          std::string(family_name) + " variant count changed");
    std::set<std::string> actual_pairs;
    for (const auto& row : variants) {
      const auto source = matrix_string(matrix_field(row, "source"), "source");
      const auto result = matrix_string(matrix_field(row, "result"), "result");
      const auto resource = matrix_string(matrix_field(row, "template"), "template");
      check(sources.count(source) == 1 && exact_types.count(result) == 1,
            std::string(family_name) + " contains an unexpected type");
      check(resource == id_prefix + source + "." + result,
            resource + ": template ID does not match its typed row");
      check(actual_pairs.insert(source + "->" + result).second,
            std::string(family_name) + " contains a duplicate typed variant");
      check(resource_ids.insert(resource).second,
            resource + ": resource ID is duplicated in compatibility matrix");
      compile_matrix_variant(directory, family_name, source, result,
                             resource, package, seed++);
    }
    std::set<std::string> expected_pairs;
    for (const auto& source : sources)
      for (const auto& result : exact_types)
        expected_pairs.insert(source + "->" + result);
    check(actual_pairs == expected_pairs,
          std::string(family_name) + " does not cover its exact Cartesian type set");
  };

  check_template_family("linear_rec", 24, linear_sources, "Package.LinearRec.");
  check_template_family("dc", 32, dc_sources, "Package.DC.");

  const auto check_dp_family = [&](const char* family_name,
                                   const std::set<std::string>& patterns,
                                   const std::string& id_prefix) {
    const auto& family = matrix_field(matrix, family_name);
    check(matrix_int(family, "result_type_count") == 8 &&
              matrix_int(family, "pattern_count") == 6 &&
              matrix_int(family, "total_pattern_variants") == 48,
          std::string(family_name) + " 8x6 count contract changed");
    const auto package = copy_package(matrix_string(
        matrix_field(family, "package"), std::string(family_name) + ".package"));
    const auto& variants = matrix_array(matrix_field(family, "variants"),
                                        std::string(family_name) + ".variants");
    check(variants.size() == 8,
          std::string(family_name) + " result variant count changed");
    std::set<std::string> results;
    for (const auto& row : variants) {
      const auto result = matrix_string(matrix_field(row, "result"), "result");
      const auto resource = matrix_string(matrix_field(row, "entry"), "entry");
      check(results.insert(result).second,
            std::string(family_name) + " contains a duplicate result type");
      check(resource == id_prefix + result,
            resource + ": entry ID does not match its typed row");
      check(matrix_string_set(matrix_field(row, "patterns"), "patterns") == patterns,
            resource + ": pattern set changed");
      check(resource_ids.insert(resource).second,
            resource + ": resource ID is duplicated in compatibility matrix");
      compile_matrix_variant(directory, family_name, "", result,
                             resource, package, seed++, patterns);
    }
    check(results == exact_types,
          std::string(family_name) + " does not cover all exact result types");
  };

  check_dp_family("dp1d", dp1_patterns, "Package.DP1D.");
  check_dp_family("dp2d", dp2_patterns, "Package.DP2D.");
  check(resource_ids.size() == 72,
        "compatibility matrix must contain exactly 72 unique resource IDs");
}

void check_custom_examples(const Loaded& restricted, const Loaded& changed,
                           const Loaded& acyclic) {
  {
    const auto genome = generate_checked(*restricted.grammar, 5,
                                         "restricted combine");
    const auto verified = verify_ast(genome.ast, inputs(*restricted.grammar));
    check(static_cast<bool>(verified),
          "restricted combine failed native verification");
    const auto& spec = only_region(genome, "restricted combine");
    const auto children = child_roots(genome.ast, verified.verified,
                                      spec.node_index);
    check(children.at(spec.phases.back().argument) < genome.ast.nodes.size() &&
              genome.ast.nodes[children.at(spec.phases.back().argument)].kind ==
                  NodeKind::CALL_MAX,
          "restricted combine example did not materialize maximum-only combine");
  }
  {
    const auto genome = generate_checked(*changed.grammar, 7,
                                         "changed recursive split");
    const auto verified = verify_ast(genome.ast, inputs(*changed.grammar));
    check(static_cast<bool>(verified),
          "changed recursive split failed native verification");
    const auto& spec = only_region(genome, "changed recursive split");
    check(spec.plan.preparations.size() == 2 &&
              spec.plan.requests.size() == 3 && spec.phases.size() == 5,
          "changed recursive split lost its two-cut/three-way structure");
    const auto children = child_roots(genome.ast, verified.verified,
                                      spec.node_index);
    check(genome.ast.nodes[children.at(spec.phases[0].argument)].kind ==
              NodeKind::LE &&
              genome.ast.nodes[children.at(spec.phases.back().argument)].kind ==
              NodeKind::CALL_CONCAT,
          "changed recursive split lost its custom base/combine structure");
  }
  {
    const auto genome = generate_checked(*acyclic.grammar, 9,
                                         "acyclic memo");
    const auto& plan = only_region(genome, "acyclic memo").plan;
    check(plan.memoized && plan.coordinate_rank.size() == 2 &&
              plan.coordinate_rank[0].direction == 1 &&
              plan.coordinate_rank[1].direction == 1 &&
              plan.requests.size() == 2 &&
              plan.requests[0].states[0].offset == -1 &&
              plan.requests[0].states[1].offset == 2 &&
              plan.requests[1].states[0].offset == 0 &&
              plan.requests[1].states[1].offset == -1,
          "acyclic memo example lost its skew ranked dependencies");
  }
}

}  // namespace

int main() {
  try {
    const auto linear = load("configs/grammar/compat/linear_rec_intlist_int.json");
    const auto dc = load("configs/grammar/compat/dc_intlist_int.json");
    const auto dp1 = load("configs/grammar/compat/dp1d_int.json");
    const auto dp2 = load("configs/grammar/compat/dp2d_int.json");
    const auto restricted = load("configs/grammar/examples/restricted_combine.json");
    const auto changed = load("configs/grammar/examples/changed_recursive_split.json");
    const auto acyclic = load("configs/grammar/examples/acyclic_memo.json");

    check_matrix_integrity();
    smoke_packages({{"linear", linear}, {"dc", dc}, {"dp1", dp1},
                    {"dp2", dp2}, {"restricted", restricted},
                    {"changed", changed}, {"acyclic", acyclic}});
    check_relocation("linear_rec_intlist_int.json",
                     "linear_rec_intlist_int.json", linear);
    check_relocation("dc_intlist_int.json", "dc_intlist_int.json", dc);
    check_relocation("dp1d_int.json", "dp1d_int.json", dp1);
    check_relocation("dp2d_int.json", "dp2d_int.json", dp2);
    cover_dp_patterns(dp1, dp2);
    check_linear_contract(linear);
    check_dc_contract(dc);
    check_dp_fuel_contracts(dp1, dp2);
    exercise_variation(linear, Family::Linear, "LinearRec");
    exercise_variation(dc, Family::Dc, "DC");
    exercise_variation(dp1, Family::Dp1, "DP1D");
    exercise_variation(dp2, Family::Dp2, "DP2D");
    check_custom_examples(restricted, changed, acyclic);
    std::cout << "grammar packages: matrix, identity, patterns, variation, and custom contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
