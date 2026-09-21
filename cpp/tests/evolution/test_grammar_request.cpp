#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/cli/grammar_artifact.hpp"
#include "gagp/evolution/genome_generation.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/population_init.hpp"

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;
using namespace gagp::cli_detail;

namespace {
using Json = JsonValue;

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

Json json(const std::string& text) { return JsonParser(text, {true, 512}).parse(); }

CompiledGrammar compile(const std::string& text) {
  return compile_grammar(parse_definition(text));
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

std::uint32_t nonterminal(const CompiledGrammar& grammar, const std::string& stable_id) {
  const auto found = std::find_if(grammar.nonterminals().begin(), grammar.nonterminals().end(),
      [&](const CompiledNonterminal& value) { return value.stable_id == stable_id; });
  if (found == grammar.nonterminals().end()) throw std::runtime_error("missing fixture nonterminal " + stable_id);
  return found->id;
}

bool same_request(const GenerationRequest& left, const GenerationRequest& right) {
  if (left.nonterminal != right.nonterminal || left.type != right.type ||
      left.budget.max_nodes != right.budget.max_nodes || left.budget.max_depth != right.budget.max_depth ||
      left.visible_environment.size() != right.visible_environment.size()) return false;
  for (std::size_t i = 0; i < left.visible_environment.size(); ++i) {
    if (left.visible_environment[i].name != right.visible_environment[i].name ||
        left.visible_environment[i].type != right.visible_environment[i].type) return false;
  }
  return true;
}

std::uint32_t prefix_depth(const AstProgram& ast) {
  std::size_t cursor = 0;
  std::function<std::uint32_t()> visit = [&]() {
    const auto arity = node_descriptor(ast.nodes.at(cursor++).kind).prefix_arity;
    std::uint32_t depth = 1;
    for (int child = 0; child < arity; ++child) depth = std::max(depth, 1 + visit());
    return depth;
  };
  const auto result = visit();
  check(cursor == ast.nodes.size(), "generated request AST is not one complete prefix tree");
  return result;
}

const char* request_fixture = R"({
  "format_version":"grammar-definition-v2",
  "entry":{"nonterminal":"Expr","type":"Int"},
  "search_limits":{"max_nodes":20,"max_depth":8},
  "execution_limits":{"fuel":1000},
  "nonterminals":[
    {"id":"Expr","type":"Int","scope":[],"alternatives":[
      {"id":"leaf","weight":1,"expression":{"constant":{"type":"Int","range":["0","3"]}}},
      {"id":"sum","weight":4,"expression":{"signature":"add(Int,Int)->Int","args":[{"ref":"Expr"},{"ref":"Expr"}]}}
    ]},
    {"id":"OtherInt","type":"Int","scope":[],"alternatives":[
      {"id":"seven","weight":1,"expression":{"constant":{"type":"Int","values":["7"]}}}
    ]},
    {"id":"OtherBool","type":"Bool","scope":[],"alternatives":[
      {"id":"true","weight":1,"expression":{"constant":{"type":"Bool","values":[true]}}}
    ]},
    {"id":"Scoped","type":"Int","scope":[{"name":"x","type":"Int"},{"name":"label","type":"String"}],"alternatives":[
      {"id":"nine","weight":1,"expression":{"constant":{"type":"Int","values":["9"]}}}
    ]},
    {"id":"BoundScoped","type":"Int","scope":[{"name":"x","type":"Int"}],"alternatives":[
      {"id":"x","weight":1,"expression":{"bound":"x"}}
    ]},
    {"id":"Deep","type":"Int","scope":[],"alternatives":[
      {"id":"add","weight":1,"expression":{"signature":"add(Int,Int)->Int","args":[
        {"constant":{"type":"Int","values":["1"]}},{"constant":{"type":"Int","values":["2"]}}]}}
    ]},
    {"id":"ProgramRoot","category":"Program","type":"Int","scope":[],"alternatives":[
      {"id":"return","weight":1,"expression":{"control":"program(Block)->Program","type":"Int","args":[
        {"control":"block_cons(Statement,Block)->Block","type":"Int","args":[
          {"control":"return(Int)->Statement","type":"Int","args":[{"constant":{"type":"Int","values":["5"]}}]},
          {"control":"block_nil()->Block","type":"Int","args":[]}]}
      ]}}
    ]},
    {"id":"StatementRoot","category":"Statement","type":"Int","scope":[],"alternatives":[
      {"id":"return","weight":1,"expression":{"control":"return(Int)->Statement","type":"Int","args":[
        {"constant":{"type":"Int","values":["4"]}}]}}
    ]}
  ]
})";

