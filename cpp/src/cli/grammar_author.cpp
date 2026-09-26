#include "gagp/cli/grammar_author.hpp"

#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

#include "gagp/cli/json.hpp"
#include "gagp/evolution/grammar/catalog.hpp"
#include "gagp/evolution/grammar/compiled.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/generate.hpp"

namespace gagp::cli_detail {
namespace {
using Json = JsonValue;
using Kind = Json::Kind;
using evo::NodeCategory;
using evo::RType;
using evo::TraversalDirection;
using namespace evo::grammar;

Json object() {
  Json value;
  value.kind = Kind::Object;
  return value;
}

Json array() {
  Json value;
  value.kind = Kind::Array;
  return value;
}

Json string(std::string text) {
  Json value;
  value.kind = Kind::String;
  value.string_v = std::move(text);
  return value;
}

Json number(std::uint64_t value) {
  Json result;
  result.kind = Kind::Number;
  result.number_v = static_cast<double>(value);
  return result;
}

Json real(double value) {
  Json result;
  result.kind = Kind::Number;
  result.number_v = value;
  return result;
}

Json boolean(bool value) {
  Json result;
  result.kind = Kind::Bool;
  result.bool_v = value;
  return result;
}

Json strings(const std::vector<std::uint32_t>& values) {
  Json result = array();
  for (const auto value : values) result.array_v.push_back(number(value));
  return result;
}

std::string category_name(NodeCategory category) {
  switch (category) {
    case NodeCategory::Program: return "Program";
    case NodeCategory::Block: return "Block";
    case NodeCategory::Statement: return "Statement";
    case NodeCategory::Expression: return "Expression";
  }
  throw std::logic_error("unknown compiled grammar category");
}

Json scope_json(const std::vector<RegionBinding>& scope) {
  Json result = array();
  for (const auto& binding : scope) {
    Json item = object();
    item.object_v["name"] = string(binding.name);
    item.object_v["type"] = string(std::string(type_name(binding.type)));
    result.array_v.push_back(std::move(item));
  }
  return result;
}

Json minimum_costs(const std::vector<std::uint32_t>& costs) {
  Json result = array();
  for (const auto cost : costs) {
    if (cost == kNoGrammarId) result.array_v.push_back(Json{});
    else result.array_v.push_back(number(cost));
  }
  return result;
}

Json identity_json(const CompiledGrammar& grammar, const char* format_version) {
  Json result = object();
  result.object_v["format_version"] = string(format_version);
  result.object_v["grammar_hash"] = string(grammar.content_hash());
  result.object_v["definition_version"] = string(kDefinitionVersion);
  result.object_v["catalog_version"] = string(std::string(kCatalogVersion));
  result.object_v["normalization_version"] = string(kNormalizationVersion);
  result.object_v["semantic_version"] = string(kGrammarSemanticVersion);
  result.object_v["generator_version"] = string(kGrammarGeneratorVersion);
  result.object_v["rng_version"] = string(kGrammarRngVersion);
  return result;
}

Json entry_json(const CompiledGrammar& grammar) {
  const auto& entry = grammar.nonterminals().at(grammar.entry());
  Json result = object();
  result.object_v["id"] = string(entry.stable_id);
  result.object_v["numeric_id"] = number(entry.id);
  result.object_v["type"] = string(std::string(type_name(entry.type)));
  result.object_v["category"] = string(category_name(entry.category));
  return result;
}

Json limits_json(const CompiledGrammar& grammar) {
  Json search = object();
  search.object_v["max_nodes"] = number(grammar.search_limits().max_nodes);
  search.object_v["max_depth"] = number(grammar.search_limits().max_depth);
  Json execution = object();
  execution.object_v["fuel"] = number(grammar.execution_limits().fuel);
  Json result = object();
  result.object_v["search"] = std::move(search);
  result.object_v["execution"] = std::move(execution);
  return result;
}

Json counts_json(const CompiledGrammar& grammar) {
  Json result = object();
  result.object_v["nonterminals"] = number(grammar.nonterminals().size());
  result.object_v["productions"] = number(grammar.productions().size());
  result.object_v["templates"] = number(grammar.templates().size());
  result.object_v["expressions"] = number(grammar.expressions().size());
  result.object_v["constant_domains"] = number(grammar.constants().size());
  result.object_v["contexts"] = number(grammar.contexts().size());
  result.object_v["structured_contracts"] = number(grammar.structured_contracts().size());
  return result;
}

Json validate_json(const CompiledGrammar& grammar) {
  Json result = identity_json(grammar, "grammar-validation-v1");
  result.object_v["valid"] = boolean(true);
  result.object_v["entry"] = entry_json(grammar);
  result.object_v["limits"] = limits_json(grammar);
  result.object_v["counts"] = counts_json(grammar);
  Json executable = object();
  try {
    grammar.require_executable();
    executable.object_v["value"] = boolean(true);
    executable.object_v["reason"] = Json{};
  } catch (const std::exception& error) {
    executable.object_v["value"] = boolean(false);
    executable.object_v["reason"] = string(error.what());
  }
  result.object_v["entry_executable"] = std::move(executable);
  return result;
}

Json nonterminals_json(const CompiledGrammar& grammar) {
  Json result = array();
  for (const auto& nonterminal : grammar.nonterminals()) {
    Json item = object();
    item.object_v["id"] = string(nonterminal.stable_id);
    item.object_v["numeric_id"] = number(nonterminal.id);
    item.object_v["type"] = string(std::string(type_name(nonterminal.type)));
    item.object_v["category"] = string(category_name(nonterminal.category));
    item.object_v["scope"] = scope_json(nonterminal.scope);
    item.object_v["mutation_locals"] = scope_json(nonterminal.mutation_locals);
    item.object_v["production_ids"] = strings(nonterminal.productions);
    item.object_v["minimum_depth"] = number(nonterminal.minimum_depth);
    item.object_v["minimum_nodes"] = number(nonterminal.minimum_nodes);
    item.object_v["minimum_nodes_by_depth"] = minimum_costs(nonterminal.minimum_nodes_by_depth);
    result.array_v.push_back(std::move(item));
  }
  return result;
}

Json productions_json(const CompiledGrammar& grammar) {
  Json result = array();
  for (const auto& production : grammar.productions()) {
    Json item = object();
    item.object_v["id"] = string(production.stable_id);
    item.object_v["numeric_id"] = number(production.id);
    item.object_v["nonterminal_id"] = number(production.nonterminal);
    item.object_v["replacement_class"] = number(production.replacement_class);
    item.object_v["closed_replacement_class"] = number(production.closed_replacement_class);
    item.object_v["crossover_group"] = string(production.crossover_group);
    item.object_v["weight"] = real(production.weight);
    item.object_v["root_expression_id"] = number(production.expression);
    item.object_v["minimum_nodes_by_depth"] = minimum_costs(production.minimum_nodes_by_depth);
    result.array_v.push_back(std::move(item));
  }
  return result;
}

Json templates_json(const CompiledGrammar& grammar) {
  Json result = array();
  for (const auto& definition : grammar.templates()) {
    Json item = object();
    item.object_v["id"] = string(definition.stable_id);
    item.object_v["numeric_id"] = number(definition.id);
    item.object_v["type"] = string(std::string(type_name(definition.type)));
    item.object_v["category"] = string(category_name(definition.category));
    item.object_v["scope"] = scope_json(definition.scope);
    item.object_v["body_expression_id"] = number(definition.body);
    Json holes = array();
    for (const auto& hole : definition.holes) {
      Json encoded = object();
      encoded.object_v["id"] = string(hole.stable_id);
      encoded.object_v["numeric_id"] = number(hole.id);
      encoded.object_v["type"] = string(std::string(type_name(hole.type)));
      encoded.object_v["category"] = string(category_name(hole.category));
      encoded.object_v["scope"] = scope_json(hole.scope);
      holes.array_v.push_back(std::move(encoded));
    }
    item.object_v["holes"] = std::move(holes);
    result.array_v.push_back(std::move(item));
  }
  return result;
}

Json region_slots_json(const std::vector<RegionSlot>& regions) {
  Json result = array();
  for (const auto& region : regions) {
    Json item = object();
    item.object_v["argument"] = number(region.argument);
    item.object_v["bindings"] = scope_json(region.bindings);
    result.array_v.push_back(std::move(item));
  }
  return result;
}

Json used_signatures_json(const CompiledGrammar& grammar) {
  std::map<std::uint32_t, std::vector<std::uint32_t>> primitives;
  std::map<std::uint32_t, std::vector<std::uint32_t>> controls;
  for (std::uint32_t id = 0; id < grammar.expressions().size(); ++id) {
    const auto& expression = grammar.expressions()[id];
    if (expression.kind == ExpressionKind::Primitive) primitives[expression.target].push_back(id);
    if (expression.kind == ExpressionKind::Control) controls[expression.target].push_back(id);
  }
  const auto& catalog = PrimitiveCatalog::standard();
  Json result = array();
  for (const auto& used : primitives) {
    const auto& signature = catalog.at(used.first);
    Json item = object();
    item.object_v["kind"] = string("primitive");
    item.object_v["numeric_id"] = number(signature.id);
    item.object_v["key"] = string(signature.key);
    item.object_v["operation"] = string(signature.operation);
    Json arguments = array();
    for (const auto type : signature.arguments)
      arguments.array_v.push_back(string(std::string(type_name(type))));
    item.object_v["arguments"] = std::move(arguments);
    item.object_v["result"] = string(std::string(type_name(signature.result)));
    item.object_v["executable"] = boolean(signature.executable());
    item.object_v["regions"] = region_slots_json(signature.regions);
    item.object_v["traversal_direction"] = signature.traversal_direction
        ? string(*signature.traversal_direction == TraversalDirection::Forward ? "forward" : "reverse")
        : Json{};
    item.object_v["expression_ids"] = strings(used.second);
    result.array_v.push_back(std::move(item));
  }
  const auto& control_signatures = catalog.control_signatures();
  for (const auto& used : controls) {
    if (used.first >= control_signatures.size())
      throw std::logic_error("compiled control signature ID is out of range");
    const auto& signature = control_signatures[used.first];
    Json item = object();
    item.object_v["kind"] = string("control");
    item.object_v["numeric_id"] = number(signature.id);
    item.object_v["key"] = string(signature.key);
    item.object_v["result_category"] = string(category_name(signature.result));
    Json arguments = array();
    for (const auto& slot : signature.arguments) {
      Json argument = object();
      argument.object_v["category"] = string(category_name(slot.category));
      argument.object_v["type"] = slot.value_type == RType::Invalid
          ? Json{} : string(std::string(type_name(slot.value_type)));
      arguments.array_v.push_back(std::move(argument));
    }
    item.object_v["arguments"] = std::move(arguments);
    item.object_v["requires_name"] = boolean(signature.requires_name);
    item.object_v["expression_ids"] = strings(used.second);
    result.array_v.push_back(std::move(item));
  }
  return result;
}

Json contexts_json(const CompiledGrammar& grammar) {
  Json result = array();
  for (const auto& context : grammar.contexts()) {
    Json item = object();
    item.object_v["numeric_id"] = number(context.id);
    item.object_v["scope"] = scope_json(context.scope);
    Json compatible = array();
    for (const auto& nonterminal : context.nonterminals) {
      Json encoded = object();
      const auto& definition = grammar.nonterminals().at(nonterminal.nonterminal);
      encoded.object_v["id"] = string(definition.stable_id);
      encoded.object_v["numeric_id"] = number(definition.id);
      encoded.object_v["scope_mapping"] = strings(nonterminal.scope_mapping);
      compatible.array_v.push_back(std::move(encoded));
    }
    item.object_v["compatible_nonterminals"] = std::move(compatible);
    Json by_type = object();
    const auto& types = value_types();
    for (std::size_t i = 0; i < types.size(); ++i)
      by_type.object_v[std::string(type_name(types[i]))] = strings(context.productions_by_type[i]);
    item.object_v["production_ids_by_type"] = std::move(by_type);
    Json by_category = object();
    const NodeCategory categories[] = {NodeCategory::Program, NodeCategory::Block,
        NodeCategory::Statement, NodeCategory::Expression};
    for (std::size_t i = 0; i < 4; ++i)
      by_category.object_v[category_name(categories[i])] = strings(context.productions_by_category[i]);
    item.object_v["production_ids_by_category"] = std::move(by_category);
    result.array_v.push_back(std::move(item));
  }
  return result;
}

Json inspect_json(const CompiledGrammar& grammar) {
  Json result = identity_json(grammar, "grammar-inspection-v1");
  result.object_v["entry"] = entry_json(grammar);
  result.object_v["limits"] = limits_json(grammar);
  result.object_v["counts"] = counts_json(grammar);
  result.object_v["inputs"] = scope_json(grammar.inputs());
  result.object_v["locals"] = scope_json(grammar.locals());
  result.object_v["nonterminals"] = nonterminals_json(grammar);
  result.object_v["productions"] = productions_json(grammar);
  result.object_v["templates"] = templates_json(grammar);
  result.object_v["used_signatures"] = used_signatures_json(grammar);
  result.object_v["contexts"] = contexts_json(grammar);
  return result;
}

void check_output_path(
    const std::filesystem::path& output,
    const std::vector<std::filesystem::path>& source_paths) {
  for (const auto& input : source_paths) {
    std::error_code error;
    if (std::filesystem::equivalent(output, input, error) ||
        std::filesystem::weakly_canonical(output) ==
            std::filesystem::weakly_canonical(input)) {
      throw std::invalid_argument(
          "--out-json must not overwrite the root grammar or an imported grammar");
    }
  }
}

void write_output(const std::filesystem::path& output, const std::string& text) {
  std::string temporary = output.string() + ".tmp.XXXXXX";
  int descriptor = ::mkstemp(temporary.data());
  if (descriptor < 0)
    throw std::system_error(errno, std::generic_category(), "cannot create output temporary file");
  try {
    std::size_t offset = 0;
    while (offset < text.size()) {
      const auto count = ::write(descriptor, text.data() + offset, text.size() - offset);
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0)
        throw std::system_error(count < 0 ? errno : EIO, std::generic_category(), "cannot write output");
      offset += static_cast<std::size_t>(count);
    }
    if (::close(descriptor) != 0)
      throw std::system_error(errno, std::generic_category(), "cannot close output");
    descriptor = -1;
    std::filesystem::rename(temporary, output);
  } catch (...) {
    if (descriptor >= 0) ::close(descriptor);
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    throw;
  }
}

Json diagnostic_json(const std::string& action, const std::string& phase,
                     const std::string& source, const std::string& message) {
  Json root = object();
  root.object_v["format_version"] = string("grammar-diagnostic-v1");
  root.object_v["valid"] = boolean(false);
  Json diagnostic = object();
  diagnostic.object_v["action"] = string(action);
  diagnostic.object_v["phase"] = string(phase);
  diagnostic.object_v["message"] = string(message);
  Json location = object();
  location.object_v["file"] = string(source);
  location.object_v["json_pointer"] = string("");
  diagnostic.object_v["source"] = std::move(location);
  Json diagnostics = array();
  diagnostics.array_v.push_back(std::move(diagnostic));
  root.object_v["diagnostics"] = std::move(diagnostics);
  return root;
}

void usage() {
  std::cout << "Usage: gagp_grammar_cli {validate|inspect|resolve} --grammar-definition PATH [--out-json PATH]\n"
               "       gagp_grammar_cli --help\n";
}
}  // namespace

