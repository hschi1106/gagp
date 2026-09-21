#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/cli/grammar_artifact.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/grammar/frame.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;

namespace {

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

CompiledGrammar compile(const std::string& text) {
  return compile_grammar(parse_definition(text));
}

std::uint32_t nonterminal(const CompiledGrammar& grammar, const std::string& stable_id) {
  const auto found = std::find_if(grammar.nonterminals().begin(), grammar.nonterminals().end(),
      [&](const CompiledNonterminal& value) { return value.stable_id == stable_id; });
  if (found == grammar.nonterminals().end())
    throw std::runtime_error("missing fixture nonterminal " + stable_id);
  return found->id;
}

void rejects(const std::function<void()>& action, const char* message) {
  try {
    action();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error(message);
}

GenerationRequest request_for(const CompiledGrammar& grammar, const std::string& stable_id,
    RType type, GrammarLimits budget = {5, 4}) {
  return {nonterminal(grammar, stable_id), type, {}, budget};
}

CompiledGrammar local_only_grammar() {
  return compile(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Safe","type":"Int"},
    "inputs":[{"name":"external","type":"Bool"}],
    "locals":[{"name":"x","type":"Int"}],
    "search_limits":{"max_nodes":12,"max_depth":7},
    "execution_limits":{"fuel":100},
    "nonterminals":[
      {"id":"Safe","type":"Int","scope":[],"alternatives":[{"id":"zero","weight":1,
        "expression":{"constant":{"type":"Int","values":["0"]}}}]},
      {"id":"LocalX","type":"Int","scope":[],"alternatives":[{"id":"x","weight":1,
        "expression":{"local":"x"}}]}
    ]
  })");
}

void test_local_donor_execution_membership_and_artifact_boundary() {
  const auto grammar = local_only_grammar();
  const auto request = request_for(grammar, "LocalX", RType::Int);
  const GenerationFrame frame{{{"x", RType::Int}}};
  const auto generated = generate_derivation_in_frame(grammar, 17, request, frame);

  check(generated.genome.ast.nodes.size() == 5 &&
        generated.genome.ast.nodes[0].kind == NodeKind::PROGRAM &&
        generated.genome.ast.nodes[1].kind == NodeKind::BLOCK_CONS &&
        generated.genome.ast.nodes[2].kind == NodeKind::RETURN &&
        generated.genome.ast.nodes[3].kind == NodeKind::VAR &&
        generated.genome.ast.nodes[4].kind == NodeKind::BLOCK_NIL,
        "contextual expression donor is not a complete native expression envelope");
  check(!generated.derivation.seed_replayable && generated.genome.derivation &&
        !generated.genome.derivation->seed_replayable,
        "contextual generation incorrectly advertised ordinary seed replay");

  const std::vector<InputSpec> original_inputs{{"external", RType::Bool}};
  const auto ordinary = verify_ast(generated.genome.ast, original_inputs);
  check(!ordinary && ordinary.diagnostic.code == VerifyCode::UndefinedLocal,
        "standalone local donor passed ordinary grammar-input validation");
  rejects([&] { require_membership(grammar, generated.genome, request); },
      "standalone local donor passed ordinary grammar membership");

  require_membership_in_frame(grammar, generated.genome, request, frame);
  VerifiedAst verified;
  const auto witness = reconstruct_derivation_in_frame(
      grammar, generated.genome, request, frame, &verified);
  check(!witness.seed_replayable && verified.return_type == RType::Int &&
        witness.derived_nodes == 1,
        "framed membership did not reconstruct a typed contextual witness");

  const auto inputs = frame_inputs(grammar, request, frame);
  check(inputs.size() == 2 && inputs[0].name == "external" &&
        inputs[0].type == RType::Bool && inputs[1].name == "x" &&
        inputs[1].type == RType::Int,
        "frame inputs did not append the declared local to original inputs");
  std::vector<std::string> names;
  for (const auto& input : inputs) names.push_back(input.name);
  const auto bytecode = compile_for_eval(generated.genome, verified, names);
  const auto x = bytecode.var2idx.at("x");
  const auto result = execute_bytecode_cpu(bytecode, {{x, Value::from_int(42)}}, 100);
  check(!result.is_error && result.value.tag == ValueTag::Int && result.value.i == 42,
        "contextual donor did not execute with the provided local value");

  rejects([&] { cli_detail::encode_generated_artifact(grammar, generated); },
      "artifact encoder accepted non-replayable contextual provenance");
}

void test_frame_validation_and_missing_local_filtering() {
  const auto grammar = local_only_grammar();
  const auto request = request_for(grammar, "LocalX", RType::Int);
  rejects([&] { generate_derivation_in_frame(grammar, 1, request, {}); },
      "generation accepted a frame missing its only productive local");
  rejects([&] { frame_inputs(grammar, request, {{{"x", RType::Float}}}); },
      "frame accepted a declared local with the wrong type");
  rejects([&] { frame_inputs(grammar, request, {{{"missing", RType::Int}}}); },
      "frame accepted an undeclared local");
  rejects([&] { frame_inputs(grammar, request,
      {{{"x", RType::Int}, {"x", RType::Int}}}); },
      "frame accepted duplicate local bindings");

  const auto alternatives = compile(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Choice","type":"Int"},
    "locals":[{"name":"x","type":"Int"}],
    "search_limits":{"max_nodes":5,"max_depth":4},"execution_limits":{"fuel":100},
    "nonterminals":[{"id":"Choice","type":"Int","scope":[],"alternatives":[
      {"id":"missing-local","weight":1e300,"expression":{"local":"x"}},
      {"id":"constant","weight":1,"expression":{"constant":{"type":"Int","values":["7"]}}}
    ]}]
  })");
  const auto choice = entry_request(alternatives);
  for (std::uint64_t seed = 0; seed < 64; ++seed) {
    const auto generated = generate_derivation_in_frame(alternatives, seed, choice, {});
    check(generated.genome.ast.nodes[3].kind == NodeKind::CONST &&
          generated.genome.ast.consts.at(generated.genome.ast.nodes[3].i0).i == 7,
          "unavailable local production remained eligible during contextual generation");
    require_membership_in_frame(alternatives, generated.genome, choice, {});
  }
}

