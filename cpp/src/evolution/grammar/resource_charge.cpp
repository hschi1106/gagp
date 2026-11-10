#include "gagp/evolution/grammar/resource_charge.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

#include "gagp/cli/json.hpp"

namespace gagp::evo::grammar {
ResourceCharge parse_resource_charge(const cli_detail::JsonValue& value) {
  using Kind = cli_detail::JsonValue::Kind;
  if (value.kind != Kind::Object || value.object_v.size() != 3 ||
      !value.object_v.count("nodes") || !value.object_v.count("depth") || !value.object_v.count("resets_depth"))
    throw std::invalid_argument("resource_charge requires exactly nodes, depth and resets_depth");
  const auto number = [&](const char* name, double maximum) {
    const auto& field = value.object_v.at(name);
    if (field.kind != Kind::Number || !std::isfinite(field.number_v) || field.number_v < 0 ||
        field.number_v > maximum || std::floor(field.number_v) != field.number_v)
      throw std::invalid_argument(std::string("resource_charge ") + name + " must be a bounded nonnegative integer");
    return static_cast<std::uint64_t>(field.number_v);
  };
  const auto& reset = value.object_v.at("resets_depth");
  if (reset.kind != Kind::Bool) throw std::invalid_argument("resource_charge resets_depth must be Boolean");
  return {number("nodes",65536),number("depth",256),reset.bool_v};
}

}  // namespace gagp::evo::grammar