void test_entry_and_non_entry_requests(const CompiledGrammar& grammar) {
  const auto automatic = entry_request(grammar);
  check(automatic.nonterminal == grammar.entry() && automatic.type == RType::Int &&
        automatic.visible_environment.empty() && automatic.budget.max_nodes == 20 &&
        automatic.budget.max_depth == 8, "entry_request did not reproduce the compiled entry contract");

  const auto implicit = generate_derivation(grammar, 123);
  const auto explicit_entry = generate_derivation(grammar, 123, automatic);
  check(encode_generated_artifact(grammar, implicit) == encode_generated_artifact(grammar, explicit_entry),
        "default generation overload differs from an exact entry request");

  GenerationRequest other{nonterminal(grammar, "OtherInt"), RType::Int, {}, {5, 4}};
  const auto generated = generate_derivation(grammar, 9, other);
  check(generated.genome.ast.nodes.size() == 5 && generated.genome.ast.consts.at(0).i == 7,
        "same-type non-entry request did not select its requested nonterminal");
  check(same_request(generated.derivation.request, other) &&
        generated.derivation.request_scope_mapping.empty() && generated.genome.derivation &&
        same_request(generated.genome.derivation->request, other),
        "request contract was not retained in result and genome metadata");
  require_membership(grammar, generated.genome, other);

  GenerationRequest boolean{nonterminal(grammar, "OtherBool"), RType::Bool, {}, {5, 4}};
  const auto different_type = generate_derivation(grammar, 10, boolean);
  check(different_type.genome.ast.consts.at(0).tag == ValueTag::Bool,
        "different-type non-entry request was coerced to the default entry type");
  require_membership(grammar, different_type.genome, boolean);

  GenerationRequest program{nonterminal(grammar, "ProgramRoot"), RType::Int, {}, {5, 4}};
  const auto complete_program = generate_derivation(grammar, 11, program);
  check(complete_program.genome.ast.nodes.size() == 5 && prefix_depth(complete_program.genome.ast) == 4 &&
        complete_program.derivation.derived_nodes == 5,
        "Program request received an expression envelope or escaped its complete budget");
  require_membership(grammar, complete_program.genome, program);
}

void test_reduced_budget_and_forwarding(const CompiledGrammar& grammar) {
  GenerationRequest request{nonterminal(grammar, "Expr"), RType::Int, {}, {9, 6}};
  for (std::uint64_t seed = 0; seed < 64; ++seed) {
    const auto generated = generate_derivation(grammar, seed, request);
    check(generated.genome.ast.nodes.size() <= request.budget.max_nodes &&
          prefix_depth(generated.genome.ast) <= request.budget.max_depth,
          "reduced request budget did not count the full expression envelope");
    check(generated.derivation.search_limits.max_nodes == request.budget.max_nodes &&
          generated.derivation.search_limits.max_depth == request.budget.max_depth &&
          generated.derivation.derived_nodes + 4 == generated.genome.ast.nodes.size(),
          "request budget or expression envelope accounting was lost from metadata");
    require_membership(grammar, generated.genome, request);

    const auto forwarded = generate_random_genome(seed, grammar, request);
    check(ast_cache_key(forwarded.ast) == ast_cache_key(generated.genome.ast) && forwarded.derivation &&
          same_request(forwarded.derivation->request, request),
          "generate_random_genome did not forward the generation request exactly");
  }

  CaseSet cases;
  cases.expected_return_type = RType::Int;
  cases.expected_values = {Value::from_int(0)};
  const auto population = initialize_population(grammar, cases, 4, 77, request);
  check(population.population.size() == 4 && !population.replayed,
        "requested population initialization returned the wrong population contract");
  for (std::size_t i = 0; i < population.population.size(); ++i) {
    const auto direct = generate_derivation(grammar, 77 + i, request);
    check(ast_cache_key(population.population[i].ast) == ast_cache_key(direct.genome.ast) &&
          population.population[i].derivation && same_request(population.population[i].derivation->request, request),
          "population initialization did not forward request with seed+i replay");
  }
}

