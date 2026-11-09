#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/cli/program_json.hpp"
#include "gagp/cli/grammar_artifact.hpp"
#include "gagp/evolution/grammar/identity.hpp"

using namespace gagp::cli_detail;
using namespace gagp::evo::grammar;
namespace {
void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
JsonValue parse(const std::string& text) { return JsonParser(text, {true, 512}).parse(); }
void rejects(const std::function<void()>& action, const char* message) {
  try { action(); } catch (const std::invalid_argument&) { return; }
  throw std::runtime_error(message);
}
std::string artifact() {
  const auto grammar = compile_grammar(parse_definition(R"({
    "format_version":"grammar-definition-v2","entry":{"nonterminal":"Value","type":"Int"},
    "inputs":[{"name":"x","type":"Int"}],
    "search_limits":{"max_nodes":5,"max_depth":4},"execution_limits":{"fuel":1},
    "nonterminals":[{"id":"Value","type":"Int","scope":[],"alternatives":[
      {"id":"constant","weight":1,"expression":{"constant":{"type":"Int","values":["7"]}}}]}]
  })"));
  return encode_generated_artifact(grammar, generate_derivation(grammar, 9));
}
void test_contract_limits() {
  const auto original = parse(artifact());
  const auto decoded = decode_materialized_program(canonical_json(original));
  check(decoded.execution_limits.fuel == 1 && decoded.search_limits.max_nodes == 5 &&
        decoded.search_limits.max_depth == 4, "decoder lost recorded limits");
  check(decoded.inputs.size() == 1 && decoded.inputs.front().name == "x" &&
        decoded.return_type == gagp::evo::RType::Int, "decoder lost recorded schema");
  for (const auto& field : std::vector<std::pair<std::string, std::string>>{
           {"search_limits", "max_nodes"}, {"search_limits", "max_depth"}, {"execution_limits", "fuel"}}) {
    for (const char* invalid : {"0", "-1", "1.5", "2147483648", "\"1\""}) {
      auto changed = original;
      changed.object_v.at(field.first).object_v.at(field.second) = parse(invalid);
      rejects([&] { decode_materialized_program(canonical_json(changed)); }, "invalid artifact limit accepted");
    }
  }
  for (const char* field : {"max_nodes", "max_depth"}) {
    auto changed = original;
    changed.object_v.at("search_limits").object_v.at(field) = parse("1");
    rejects([&] { decode_materialized_program(canonical_json(changed)); }, "AST exceeded recorded shape budget");
  }
}
}  // namespace
int main() {
  try {
    test_contract_limits();
    std::cout << "grammar artifact: recorded limits and schema passed\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
