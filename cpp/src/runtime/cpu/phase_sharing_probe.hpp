#pragma once
// RESEARCH(2026-10-30): decision experiment for population-wide sharing of
// bounded-region phase invocations. Enabled only by GAGP_RESEARCH_PHASE_SHARING
// (output JSON-lines path). Counts how much phase fuel is spent on invocations
// whose (phase program, input values) already ran earlier in the same
// generation, or in the previous generation. Diagnostic only; never changes
// any result.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include "gagp/core/bytecode.hpp"

namespace gagp::detail::phase_sharing {
struct Stats {
  std::mutex mutex;
  bool enabled = std::getenv("GAGP_RESEARCH_PHASE_SHARING") != nullptr;
  int generation = 0;
  std::unordered_map<const void*, std::uint64_t> program_keys;
  std::unordered_set<std::uint64_t> current, previous;
  // Per phase content: distinct programs using it and its total/redundant fuel.
  std::unordered_map<std::uint64_t, std::unordered_set<std::uint64_t>> users;
  std::unordered_map<std::uint64_t, std::pair<std::uint64_t, std::uint64_t>> content_fuel;
  std::uint64_t invocations = 0, fuel = 0, redundant_fuel = 0, previous_fuel = 0,
                redundant_invocations = 0, previous_invocations = 0;
};
inline Stats& stats() { static Stats s; return s; }
// Case being evaluated by this thread; part of every key (conservative: no
// sharing across cases even when bound values coincide).
inline std::uint64_t& current_case() { thread_local std::uint64_t value = 0; return value; }
// Population index of the first program of the batch being evaluated.
inline std::uint64_t& program_base() { thread_local std::uint64_t value = 0; return value; }
inline std::uint64_t& current_program() { thread_local std::uint64_t value = 0; return value; }
inline bool enabled() { return stats().enabled; }
inline std::uint64_t mix(std::uint64_t h, std::uint64_t x) {
  h ^= x + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
  return h * 0xff51afd7ed558ccdULL;
}
inline std::uint64_t value_key(std::uint64_t h, const Value& v) {
  return mix(mix(mix(h, static_cast<std::uint64_t>(v.tag)), static_cast<std::uint64_t>(v.i)), v.b);
}
// Content key of a phase program, computed on every call: evaluators may run
// temporary program copies whose addresses are reused, so no address cache.
inline std::uint64_t program_key(const PhaseProgram& program) {
  std::uint64_t h = 0x1234;
  for (const auto& ins : program.code)
    h = mix(mix(mix(mix(h, static_cast<std::uint64_t>(ins.op)), static_cast<std::uint64_t>(ins.a)),
                static_cast<std::uint64_t>(ins.b)), (ins.has_a ? 1u : 0u) | (ins.has_b ? 2u : 0u));
  for (const auto& c : program.consts) h = value_key(h, c);
  for (const auto f : program.instruction_fuel) h = mix(h, f);
  return h;
}
inline void record(std::uint64_t key, std::uint64_t fuel_used, std::uint64_t content = 0) {
  auto& s = stats();
  std::lock_guard<std::mutex> lock(s.mutex);
  ++s.invocations; s.fuel += fuel_used;
  s.users[content].insert(current_program());
  auto& cf = s.content_fuel[content]; cf.first += fuel_used;
  if (!s.current.insert(key).second) { s.redundant_fuel += fuel_used; ++s.redundant_invocations; cf.second += fuel_used; }
  else if (s.previous.count(key)) { s.previous_fuel += fuel_used; ++s.previous_invocations; }
}
inline void end_generation() {
  auto& s = stats();
  std::lock_guard<std::mutex> lock(s.mutex);
  // Fuel split by how many programs use the phase content: >=90% of programs
  // (skeleton-like), 2..90%, and a single program.
  std::uint64_t programs = 0;
  for (const auto& [c, u] : s.users) programs = std::max<std::uint64_t>(programs, u.size());
  std::uint64_t shared_all[2] = {0, 0}, shared_some[2] = {0, 0}, single[2] = {0, 0};
  for (const auto& [c, f] : s.content_fuel) {
    const auto n = s.users[c].size();
    auto* bucket = n * 10 >= programs * 9 ? shared_all : n > 1 ? shared_some : single;
    bucket[0] += f.first; bucket[1] += f.second;
  }
  if (FILE* out = std::fopen(std::getenv("GAGP_RESEARCH_PHASE_SHARING"), "a")) {
    std::fprintf(out, "{\"skeleton_fuel\":%llu,\"skeleton_redundant\":%llu,\"shared_fuel\":%llu,\"shared_redundant\":%llu,"
                      "\"single_fuel\":%llu,\"single_redundant\":%llu,\"max_users\":%llu,",
                 (unsigned long long)shared_all[0], (unsigned long long)shared_all[1], (unsigned long long)shared_some[0],
                 (unsigned long long)shared_some[1], (unsigned long long)single[0], (unsigned long long)single[1], (unsigned long long)programs);
    std::fprintf(out, "\"generation\":%d,\"invocations\":%llu,\"unique\":%zu,\"fuel\":%llu,"
                      "\"redundant_fuel\":%llu,\"redundant_invocations\":%llu,"
                      "\"previous_fuel\":%llu,\"previous_invocations\":%llu}\n",
                 s.generation, (unsigned long long)s.invocations, s.current.size(), (unsigned long long)s.fuel,
                 (unsigned long long)s.redundant_fuel, (unsigned long long)s.redundant_invocations,
                 (unsigned long long)s.previous_fuel, (unsigned long long)s.previous_invocations);
    std::fclose(out);
  }
  ++s.generation;
  s.previous.swap(s.current); s.current.clear(); s.program_keys.clear(); s.users.clear(); s.content_fuel.clear();
  s.invocations = s.fuel = s.redundant_fuel = s.previous_fuel = s.redundant_invocations = s.previous_invocations = 0;
}
}  // namespace gagp::detail::phase_sharing
