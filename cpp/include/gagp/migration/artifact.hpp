#pragma once

#include <cstdint>
#include <string>

#include "gagp/cli/json.hpp"
#include "gagp/migration/legacy_ast_v1.hpp"

namespace gagp::migration {

inline constexpr const char* kMaterializedVersion = "grammar-materialized-v2";

legacy_v1::AstProgram decode_legacy_ast_artifact(
    const cli_detail::JsonValue& document);

// Migrates one complete legacy artifact. Dispatch is exclusively by the
// top-level format_version field.
std::string migrate_artifact(const std::string& input_text,
                             const std::string& cases_text,
                             const std::string& conversion_profile,
                             std::uint32_t fuel,
                             std::uint32_t max_nodes,
                             std::uint32_t max_depth,
                             bool explicit_ast_limits);

}  // namespace gagp::migration
