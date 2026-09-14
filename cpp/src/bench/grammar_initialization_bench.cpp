#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

#include "gagp/cli/commands.hpp"
#include "gagp/evolution/grammar/config_adapter.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/population_init.hpp"

namespace {
namespace evo = gagp::evo;
namespace grammar = evo::grammar;
namespace cli = gagp::cli_detail;
using Json = cli::JsonValue;
using Clock = std::chrono::steady_clock;

Json string(std::string value) { Json out; out.kind = Json::Kind::String; out.string_v = std::move(value); return out; }
Json number(double value) { Json out; out.kind = Json::Kind::Number; out.number_v = value; return out; }
Json boolean(bool value) { Json out; out.kind = Json::Kind::Bool; out.bool_v = value; return out; }
Json object(std::map<std::string, Json> values) { Json out; out.kind = Json::Kind::Object; out.object_v = std::move(values); return out; }
double milliseconds(Clock::time_point begin, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - begin).count();
}

std::uint64_t parse_unsigned(const std::string& text, const std::string& flag) {
  std::uint64_t value = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || (text.size() > 1 && text.front() == '0') ||
      result.ec != std::errc{} || result.ptr != text.data() + text.size())
    throw std::invalid_argument(flag + " requires a canonical unsigned 64-bit integer");
  return value;
}

Json read_json(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("cannot open input: " + path);
  std::string text;
  std::array<char, 65536> buffer{};
  constexpr std::size_t max_bytes = 256u * 1024u * 1024u;
  while (input.read(buffer.data(), buffer.size()) || input.gcount() != 0) {
    const auto count = static_cast<std::size_t>(input.gcount());
    if (count > max_bytes - text.size()) throw std::invalid_argument("input exceeds 256 MiB: " + path);
    text.append(buffer.data(), count);
  }
  if (!input.eof()) throw std::runtime_error("cannot read input: " + path);
  return cli::JsonParser(std::move(text), {true, 512}).parse();
}

void require_distinct(const std::string& first, const std::string& second) {
  std::error_code error;
  if (std::filesystem::equivalent(first, second, error) ||
      std::filesystem::weakly_canonical(first) == std::filesystem::weakly_canonical(second))
    throw std::invalid_argument("output paths must differ from inputs and each other");
}

void write_file(const std::string& path, const std::string& text) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) throw std::runtime_error("cannot open output: " + path);
  output << text << '\n';
  output.close();
  if (!output) throw std::runtime_error("cannot write output: " + path);
}

