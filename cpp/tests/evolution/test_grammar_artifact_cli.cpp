#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/cli/commands.hpp"
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
void rejects_nesting(const std::function<void()>& action, const char* message) {
  try {
    action();
  } catch (const std::runtime_error& error) {
    check(std::string(error.what()) == "JSON nesting limit exceeded", message);
    return;
  }
  throw std::runtime_error(message);
}
struct TempFiles {
  std::filesystem::path directory;
  TempFiles() {
    auto pattern = (std::filesystem::temp_directory_path() / "gagp-artifact-cli-XXXXXX").string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const char* result = mkdtemp(buffer.data());
    if (!result) throw std::runtime_error("cannot create artifact CLI test directory");
    directory = result;
  }
  ~TempFiles() { std::error_code error; std::filesystem::remove_all(directory, error); }
  std::string path(const char* name) const { return (directory / name).string(); }
  void write(const char* name, const std::string& text) const {
    std::ofstream out(path(name));
    out << text;
    check(static_cast<bool>(out), "cannot write test fixture");
  }
  JsonValue read(const char* name) const {
    std::ifstream in(path(name));
    std::ostringstream text;
    text << in.rdbuf();
    return parse(text.str());
  }
};
struct CaptureOutput {
  std::ostringstream output;
  std::streambuf* original = std::cout.rdbuf(output.rdbuf());
  ~CaptureOutput() { std::cout.rdbuf(original); }
};
std::string artifact() {
  const auto grammar = compile_grammar(parse_definition(R"({
    "format_version":"grammar-definition-v1","entry":{"nonterminal":"Value","type":"Int"},
    "inputs":[{"name":"x","type":"Int"}],
    "search_limits":{"max_nodes":5,"max_depth":4},"execution_limits":{"fuel":1},
    "nonterminals":[{"id":"Value","type":"Int","scope":[],"alternatives":[
      {"id":"constant","weight":1,"expression":{"constant":{"type":"Int","values":["7"]}}}]}]
  })"));
  return encode_generated_artifact(grammar, generate_derivation(grammar, 9));
}
std::string cases(const std::string& inputs, const std::string& expected = R"({"type":"int","value":7})") {
  return R"({"format_version":"fitness-cases","cases":[{"inputs":)" + inputs +
      R"(,"expected":)" + expected + "}]}";
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
void test_cli_evaluation() {
  TempFiles files;
  files.write("artifact.json", artifact());
  const auto valid_cases = cases(R"({"x":{"type":"int","value":3}})");
  files.write("cases.json", valid_cases);
  CliOptions options;
  options.cases_path = files.path("cases.json");
  options.eval_ast_json = files.path("artifact.json");
  options.out_json = files.path("result.json");
  options.penalty = 19;
  CaptureOutput capture;
  check(run_eval_ast_command(options) == 0, "artifact evaluation failed");
  auto result = files.read("result.json");
  check(result.object_v.at("meta").object_v.at("fuel").number_v == 1, "CLI ignored recorded fuel");
  check(result.object_v.at("result").object_v.at("fitness").number_v == -19,
        "recorded fuel did not constrain execution");
  options.fuel_explicit = true;
  options.fuel = 1;
  check(run_eval_ast_command(options) == 0, "matching explicit fuel rejected");
  options.fuel = 2;
  rejects([&] { run_eval_ast_command(options); }, "conflicting explicit fuel accepted");
  options.fuel_explicit = false;
  for (const char* inputs : {R"({})", R"({"x":{"type":"bool","value":true}})",
       R"({"x":{"type":"int","value":3},"extra":{"type":"int","value":0}})"}) {
    files.write("cases.json", cases(inputs));
    rejects([&] { run_eval_ast_command(options); }, "fixture input schema mismatch accepted");
  }
  files.write("cases.json", cases(R"({"x":{"type":"int","value":3}})",
                                  R"({"type":"bool","value":true})"));
  rejects([&] { run_eval_ast_command(options); }, "fixture return type mismatch accepted");
  files.write("cases.json", valid_cases);
  files.write("legacy.json", encode_ast_json(decode_materialized_program(artifact()).genome.ast));
  options.eval_ast_json = files.path("legacy.json");
  options.fuel = 20000;
  check(run_eval_ast_command(options) == 0, "legacy AST evaluation failed");
  result = files.read("result.json");
  check(result.object_v.at("result").object_v.at("fitness").number_v == 0,
        "legacy AST changed evaluation behavior");
}
void test_cli_rejects_excessive_dispatch_depth() {
  TempFiles files;
  auto deeply_nested = artifact();
  check(!deeply_nested.empty() && deeply_nested.back() == '}', "invalid artifact fixture");
  deeply_nested.pop_back();
  deeply_nested += R"(,"extra":)" + std::string(512, '[') + "null" +
      std::string(512, ']') + "}";
  files.write("deep-artifact.json", deeply_nested);
  files.write("cases.json", cases(R"({"x":{"type":"int","value":3}})"));
  CliOptions options;
  options.cases_path = files.path("cases.json");
  options.eval_ast_json = files.path("deep-artifact.json");
  rejects_nesting([&] { run_eval_ast_command(options); },
                  "CLI did not reject excessive artifact dispatch nesting");

  deeply_nested = encode_ast_json(decode_materialized_program(artifact()).genome.ast);
  check(!deeply_nested.empty() && deeply_nested.back() == '}', "invalid legacy AST fixture");
  deeply_nested.pop_back();
  deeply_nested += R"(,"extra":)" + std::string(512, '[') + "null" +
      std::string(512, ']') + "}";
  files.write("deep-legacy.json", deeply_nested);
  options.eval_ast_json = files.path("deep-legacy.json");
  rejects_nesting([&] { run_eval_ast_command(options); },
                  "CLI did not reject excessive legacy AST dispatch nesting");
}
void test_fuel_option_tracking() {
  std::vector<std::string> args{"gagp_evolve_cli", "--cases", "unused.json"};
  auto parse_args = [&] {
    std::vector<char*> argv;
    for (auto& arg : args) argv.push_back(arg.data());
    return parse_cli_options(static_cast<int>(argv.size()), argv.data());
  };
  check(!parse_args().fuel_explicit, "default fuel marked explicit");
  args.insert(args.end(), {"--fuel", "1"});
  const auto parsed = parse_args();
  check(parsed.fuel_explicit && parsed.fuel == 1, "explicit fuel not recorded");
}
}  // namespace
int main() {
  try {
    test_contract_limits();
    test_cli_evaluation();
    test_cli_rejects_excessive_dispatch_depth();
    test_fuel_option_tracking();
    std::cout << "grammar artifact CLI: limits, schema, dispatch depth, recorded fuel, and legacy evaluation passed\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
