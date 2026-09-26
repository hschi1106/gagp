#pragma once

inline constexpr const char* kClosedCrossoverGrammar = R"({
  "format_version":"grammar-definition-v2",
  "entry":{"nonterminal":"Main","type":"Int"},
  "search_limits":{"max_nodes":20,"max_depth":10},
  "execution_limits":{"fuel":100},
  "nonterminals":[
    {"id":"Main","type":"Int","scope":[],"variation":false,"alternatives":[
      {"id":"body","weight":1,"expression":{"signature":"add(Int,Int)->Int","args":[
        {"ref":"Outside"},{"signature":"let(Int,Int)->Int","bind":{"1":["x"]},"args":[
          {"constant":{"type":"Int","values":["1"]}},{"ref":"Inside"}]}]}}]},
    {"id":"Outside","type":"Int","scope":[],"alternatives":[
      {"id":"literal","weight":1,"crossover_group":"value","crossover_scope":"closed",
       "expression":{"constant":{"type":"Int","range":["0","3"],
         "sample_from":{"type":"Int","values":["1"]}}}}]},
    {"id":"Inside","type":"Int","scope":[{"name":"x","type":"Int"}],"alternatives":[
      {"id":"literal","weight":1,"crossover_group":"value","crossover_scope":"closed",
       "expression":{"constant":{"type":"Int","range":["0","3"],
         "sample_from":{"type":"Int","values":["2"]}}}},
      {"id":"capture","weight":1,"generation_stages":[],
       "crossover_group":"value","crossover_scope":"closed","expression":{"bound":"x"}}]}
  ]})";
