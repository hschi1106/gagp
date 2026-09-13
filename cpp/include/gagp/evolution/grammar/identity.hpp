#pragma once

#include <string>
#include <string_view>

namespace gagp::evo::grammar {

// Lowercase SHA-256 of the exact bytes; grammar identity hashes canonical export.
std::string content_sha256(std::string_view bytes);

}  // namespace gagp::evo::grammar
