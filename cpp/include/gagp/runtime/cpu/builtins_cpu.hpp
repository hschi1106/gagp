#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "gagp/core/builtin.hpp"
#include "gagp/core/errors.hpp"
#include "gagp/core/value.hpp"

namespace gagp {

struct BuiltinResult {
  bool is_error = false;
  Value value = Value::invalid();
  Err err{ErrCode::Value, ""};
};

// Borrows the contiguous argument range for the duration of the call. args may
// be null only when argc is zero.
BuiltinResult builtin_call(BuiltinId id, const Value* args, std::size_t argc);
BuiltinResult builtin_call(BuiltinId id, const std::vector<Value>& args);

}  // namespace gagp
