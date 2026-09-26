#pragma once

#include <memory>
#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/grammar/definition.hpp"

namespace gagp::test {

inline evo::EvolutionConfig mixed_population_config() {
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
  evo::EvolutionConfig cfg;
  cfg.compiled_grammar = grammar;
  cfg.generation_request = evo::grammar::entry_request(*grammar);
  for (const auto& root : grammar->nonterminals()) {
    if (root.id == grammar->entry()) continue;
    auto request = *cfg.generation_request;
    request.nonterminal = root.id;
    request.type = root.type;
    cfg.additional_generation_requests.push_back(request);
  }
  cfg.fuel = 100;
  cfg.penalty = 9;
  cfg.population_size = 16;
  cfg.generations = 3;
  cfg.selection_pressure = 16;
  cfg.mutation_rate = 1;
  cfg.mutation_subtree_prob = 0.5;
  cfg.gpu_blocksize = 256;
  cfg.seed = 42;
  return cfg;
}

}  // namespace gagp::test
