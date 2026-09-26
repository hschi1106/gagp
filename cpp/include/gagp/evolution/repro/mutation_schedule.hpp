#pragma once

#include <cstdint>

#ifdef __CUDACC__
#define GAGP_MUTATION_HD __host__ __device__
#else
#define GAGP_MUTATION_HD
#endif

namespace gagp::evo::repro {

GAGP_MUTATION_HD inline std::uint64_t compiled_mutation_seed(std::uint64_t seed, int child) {
  auto x = seed ^ (static_cast<std::uint64_t>(child + 1) * UINT64_C(0x9e3779b97f4a7c15));
  x ^= x >> 30; x *= UINT64_C(0xbf58476d1ce4e5b9);
  x ^= x >> 27; x *= UINT64_C(0x94d049bb133111eb);
  return x ^ (x >> 31);
}

template<class Random>
GAGP_MUTATION_HD inline double compiled_mutation_unit(Random* random) {
  return static_cast<double>(random->next() >> 11) * (1.0 / 9007199254740992.0);
}

// Predict only which donor pool may be read. The GPU still executes the mutation
// decision, constant operation, candidate draw and splice. A constant branch may
// fall through when it has no eligible groups, consuming one extra seed first.
template<class Random>
inline int anticipated_mutation_candidate(std::uint64_t seed, int child,
    double mutation_ratio, double subtree_ratio, std::uint64_t candidate_count,
    bool has_constant_groups = false) {
  if (!candidate_count) return -1;
  Random random(compiled_mutation_seed(seed, child));
  if (compiled_mutation_unit(&random) >= mutation_ratio) return -1;
  if (compiled_mutation_unit(&random) >= subtree_ratio) {
    // A prepared constant group rules out NoGroups, the only constant outcome
    // that falls through to subtree mutation. The GPU still runs that operation.
    if (has_constant_groups) return -1;
    (void)random.next();
  }
  return static_cast<int>(random.bounded(candidate_count));
}

}  // namespace gagp::evo::repro

#undef GAGP_MUTATION_HD
