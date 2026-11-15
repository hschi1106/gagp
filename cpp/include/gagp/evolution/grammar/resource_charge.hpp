#pragma once

#include <cstdint>

namespace gagp::cli_detail { struct JsonValue; }

namespace gagp::evo::grammar {

// An authored `resource_charge`, independent of runtime fuel and physical
// allocation. It participates in grammar identity only: no search budgets it.
struct ResourceCharge {
  std::uint64_t nodes = 1;
  std::uint64_t depth = 1;
  bool resets_depth = false;
};

// Exact authored charge object; shared by source validation and compilation.
ResourceCharge parse_resource_charge(const cli_detail::JsonValue& value);

}  // namespace gagp::evo::grammar
