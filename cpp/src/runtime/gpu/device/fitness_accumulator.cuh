#pragma once

#include <cmath>

namespace gagp::gpu_detail {

__device__ inline double d_canonicalize_fitness_accumulator(double value) {
  if (!isfinite(value) || value == 0.0) return value == 0.0 ? 0.0 : value;
  // A 48-bit significand retains every integer in this closed interval.
  // The range check precedes conversion, including for very large doubles.
  constexpr double exact_integer_limit = 281474976710656.0; // 2^48
  if (value >= -exact_integer_limit && value <= exact_integer_limit &&
      static_cast<double>(static_cast<long long>(value)) == value) return value;
  int exponent = 0;
  const double mantissa = frexp(value, &exponent);
  constexpr int kMantissaBits = 48;
  const long long quantized_mantissa = llround(ldexp(mantissa, kMantissaBits));
  return ldexp(static_cast<double>(quantized_mantissa), exponent - kMantissaBits);
}

} // namespace gagp::gpu_detail