int run_grammar_author_command(int argc, char** argv) {
  std::string action;
  std::string source;
  std::string phase = "command";
  try {
    if (argc == 2 && std::string(argv[1]) == "--help") {
      usage();
      return 0;
    }
    if (argc < 2) throw std::invalid_argument("missing action; expected validate, inspect, or resolve");
    action = argv[1];
    if (action != "validate" && action != "inspect" && action != "resolve")
      throw std::invalid_argument("unknown action: " + action);
    std::map<std::string, std::string> options;
    for (int i = 2; i < argc; ++i) {
      const std::string flag = argv[i];
      if (flag != "--grammar-definition" && flag != "--out-json")
        throw std::invalid_argument("unknown flag: " + flag);
      if (options.count(flag)) throw std::invalid_argument("duplicate flag: " + flag);
      if (++i == argc || std::string(argv[i]).empty() || std::string(argv[i]).rfind("--", 0) == 0)
        throw std::invalid_argument("missing value for " + flag);
      options.emplace(flag, argv[i]);
    }
    if (!options.count("--grammar-definition"))
      throw std::invalid_argument("required flag: --grammar-definition");
    source = options.at("--grammar-definition");

    phase = "resolve";
    const auto definition = load_definition(source);
    if (options.count("--out-json")) {
      phase = "command";
      check_output_path(options.at("--out-json"), definition.source_paths);
    }
    phase = "resolve";
    std::string artifact;
    if (action == "resolve") {
      artifact = definition.canonical;
    } else {
      phase = "compile";
      const auto grammar = compile_grammar(definition);
      artifact = canonical_json(action == "validate" ? validate_json(grammar) : inspect_json(grammar));
    }
    if (options.count("--out-json")) {
      phase = "output";
      write_output(options.at("--out-json"), artifact);
      std::cout << "action=" << action << " grammar_hash=" << definition.content_hash << '\n';
    } else {
      std::cout << artifact;
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << canonical_json(diagnostic_json(action, phase, source, error.what())) << '\n';
    return 2;
  }
}

}  // namespace gagp::cli_detail
