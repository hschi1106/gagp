#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/cli/grammar_generate.hpp"
#include "gagp/cli/grammar_population_artifact.hpp"

using namespace gagp::cli_detail;
using namespace gagp::evo::grammar;
namespace {
void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
JsonValue parse(const std::string& text) { return JsonParser(text, {true, 512}).parse(); }
struct TempFiles {
  std::filesystem::path directory;
  TempFiles() {
    auto pattern = (std::filesystem::temp_directory_path() / "gagp-generate-cli-XXXXXX").string();
    const auto* result = ::mkdtemp(pattern.data());
    if (!result) throw std::runtime_error("cannot create test directory");
    directory = result;
  }
  ~TempFiles() { std::error_code error; std::filesystem::remove_all(directory, error); }
  std::string path(const char* name) const { return (directory / name).string(); }
  void write(const char* name, const std::string& text) const {
    std::ofstream output(path(name));
    output << text;
    check(static_cast<bool>(output), "cannot write test fixture");
  }
  std::string read(const char* name) const {
    std::ifstream input(path(name));
    check(static_cast<bool>(input), "cannot read test output");
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
  }
};
struct CaptureOutput {
  std::ostringstream output;
  std::streambuf* original = std::cout.rdbuf(output.rdbuf());
  ~CaptureOutput() { std::cout.rdbuf(original); }
};
int run(std::vector<std::string> args) {
  args.insert(args.begin(), "gagp_generate_cli");
  std::vector<char*> argv;
  for (auto& arg : args) argv.push_back(arg.data());
  return run_grammar_generate_command(static_cast<int>(argv.size()), argv.data());
}
void rejects(const std::vector<std::string>& args) {
  try { run(args); } catch (const std::exception&) { return; }
  throw std::runtime_error("invalid generation CLI invocation accepted");
}
const std::string definition = R"({
  "format_version":"grammar-definition-v2","entry":{"nonterminal":"Value","type":"Int"},
  "inputs":[{"name":"x","type":"Int"}],
  "search_limits":{"max_nodes":5,"max_depth":4},"execution_limits":{"fuel":100},
  "nonterminals":[{"id":"Value","type":"Int","scope":[],"alternatives":[
    {"id":"constant","weight":1,"expression":{"constant":{"type":"Int","values":["7"]}}}]}]
})";
const std::string cases = R"({"format_version":"fitness-cases","cases":[{
  "inputs":{"x":{"type":"int","value":3}},"expected":{"type":"int","value":7}}]})";

void test_generation_and_replay() {
  TempFiles files;
  files.write("grammar.json", definition);
  files.write("cases.json", cases);
  const std::vector<std::string> generate{
      "--grammar-definition", files.path("grammar.json"), "--cases", files.path("cases.json"),
      "--out-json", files.path("population.json"), "--population-size", "3", "--seed", "18446744073709551615"};
  CaptureOutput capture;
  check(run(generate) == 0, "generation failed");
  const auto original = files.read("population.json");
  const auto root = parse(original);
  check(root.object_v.at("count").number_v == 3, "wrong population count");
  const auto hash = root.object_v.at("grammar_hash").string_v;
  check(capture.output.str().find(hash) != std::string::npos &&
        capture.output.str().find("count=3") != std::string::npos, "missing generation summary");
  const auto& members = root.object_v.at("members").array_v;
  const std::vector<std::string> seeds{"18446744073709551615", "0", "1"};
  check(members.size() == seeds.size(), "wrong member count");
  for (std::size_t i = 0; i < members.size(); ++i) {
    check(members[i].object_v.at("seed").string_v == seeds[i], "seed wraparound changed");
    const auto member = decode_materialized_program(canonical_json(members[i]));
    check(member.inputs.size() == 1 && member.inputs[0].name == "x" &&
          member.inputs[0].type == gagp::evo::RType::Int && member.return_type == gagp::evo::RType::Int,
          "member input or return contract changed");
    check(member.search_limits.max_nodes == 5 && member.search_limits.max_depth == 4 &&
          member.execution_limits.fuel == 100, "member limits changed");
  }
  check(replay_generated_population_artifact(original).size() == 3, "output cannot replay");
  std::vector<std::string> replay{"--cases", files.path("cases.json"), "--replay-json",
                                  files.path("population.json"), "--out-json", files.path("replayed.json")};
  check(run(replay) == 0 && files.read("replayed.json") == original, "replay changed artifact bytes");
  replay.insert(replay.end(), {"--grammar-definition", files.path("grammar.json")});
  check(run(replay) == 0, "matching required grammar rejected");
  auto changed = parse(definition);
  changed.object_v.at("execution_limits").object_v.at("fuel").number_v = 101;
  files.write("grammar.json", canonical_json(changed));
  rejects(replay);
  check(files.read("replayed.json") == original, "identity failure overwrote output");
  files.write("grammar.json", definition);
  auto bad_cases = parse(cases);
  bad_cases.object_v.at("cases").array_v[0].object_v.at("inputs") = parse("{}");
  files.write("cases.json", canonical_json(bad_cases));
  rejects(replay);
  rejects(generate);
  check(files.read("replayed.json") == original && files.read("population.json") == original,
        "input mismatch overwrote output");
  bad_cases = parse(cases);
  bad_cases.object_v.at("cases").array_v[0].object_v.at("expected") = parse(R"({"type":"bool","value":true})");
  files.write("cases.json", canonical_json(bad_cases));
  rejects(replay);
  rejects(generate);
  files.write("cases.json", cases);
  for (const auto* input : {"grammar.json", "cases.json", "population.json"}) {
    auto collision = replay;
    collision[5] = files.path(input);
    const auto before = files.read(input);
    rejects(collision);
    check(files.read(input) == before, "input collision overwrote file");
  }
  for (const auto* flag : {"--seed", "--population-size"}) {
    auto invalid = replay;
    invalid.insert(invalid.end(), {flag, "1"});
    rejects(invalid);
  }
}

