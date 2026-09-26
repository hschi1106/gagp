#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

#include "gagp/cli/grammar_author.hpp"
#include "gagp/cli/json.hpp"
#include "gagp/evolution/grammar/definition.hpp"

using namespace gagp::cli_detail;
using namespace gagp::evo::grammar;

namespace {
void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

struct TempFiles {
  std::filesystem::path directory;
  TempFiles() {
    auto pattern = (std::filesystem::temp_directory_path() / "gagp-grammar-cli-XXXXXX").string();
    const auto* created = ::mkdtemp(pattern.data());
    if (!created) throw std::runtime_error("cannot create temporary directory");
    directory = created;
  }
  ~TempFiles() {
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
  }
  std::string path(const char* name) const { return (directory / name).string(); }
  void write(const char* name, const std::string& text) const {
    std::ofstream stream(path(name));
    stream << text;
    check(static_cast<bool>(stream), "cannot write test input");
  }
  std::string read(const char* name) const {
    std::ifstream stream(path(name));
    check(static_cast<bool>(stream), "cannot read test output");
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
  }
};

struct Capture {
  std::ostringstream output;
  std::ostringstream error;
  std::streambuf* old_output = std::cout.rdbuf(output.rdbuf());
  std::streambuf* old_error = std::cerr.rdbuf(error.rdbuf());
  ~Capture() {
    std::cout.rdbuf(old_output);
    std::cerr.rdbuf(old_error);
  }
};

int run(std::vector<std::string> arguments) {
  arguments.insert(arguments.begin(), "gagp_grammar_cli");
  std::vector<char*> argv;
  for (auto& argument : arguments) argv.push_back(argument.data());
  return run_grammar_author_command(static_cast<int>(argv.size()), argv.data());
}

JsonValue parse(const std::string& text) {
  return JsonParser(text, {true, 512}).parse();
}

const std::string library = R"({
  "format_version":"grammar-definition-v2",
  "templates":[{"id":"Identity","type":"Int","scope":[],
    "holes":[{"id":"value","type":"Int","scope":[]}],
    "body":{"hole":"value"}}],
  "nonterminals":[{"id":"Expr","type":"Int","scope":[],"alternatives":[
    {"id":"sum","weight":1,"expression":{"template":"Identity","holes":{"value":
      {"signature":"add(Int,Int)->Int","args":[{"input":"x"},
        {"constant":{"type":"Int","values":["1"]}}]}}}}]}]
})";

const std::string root = R"({
  "format_version":"grammar-definition-v2",
  "imports":["library.json"],
  "entry":{"nonterminal":"Program","type":"Int","category":"Program"},
  "inputs":[{"name":"x","type":"Int"}],
  "locals":[{"name":"answer","type":"Int"}],
  "search_limits":{"max_nodes":20,"max_depth":10},
  "execution_limits":{"fuel":1000},
  "nonterminals":[{"id":"Program","category":"Program","type":"Int","scope":[],"alternatives":[
    {"id":"body","weight":1,"expression":{"control":"program(Block)->Program","type":"Int","args":[
      {"control":"block_cons(Statement,Block)->Block","type":"Int","args":[
        {"control":"return(Int)->Statement","type":"Int","args":[{"ref":"Expr"}]},
        {"control":"block_nil()->Block","type":"Int","args":[]}]}]}}]}]
})";

void test_validate_and_inspect(const TempFiles& files) {
  Capture capture;
  check(run({"validate", "--grammar-definition", files.path("root.json")}) == 0,
        "validate command failed");
  const auto validation = parse(capture.output.str());
  check(validation.object_v.at("format_version").string_v == "grammar-validation-v1" &&
        validation.object_v.at("valid").bool_v &&
        validation.object_v.at("definition_version").string_v == "grammar-definition-v2" &&
        validation.object_v.at("catalog_version").string_v == "gagp-primitives-v3" &&
        validation.object_v.at("normalization_version").string_v == "1" &&
        validation.object_v.at("semantic_version").string_v == "gagp-native-2.0.0-restricted-1" &&
        validation.object_v.at("generator_version").string_v == "typed-derivation-v2" &&
        validation.object_v.at("rng_version").string_v == "splitmix64-rejection-v1" &&
        validation.object_v.at("entry").object_v.at("id").string_v == "Program" &&
        validation.object_v.at("limits").object_v.at("search").object_v.at("max_nodes").number_v == 20 &&
        validation.object_v.at("counts").object_v.at("templates").number_v == 1 &&
        validation.object_v.at("entry_executable").object_v.at("value").bool_v,
        "validation report lost identity, entry, limits, counts, or executability");

  capture.output.str("");
  capture.output.clear();
  check(run({"inspect", "--grammar-definition", files.path("root.json")}) == 0,
        "inspect command failed");
  const auto inspection = parse(capture.output.str());
  check(inspection.object_v.at("format_version").string_v == "grammar-inspection-v1" &&
        inspection.object_v.at("inputs").array_v.size() == 1 &&
        inspection.object_v.at("locals").array_v.size() == 1 &&
        inspection.object_v.at("nonterminals").array_v.size() == 2 &&
        inspection.object_v.at("productions").array_v.size() == 2 &&
        inspection.object_v.at("templates").array_v.front().object_v.at("holes").array_v.size() == 1 &&
        !inspection.object_v.at("contexts").array_v.empty(),
        "inspection report lost compiled grammar structure");
  bool primitive = false;
  bool control = false;
  for (const auto& signature : inspection.object_v.at("used_signatures").array_v) {
    primitive |= signature.object_v.at("kind").string_v == "primitive" &&
        signature.object_v.at("key").string_v == "add(Int,Int)->Int";
    control |= signature.object_v.at("kind").string_v == "control" &&
        signature.object_v.at("key").string_v == "return(Int)->Statement";
  }
  check(primitive && control, "inspection omitted an exact used signature");
}