CompiledGrammar shared_alias_grammar() {
  return compile(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","type":"Int"},
    "locals":[{"name":"x","type":"Int"}],
    "search_limits":{"max_nodes":16,"max_depth":8},"execution_limits":{"fuel":100},
    "templates":[{
      "id":"Double","type":"Int","scope":[],
      "holes":[{"id":"value","type":"Int","scope":[]}],
      "body":{"signature":"add(Int,Int)->Int","args":[{"hole":"value"},{"hole":"value"}]}
    }],
    "nonterminals":[
      {"id":"Main","type":"Int","scope":[],"alternatives":[{"id":"double","weight":1,
        "expression":{"template":"Double","holes":{"value":{"ref":"Alias"}}}}]},
      {"id":"Alias","type":"Int","scope":[],"alternatives":[{"id":"choice","weight":1,
        "expression":{"ref":"Choice"}}]},
      {"id":"Choice","type":"Int","scope":[],"alternatives":[
        {"id":"too-cheap-missing-local","weight":1e300,"expression":{"local":"x"}},
        {"id":"sum","weight":1,"expression":{"signature":"add(Int,Int)->Int","args":[
          {"constant":{"type":"Int","values":["2"]}},
          {"constant":{"type":"Int","values":["3"]}}]}}
      ]}
    ]
  })");
}

void test_contextual_minimum_cost_with_shared_holes_and_aliases() {
  const auto grammar = shared_alias_grammar();
  const auto exact = request_for(grammar, "Main", RType::Int, {11, 6});
  for (std::uint64_t seed = 0; seed < 64; ++seed) {
    const auto generated = generate_derivation_in_frame(grammar, seed, exact, {});
    check(generated.genome.ast.nodes.size() == 11 &&
          generated.genome.ast.nodes[3].kind == NodeKind::ADD &&
          generated.genome.ast.nodes[4].kind == NodeKind::ADD &&
          generated.genome.ast.nodes[7].kind == NodeKind::ADD,
          "contextual minimum did not exclude the missing-local alias inside a shared hole");
    require_membership_in_frame(grammar, generated.genome, exact, {});
  }

  const auto too_small = request_for(grammar, "Main", RType::Int, {10, 6});
  rejects([&] { generate_derivation_in_frame(grammar, 0, too_small, {}); },
      "contextual shared-hole generation used the unavailable local's cheaper minimum");

  const GenerationFrame frame{{{"x", RType::Int}}};
  const auto local_budget = request_for(grammar, "Main", RType::Int, {7, 5});
  const auto local = generate_derivation_in_frame(grammar, 0, local_budget, frame);
  check(local.genome.ast.nodes.size() == 7 &&
        local.genome.ast.nodes[4].kind == NodeKind::VAR &&
        local.genome.ast.nodes[5].kind == NodeKind::VAR,
        "available local did not restore the small shared-hole derivation");
  require_membership_in_frame(grammar, local.genome, local_budget, frame);
}

