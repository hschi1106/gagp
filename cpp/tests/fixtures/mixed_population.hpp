#pragma once

#include <memory>
#include <vector>

#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/request.hpp"

namespace gagp::test {

// One grammar with a root nonterminal per exact runtime type. The entry
// request comes first; the others are additional exact population roots.
struct MixedPopulation {
  std::shared_ptr<const evo::grammar::CompiledGrammar> grammar;
  std::vector<evo::grammar::GenerationRequest> requests;
};

inline MixedPopulation mixed_population() {
  auto grammar = std::make_shared<const evo::grammar::CompiledGrammar>(
      evo::grammar::compile_grammar(evo::grammar::parse_definition(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"RootInt","type":"Int"},
    "search_limits":{"max_nodes":5,"max_depth":4},
    "execution_limits":{"fuel":100},
    "nonterminals":[
      {"id":"RootInt","type":"Int","scope":[],"alternatives":[
        {"id":"value","weight":1,"expression":{"constant":{"type":"Int","values":["0"]}}}]},
      {"id":"RootFloat","type":"Float","scope":[],"alternatives":[
        {"id":"value","weight":1,"expression":{"constant":{"type":"Float","values":[1.25,2.5]}}}]},
      {"id":"RootBool","type":"Bool","scope":[],"alternatives":[
        {"id":"value","weight":1,"expression":{"constant":{"type":"Bool","values":[true,false]}}}]},
      {"id":"RootChar","type":"Char","scope":[],"alternatives":[
        {"id":"value","weight":1,"expression":{"constant":{"type":"Char","values":["a","b"]}}}]},
      {"id":"RootString","type":"String","scope":[],"alternatives":[
        {"id":"value","weight":1,"expression":{"constant":{"type":"String","values":["left","right"]}}}]},
      {"id":"RootInts","type":"IntList","scope":[],"alternatives":[
        {"id":"value","weight":1,"expression":{"constant":{"type":"IntList","values":[["1"],["2"]]}}}]},
      {"id":"RootFloats","type":"FloatList","scope":[],"alternatives":[
        {"id":"value","weight":1,"expression":{"constant":{"type":"FloatList","values":[[1.25],[2.5]]}}}]},
      {"id":"RootStrings","type":"StringList","scope":[],"alternatives":[
        {"id":"value","weight":1,"expression":{"constant":{"type":"StringList","values":[["one"],["two"]]}}}]}
    ]})")));
  MixedPopulation out{grammar, {evo::grammar::entry_request(*grammar)}};
  for (const auto& root : grammar->nonterminals()) {
    if (root.id == grammar->entry()) continue;
    auto request = out.requests.front();
    request.nonterminal = root.id;
    request.type = root.type;
    out.requests.push_back(request);
  }
  return out;
}

}  // namespace gagp::test