void test_resolve_and_output(const TempFiles& files) {
  const auto expected = load_definition(files.path("root.json"));
  Capture capture;
  check(run({"resolve", "--grammar-definition", files.path("root.json")}) == 0 &&
        capture.output.str() == expected.canonical,
        "resolve did not emit exact canonical bytes");
  capture.output.str("");
  capture.output.clear();
  check(run({"inspect", "--grammar-definition", files.path("root.json"),
             "--out-json", files.path("inspection.json")}) == 0,
        "inspection file output failed");
  check(parse(files.read("inspection.json")).object_v.at("grammar_hash").string_v ==
            expected.content_hash &&
        capture.output.str() == "action=inspect grammar_hash=" + expected.content_hash + "\n",
        "file output or identity status changed");
}

void test_diagnostics_and_overwrite(const TempFiles& files) {
  Capture capture;
  check(run({"validate", "--grammar-definition", files.path("root.json"),
             "--out-json", files.path("root.json")}) == 2,
        "root overwrite was accepted");
  auto diagnostic = parse(capture.error.str());
  const auto& first = diagnostic.object_v.at("diagnostics").array_v.front().object_v;
  check(diagnostic.object_v.at("format_version").string_v == "grammar-diagnostic-v1" &&
        first.at("action").string_v == "validate" &&
        first.at("phase").string_v == "command" &&
        first.at("source").object_v.at("file").string_v == files.path("root.json") &&
        first.at("source").object_v.at("json_pointer").string_v.empty(),
        "overwrite diagnostic lost root-level context");

  capture.error.str("");
  capture.error.clear();
  const std::string imported_before = files.read("library.json");
  check(run({"resolve", "--grammar-definition", files.path("root.json"),
             "--out-json", files.path("library.json")}) == 2,
        "imported grammar overwrite was accepted");
  diagnostic = parse(capture.error.str());
  check(diagnostic.object_v.at("diagnostics").array_v.front().object_v.at("message")
                .string_v.find("imported grammar") != std::string::npos &&
            files.read("library.json") == imported_before,
        "imported grammar overwrite diagnostic or preservation changed");

  files.write("invalid.json", R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Missing","type":"Int"},
    "search_limits":{"max_nodes":5,"max_depth":4},"execution_limits":{"fuel":10},
    "nonterminals":[{"id":"Value","type":"Int","scope":[],"alternatives":[
      {"id":"zero","weight":1,"expression":{"constant":{"type":"Int","values":["0"]}}}]}]
  })");
  capture.error.str("");
  capture.error.clear();
  files.write("preserved.json", "preserve-me");
  check(run({"inspect", "--grammar-definition", files.path("invalid.json"),
             "--out-json", files.path("preserved.json")}) == 2,
        "invalid compiled grammar was accepted");
  diagnostic = parse(capture.error.str());
  const auto& compile = diagnostic.object_v.at("diagnostics").array_v.front().object_v;
  check(compile.at("action").string_v == "inspect" &&
        compile.at("phase").string_v == "compile" &&
        compile.at("source").object_v.at("json_pointer").string_v.empty() &&
        compile.at("message").string_v.find("unknown nonterminal") != std::string::npos,
        "compile diagnostic claimed or lost source context");
  check(files.read("preserved.json") == "preserve-me",
        "failed inspection replaced an existing output");
}

void test_command_contract() {
  Capture capture;
  check(run({"--help"}) == 0 &&
        capture.output.str().find("{validate|inspect|resolve}") != std::string::npos,
        "help contract changed");
  capture.output.str("");
  capture.output.clear();
  check(run({"unknown"}) == 2, "unknown action did not return status 2");
  const auto diagnostic = parse(capture.error.str());
  check(diagnostic.object_v.at("diagnostics").array_v.front()
            .object_v.at("phase").string_v == "command",
        "command error did not report its phase");
}
}  // namespace

int main() {
  try {
    TempFiles files;
    files.write("library.json", library);
    files.write("root.json", root);
    test_validate_and_inspect(files);
    test_resolve_and_output(files);
    test_diagnostics_and_overwrite(files);
    test_command_contract();
    std::cout << "grammar author CLI: validation, inspection, resolution, output, and diagnostics passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
