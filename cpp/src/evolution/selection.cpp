#include "gagp/evolution/selection.hpp"

#include <cmath>

namespace gagp::evo {

double canonicalize_fitness_for_ranking(double fitness) {
  if (!std::isfinite(fitness) || fitness == 0.0) {
    return fitness == 0.0 ? 0.0 : fitness;
  }

  int exponent = 0;
  const double mantissa = std::frexp(fitness, &exponent);
  constexpr int kMantissaBits = 40;
  const long long quantized_mantissa = std::llround(std::ldexp(mantissa, kMantissaBits));
  return std::ldexp(static_cast<double>(quantized_mantissa), exponent - kMantissaBits);
}

}  // namespace gagp::evo
