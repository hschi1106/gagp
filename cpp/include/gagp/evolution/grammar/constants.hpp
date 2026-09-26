#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "gagp/cli/json.hpp"
#include "gagp/evolution/ast_program.hpp"
#include "gagp/evolution/grammar/constant_policy.hpp"

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
  bool float_range = false;
  double float_minimum = 0;
  double float_maximum = 0;
  double float_quantization_scale = 0;
  // Optional checked numeric subset used for construction and resampling.
  // Membership continues to use this domain's own range and quantization.
  std::shared_ptr<const ConstantDomain> sampling;
  // A bounded sequence has an exact scalar element domain (String uses Char).
  // StringList may in turn use a bounded String domain; nested lists are absent.
  std::shared_ptr<const ConstantDomain> elements;
  std::uint32_t minimum_length = 0;
  std::uint32_t maximum_length = 0;
  std::uint64_t sequence_storage_bound = 0;
  ConstantMutationPolicy mutation = ConstantMutationPolicy::Resample;
  ConstantMutationDelta delta;
};

ConstantDomain parse_constant_domain(const cli_detail::JsonValue& definition);

}  // namespace gagp::evo::grammar