void test_validation_and_scope_mapping(const CompiledGrammar& grammar) {
  const auto expr = nonterminal(grammar, "Expr");
  const auto deep = nonterminal(grammar, "Deep");
  const auto statement = nonterminal(grammar, "StatementRoot");
  const auto scoped = nonterminal(grammar, "Scoped");
  rejects([&] { validate_request(grammar, {kNoGrammarId, RType::Int, {}, {5, 4}}); },
      "unknown request nonterminal was accepted");
  for (auto type : {RType::Float, RType::Any, RType::Invalid})
    rejects([&] { validate_request(grammar, {expr, type, {}, {5, 4}}); },
        "request type mismatch or wildcard was accepted");
  rejects([&] { validate_request(grammar, {statement, RType::Int, {}, {5, 4}}); },
      "non-Expression/Program request was accepted");
  rejects([&] { validate_request(grammar, {expr, RType::Int, {}, {0, 4}}); },
      "zero node request budget was accepted");
  rejects([&] { validate_request(grammar, {expr, RType::Int, {}, {5, 0}}); },
      "zero depth request budget was accepted");
  rejects([&] { validate_request(grammar, {expr, RType::Int, {}, {21, 8}}); },
      "request node budget above compiled limits was accepted");
  rejects([&] { validate_request(grammar, {expr, RType::Int, {}, {20, 9}}); },
      "request depth budget above compiled limits was accepted");
  rejects([&] { validate_request(grammar, {expr, RType::Int, {}, {4, 8}}); },
      "expression request without room for its node envelope was accepted");
  rejects([&] { validate_request(grammar, {expr, RType::Int, {}, {20, 3}}); },
      "expression request without room for its depth envelope was accepted");
  rejects([&] { validate_request(grammar, {deep, RType::Int, {}, {6, 8}}); },
      "request with no feasible derivation was accepted");

  rejects([&] { validate_request(grammar, {scoped, RType::Int, {}, {5, 4}}); },
      "missing visible environment was accepted");
  rejects([&] { validate_request(grammar, {scoped, RType::Int,
      {{"x", RType::Int}, {"x", RType::Int}, {"label", RType::String}}, {5, 4}}); },
      "duplicate visible binding was accepted");
  for (auto wrong : {RType::Float, RType::Any, RType::Invalid})
    rejects([&] { validate_request(grammar, {scoped, RType::Int,
        {{"x", wrong}, {"label", RType::String}}, {5, 4}}); },
        "mistyped visible binding or wildcard coercion was accepted");

  GenerationRequest request{scoped, RType::Int,
      {{"extra", RType::Bool}, {"label", RType::String}, {"x", RType::Int}}, {5, 4}};
  check(validate_request(grammar, request) == std::vector<std::uint32_t>({2, 1}),
        "required scope did not map exactly into reordered visible bindings with extras");
  const auto generated = generate_derivation(grammar, 44, request);
  check(generated.derivation.request_scope_mapping == std::vector<std::uint32_t>({2, 1}) &&
        generated.genome.derivation &&
        generated.genome.derivation->request_scope_mapping == std::vector<std::uint32_t>({2, 1}),
        "stable request scope mapping was not retained in metadata");
  require_membership(grammar, generated.genome, request);
}

