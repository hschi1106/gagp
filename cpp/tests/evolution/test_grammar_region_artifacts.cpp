#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/cli/grammar_artifact.hpp"
#include "gagp/cli/grammar_generate.hpp"
#include "gagp/cli/grammar_population_artifact.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"

using namespace gagp;
using namespace gagp::cli_detail;
using namespace gagp::evo;
using namespace gagp::evo::grammar;

namespace {
using Json = JsonValue;

const std::string definition = R"({
  "format_version":"grammar-definition-v2",
  "entry":{"nonterminal":"Main","type":"Int"},
  "search_limits":{"max_nodes":40,"max_depth":12},
  "execution_limits":{"fuel":1000},
  "templates":[{
    "id":"Twice","type":"Int","scope":[],
    "holes":[{"id":"value","type":"Int","scope":[]}],
    "body":{"signature":"add(Int,Int)->Int","args":[{"hole":"value"},{"hole":"value"}]}
  }],
  "nonterminals":[
    {"id":"Main","type":"Int","scope":[],"alternatives":[{"id":"twice","weight":1,
      "expression":{"template":"Twice","holes":{"value":{"ref":"Region"}}}}]},
    {"id":"Region","type":"Int","scope":[],"alternatives":[{"id":"region","weight":1,
      "expression":{"signature":"let(Int,Int)->Int","args":[
        {"constant":{"type":"Int","values":["10"]}},
        {"signature":"traverse(IntList,Int,Int,Int)->Int","args":[
          {"constant":{"type":"IntList","values":[["1","2"]]}},
          {"constant":{"type":"Int","values":["0"]}},
          {"bound":"initial"},
          {"signature":"add(Int,Int)->Int","args":[{"bound":"acc"},{"bound":"element"}]}
        ],"bind":{"3":["element","index","acc"]}}
      ],"bind":{"1":["initial"]}}}]}
  ]
})";

const std::string cases = R"({"format_version":"fitness-cases","cases":[{
  "inputs":{},"expected":{"type":"int","value":26}
}]})";

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

Json parse(const std::string& text) {
  return JsonParser(text, {true, 512}).parse();
}

void rejects(const std::function<void()>& action, const char* message) {
  try {
    action();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error(message);
}

CompiledGrammar fixture() {
  return compile_grammar(parse_definition(definition));
}

void check_region_metadata(const AstProgram& ast) {
  check(ast.lexical_regions.size() == 4, "repeated hole lost let/traverse lexical metadata");
  check(ast.traversal_specs.size() == 2, "repeated hole lost traversal metadata");
  std::set<int> binder_ids;
  for (const auto& region : ast.lexical_regions)
    for (const auto& binding : region.bindings)
      check(binder_ids.insert(binding.id).second, "repeated hole reused a lexical binder ID");
  check(binder_ids.size() == 8 && *binder_ids.begin() == 0 && *binder_ids.rbegin() == 7,
        "generated binder IDs are not fresh and monotonic");
  for (const auto& traversal : ast.traversal_specs)
    check(traversal.direction == TraversalDirection::Forward,
          "forward traversal direction was not preserved");
}

void test_generated_artifact() {
  const auto grammar = fixture();
  const auto generated = generate_derivation(grammar, 17);
  check_region_metadata(generated.genome.ast);
  const auto artifact = encode_generated_artifact(grammar, generated);

  const auto materialized = decode_materialized_artifact(artifact);
  check_region_metadata(materialized.ast);
  check(ast_cache_key(materialized.ast) == ast_cache_key(generated.genome.ast),
        "materialized artifact changed region metadata identity");
  const auto result = execute_bytecode_cpu(compile_for_eval(materialized), {}, 1000);
  check(!result.is_error && result.value.tag == ValueTag::Int && result.value.i == 26,
        "materialized region artifact did not execute without a selected grammar");

  const auto replayed = replay_generated_artifact(artifact, &grammar);
  check(encode_generated_artifact(grammar, replayed) == artifact,
        "region artifact failed exact same-seed replay");
  check(replayed.genome.derivation &&
        replayed.genome.derivation->holes.size() == generated.derivation.holes.size(),
        "region artifact replay lost derivation metadata");

  auto changed_ast = materialized.ast;
  changed_ast.traversal_specs.front().direction = TraversalDirection::Reverse;
  check(ast_cache_key(changed_ast) != ast_cache_key(materialized.ast),
        "traversal direction did not affect AST cache identity");

  auto stale = parse(artifact);
  stale.object_v.at("ast_shape").object_v.at("traversal_specs").array_v[0]
      .object_v.at("direction").string_v = "reverse";
  rejects([&] { replay_generated_artifact(canonical_json(stale), &grammar); },
          "seed replay accepted mutated traversal metadata");
}

void test_population_artifact() {
  const auto grammar = fixture();
  std::vector<ProgramGenome> population{
      generate_derivation(grammar, 21).genome,
      generate_derivation(grammar, 22).genome,
  };
  const auto artifact = encode_generated_population_artifact(grammar, population);
  const auto replayed = replay_generated_population_artifact(artifact);
  check(replayed.size() == population.size(), "region population changed size on replay");
  for (std::size_t i = 0; i < replayed.size(); ++i) {
    check_region_metadata(replayed[i].ast);
    check(replayed[i].derivation &&
          ast_cache_key(replayed[i].ast) == ast_cache_key(population[i].ast),
          "region population replay lost AST or derivation identity");
  }
  check(encode_generated_population_artifact(grammar, replayed) == artifact,
        "region population artifact did not roundtrip exactly");
}

struct TempFiles {
  std::filesystem::path directory;
  TempFiles() {
    auto pattern = (std::filesystem::temp_directory_path() /
                    "gagp-region-artifact-cli-XXXXXX").string();
    const auto* created = ::mkdtemp(pattern.data());
    if (!created) throw std::runtime_error("cannot create region artifact CLI directory");
    directory = created;
  }
  ~TempFiles() {
    std::error_code error;
    std::filesystem::remove_all(directory, error);
  }
  std::string path(const char* name) const { return (directory / name).string(); }
  void write(const char* name, const std::string& text) const {
    std::ofstream stream(path(name));
    stream << text;
    check(static_cast<bool>(stream), "cannot write region artifact CLI fixture");
  }
  std::string read(const char* name) const {
    std::ifstream stream(path(name));
    check(static_cast<bool>(stream), "cannot read region artifact CLI output");
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
  }
};

int run(std::vector<std::string> arguments) {
  arguments.insert(arguments.begin(), "gagp_generate_cli");
  std::vector<char*> argv;
  for (auto& argument : arguments) argv.push_back(argument.data());
  return run_grammar_generate_command(static_cast<int>(argv.size()), argv.data());
}

void test_generate_cli() {
  TempFiles files;
  files.write("grammar.json", definition);
  files.write("cases.json", cases);
  check(run({"--grammar-definition", files.path("grammar.json"),
             "--cases", files.path("cases.json"),
             "--out-json", files.path("population.json"),
             "--population-size", "1", "--seed", "31"}) == 0,
        "public generate command rejected closed region grammar");
  const auto population = replay_generated_population_artifact(files.read("population.json"));
  check(population.size() == 1, "public generate command wrote the wrong population size");
  check_region_metadata(population.front().ast);
}
}  // namespace

int main() {
  try {
    test_generated_artifact();
    test_population_artifact();
    test_generate_cli();
    std::cout << "grammar region artifacts: materialization, replay, population, and CLI passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
