#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/bounded_region.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/serialization/region_plan_json.hpp"

namespace {

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;

void check(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

RegionPlan coordinate_plan(std::size_t parameters) {
  RegionPlan plan;
  plan.state_types = {ValueTag::Int};
  plan.result_type = ValueTag::Int;
  plan.parameter_types.assign(parameters, ValueTag::Int);
  RegionStateTransition transition;
  transition.kind = RegionTransitionKind::CoordinateOffset;
  transition.source_state = 0;
  transition.offset = -1;
  plan.requests = {{{transition}}};
  plan.limits = {8, 0, 1};
  plan.coordinate_slots = {0};
  plan.coordinate_rank = {{0, 1}};
  RegionCoordinateDomain domain;
  domain.lower.kind = RegionBoundKind::Literal;
  domain.lower.literal = 0;
  domain.upper.kind = RegionBoundKind::Literal;
  domain.upper.literal = 5;
  plan.coordinate_domains = {domain};
  plan.coordinate_endpoint = DomainEndpoint::Exclusive;
  return plan;
}

std::string encoded_plan(const RegionPlan& plan) {
  return canonical_json(serialization::encode_region_plan(plan));
}

CompiledGrammar compile(const std::string& text) {
  return compile_grammar(parse_definition(text));
}

std::vector<InputSpec> input_specs(const CompiledGrammar& grammar) {
  std::vector<InputSpec> result;
  for (const auto& input : grammar.inputs())
    result.push_back({input.name, input.type});
  return result;
}

void rejects_valid_member(const CompiledGrammar& grammar,
                          const ProgramGenome& genome,
                          const std::string& message) {
  check(static_cast<bool>(verify_ast(genome.ast, input_specs(grammar))),
        message + ": fixture is not native-valid");
  try {
    require_membership(grammar, genome);
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error(message);
}

const BoundedRegionSpec& only_bounded(const AstProgram& ast) {
  check(ast.bounded_region_specs.size() == 1,
        "fixture did not materialize exactly one bounded region");
  return ast.bounded_region_specs.front();
}

std::string ownership_grammar() {
  return R"({
    "format_version":"grammar-definition-v1",
    "entry":{"nonterminal":"Main","type":"Int"},
    "inputs":[{"name":"n","type":"Int"}],
    "locals":[{"name":"tmp","type":"Int"}],
    "search_limits":{"max_nodes":48,"max_depth":16},
    "execution_limits":{"fuel":1000},
    "nonterminals":[
      {"id":"Main","type":"Int","scope":[],"alternatives":[
        {"id":"region","weight":1,"expression":
          {"signature":"let(Int,Int)->Int","args":[
            {"constant":{"type":"Int","values":["1"]}},
            {"signature":"let(Int,Int)->Int","args":[
              {"constant":{"type":"Int","values":["2"]}},
              {"structured":{"family":"bounded","plan":)" +
      encoded_plan(coordinate_plan(3)) + R"(},
               "captures":[{"input":"n"},{"local":"tmp"},{"bound":"x"}],
               "phases":[
                 {"argument":1,"bindings":[
                   {"bank":"state","slot":0,"name":"s"},
                   {"bank":"parameter","slot":2,"name":"limit"}]},
                 {"argument":2,"bindings":[
                   {"bank":"state","slot":0,"name":"base_state"}]},
                 {"argument":3,"bindings":[
                   {"bank":"result","slot":0,"name":"answer"}]},
                 {"argument":4,"bindings":[]}],
               "args":[{"ref":"Initial"},{"ref":"Predicate"},
                       {"ref":"Base"},{"ref":"Combine"},{"ref":"Zero"}]}
            ],"bind":{"1":["y"]}}
          ],"bind":{"1":["x"]}}}
      ]},
      {"id":"Initial","type":"Int","scope":[],"alternatives":[
        {"id":"three","weight":1,
         "expression":{"constant":{"type":"Int","values":["3"]}}}]},
      {"id":"Predicate","type":"Bool","scope":[
        {"name":"limit","type":"Int"},{"name":"s","type":"Int"}],
       "alternatives":[{"id":"le","weight":1,
         "expression":{"signature":"le(Int,Int)->Bool","args":[
           {"bound":"s"},{"bound":"limit"}]}}]},
      {"id":"Base","type":"Int","scope":[
        {"name":"base_state","type":"Int"}],"alternatives":[
          {"id":"state","weight":1,"expression":{"bound":"base_state"}}]},
      {"id":"Combine","type":"Int","scope":[
        {"name":"answer","type":"Int"}],"alternatives":[
          {"id":"answer","weight":1,"expression":{"bound":"answer"}}]},
      {"id":"Zero","type":"Int","scope":[],"alternatives":[
        {"id":"zero","weight":1,
         "expression":{"constant":{"type":"Int","values":["0"]}}}]}
    ]
  })";
}

std::uint32_t nonterminal_id(const CompiledGrammar& grammar,
                             const std::string& stable_id) {
  const auto found = std::find_if(grammar.nonterminals().begin(),
      grammar.nonterminals().end(), [&](const CompiledNonterminal& candidate) {
        return candidate.stable_id == stable_id;
      });
  check(found != grammar.nonterminals().end(), "missing compiled nonterminal");
  return found->id;
}

void check_ownership_and_exact_matching() {
  const CompiledGrammar grammar = compile(ownership_grammar());
  const GeneratedDerivation generated = generate_derivation(grammar, 17);
  require_membership(grammar, generated.genome);

  VerifiedAst verified;
  std::vector<std::vector<int>> environments;
  const DerivationMetadata witness = reconstruct_derivation(
      grammar, generated.genome, entry_request(grammar), &verified,
      &environments);
  check(witness.choices.size() == environments.size(),
        "bounded witness lost lexical sidecar alignment");
  const auto& spec = only_bounded(generated.genome.ast);
  check(spec.parameters.size() == 3 &&
            spec.parameters[0].kind == RegionCaptureKind::Name &&
            generated.genome.ast.names.at(spec.parameters[0].index) == "n" &&
            spec.parameters[1].kind == RegionCaptureKind::Name &&
            generated.genome.ast.names.at(spec.parameters[1].index) == "tmp" &&
            spec.parameters[2].kind == RegionCaptureKind::Lexical,
        "bounded capture kinds or ordinary-name ownership changed");

  const auto predicate = nonterminal_id(grammar, "Predicate");
  const auto choice = std::find_if(witness.choices.begin(), witness.choices.end(),
      [&](const DerivationChoice& item) { return item.nonterminal == predicate; });
  check(choice != witness.choices.end(), "predicate witness choice is missing");
  const std::size_t choice_index = static_cast<std::size_t>(
      choice - witness.choices.begin());
  check(environments[choice_index].size() == 2 &&
            environments[choice_index][0] == spec.phases[0].bindings[1].binder_id &&
            environments[choice_index][1] == spec.phases[0].bindings[0].binder_id,
        "phase witness scope did not project reordered formal bindings");
  check(std::find(environments[choice_index].begin(),
                  environments[choice_index].end(),
                  spec.parameters[2].index) == environments[choice_index].end(),
        "closed bounded phase inherited its outer lexical capture");

  ProgramGenome changed_plan = generated.genome;
  ++changed_plan.ast.bounded_region_specs[0].plan.limits.frames;
  rejects_valid_member(grammar, changed_plan,
                       "membership accepted a different complete RegionPlan");

  ProgramGenome changed_name = generated.genome;
  std::swap(changed_name.ast.bounded_region_specs[0].parameters[0].index,
            changed_name.ast.bounded_region_specs[0].parameters[1].index);
  rejects_valid_member(grammar, changed_name,
                       "membership accepted an input capture as a local capture");

  ProgramGenome changed_bound = generated.genome;
  int y = -1;
  for (const auto& region : changed_bound.ast.lexical_regions)
    for (const auto& binding : region.bindings)
      if (binding.id != spec.parameters[2].index) y = binding.id;
  check(y >= 0, "ownership fixture did not expose its second visible binder");
  changed_bound.ast.bounded_region_specs[0].parameters[2].index = y;
  rejects_valid_member(grammar, changed_bound,
                       "membership accepted the wrong visible bound capture");

  ProgramGenome changed_source = generated.genome;
  changed_source.ast.bounded_region_specs[0].phases[0].bindings[1].source.slot = 1;
  rejects_valid_member(grammar, changed_source,
                       "membership accepted a different same-type parameter source");
}

std::string repeated_hole_grammar() {
  return R"({
    "format_version":"grammar-definition-v1",
    "entry":{"nonterminal":"Main","type":"Int"},
    "search_limits":{"max_nodes":48,"max_depth":16},
    "execution_limits":{"fuel":1000},
    "templates":[
      {"id":"Sibling","type":"Int","scope":[],
       "holes":[{"id":"payload","type":"Int","scope":[
         {"name":"x","type":"Int"}]}],
       "body":{"signature":"add(Int,Int)->Int","args":[
         {"signature":"let(Int,Int)->Int","args":[
           {"constant":{"type":"Int","values":["1"]}},{"hole":"payload"}],
          "bind":{"1":["x"]}},
         {"signature":"let(Int,Int)->Int","args":[
           {"constant":{"type":"Int","values":["2"]}},{"hole":"payload"}],
          "bind":{"1":["x"]}}]}}
    ],
    "nonterminals":[
      {"id":"Main","type":"Int","scope":[],"alternatives":[
        {"id":"siblings","weight":1,
         "expression":{"template":"Sibling","holes":{"payload":
           {"structured":{"family":"bounded","plan":)" +
      encoded_plan(coordinate_plan(1)) + R"(},
            "captures":[{"bound":"x"}],
            "phases":[
              {"argument":1,"bindings":[
                {"bank":"parameter","slot":0,"name":"p"}]},
              {"argument":2,"bindings":[
                {"bank":"state","slot":0,"name":"s"}]},
              {"argument":3,"bindings":[
                {"bank":"result","slot":0,"name":"r"}]},
              {"argument":4,"bindings":[]}],
            "args":[
              {"constant":{"type":"Int","values":["0"]}},
              {"ref":"SameParameter"},{"ref":"State"},{"ref":"Result"},
              {"constant":{"type":"Int","values":["0"]}}]}}}}
      ]},
      {"id":"SameParameter","type":"Bool","scope":[
        {"name":"p","type":"Int"}],"alternatives":[
          {"id":"same","weight":1,
           "expression":{"signature":"eq(Int,Int)->Bool","args":[
             {"bound":"p"},{"bound":"p"}]}}]},
      {"id":"State","type":"Int","scope":[
        {"name":"s","type":"Int"}],"alternatives":[
          {"id":"state","weight":1,"expression":{"bound":"s"}}]},
      {"id":"Result","type":"Int","scope":[
        {"name":"r","type":"Int"}],"alternatives":[
          {"id":"result","weight":1,"expression":{"bound":"r"}}]}
    ]
  })";
}

