#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "gagp/cli/json.hpp"
#include "gagp/evolution/ast_program.hpp"

namespace gagp::evo::grammar {

using ConstantData = std::variant<std::int64_t, double, bool, char32_t, std::string,
    std::vector<std::int64_t>, std::vector<double>, std::vector<std::string>>;

// Decoded owned values: compilation never depends on payload registry tokens.
struct ConstantDomain {
  RType type = RType::Invalid;
  std::vector<ConstantData> values;
  bool integer_range = false;
  std::int64_t minimum = 0;
  std::int64_t maximum = 0;
};

ConstantDomain parse_constant_domain(const cli_detail::JsonValue& definition);

}  // namespace gagp::evo::grammar
