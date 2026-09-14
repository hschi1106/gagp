#pragma once

#include "gagp/evolution/grammar/constants.hpp"

namespace gagp::evo::grammar {

Value materialize_constant(RType type, const ConstantData& data);
// Singleton-domain encoding preserves signed-64 integers and decoded payloads.
cli_detail::JsonValue encode_constant(const Value& value);
Value decode_constant(const cli_detail::JsonValue& value);

}  // namespace gagp::evo::grammar
