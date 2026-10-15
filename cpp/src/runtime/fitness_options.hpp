#pragma once
#include <cstdlib>
#include <cstring>
#include "gagp/core/fitness_policy.hpp"

namespace gagp {
// Capture on the host, once per CPU call or GPU evaluator initialization.
inline FitnessPolicy capture_fitness_options(FitnessPolicy policy) {
  const char* option=std::getenv("GAGP_FITNESS_TRIM_SEQUENCE_ENDS");
  if(option && std::strcmp(option,"1")==0)policy.trim_sequence_ends=true;
  return policy;
}
} // namespace gagp
