#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/evolution/grammar/frame.hpp"

using namespace gagp::evo;
using namespace gagp::evo::grammar;

namespace {

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void rejects(const std::function<void()>& action, const char* message,
    const std::string& diagnostic = {}) {
  try {
    action();
  } catch (const std::invalid_argument& error) {
    if (!diagnostic.empty() && std::string(error.what()).find(diagnostic) == std::string::npos)
      throw std::runtime_error(std::string(message) + ": " + error.what());
    return;
  }
  throw std::runtime_error(message);
}

CompiledGrammar fixture() {
  return compile_grammar(parse_definition(R"({
    "format_version":"grammar-definition-v1",
    "entry":{"nonterminal":"Expr","type":"Int"},
    "inputs":[{"name":"source","type":"String"},{"name":"count","type":"Int"}],
    "locals":[
      {"name":"li","type":"Int"},{"name":"lf","type":"Float"},
      {"name":"lb","type":"Bool"},{"name":"lc","type":"Char"},
      {"name":"ls","type":"String"},{"name":"lis","type":"IntList"},
      {"name":"lfs","type":"FloatList"},{"name":"lss","type":"StringList"}
    ],
    "search_limits":{"max_nodes":20,"max_depth":8},
    "execution_limits":{"fuel":1000},
    "nonterminals":[
      {"id":"Expr","type":"Int","scope":[],"alternatives":[
        {"id":"zero","weight":1,"expression":{"constant":{"type":"Int","values":["0"]}}}
      ]},
      {"id":"ProgramRoot","category":"Program","type":"Int","scope":[],"alternatives":[
        {"id":"return","weight":1,"expression":{"control":"program(Block)->Program","type":"Int","args":[
          {"control":"block_cons(Statement,Block)->Block","type":"Int","args":[
            {"control":"return(Int)->Statement","type":"Int","args":[
              {"constant":{"type":"Int","values":["0"]}}]},
            {"control":"block_nil()->Block","type":"Int","args":[]}]}
        ]}}
      ]}
    ]
  })"));
}

std::uint32_t nonterminal(const CompiledGrammar& grammar, const std::string& stable_id) {
  const auto found = std::find_if(grammar.nonterminals().begin(), grammar.nonterminals().end(),
      [&](const auto& value) { return value.stable_id == stable_id; });
  if (found == grammar.nonterminals().end()) throw std::runtime_error("fixture nonterminal missing");
  return found->id;
}

void test_inputs_and_frame_order(const CompiledGrammar& grammar) {
  GenerationRequest request{nonterminal(grammar, "Expr"), RType::Int,
      {{"lexical_only", RType::Bool}}, {5, 4}};
  GenerationFrame frame{{{"lss", RType::StringList}, {"li", RType::Int},
      {"lc", RType::Char}, {"lf", RType::Float}}};
  const auto inputs = frame_inputs(grammar, request, frame);
  const std::vector<InputSpec> expected{{"source", RType::String}, {"count", RType::Int},
      {"lss", RType::StringList}, {"li", RType::Int}, {"lc", RType::Char},
      {"lf", RType::Float}};
  check(inputs.size() == expected.size(), "frame input count changed");
  for (std::size_t i = 0; i < inputs.size(); ++i)
    check(inputs[i].name == expected[i].name && inputs[i].type == expected[i].type,
        "frame input name, type, or order changed");
  check(std::none_of(inputs.begin(), inputs.end(), [](const auto& input) {
          return input.name == "lexical_only";
        }), "request visible_environment leaked into native verifier inputs");

  const auto empty = frame_inputs(grammar, request, {});
  check(empty.size() == grammar.inputs().size(), "empty expression frame changed grammar inputs");
}

void test_frame_validation(const CompiledGrammar& grammar) {
  GenerationRequest request{nonterminal(grammar, "Expr"), RType::Int, {}, {5, 4}};
  rejects([&] { frame_inputs(grammar, request, {{{"li", RType::Int}, {"li", RType::Int}}}); },
      "duplicate frame local was accepted", "duplicate");
  rejects([&] { frame_inputs(grammar, request, {{{"missing", RType::Int}}}); },
      "unknown frame local was accepted", "unknown");
  rejects([&] { frame_inputs(grammar, request, {{{"source", RType::String}}}); },
      "input shadow in frame was accepted", "input");
  rejects([&] { frame_inputs(grammar, request, {{{"li", RType::Float}}}); },
      "wrong frame local type was accepted", "wrong exact type");
  for (const auto type : {RType::Any, RType::Invalid})
    rejects([&] { frame_inputs(grammar, request, {{{"li", type}}}); },
        "non-exact frame local type was accepted", "exact public value types");

  GenerationFrame all{{{"li", RType::Int}, {"lf", RType::Float}, {"lb", RType::Bool},
      {"lc", RType::Char}, {"ls", RType::String}, {"lis", RType::IntList},
      {"lfs", RType::FloatList}, {"lss", RType::StringList}}};
  check(frame_inputs(grammar, request, all).size() == grammar.inputs().size() + 8,
      "one of the eight exact frame local types was rejected");
}

void test_program_and_request_validation(const CompiledGrammar& grammar) {
  GenerationRequest program{nonterminal(grammar, "ProgramRoot"), RType::Int, {}, {5, 4}};
  check(frame_inputs(grammar, program, {}).size() == grammar.inputs().size(),
      "empty Program frame was rejected");
  rejects([&] { frame_inputs(grammar, program, {{{"li", RType::Int}}}); },
      "nonempty Program frame was accepted", "Program");

  GenerationRequest invalid{kNoGrammarId, RType::Int, {}, {5, 4}};
  rejects([&] { frame_inputs(grammar, invalid, {{{"missing", RType::Any}}}); },
      "invalid custom request reached frame validation", "generation request");
  GenerationRequest wrong_type{nonterminal(grammar, "Expr"), RType::Float, {}, {5, 4}};
  rejects([&] { frame_inputs(grammar, wrong_type, {{{"missing", RType::Any}}}); },
      "mistyped custom request reached frame validation", "generation request exact type");
}

}  // namespace

int main() {
  try {
    const auto grammar = fixture();
    test_inputs_and_frame_order(grammar);
    test_frame_validation(grammar);
    test_program_and_request_validation(grammar);
    std::cout << "grammar frame tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
