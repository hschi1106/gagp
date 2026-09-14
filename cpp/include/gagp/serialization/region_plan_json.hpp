#pragma once

#include "gagp/cli/json.hpp"
#include "gagp/core/region_plan.hpp"

namespace gagp::serialization {

cli_detail::JsonValue encode_region_plan(const RegionPlan& plan);
RegionPlan decode_region_plan(const cli_detail::JsonValue& value);

}  // namespace gagp::serialization
