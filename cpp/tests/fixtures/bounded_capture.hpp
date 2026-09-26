#pragma once

#include <string>

#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/serialization/region_plan_json.hpp"

namespace gagp::test {
inline std::string bounded_capture_definition() {
  using namespace gagp;
  using namespace gagp::evo;
  using namespace gagp::evo::grammar;
  RegionPlan plan;
  plan.state_types = {ValueTag::Int};
  plan.parameter_types = {ValueTag::Int};
  plan.result_type = ValueTag::Int;
  plan.coordinate_slots = {0}; plan.coordinate_rank = {{0, 1}};
  plan.coordinate_domains = {{{RegionBoundKind::Literal, 0, 0}, {RegionBoundKind::Literal, 5, 0}}};
  RegionStateTransition edge;
  edge.kind = RegionTransitionKind::CoordinateOffset; edge.offset = -1;
  plan.requests = {{{edge}}}; plan.memoized = true; plan.limits = {8, 8, 1};
  std::string text = R"JSON({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","type":"Int"},
    "search_limits":{"max_nodes":100,"max_depth":20},
    "execution_limits":{"fuel":1000},
    "templates":[{"id":"Pair","type":"Int","scope":[],
      "holes":[{"id":"recurrence","type":"Int","scope":[{"name":"x","type":"Int"}]}],
      "body":{"signature":"add(Int,Int)->Int","args":[
        {"signature":"let(Int,Int)->Int","args":[{"constant":{"type":"Int","values":["1"]}},
          {"hole":"recurrence"}],"bind":{"1":["x"]}},
        {"signature":"let(Int,Int)->Int","args":[{"constant":{"type":"Int","values":["2"]}},
          {"hole":"recurrence"}],"bind":{"1":["x"]}}]}}],
    "nonterminals":[
      {"id":"Main","type":"Int","scope":[],"alternatives":[{"id":"main","weight":1,
        "expression":{"template":"Pair","holes":{"recurrence":{"ref":"Recurrence"}}}}]},
      {"id":"Recurrence","type":"Int","scope":[{"name":"x","type":"Int"}],"alternatives":[
        {"id":"region","weight":1,"expression":{
          "structured":{"family":"bounded","plan":PLAN},
          "captures":[{"bound":"x"}],
          "phases":[
            {"argument":1,"bindings":[{"bank":"state","slot":0,"name":"n"}]},
            {"argument":2,"bindings":[{"bank":"parameter","slot":0,"name":"seed"}]},
            {"argument":3,"bindings":[{"bank":"result","slot":0,"name":"child"}]},
            {"argument":4,"bindings":[]}],
          "args":[{"constant":{"type":"Int","values":["3"]}},
            {"signature":"le(Int,Int)->Bool","args":[{"bound":"n"},{"constant":{"type":"Int","values":["0"]}}]},
            {"ref":"Base"},
            {"signature":"add(Int,Int)->Int","args":[{"bound":"child"},{"constant":{"type":"Int","values":["1"]}}]},
            {"constant":{"type":"Int","values":["0"]}}]}}]},
      {"id":"Base","type":"Int","scope":[{"name":"seed","type":"Int"}],"alternatives":[
        {"id":"seed","weight":1,"expression":{"bound":"seed"}},
        {"id":"plus","weight":1,"expression":{"signature":"add(Int,Int)->Int","args":[
          {"bound":"seed"},{"constant":{"type":"Int","values":["10"]}}]}}]}]
  })JSON";
  text.replace(text.find("PLAN"), 4, canonical_json(serialization::encode_region_plan(plan)));
  return text;
}
}  // namespace gagp::test