int run(int argc, char** argv) {
  std::map<std::string, std::string> options;
  bool help = false;
  for (int i = 1; i < argc; ++i) {
    const std::string flag = argv[i];
    if (flag == "--help") {
      if (help) throw std::invalid_argument("duplicate flag: --help");
      help = true;
      continue;
    }
    if (flag != "--grammar-config" && flag != "--cases" && flag != "--population-size" &&
        flag != "--seed" && flag != "--warmups" && flag != "--trials" &&
        flag != "--out-json" && flag != "--out-definition")
      throw std::invalid_argument("unknown flag: " + flag);
    if (options.count(flag)) throw std::invalid_argument("duplicate flag: " + flag);
    if (++i == argc || std::string(argv[i]).empty() || std::string(argv[i]).rfind("--", 0) == 0)
      throw std::invalid_argument("missing value for " + flag);
    options.emplace(flag, argv[i]);
  }
  if (help) {
    std::cout << "Usage: gagp_grammar_initialization_bench --grammar-config PATH --cases PATH\n"
                 "  --population-size N --seed N --out-json PATH --out-definition PATH\n"
                 "  [--warmups N (default 3)] [--trials N (default 15)]\n"
                 "Scalar conversion early warning; not a legacy distribution equivalence benchmark.\n";
    return 0;
  }
  for (const auto* required : {"--grammar-config", "--cases", "--population-size", "--seed", "--out-json", "--out-definition"})
    if (!options.count(required)) throw std::invalid_argument(std::string("required flag: ") + required);
  const auto size = parse_unsigned(options.at("--population-size"), "--population-size");
  const auto seed = parse_unsigned(options.at("--seed"), "--seed");
  const auto warmups = options.count("--warmups") ? parse_unsigned(options.at("--warmups"), "--warmups") : 3;
  const auto trials = options.count("--trials") ? parse_unsigned(options.at("--trials"), "--trials") : 15;
  if (size == 0 || size > 65536) throw std::invalid_argument("--population-size must be 1..65536");
  if (warmups > 65536 || trials == 0 || trials > 65536)
    throw std::invalid_argument("--warmups must be 0..65536 and --trials must be 1..65536");
  for (const auto* output : {"--out-json", "--out-definition"})
    for (const auto* input : {"--grammar-config", "--cases"}) require_distinct(options.at(output), options.at(input));
  require_distinct(options.at("--out-json"), options.at("--out-definition"));

  const auto setup_begin = Clock::now();
  const auto config = cli::decode_grammar_config_json(read_json(options.at("--grammar-config")));
  if (!config.value_int || !config.value_bool || config.value_char || config.value_string ||
      config.value_int_list || config.value_float_list || config.value_string_list)
    throw std::invalid_argument("benchmark requires only Int/Float/Bool types, with Int and Bool enabled");
  if (config.expression_map_list || config.expression_filter_list || config.expression_linear_rec ||
      config.expression_asgp_dc || config.expression_asgp_dp1d || config.expression_asgp_dp2d)
    throw std::invalid_argument("benchmark requires map/filter/linear-rec/ASGP expressions disabled");
  const auto cases = evo::prepare_case_set(cli::decode_fitness_cases_json(read_json(options.at("--cases"))), config);
  grammar::GrammarConfigConversion conversion;
  conversion.return_type = cases.expected_return_type;
  conversion.inputs = cases.input_specs;
  std::set<std::string> input_names;
  for (const auto& input : cases.input_specs) input_names.insert(input.name);
  std::string local_name = "benchmark_i";
  for (std::uint64_t suffix = 1; input_names.count(local_name); ++suffix)
    local_name = "benchmark_i_" + std::to_string(suffix);
  conversion.locals.push_back({local_name, evo::RType::Int});
  conversion.search_limits = {80, 32};
  conversion.execution_limits = {20000};
  conversion.max_statements_per_block = 6;
  conversion.max_for_k = 16;
  grammar::ConstantDomain integers;
  integers.type = evo::RType::Int;
  integers.integer_range = true;
  integers.minimum = -8;
  integers.maximum = 8;
  conversion.constants.push_back(std::move(integers));
  if (config.value_float) {
    grammar::ConstantDomain floats;
    floats.type = evo::RType::Float;
    for (int i = -8000; i <= 8000; ++i) floats.values.emplace_back(static_cast<double>(i) / 1000.0);
    floats.values.emplace_back(-0.0);
    conversion.constants.push_back(std::move(floats));
  }
  grammar::ConstantDomain booleans;
  booleans.type = evo::RType::Bool;
  booleans.values = {false, true};
  conversion.constants.push_back(std::move(booleans));

  const auto definition = grammar::convert_grammar_config(config, conversion);
  const auto compiled = grammar::compile_grammar(definition);
  const double setup_ms = milliseconds(setup_begin, Clock::now());
  Json rows;
  rows.kind = Json::Kind::Array;
  for (std::uint64_t index = 0; index < warmups + trials; ++index) {
    const auto trial_seed = seed + index * size;  // Deliberate uint64 modular seed arithmetic.
    const auto begin = Clock::now();
    const auto initialized = evo::initialize_population(compiled, cases, static_cast<int>(size), trial_seed);
    const double init_ms = milliseconds(begin, Clock::now());
    std::uint64_t nodes = 0, steps = 0, instructions = 0;
    for (const auto& genome : initialized.population) {
      if (!genome.derivation) throw std::runtime_error("generated member missing derivation metadata");
      nodes += genome.ast.nodes.size();
      steps += genome.derivation->logical_steps;
      instructions += genome.derivation->lowered_instructions;
    }
    rows.array_v.push_back(object({{"index", number(index)}, {"measured", boolean(index >= warmups)},
        {"warmup", boolean(index < warmups)}, {"seed", string(std::to_string(trial_seed))},
        {"population_size", number(initialized.population.size())}, {"init_ms", number(init_ms)},
        {"node_count_total", number(nodes)}, {"logical_steps_total", number(steps)},
        {"lowered_instructions_total", number(instructions)}}));
  }
  const auto result = object({{"format_version", string("grammar-initialization-v1")},
      {"grammar_hash", string(compiled.content_hash())}, {"setup_ms", number(setup_ms)},
      {"grammar_config", string(options.at("--grammar-config"))}, {"cases", string(options.at("--cases"))},
      {"seed", string(std::to_string(seed))}, {"population_size", number(size)},
      {"warmups", number(warmups)}, {"trials", number(trials)}, {"rows", std::move(rows)},
      {"conversion_version", string(grammar::kGrammarConfigConversionVersion)},
      {"conversion_policy", object({{"max_nodes", number(80)}, {"max_depth", number(32)},
          {"fuel", number(20000)}, {"max_statements_per_block", number(6)}, {"max_for_k", number(16)},
          {"local_name", string(local_name)}, {"int_constants", string("[-8,8]")},
          {"float_constants", string(config.value_float ? "all thousandths in [-8,8], plus -0.0" : "disabled")},
          {"bool_constants", string("false,true")}})},
      {"policy_note", string("Explicit scalar conversion early warning, not legacy distribution equivalence. Legacy depth semantics differ from the new materialized-prefix depth limit. New initialization includes membership checks and native bytecode lowering/compilation. setup_ms includes input loading, case preparation, constant-domain construction, conversion, its internal validation compile, and final compile. Per-population init_ms includes generation, membership and bytecode compilation. Summaries, output, and population destruction are outside initialization timing. Trial seeds use seed + index * population_size modulo 2^64, including warmups.")}});
  write_file(options.at("--out-definition"), definition.canonical);
  write_file(options.at("--out-json"), grammar::canonical_json(result));
  std::cout << "trials=" << trials << " population_size=" << size << " setup_ms=" << setup_ms
            << " grammar_hash=" << compiled.content_hash() << '\n';
  return 0;
}
}  // namespace

int main(int argc, char** argv) {
  try { return run(argc, argv); }
  catch (const std::exception& error) {
    std::cerr << "grammar initialization benchmark: " << error.what() << '\n';
    return 1;
  }
}