void test_artifact_request_replay(const CompiledGrammar& grammar) {
  GenerationRequest request{nonterminal(grammar, "Scoped"), RType::Int,
      {{"extra", RType::Bool}, {"label", RType::String}, {"x", RType::Int}}, {5, 4}};
  const auto artifact = encode_generated_artifact(grammar, generate_derivation(grammar, 91, request));
  const auto root = json(artifact);
  const auto& encoded_request = root.object_v.at("request");
  check(encoded_request.object_v.at("nonterminal").number_v == request.nonterminal &&
        encoded_request.object_v.at("type").string_v == "Int" &&
        encoded_request.object_v.at("visible_environment").array_v.size() == 3 &&
        encoded_request.object_v.at("scope_mapping").array_v[0].number_v == 2 &&
        encoded_request.object_v.at("scope_mapping").array_v[1].number_v == 1 &&
        !encoded_request.object_v.count("budget") &&
        root.object_v.at("search_limits").object_v.at("max_nodes").number_v == 5 &&
        root.object_v.at("search_limits").object_v.at("max_depth").number_v == 4,
        "artifact did not encode the complete request contract at its canonical locations");
  check(encode_generated_artifact(grammar, replay_generated_artifact(artifact, &grammar)) == artifact,
        "non-entry scoped request did not replay byte-exactly");

  const auto tamper = [&](const std::function<void(Json&)>& change, const char* message) {
    auto changed = root;
    change(changed);
    rejects([&] { replay_generated_artifact(canonical_json(changed), &grammar); }, message);
  };
  tamper([](Json& value) { value.object_v.at("request").object_v.at("type").string_v = "Float"; },
      "tampered artifact request type was accepted");
  tamper([](Json& value) { value.object_v.at("request").object_v.at("scope_mapping").array_v[0].number_v = 0; },
      "tampered artifact request scope mapping was accepted");
  tamper([](Json& value) { value.object_v.at("request").object_v.at("visible_environment").array_v[2]
      .object_v.at("type").string_v = "Float"; }, "tampered artifact visible environment was accepted");
  tamper([](Json& value) { value.object_v.at("search_limits").object_v.at("max_nodes").number_v = 4; },
      "tampered artifact request budget was accepted");
}

void test_bound_runtime_boundary(const CompiledGrammar& grammar) {
  GenerationRequest bound{nonterminal(grammar, "BoundScoped"), RType::Int,
      {{"x", RType::Int}}, {5, 4}};
  check(validate_request(grammar, bound) == std::vector<std::uint32_t>({0}),
        "valid Bound request failed before the execution boundary");
  rejects([&] { generate_derivation(grammar, 0, bound); },
      "external Bound request reached materialization", "materialization frame");
  GenerationRequest unrelated{nonterminal(grammar, "OtherInt"), RType::Int, {}, {5, 4}};
  const auto constant = generate_derivation(grammar, 0, unrelated);
  rejects([&] { require_membership(grammar, constant.genome, bound); },
      "external Bound request accepted an unrelated constant", "cannot be derived");

  const auto default_bound = compile(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","type":"Int"},
    "search_limits":{"max_nodes":12,"max_depth":6},"execution_limits":{"fuel":100},
    "nonterminals":[
      {"id":"Main","type":"Int","scope":[],"alternatives":[{"id":"let","weight":1,
        "expression":{"signature":"let(Int,Int)->Int","args":[
          {"constant":{"type":"Int","values":["0"]}},{"bound":"x"}],"bind":{"1":["x"]}}}]},
      {"id":"Safe","type":"Int","scope":[],"alternatives":[{"id":"constant","weight":1,
        "expression":{"constant":{"type":"Int","values":["8"]}}}]}
    ]
  })");
  const auto bound_entry = generate_derivation(default_bound, 1);
  check(bound_entry.genome.ast.nodes.at(3).kind == NodeKind::LET_REGION &&
        bound_entry.genome.ast.lexical_regions.size() == 1,
        "closed lexical entry did not materialize a native region");
  require_membership(default_bound, bound_entry.genome);
  GenerationRequest safe{nonterminal(default_bound, "Safe"), RType::Int, {}, {5, 4}};
  const auto generated = generate_derivation(default_bound, 1, safe);
  check(generated.genome.ast.consts.at(0).i == 8,
        "lexical default entry blocked a separate executable request");
  require_membership(default_bound, generated.genome, safe);
}
}  // namespace

int main() {
  try {
    const auto grammar = compile(request_fixture);
    test_entry_and_non_entry_requests(grammar);
    test_reduced_budget_and_forwarding(grammar);
    test_validation_and_scope_mapping(grammar);
    test_artifact_request_replay(grammar);
    test_bound_runtime_boundary(grammar);
    std::cout << "grammar requests: scoped non-entry generation, full budgets, replay, and rejection cases passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
