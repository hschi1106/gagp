#include <iostream>
#include <stdexcept>
#include <limits>

#include "gagp/evolution/grammar/compiled.hpp"
#include "gagp/evolution/grammar/random.hpp"

using namespace gagp::evo::grammar;
using gagp::cli_detail::JsonParser;
using Json = gagp::cli_detail::JsonValue;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template <class Action> void rejects(Action action, const std::string& diagnostic) {
  try { action(); }
  catch (const std::exception& error) {
    if (std::string(error.what()).find(diagnostic) != std::string::npos) return;
    throw std::runtime_error("unexpected diagnostic: " + std::string(error.what()));
  }
  throw std::runtime_error("missing rejection: " + diagnostic);
}
Json json(const std::string& text) { return JsonParser(text, {true, 256}).parse(); }
const std::string custom = R"({
  "format_version":"grammar-definition-v2",
  "entry":{"nonterminal":"Expr","type":"Int"},
  "inputs":[{"name":"n","type":"Int"}],
  "search_limits":{"max_nodes":20,"max_depth":8},
  "execution_limits":{"fuel":100},
  "nonterminals":[
    {"id":"Expr","type":"Int","scope":[],"alternatives":[
      {"id":"zero","weight":1,"expression":{"constant":{"type":"Int","values":["0"]}}},
      {"id":"input","weight":1,"expression":{"input":"n"}},
      {"id":"sum","weight":2,"expression":{"signature":"add(Int,Int)->Int","args":[{"ref":"Expr"},{"ref":"Expr"}]}},
      {"id":"local","weight":1,"expression":{"signature":"let(Int,Int)->Int","args":[{"ref":"Expr"},{"ref":"Narrow"}],"bind":{"1":["x"]}}}
    ]},
    {"id":"Narrow","type":"Int","scope":[{"name":"x","type":"Int"}],"alternatives":[
      {"id":"value","weight":1,"expression":{"bound":"x"}}
    ]}
  ]
})";
CompiledGrammar compile(Json doc) { return compile_grammar(parse_definition(canonical_json(doc))); }
Json& alternatives(Json& doc, std::size_t nt = 0) { return doc.object_v.at("nonterminals").array_v.at(nt).object_v.at("alternatives"); }
}  // namespace
int main() {
  try {
    const auto example = compile_grammar(load_definition(std::string(GAGP_REPOSITORY_ROOT) +
        "/configs/grammar_definitions/custom_integer.json"));
    check(example.productions().size() == 5, "documented custom grammar did not compile");
    rejects([] { compile_grammar(load_definition(std::string(GAGP_REPOSITORY_ROOT) +
        "/cpp/tests/fixtures/grammar/invalid_scope.json")); }, "not visible");
    const auto grammar = compile(json(custom));
    GrammarRandom golden(0);
    check(golden.next() == UINT64_C(0xe220a8397b1dcdaf) && golden.next() == UINT64_C(0x6e789e6aa1b965f4) &&
          golden.next() == UINT64_C(0x06c45d188009454f), "grammar RNG version changed");
    GrammarRandom random(42);
    check(random.integer(std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::min()) ==
          std::numeric_limits<std::int64_t>::min(), "minimum Int sampling overflow");
    check(random.integer(std::numeric_limits<std::int64_t>::max(), std::numeric_limits<std::int64_t>::max()) ==
          std::numeric_limits<std::int64_t>::max(), "maximum Int sampling overflow");
    bool negative = false, positive = false;
    unsigned sums = 0;
    for (unsigned i = 0; i < 10000; ++i) {
      const auto value = random.integer(std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max());
      negative |= value < 0; positive |= value >= 0;
      const auto chosen = random.production(grammar, {0, 2});
      check(chosen == 0 || chosen == 2, "sampling enabled an excluded production");
      sums += chosen == 2;
    }
    check(negative && positive && sums > 6000 && sums < 7300, "sampling range or eligible weight normalization changed");
    rejects([&] { random.production(grammar, {}); }, "no grammar production");
    check(grammar.nonterminals().size() == 2 && grammar.productions().size() == 5, "compiled rule count");
    check(grammar.nonterminals()[0].minimum_nodes == 1 && grammar.nonterminals()[0].minimum_depth == 1, "recursive minimum cost");
    check(grammar.productions_for_type(gagp::evo::RType::Int).size() == 5, "type index");
    check(grammar.productions_for_type(gagp::evo::RType::Bool).empty(), "type index leaked overload");
    const auto& entry_context = grammar.contexts()[grammar.nonterminals()[0].context];
    const auto& narrow_context = grammar.contexts()[grammar.nonterminals()[1].context];
    check(entry_context.nonterminals.size() == 1 && narrow_context.nonterminals.size() == 2,
          "context index admitted an unavailable binding");
    check(narrow_context.nonterminals[1].scope_mapping == std::vector<std::uint32_t>{0},
          "context binding mapping changed");
    const auto sum = grammar.productions()[2]; // IDs sorted input, local, sum, zero.
    check(sum.stable_id == "Expr/sum" && sum.minimum_nodes_by_depth[1] == kNoGrammarId &&
          sum.minimum_nodes_by_depth[2] == 3, "minimum depth/node table");
    grammar.require_executable();
    const auto roundtrip = compile_grammar(parse_definition(grammar.canonical_definition()));
    check(roundtrip.content_hash() == grammar.content_hash(), "compiled identity roundtrip");
    for (std::size_t i = 0; i < grammar.productions().size(); ++i)
      check(grammar.productions()[i].stable_id == roundtrip.productions()[i].stable_id, "numeric IDs changed");

    auto doc = json(R"({
      "format_version":"grammar-definition-v2",
      "entry":{"nonterminal":"Expr","type":"Int"},
      "inputs":[{"name":"xs","type":"IntList"}],
      "search_limits":{"max_nodes":30,"max_depth":10},
      "execution_limits":{"fuel":100},
      "nonterminals":[
        {"id":"Expr","type":"Int","scope":[],"alternatives":[
          {"id":"fold","weight":1,"expression":{
            "signature":"traverse_range_reverse(IntList,Int,Int,Int,Int,Int)->Int",
            "args":[{"input":"xs"},{"constant":{"type":"Int","values":["0"]}},
              {"constant":{"type":"Int","values":["0"]}},
              {"constant":{"type":"Int","values":["3"]}},
              {"constant":{"type":"Int","values":["0"]}},{"ref":"Step"}],
            "bind":{"5":["element","index","accumulator"]}}}]},
        {"id":"Step","type":"Int","scope":[
          {"name":"element","type":"Int"},{"name":"index","type":"Int"},
          {"name":"accumulator","type":"Int"}],"alternatives":[
          {"id":"sum","weight":1,"expression":{"signature":"add(Int,Int)->Int",
            "args":[{"bound":"accumulator"},{"bound":"element"}]}}]}
      ]
    })");
    const auto ranged_traversal = compile(doc);
    ranged_traversal.require_executable();
    check(ranged_traversal.nonterminals()[0].minimum_nodes == 9,
          "ranged traversal schema lost primitive or body materialization cost");
    doc.object_v.at("nonterminals").array_v[1].object_v.at("scope").array_v.push_back(
        json(R"({"name":"outside","type":"Int"})"));
    rejects([&] { compile(doc); }, "not visible");

    // Authored profiles are accepted only on physical owners, validated against
    // that NodeKind, and retained in canonical FuelEvent order.
    doc = json(custom);
    auto& profiled_sum = alternatives(doc).array_v[2].object_v.at("expression");
    profiled_sum.object_v["fuel_events"] = json(R"({"operation":0})");
    auto profiled = compile(doc);
    const auto& sum_profile = profiled.expressions().at(
        profiled.productions().at(2).expression).fuel_charges;
    check(sum_profile.size() == 1 &&
              sum_profile[0].event == gagp::evo::FuelEvent::Operation &&
              sum_profile[0].cost == 0,
          "compiled fuel profile lost explicit zero");

    auto binding_profiles_doc = json(custom);
    binding_profiles_doc.object_v["locals"] =
        json(R"([{"name":"saved","type":"Int"}])");
    alternatives(binding_profiles_doc).array_v[1]
        .object_v.at("expression").object_v["fuel_events"] =
        json(R"({"operation":0})");
    alternatives(binding_profiles_doc, 1).array_v[0]
        .object_v.at("expression").object_v["fuel_events"] =
        json(R"({"operation":2})");
    alternatives(binding_profiles_doc).array_v.push_back(json(R"({
      "id":"saved","weight":1,"expression":{"local":"saved",
      "fuel_events":{"operation":3}}})"));
    const auto binding_profiles = compile(binding_profiles_doc);
    bool saw_input = false, saw_bound = false, saw_local = false;
    for (const auto& expression : binding_profiles.expressions()) {
      if (expression.fuel_charges.empty()) continue;
      saw_input |= expression.kind == ExpressionKind::Input &&
          expression.fuel_charges[0].cost == 0;
      saw_bound |= expression.kind == ExpressionKind::Bound &&
          expression.fuel_charges[0].cost == 2;
      saw_local |= expression.kind == ExpressionKind::Local &&
          expression.fuel_charges[0].cost == 3;
    }
    check(saw_input && saw_bound && saw_local,
          "binding-leaf fuel profiles did not retain their concrete owners");

    auto traversal_doc = json(R"({
      "format_version":"grammar-definition-v2",
      "entry":{"nonterminal":"Expr","type":"Int"},
      "inputs":[{"name":"xs","type":"IntList"}],
      "search_limits":{"max_nodes":20,"max_depth":8},
      "execution_limits":{"fuel":100},
      "nonterminals":[{"id":"Expr","type":"Int","scope":[],"alternatives":[{
        "id":"fold","weight":1,"expression":{
          "signature":"traverse_range_reverse(IntList,Int,Int,Int,Int,Int)->Int",
          "fuel_events":{"result":0,"test_cursor":4,"store_sequence":2},
          "args":[{"input":"xs"},{"constant":{"type":"Int","values":["0"]}},
            {"constant":{"type":"Int","values":["0"]}},
            {"constant":{"type":"Int","values":["1"]}},
            {"constant":{"type":"Int","values":["0"]}},
            {"bound":"acc"}],"bind":{"5":["elem","index","acc"]}}
      }]}]})");
    const auto traversal_profiled = compile(traversal_doc);
    const auto& charges = traversal_profiled.expressions().at(
        traversal_profiled.productions().front().expression).fuel_charges;
    check(charges.size() == 3 &&
              charges[0].event == gagp::evo::FuelEvent::StoreSequence &&
              charges[1].event == gagp::evo::FuelEvent::TestCursor &&
              charges[2].event == gagp::evo::FuelEvent::Result,
          "compiled fuel charges are not in canonical event order");

    const auto rejects_profile = [&](const std::string& profile,
                                     const std::string& diagnostic) {
      auto invalid = json(custom);
      alternatives(invalid).array_v[0].object_v.at("expression")
          .object_v["fuel_events"] = json(profile);
      rejects([&] { compile(invalid); }, diagnostic);
    };
    rejects_profile("{}", "nonempty object");
    rejects_profile(R"({"operation":true})", "uint32 integer");
    rejects_profile(R"({"operation":1.5})", "uint32 integer");
    rejects_profile(R"({"operation":-1})", "uint32 integer");
    rejects_profile(R"({"operation":2147483648})", "0..INT_MAX");
    rejects_profile(R"({"unknown":1})", "unknown fuel event");
    rejects_profile(R"({"branch_test":1})", "not supported");

    auto annotated_alias = json(custom);
    alternatives(annotated_alias).array_v[0].object_v.at("expression") =
        json(R"({"ref":"Expr","fuel_events":{"operation":1}})");
    rejects([&] { compile(annotated_alias); }, "unknown key fuel_events");

    doc = json(custom);
    alternatives(doc).array_v[3].object_v.at("expression").object_v.at("args").array_v[0] = json(R"({"ref":"Narrow"})");
    rejects([&] { compile(doc); }, "not visible");
    doc = json(custom);
    alternatives(doc, 1).array_v[0].object_v.at("expression") = json(R"({"bound":"hidden"})");
    rejects([&] { compile(doc); }, "not visible");
    doc = json(custom);
    alternatives(doc).array_v[2].object_v.at("expression").object_v.at("signature") = json(R"("add(Float,Float)->Float")");
    rejects([&] { compile(doc); }, "argument type mismatch");
    doc = json(custom);
    alternatives(doc).array_v[0].object_v.at("weight") = json("0");
    rejects([&] { compile(doc); }, "positive and finite");
    doc = json(custom);
    alternatives(doc).array_v[0].object_v["hook"] = json("true");
    rejects([&] { compile(doc); }, "unknown key");
    doc = json(custom);
    alternatives(doc).array_v = {json(R"({"id":"cycle","weight":1,"expression":{"ref":"Expr"}})")};
    rejects([&] { compile(doc); }, "unproductive");
    doc = json(custom);
    alternatives(doc).array_v = {json(R"({"id":"missing","weight":1,"expression":{"ref":"Unknown"}})")};
    rejects([&] { compile(doc); }, "unknown nonterminal");
    doc = json(custom);
    alternatives(doc).array_v = {json(R"({"id":"fixed","weight":1,"expression":{"signature":"add(Int,Int)->Int","args":[{"constant":{"type":"Int","values":["1"]}},{"constant":{"type":"Int","values":["2"]}}]}})")};
    doc.object_v.at("search_limits").object_v.at("max_nodes") = json("2");
    rejects([&] { compile(doc); }, "search budget");
    doc.object_v.at("search_limits").object_v.at("max_nodes") = json("3");
    doc.object_v.at("search_limits").object_v.at("max_depth") = json("1");
    rejects([&] { compile(doc); }, "search budget");
    doc.object_v.at("search_limits").object_v.at("max_depth") = json("2");
    check(compile(doc).nonterminals()[0].minimum_nodes == 3, "exact feasible budget rejected");
    // Productive mutual aliases reach the same fixed point independent of ID order.
    doc = json(custom);
    doc.object_v.at("nonterminals") = json(R"([
      {"id":"Expr","type":"Int","scope":[],"alternatives":[{"id":"other","weight":1,"expression":{"ref":"Other"}}]},
      {"id":"Other","type":"Int","scope":[],"alternatives":[
        {"id":"cycle","weight":1,"expression":{"ref":"Expr"}},
        {"id":"leaf","weight":1,"expression":{"constant":{"type":"Int","values":["7"]}}}]}])");
    const auto mutual = compile(doc);
    check(mutual.nonterminals()[0].minimum_nodes == 1 && mutual.nonterminals()[1].minimum_nodes == 1,
          "productive mutual references rejected");
    mutual.require_executable();

    doc = json(custom);
    doc.object_v["templates"] = json(R"([{
      "id":"Local","type":"Int","scope":[],
      "holes":[{"id":"body","type":"Int","scope":[{"name":"x","type":"Int"}]}],
      "body":{"signature":"let(Int,Int)->Int","args":[
        {"constant":{"type":"Int","values":["4"]}},{"hole":"body"}],"bind":{"1":["x"]}}
    }])");
    alternatives(doc).array_v = {json(R"({"id":"local","weight":1,"expression":{
      "template":"Local","holes":{"body":{"ref":"Narrow"}}}})")};
    const auto templated = compile(doc);
    check(templated.templates().size() == 1 && templated.templates()[0].holes.size() == 1, "template slots missing");
    check(templated.nonterminals()[0].minimum_nodes == 3 && templated.nonterminals()[0].minimum_depth == 2,
          "template wrappers charged as AST nodes");
    bool fixed_body = false, open_hole = false;
    for (const auto& expr : templated.expressions()) {
      if (expr.kind == ExpressionKind::Primitive && expr.template_id == 0) fixed_body |= expr.fixed;
      if (expr.kind == ExpressionKind::Hole) open_hole |= !expr.fixed && expr.scope_mapping.size() == 1;
    }
    check(fixed_body && open_hole, "template skeleton and hole contracts lost");
    auto annotated_template = doc;
    alternatives(annotated_template).array_v[0].object_v.at("expression")
        .object_v["fuel_events"] = json(R"({"operation":1})");
    rejects([&] { compile(annotated_template); }, "unknown key fuel_events");
    auto annotated_hole = doc;
    annotated_hole.object_v.at("templates").array_v[0].object_v.at("body")
        .object_v.at("args").array_v[1].object_v["fuel_events"] =
        json(R"({"operation":1})");
    rejects([&] { compile(annotated_hole); }, "unknown key fuel_events");
    const auto template_doc = doc;
    alternatives(doc).array_v[0].object_v.at("expression").object_v.at("holes").object_v.at("body") =
        json(R"({"constant":{"type":"Bool","values":[true]}})");
    rejects([&] { compile(doc); }, "hole result type mismatch");
    doc = template_doc;
    doc.object_v.at("templates").array_v[0].object_v.at("holes").array_v[0].object_v.at("scope") = json("[]");
    rejects([&] { compile(doc); }, "not visible");
    doc = template_doc;
    doc.object_v.at("templates").array_v[0].object_v.at("body") = json(R"({"template":"Local","holes":{"body":{"ref":"Narrow"}}})");
    rejects([&] { compile(doc); }, "template expansion cycle");
    doc = template_doc;
    doc.object_v.at("templates").array_v[0].object_v.at("body").object_v.at("args").array_v[1] = json(R"({"ref":"Narrow"})");
    rejects([&] { compile(doc); }, "declared hole");
    doc = template_doc;
    doc.object_v.at("templates").array_v[0].object_v.at("body").object_v.at("args").array_v[1] = json(R"({"bound":"x"})");
    rejects([&] { compile(doc); }, "must occur");

    doc = json(custom);
    alternatives(doc).array_v = {json(R"({"id":"general","weight":1,"expression":{
      "structured":{"family":"recur","state_types":["Int"],"result_type":"Int","requests":1},
      "args":[{"input":"n"},{"constant":{"type":"Bool","values":[true]}},
        {"constant":{"type":"Int","values":["0"]}},{"bound":"s"},{"bound":"r"}],
      "bind":{"1":["s"],"2":["s"],"3":["s"],"4":["s","r"]}}})")};
    const auto declared_recursion = compile(doc);
    check(declared_recursion.structured_contracts().size() == 1 &&
          declared_recursion.nonterminals()[0].minimum_nodes == 6, "structured expression lost static regions");
    rejects([&] { declared_recursion.require_executable(); }, "execution is not implemented");
    alternatives(doc).array_v[0].object_v.at("expression").object_v.at("args").array_v[3] = json(R"({"bound":"r"})");
    rejects([&] { compile(doc); }, "not visible");
    doc = template_doc;
    auto& body_args = doc.object_v.at("templates").array_v[0].object_v.at("body").object_v.at("args");
    body_args.array_v[1] = json(R"({"signature":"add(Int,Int)->Int","args":[{"hole":"body"},{"hole":"body"}]})");
    const auto duplicate_hole = compile(doc);
    check(duplicate_hole.nonterminals()[0].minimum_nodes == 5 && duplicate_hole.nonterminals()[0].minimum_depth == 3,
          "repeated hole materialization budget omitted a copy");

    doc = template_doc;
    doc.object_v.at("templates").array_v.push_back(json(R"({
      "id":"Wrapper","type":"Int","scope":[],
      "holes":[{"id":"forwarded","type":"Int","scope":[{"name":"x","type":"Int"}]}],
      "body":{"template":"Local","holes":{"body":{"hole":"forwarded"}}}
    })"));
    alternatives(doc).array_v[0].object_v.at("expression") = json(R"({"template":"Wrapper","holes":{"forwarded":{"ref":"Narrow"}}})");
    check(compile(doc).nonterminals()[0].minimum_nodes == 3, "template hole forwarding lost scope or cost");

    doc = json(R"({
      "format_version":"grammar-definition-v2",
      "entry":{"nonterminal":"Main","category":"Program","type":"Int"},
      "locals":[{"name":"i","type":"Int"}],
      "search_limits":{"max_nodes":30,"max_depth":10},"execution_limits":{"fuel":100},
      "nonterminals":[{"id":"Main","category":"Program","type":"Int","scope":[],"alternatives":[
        {"id":"return","weight":1,"expression":{"control":"program(Block)->Program","type":"Int","args":[
          {"control":"block_cons(Statement,Block)->Block","type":"Int","args":[
            {"control":"return(Int)->Statement","type":"Int","args":[{"constant":{"type":"Int","values":["7"]}}]},
            {"control":"block_nil()->Block","type":"Int","args":[]}]}]}}]}]
    })");
    const auto program_grammar = compile(doc);
    check(program_grammar.nonterminals()[0].minimum_nodes == 5 && program_grammar.nonterminals()[0].minimum_depth == 4,
          "control materialization costs missing");
    check(program_grammar.productions_for_type(gagp::evo::RType::Int).empty() &&
          program_grammar.productions_for_category(gagp::evo::NodeCategory::Program).size() == 1,
          "syntax categories leaked into value overload index");
    program_grammar.require_executable();
    const auto program_doc = doc;
    auto& statement = alternatives(doc).array_v[0].object_v.at("expression").object_v.at("args").array_v[0]
        .object_v.at("args").array_v[0];
    statement.object_v.at("args").array_v[0] = json(R"({"control":"block_nil()->Block","type":"Int","args":[]})");
    rejects([&] { compile(doc); }, "category or type mismatch");
    doc = program_doc;
    doc.object_v.at("entry").object_v["category"] = json(R"("Block")");
    rejects([&] { compile(doc); }, "entry requires");
    doc = program_doc;
    auto& assignment = alternatives(doc).array_v[0].object_v.at("expression").object_v.at("args").array_v[0]
        .object_v.at("args").array_v[0];
    assignment = json(R"({"control":"assign(Int)->Statement","type":"Int","name":"i","args":[{"constant":{"type":"Int","values":["1"]}}]})");
    compile(doc).require_executable();
    assignment.object_v.at("name") = json(R"("missing")");
    rejects([&] { compile(doc); }, "not visible");

    auto domain = parse_constant_domain(json(R"({"type":"Int","range":["-9223372036854775808","9223372036854775807"]})"));
    check(domain.minimum == std::numeric_limits<std::int64_t>::min() && domain.maximum == std::numeric_limits<std::int64_t>::max(),
          "64-bit constant range lost precision");
    for (const auto& text : {
        R"({"type":"Int","values":["9223372036854775808"]})", R"({"type":"Int","values":["01"]})",
        R"({"type":"Int","values":[1]})", R"({"type":"Int","values":[]})",
        R"({"type":"Float","range":["0","1"]})", R"({"type":"Int","range":["2","1"]})",
        R"({"type":"Char","values":["ab"]})", R"({"type":"StringList","values":[[1]]})",
        R"({"type":"Bool","values":[1]})", R"({"type":"FloatList","values":[["1"]]})"})
      rejects([&] { parse_constant_domain(json(text)); }, "");
    check(std::get<char32_t>(parse_constant_domain(json(R"({"type":"Char","values":["\ud83d\ude00"]})")).values[0]) == 0x1f600,
          "Char constant lost Unicode scalar");
    check(std::get<std::vector<std::int64_t>>(parse_constant_domain(json(R"({"type":"IntList","values":[["9223372036854775807"]]})")).values[0])[0] ==
          std::numeric_limits<std::int64_t>::max(), "IntList precision");
    check(std::get<std::vector<std::string>>(parse_constant_domain(json(R"({"type":"StringList","values":[["","\u0000"]]})")).values[0])[1] == std::string(1, '\0'),
          "StringList embedded NUL lost");
    std::cout << "typed grammar compilation, scope and productivity checks passed\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