void test_all_public_local_types_become_frame_inputs() {
  const auto grammar = compile(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"IntLocal","type":"Int"},
    "inputs":[{"name":"base","type":"Bool"}],
    "locals":[
      {"name":"li","type":"Int"},{"name":"lf","type":"Float"},
      {"name":"lb","type":"Bool"},{"name":"lc","type":"Char"},
      {"name":"ls","type":"String"},{"name":"lis","type":"IntList"},
      {"name":"lfs","type":"FloatList"},{"name":"lss","type":"StringList"}
    ],
    "search_limits":{"max_nodes":5,"max_depth":4},"execution_limits":{"fuel":100},
    "nonterminals":[
      {"id":"IntLocal","type":"Int","scope":[],"alternatives":[{"id":"v","weight":1,"expression":{"local":"li"}}]},
      {"id":"FloatLocal","type":"Float","scope":[],"alternatives":[{"id":"v","weight":1,"expression":{"local":"lf"}}]},
      {"id":"BoolLocal","type":"Bool","scope":[],"alternatives":[{"id":"v","weight":1,"expression":{"local":"lb"}}]},
      {"id":"CharLocal","type":"Char","scope":[],"alternatives":[{"id":"v","weight":1,"expression":{"local":"lc"}}]},
      {"id":"StringLocal","type":"String","scope":[],"alternatives":[{"id":"v","weight":1,"expression":{"local":"ls"}}]},
      {"id":"IntListLocal","type":"IntList","scope":[],"alternatives":[{"id":"v","weight":1,"expression":{"local":"lis"}}]},
      {"id":"FloatListLocal","type":"FloatList","scope":[],"alternatives":[{"id":"v","weight":1,"expression":{"local":"lfs"}}]},
      {"id":"StringListLocal","type":"StringList","scope":[],"alternatives":[{"id":"v","weight":1,"expression":{"local":"lss"}}]}
    ]
  })");
  const std::vector<RegionBinding> locals{
      {"li", RType::Int}, {"lf", RType::Float}, {"lb", RType::Bool},
      {"lc", RType::Char}, {"ls", RType::String}, {"lis", RType::IntList},
      {"lfs", RType::FloatList}, {"lss", RType::StringList}};
  const std::vector<std::string> nts{"IntLocal", "FloatLocal", "BoolLocal", "CharLocal",
      "StringLocal", "IntListLocal", "FloatListLocal", "StringListLocal"};

  const GenerationFrame complete{locals};
  const auto complete_inputs = frame_inputs(grammar, entry_request(grammar), complete);
  check(complete_inputs.size() == 9 && complete_inputs.front().name == "base",
        "complete frame did not preserve the original input prefix");
  for (std::size_t i = 0; i < locals.size(); ++i)
    check(complete_inputs[i + 1].name == locals[i].name &&
          complete_inputs[i + 1].type == locals[i].type,
          "public typed local changed name, type, or order in frame inputs");

  for (std::size_t i = 0; i < locals.size(); ++i) {
    const auto request = request_for(grammar, nts[i], locals[i].type);
    const GenerationFrame frame{{locals[i]}};
    const auto generated = generate_derivation_in_frame(grammar, 100 + i, request, frame);
    check(generated.genome.ast.nodes[3].kind == NodeKind::VAR,
          "typed contextual local did not materialize as a native variable");
    VerifiedAst verified;
    reconstruct_derivation_in_frame(grammar, generated.genome, request, frame, &verified);
    check(verified.return_type == locals[i].type,
          "typed contextual local changed type during native verification");
  }
}

}  // namespace

int main() {
  try {
    test_local_donor_execution_membership_and_artifact_boundary();
    test_frame_validation_and_missing_local_filtering();
    test_contextual_minimum_cost_with_shared_holes_and_aliases();
    test_all_public_local_types_become_frame_inputs();
    std::cout << "contextual grammar donors: frame validation, scoped generation, minimum costs, and execution passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
