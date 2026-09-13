#pragma once

#include <filesystem>
#include <string>

#include "gagp/cli/json.hpp"

namespace gagp::evo::grammar {

inline constexpr const char* kDefinitionVersion = "grammar-definition-v1";
inline constexpr const char* kNormalizationVersion = "1";

struct ResolvedDefinition {
  cli_detail::JsonValue document;
  std::string canonical;
  std::string content_hash;
};

std::string canonical_json(const cli_detail::JsonValue& value);
ResolvedDefinition load_definition(const std::filesystem::path& path);
// In-memory definitions must be self-contained; relative imports need a file.
ResolvedDefinition parse_definition(const std::string& text);

}  // namespace gagp::evo::grammar
