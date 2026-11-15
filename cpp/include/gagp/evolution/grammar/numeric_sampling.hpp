#pragma once

#include <cfloat>
#include <cmath>
#include <cstdint>

namespace gagp::evo::grammar {

#ifdef __CUDACC__
#define GAGP_NUMERIC_HD __host__ __device__
#else
#define GAGP_NUMERIC_HD
#endif

// Preconditions: finite, ordered endpoints. Each operation rounds separately;
// CUDA contraction must not change seed replay relative to the CPU.
GAGP_NUMERIC_HD inline double sample_float_interval(
    double minimum, double maximum, std::uint64_t word) {
  if (minimum == maximum) return minimum;
  const double fraction = static_cast<double>(word >> 11) * 0x1.0p-53;
  double result;
#ifdef __CUDA_ARCH__
  const double width = __dsub_rn(maximum, minimum);
  if (width <= DBL_MAX) {
    result = __dadd_rn(minimum, __dmul_rn(fraction, width));
  } else {
    result = __dadd_rn(__dmul_rn(minimum, __dsub_rn(1.0, fraction)),
                      __dmul_rn(maximum, fraction));
  }
  if (result >= maximum) return nextafter(maximum, minimum);
#else
  volatile double width = maximum - minimum;
  if (width <= DBL_MAX) {
    volatile double offset = fraction * width;
    result = minimum + offset;
  } else {
    volatile double complement = 1.0 - fraction;
    volatile double left = minimum * complement;
    volatile double right = maximum * fraction;
    result = left + right;
  }
  if (result >= maximum) return std::nextafter(maximum, minimum);
#endif
  return result < minimum ? minimum : result;
}

// Zero disables quantization. Positive scales round half-way cases away from
// zero, matching round(value * scale) / scale without fused arithmetic.
GAGP_NUMERIC_HD inline double quantize_float(double value, double scale) {
  if (scale == 0) return value;
#ifdef __CUDA_ARCH__
  return __ddiv_rn(round(__dmul_rn(value, scale)), scale);
#else
  volatile double scaled = value * scale;
  return std::round(scaled) / scale;
#endif
}

// The grid-index bound leaves enough precision that quantizing a constructed
// value is idempotent, so membership can recognize exactly the declared grid.
GAGP_NUMERIC_HD inline bool valid_float_quantization(
    double minimum, double maximum, double scale) {
  if (scale == 0) return true;
  if (!(scale > 0 && scale <= DBL_MAX)) return false;
  const double low = minimum * scale, high = maximum * scale;
#ifdef __CUDA_ARCH__
  if (!(round(low) >= -0x1.0p50 && round(high) <= 0x1.0p50)) return false;
#else
  if (!(std::round(low) >= -0x1.0p50 && std::round(high) <= 0x1.0p50)) return false;
#endif
  return quantize_float(minimum, scale) == minimum &&
      quantize_float(maximum, scale) == maximum;
}

#undef GAGP_NUMERIC_HD

}  // namespace gagp::evo::grammar
