#include "gagp/cli/grammar_generate.hpp"

#include <array>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

#include "gagp/cli/commands.hpp"
#include "gagp/cli/grammar_population_artifact.hpp"
#include "gagp/evolution/population_init.hpp"

namespace gagp::cli_detail {
namespace {
constexpr std::size_t kMaxInputBytes = 256u * 1024u * 1024u;

std::uint64_t parse_unsigned(const std::string& text, const std::string& flag) {
  std::uint64_t value = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || (text.size() > 1 && text.front() == '0') ||
      result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
    throw std::invalid_argument(flag + " requires a canonical unsigned 64-bit integer");
  }
  return value;
}

std::string read_input(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("cannot open input: " + path.string());
  std::string text;
  std::array<char, 65536> buffer{};
  while (stream.read(buffer.data(), buffer.size()) || stream.gcount() != 0) {
    const auto count = static_cast<std::size_t>(stream.gcount());
    if (count > kMaxInputBytes - text.size())
      throw std::invalid_argument("input exceeds 256 MiB: " + path.string());
    text.append(buffer.data(), count);
  }
  if (!stream.eof()) throw std::runtime_error("cannot read input: " + path.string());
  return text;
}

void check_output_path(const std::filesystem::path& output,
                       const std::filesystem::path& input) {
  std::error_code error;
  const bool equivalent = std::filesystem::equivalent(output, input, error);
  if (equivalent || std::filesystem::weakly_canonical(output) ==
                        std::filesystem::weakly_canonical(input)) {
    throw std::invalid_argument("--out-json must not overwrite an input file");
  }
}

void write_output(const std::filesystem::path& output, const std::string& text) {
  std::string pattern = output.string() + ".tmp.XXXXXX";
  int descriptor = ::mkstemp(pattern.data());
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
    const int status = ::close(descriptor);
    descriptor = -1;
    if (status != 0)
      throw std::system_error(errno, std::generic_category(), "cannot close output");
    std::filesystem::rename(pattern, output);
  } catch (...) {
    if (descriptor >= 0) ::close(descriptor);
    std::error_code ignored;
    std::filesystem::remove(pattern, ignored);
    throw;
  }
}

void validate_member_cases(const MaterializedGrammarArtifact& member, const evo::CaseSet& cases) {
  if (member.return_type != cases.expected_return_type)
    throw std::invalid_argument("case expected_return_type must match replay member return type");
  std::map<std::string, evo::RType> inputs;
  for (const auto& input : member.inputs) inputs.emplace(input.name, input.type);
  if (inputs.size() != cases.input_specs.size())
    throw std::invalid_argument("case input schema must match replay member inputs");
  for (const auto& input : cases.input_specs) {
    const auto found = inputs.find(input.name);
    if (input.type == evo::RType::Any || input.type == evo::RType::Invalid ||
        found == inputs.end() || found->second != input.type)
      throw std::invalid_argument("case input schema must match replay member inputs by name and exact type");
  }
}
}  // namespace

int run_grammar_generate_command(int argc, char** argv) {
  std::map<std::string, std::string> options;
  bool help = false;
  for (int i = 1; i < argc; ++i) {
    const std::string flag = argv[i];
    if (flag == "--help") {
      if (help) throw std::invalid_argument("duplicate flag: --help");
      help = true;
      continue;
    }
    if (flag != "--grammar-definition" && flag != "--cases" && flag != "--out-json" &&
        flag != "--population-size" && flag != "--seed" && flag != "--replay-json")
      throw std::invalid_argument("unknown flag: " + flag);
    if (options.count(flag)) throw std::invalid_argument("duplicate flag: " + flag);
    if (++i == argc || std::string(argv[i]).empty() || std::string(argv[i]).rfind("--", 0) == 0)
      throw std::invalid_argument("missing value for " + flag);
    options.emplace(flag, argv[i]);
  }
  if (help) {
    std::cout << "Usage: gagp_generate_cli --cases PATH --out-json PATH\n"
                 "  --grammar-definition PATH  Required for generation; optional identity check for replay\n"
                 "  --population-size N        1..65536 (default: 1)\n"
                 "  --seed N                   Canonical uint64 (default: 0)\n"
                 "  --replay-json PATH         Replay grammar-population-v1; excludes size and seed\n"
                 "  --help                     Show this help\n";
    return 0;
  }
  for (const auto* required : {"--cases", "--out-json"})
    if (!options.count(required)) throw std::invalid_argument(std::string("required flag: ") + required);
  const bool replay = options.count("--replay-json") != 0;
  if (!replay && !options.count("--grammar-definition"))
    throw std::invalid_argument("generation requires --grammar-definition");
  if (replay && (options.count("--seed") || options.count("--population-size")))
    throw std::invalid_argument("--replay-json excludes --seed and --population-size");
  const auto size = options.count("--population-size") ? parse_unsigned(options.at("--population-size"), "--population-size") : 1;
  if (size == 0 || size > 65536) throw std::invalid_argument("--population-size must be 1..65536");
  const auto seed = options.count("--seed") ? parse_unsigned(options.at("--seed"), "--seed") : 0;
  for (const auto* input : {"--cases", "--grammar-definition", "--replay-json"})
    if (options.count(input)) check_output_path(options.at("--out-json"), options.at(input));

  const auto cases = evo::prepare_case_set(
      decode_fitness_cases_json(JsonParser(read_input(options.at("--cases")), {true, 512}).parse()),
      evo::GrammarConfig::all_enabled());
  std::optional<evo::grammar::CompiledGrammar> grammar;
  if (options.count("--grammar-definition"))
    grammar.emplace(evo::grammar::compile_grammar(evo::grammar::load_definition(options.at("--grammar-definition"))));
  std::string artifact, hash;
  std::size_t count = 0;
  if (replay) {
    artifact = read_input(options.at("--replay-json"));
    count = replay_generated_population_artifact(artifact, grammar ? &*grammar : nullptr).size();
    const auto root = JsonParser(artifact, {true, 512}).parse();
    for (const auto& member : require_object_field(root, "members").array_v)
      validate_member_cases(decode_materialized_program(evo::grammar::canonical_json(member)), cases);
    artifact = evo::grammar::canonical_json(root);
    hash = require_string(require_object_field(root, "grammar_hash"), "grammar_hash");
  } else {
    const auto initialized = evo::initialize_population(*grammar, cases, static_cast<int>(size), seed);
    artifact = encode_generated_population_artifact(*grammar, initialized.population);
    count = initialized.population.size();
    hash = grammar->content_hash();
  }
  write_output(options.at("--out-json"), artifact);
  std::cout << "count=" << count << " grammar_hash=" << hash << '\n';
  return 0;
}

}  // namespace gagp::cli_detail