void check_repeated_hole_alpha_equivalence() {
  const CompiledGrammar grammar = compile(repeated_hole_grammar());
  const GeneratedDerivation generated = generate_derivation(grammar, 9);
  check(generated.genome.ast.bounded_region_specs.size() == 2,
        "repeated bounded hole lost an occurrence spec");
  const auto& first = generated.genome.ast.bounded_region_specs[0];
  const auto& second = generated.genome.ast.bounded_region_specs[1];
  check(first.parameters[0].index != second.parameters[0].index &&
            first.phases[0].bindings[0].binder_id !=
                second.phases[0].bindings[0].binder_id,
        "repeated bounded hole did not alpha-remap captures and declarations");
  require_membership(grammar, generated.genome);
  VerifiedAst verified;
  std::vector<std::vector<int>> environments;
  const DerivationMetadata witness = reconstruct_derivation(
      grammar, generated.genome, entry_request(grammar), &verified,
      &environments);
  check(environments.size() == witness.choices.size(),
        "repeated bounded witness lost its alpha-remapped scope sidecar");
  check(witness.holes.size() == 2 &&
            witness.holes[0].template_instance ==
                witness.holes[1].template_instance &&
            witness.holes[0].slot == witness.holes[1].slot,
        "bounded repeated-hole witness lost logical equivalence");

  ProgramGenome renamed = generated.genome;
  std::map<int, int> alpha;
  int next = 1000;
  for (auto& region : renamed.ast.lexical_regions)
    for (auto& binding : region.bindings) {
      alpha.emplace(binding.id, next);
      binding.id = next++;
    }
  for (auto& spec : renamed.ast.bounded_region_specs)
    for (auto& phase : spec.phases)
      for (auto& binding : phase.bindings) {
        alpha.emplace(binding.binder_id, next);
        binding.binder_id = next++;
      }
  for (auto& node : renamed.ast.nodes)
    if (node.kind == NodeKind::REGION_VAR) node.i0 = alpha.at(node.i0);
  for (auto& spec : renamed.ast.bounded_region_specs)
    for (auto& capture : spec.parameters)
      if (capture.kind == RegionCaptureKind::Lexical)
        capture.index = alpha.at(capture.index);
  require_membership(grammar, renamed);
  (void)reconstruct_derivation(grammar, renamed, entry_request(grammar),
                               &verified, &environments);
}

void check_exact_source_schema() {
  std::string malformed = ownership_grammar();
  const auto needle = std::string{"\"captures\":["};
  const auto position = malformed.find(needle);
  check(position != std::string::npos, "schema fixture lost captures field");
  malformed.insert(position, "\"bind\":{},");
  try {
    (void)compile(malformed);
  } catch (const std::invalid_argument& error) {
    check(std::string(error.what()).find("unknown key bind") != std::string::npos,
          "bounded exact-dictionary rejection returned the wrong diagnostic");
    return;
  }
  throw std::runtime_error("bounded grammar accepted the legacy bind shorthand");
}

}  // namespace

int main() {
  try {
    check_ownership_and_exact_matching();
    check_repeated_hole_alpha_equivalence();
    check_exact_source_schema();
    std::cout << "gagp_test_grammar_bounded_membership: OK\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
