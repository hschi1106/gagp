#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "gagp/cli/json.hpp"

namespace gagp::evo::grammar {

inline constexpr const char* kDefinitionVersion = "grammar-definition-v2";
inline constexpr const char* kNormalizationVersion = "1";

enum class GenerationStage : std::uint8_t { Initial, Mutation };
const char* generation_stage_name(GenerationStage stage);
GenerationStage parse_generation_stage(const std::string& name);
// Missing generation_stages means both stages; [] admits membership only.
std::uint8_t production_generation_mask(const cli_detail::JsonValue& alternative);

struct ResolvedDefinition {
  cli_detail::JsonValue document;
  std::string canonical;
  std::string content_hash;
  // Canonical root/import paths consulted while resolving a file-backed
  // definition. In-memory definitions have no source paths.
  std::vector<std::filesystem::path> source_paths;
};

std::string canonical_json(const cli_detail::JsonValue& value);
ResolvedDefinition load_definition(const std::filesystem::path& path);
// In-memory definitions must be self-contained; relative imports need a file.
ResolvedDefinition parse_definition(const std::string& text);

}  // namespace gagp::evo::grammar
