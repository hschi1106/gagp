#include "gagp/migration/artifact.hpp"

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace {

std::string read_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot read migration input: " + path);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

void check_output_path(const std::filesystem::path& input,
                       const std::filesystem::path& output) {
  std::error_code error;
  const bool equivalent = std::filesystem::equivalent(input, output, error);
  if (equivalent || std::filesystem::weakly_canonical(input) ==
                        std::filesystem::weakly_canonical(output)) {
    throw std::invalid_argument("migration input and output must differ");
  }
}

void write_file_atomically(const std::filesystem::path& output,
                           const std::string& contents) {
  const auto directory = output.has_parent_path() ? output.parent_path()
                                                   : std::filesystem::path{"."};
  std::string temporary =
      (directory / ".gagp_migrate_artifact.tmp.XXXXXX").string();
  int descriptor = ::mkstemp(temporary.data());
  if (descriptor < 0)
    throw std::runtime_error("cannot write migration output: " + output.string());

  try {
    std::size_t offset = 0;
    while (offset < contents.size()) {
      const auto count =
          ::write(descriptor, contents.data() + offset, contents.size() - offset);
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0)
        throw std::runtime_error("failed writing migration output: " + output.string());
      offset += static_cast<std::size_t>(count);
    }
    if (::fsync(descriptor) != 0)
      throw std::runtime_error("failed writing migration output: " + output.string());
    const int close_status = ::close(descriptor);
    descriptor = -1;
    if (close_status != 0)
      throw std::runtime_error("failed writing migration output: " + output.string());
    std::error_code rename_error;
    std::filesystem::rename(temporary, output, rename_error);
    if (rename_error)
      throw std::runtime_error("failed writing migration output: " + output.string());
  } catch (...) {
    if (descriptor >= 0) ::close(descriptor);
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    throw;
  }
}

std::uint32_t positive(const std::string& text, const char* option) {
  std::size_t used = 0;
  unsigned long value = 0;
  try { value = std::stoul(text, &used, 10); }
  catch (...) { throw std::invalid_argument(std::string(option) + " requires a positive integer"); }
  if (used != text.size() || value == 0 ||
      value > std::numeric_limits<std::uint32_t>::max())
    throw std::invalid_argument(std::string(option) + " requires a positive integer");
  return static_cast<std::uint32_t>(value);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    std::map<std::string, std::string> options;
    for (int i = 1; i < argc; ++i) {
      const std::string option = argv[i];
      if (option != "--input" && option != "--cases" && option != "--out" &&
          option != "--conversion-profile" &&
          option != "--fuel" && option != "--max-nodes" && option != "--max-depth")
        throw std::invalid_argument("unknown migration option: " + option);
      if (++i == argc) throw std::invalid_argument("missing value for " + option);
      if (!options.emplace(option, argv[i]).second)
        throw std::invalid_argument("duplicate migration option: " + option);
    }
    for (const char* required : {"--input", "--out"})
      if (!options.count(required)) throw std::invalid_argument(std::string("missing required option ") + required);
    const bool any_limit = options.count("--fuel") || options.count("--max-nodes") ||
                           options.count("--max-depth");
    const bool all_limits = options.count("--fuel") && options.count("--max-nodes") &&
                           options.count("--max-depth");
    if (any_limit && !all_limits)
      throw std::invalid_argument("--fuel, --max-nodes and --max-depth must be supplied together");
    check_output_path(options.at("--input"), options.at("--out"));
    const auto result = gagp::migration::migrate_artifact(
        read_file(options.at("--input")),
        options.count("--cases") ? read_file(options.at("--cases")) : std::string{},
        options.count("--conversion-profile")
            ? options.at("--conversion-profile")
            : std::string{},
        all_limits ? positive(options.at("--fuel"), "--fuel") : 0,
        all_limits ? positive(options.at("--max-nodes"), "--max-nodes") : 0,
        all_limits ? positive(options.at("--max-depth"), "--max-depth") : 0,
        all_limits);
    write_file_atomically(options.at("--out"), result + '\n');
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "gagp_migrate_artifact: " << error.what() << '\n';
    return 2;
  }
}