void test_import_identity() {
  TempFiles files;
  auto root = parse(definition);
  auto library = parse(R"({"format_version":"grammar-definition-v2"})");
  library.object_v["nonterminals"] = root.object_v.at("nonterminals");
  root.object_v["nonterminals"] = parse("[]");
  root.object_v["imports"] = parse(R"(["library.json"])");
  files.write("grammar.json", canonical_json(root));
  files.write("library.json", canonical_json(library));
  files.write("cases.json", cases);
  CaptureOutput capture;
  check(run({"--grammar-definition", files.path("grammar.json"), "--cases", files.path("cases.json"),
             "--out-json", files.path("population.json")}) == 0, "import generation failed");
  library.object_v.at("nonterminals").array_v[0].object_v.at("alternatives").array_v[0]
      .object_v.at("expression").object_v.at("constant").object_v.at("values") = parse(R"(["8"])");
  files.write("library.json", canonical_json(library));
  files.write("result.json", "preserve me");
  rejects({"--grammar-definition", files.path("grammar.json"), "--cases", files.path("cases.json"),
           "--replay-json", files.path("population.json"), "--out-json", files.path("result.json")});
  check(files.read("result.json") == "preserve me", "changed import identity overwrote output");
}

void test_options() {
  CaptureOutput capture;
  check(run({"--help"}) == 0 && capture.output.str().find("Usage:") != std::string::npos,
        "help requires fixture paths");
  for (const auto& args : std::vector<std::vector<std::string>>{
      {}, {"--unknown"}, {"--cases"}, {"--cases", "a", "--cases", "b"},
      {"--help", "--help"}, {"--help", "--unknown"}}) rejects(args);
  TempFiles files;
  files.write("grammar.json", definition);
  files.write("cases.json", cases);
  files.write("result.json", "preserve me");
  const std::vector<std::string> base{"--grammar-definition", files.path("grammar.json"),
      "--cases", files.path("cases.json"), "--out-json", files.path("result.json")};
  for (const auto* seed : {"-1", "+1", "01", "1.0", "1e2", "18446744073709551616", ""}) {
    auto args = base;
    args.insert(args.end(), {"--seed", seed});
    rejects(args);
  }
  for (const auto* size : {"0", "65537", "-1", "1.5", "18446744073709551616"}) {
    auto args = base;
    args.insert(args.end(), {"--population-size", size});
    rejects(args);
  }
  check(files.read("result.json") == "preserve me", "invalid flags overwrote output");
}
}  // namespace

int main() {
  try {
    test_generation_and_replay();
    test_import_identity();
    test_options();
    std::cout << "grammar generate CLI: generation, replay, identity, schema, and validation passed\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
