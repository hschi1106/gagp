#pragma once

#include "gagp/cli/json.hpp"
#include "gagp/core/bytecode.hpp"
#include "gagp/serialization/region_plan_json.hpp"

namespace gagp::cli_detail {

using serialization::decode_region_plan;
using serialization::encode_region_plan;

JsonValue encode_bounded_region_segment(const BoundedRegionSegment& segment);
BoundedRegionSegment decode_bounded_region_segment(const JsonValue& value);

}  // namespace gagp::cli_detail
