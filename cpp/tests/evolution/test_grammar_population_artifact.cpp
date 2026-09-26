#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "gagp/cli/grammar_population_artifact.hpp"
#include "gagp/cli/commands.hpp"
#include "gagp/evolution/grammar/values.hpp"
#include "gagp/runtime/payload/payload.hpp"
#include "gagp/evolution/population_init.hpp"
#include "../fixtures/mixed_population.hpp"

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;
using namespace gagp::cli_detail;
namespace {
using Json = JsonValue;
void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void rejects(const std::function<void()>& action) {
  try { action(); } catch (const std::invalid_argument&) { return; }
  throw std::runtime_error("invalid population artifact accepted");
}
void rejects_with(const std::function<void()>& action, const std::string& message) {
  try { action(); }
  catch (const std::invalid_argument& error) {
    check(std::string(error.what()).find(message) != std::string::npos,
          "population artifact rejection diagnostic changed");
    return;
  }
  throw std::runtime_error("invalid population artifact accepted");
}
Json parse(const std::string& text) { return JsonParser(text, {true, 512}).parse(); }
CompiledGrammar fixture(const std::string& values = R"([["a","b"],["c"]])") {
  return compile_grammar(parse_definition(
      R"({"format_version":"grammar-definition-v2","entry":{"nonterminal":"Value","type":"StringList"},
      "search_limits":{"max_nodes":5,"max_depth":4},"execution_limits":{"fuel":100},
      "nonterminals":[{"id":"Value","type":"StringList","scope":[],"alternatives":[
      {"id":"constant","weight":1,"expression":{"constant":{"type":"StringList","values":)" + values + "}}}]}]}"));
}
void test_population() {
  const auto grammar = fixture();
  std::vector<ProgramGenome> population;
  for (std::uint64_t offset = 0; offset < 3; ++offset)
    population.push_back(generate_derivation(grammar, std::numeric_limits<std::uint64_t>::max() + offset).genome);
  const auto artifact = encode_generated_population_artifact(grammar, population);
  const auto root = parse(artifact);
  check(root.object_v.at("format_version").string_v == "grammar-population-v2",
        "population artifact did not publish the current version");
  const auto& members = root.object_v.at("members").array_v;
  check(members[0].object_v.at("seed").string_v == "18446744073709551615" &&
        members[1].object_v.at("seed").string_v == "0" &&
        members[2].object_v.at("seed").string_v == "1", "population seed wraparound changed");
  payload::clear();
  const auto replayed = replay_generated_population_artifact(artifact, &grammar);
  check(replayed.size() == 3, "population count changed");
  check(encode_generated_population_artifact(grammar, replayed) == artifact,
        "population replay changed after clearing payload registry");
  payload::clear();
  const auto embedded_replayed = replay_generated_population_artifact(artifact);
  check(encode_generated_population_artifact(grammar, embedded_replayed) == artifact,
        "embedded-grammar payload replay was not byte stable");
  for (std::size_t i = 0; i < replayed.size(); ++i) {
    check(replayed[i].derivation != nullptr, "population replay lost immutable provenance");
    check(canonical_json(encode_constant(replayed[i].ast.consts[0])) ==
          canonical_json(members[i].object_v.at("constants").array_v[0]), "population payload changed");
  }
  const auto tamper = [&](const std::function<void(Json&)>& edit) {
    auto changed = root; edit(changed);
    rejects([&] { replay_generated_population_artifact(canonical_json(changed)); });
  };
  tamper([](Json& value) {
    value.object_v.at("format_version").string_v = "grammar-population-v1";
  });
  tamper([](Json& value) { value.object_v.at("count").number_v += 1; });
  tamper([](Json& value) { value.object_v.at("count").number_v = 1.5; });
  tamper([](Json& value) { value.object_v.at("count").kind = Json::Kind::String; });
  tamper([](Json& value) { value.object_v.at("grammar_hash").string_v = "wrong"; });
  tamper([](Json& value) { value.object_v.at("format_version").string_v = "future"; });
  tamper([](Json& value) { value.object_v["path"] = Json{}; });
  tamper([](Json& value) { value.object_v.erase("count"); });
  tamper([](Json& value) { value.object_v.at("members").array_v.clear(); });
  tamper([](Json& value) {
    value.object_v.at("members").array_v[0].object_v.at("derivation")
        .object_v.at("logical_steps").number_v += 1;
  });
  tamper([](Json& value) {
    value.object_v.at("members").array_v[1].object_v.at("derivation")
        .object_v["unknown"] = Json{};
  });
  tamper([](Json& value) {
    value.object_v.at("members").array_v[0].object_v.at("constants").array_v[0]
        .object_v.at("values").array_v[0].array_v[0].string_v = "edited";
  });
  const auto other = fixture(R"([["other"]])");
  rejects([&] { replay_generated_population_artifact(artifact, &other); });
  tamper([&](Json& value) {
    value.object_v.at("members").array_v[0] = parse(encode_generated_artifact(other, generate_derivation(other, 0)));
  });
  {
    auto changed = root;
    changed.object_v.at("members").array_v[0].object_v.at("grammar")
        .object_v.at("execution_limits").object_v.at("fuel").number_v += 1;
    rejects([&] {
      replay_generated_population_artifact(canonical_json(changed), &grammar);
    });
  }
  {
    auto changed = root;
    changed.object_v.at("members").array_v[0].object_v.at("request")
        .object_v.at("nonterminal").number_v = -0.0;
    rejects([&] {
      replay_generated_population_artifact(canonical_json(changed), &grammar);
    });
  }
  auto invalid = replayed;
  invalid[0].derivation.reset();
  rejects([&] { encode_generated_population_artifact(grammar, invalid); });
  invalid = replayed;
  invalid[0].ast.consts[0] = generate_derivation(other, 0).genome.ast.consts[0];
  rejects([&] { encode_generated_population_artifact(grammar, invalid); });
  invalid = replayed;
  invalid[0] = generate_derivation(other, 0).genome;
  rejects([&] { encode_generated_population_artifact(grammar, invalid); });
  rejects([&] { encode_generated_population_artifact(grammar, {}); });
  rejects([&] { encode_generated_population_artifact(grammar, std::vector<ProgramGenome>(65537)); });
  rejects([&] { replay_generated_population_artifact("{\"count\":3," + artifact.substr(1)); });
  rejects([&] { replay_generated_population_artifact(std::string(513, '[') + "0" + std::string(513, ']')); });
  rejects_with([&] {
    replay_generated_population_artifact(
        R"({"format_version":"population-seeds","seeds":[{"seed":1}]})");
  }, "legacy population-seeds cannot replay compiled grammar evolution exactly");
}

void test_mixed_population() {
  const auto cfg = gagp::test::mixed_population_config();
  const auto& grammar = *cfg.compiled_grammar;
  const auto requests = population_requests(cfg);
  std::vector<ProgramGenome> population;
  for (std::size_t i = 0; i < requests.size(); ++i)
    population.push_back(generate_derivation(grammar, i, requests[i]).genome);
  const auto artifact = encode_generated_population_artifact(grammar, population);
  payload::clear();
  const auto replayed = replay_generated_population_artifact(artifact, &grammar);
  check(replayed.size() == 8, "mixed population lost a root type");
  for (std::size_t i = 0; i < requests.size(); ++i)
    check(replayed[i].derivation->request.type == requests[i].type,
          "mixed population changed member root contract");
  check(encode_generated_population_artifact(grammar, replayed) == artifact,
        "mixed population cold replay changed artifact");
  payload::clear();
  check(encode_generated_population_artifact(grammar,
        replay_generated_population_artifact(artifact)) == artifact,
        "mixed population embedded grammar replay changed artifact");
}
}  // namespace
int main() {
  try {
    test_population();
    test_mixed_population();
    std::cout << "grammar population artifact: cold replay, seed wraparound, and rejection passed\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
