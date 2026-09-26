#pragma once

#include "gagp/evolution/grammar/constants.hpp"

namespace gagp::evo::grammar {

class GrammarRandom;
Value materialize_constant(RType type, const ConstantData& data);
Value sample_constant(const ConstantDomain& domain, GrammarRandom& random);
// The previous value must be a member of the compiled domain.
Value mutate_constant_value(const ConstantDomain& domain, const Value& previous, GrammarRandom& random);
bool constant_domain_contains(const ConstantDomain& domain, const Value& value);
// Singleton-domain encoding preserves signed-64 integers and decoded payloads.
cli_detail::JsonValue encode_constant(const Value& value);
// Byte-identical to canonical_json(encode_constant(value)), without constructing
// the singleton domain object for scalar values.
std::string canonical_constant_encoding(const Value& value);
Value decode_constant(const cli_detail::JsonValue& value);

}  // namespace gagp::evo::grammar
