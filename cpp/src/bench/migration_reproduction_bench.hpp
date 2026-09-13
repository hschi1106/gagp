#pragma once

#include <chrono>
#include <sstream>

#include "migration_reproduction.hpp"
#include "gagp/evolution/repro/gpu.hpp"

namespace gagp::migration {

inline void steady_reproduction(std::ostream& out, const ReproductionSnapshot& frozen,
                                const evo::EvolutionConfig& cfg, int warmups, int trials) {
  using Clock = std::chrono::steady_clock;
  if (cfg.reproduction_backend != evo::repro::ReproductionBackend::Gpu || cfg.repro_overlap ||
      cfg.cpu_repro_ablation != evo::repro::CpuReproAblation::None) {
    throw std::runtime_error("repro-steady requires GPU reproduction, no overlap, and no ablation");
  }
  // Derive only dimensions/limits, without invoking donor generation. Reusing
  // captured donors with different search settings would measure different work.
  const auto expected_config = evo::repro::make_gpu_repro_config(frozen.parents, cfg);
  std::ostringstream expected, actual;
  encode_reproduction(expected, frozen.parents, frozen.fitness, expected_config, frozen.prep);
  encode_reproduction(actual, frozen.parents, frozen.fitness, frozen.config, frozen.prep);
  if (expected.str() != actual.str()) throw std::runtime_error("reproduction settings differ from frozen capture");
  for (const auto& parent : frozen.parents) {
    if (!evo::verify_ast(parent.ast, cfg.verification_inputs).ok) {
      throw std::runtime_error("frozen reproduction parent failed verification");
    }
  }
  const auto start_pack = Clock::now();
  evo::repro::GpuReproPreparedData prepared;
  prepared.config = frozen.config;
  prepared.packed = evo::repro::pack_population(frozen.parents, frozen.prep, frozen.config);
  const double pack_ms = std::chrono::duration<double, std::milli>(Clock::now() - start_pack).count();
  std::vector<evo::ScoredGenomeRef> scored;
  for (std::size_t i = 0; i < frozen.parents.size(); ++i) {
    scored.push_back({&frozen.parents[i], frozen.fitness[i]});
  }
  out << std::setprecision(17) << "{\"format_version\":\"migration-steady-reproduction-v1\","
      << "\"warmups\":" << warmups << ",\"measured_trials\":" << trials
      << ",\"pack_ms\":" << pack_ms << ",\"samples\":[";
  std::string reference;
  for (int i = 0; i < warmups + trials; ++i) {
    const auto start = Clock::now();
    const auto result = evo::repro::run_gpu_repro_backend_prepared(scored, cfg, prepared);
    const double call_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    if (result.next_population.size() != frozen.parents.size()) {
      throw std::runtime_error("reproduction output count changed");
    }
    for (const auto& child : result.next_population) {
      if (!evo::verify_ast(child.ast, cfg.verification_inputs).ok) {
        throw std::runtime_error("reproduction emitted invalid child");
      }
    }
    const auto encoded = encode_population(result.next_population);
    if (i == 0) reference = encoded;
    else if (encoded != reference) throw std::runtime_error("frozen reproduction output changed between trials");
    if (i) out << ',';
    out << "{\"index\":" << i << ",\"warmup\":" << (i < warmups ? "true" : "false")
        << ",\"call_ms\":" << call_ms << ",\"phases\":{";
#define GAGP_REPRO_TIMING(name) out << "\"" #name "\":" << result.stats.name << ','
    GAGP_REPRO_TIMING(setup_ms);
    GAGP_REPRO_TIMING(upload_ms);
    GAGP_REPRO_TIMING(kernel_ms);
    GAGP_REPRO_TIMING(copyback_ms);
    GAGP_REPRO_TIMING(decode_ms);
    GAGP_REPRO_TIMING(selection_kernel_ms);
#undef GAGP_REPRO_TIMING
    out << "\"variation_kernel_ms\":" << result.stats.variation_kernel_ms << "}}";
  }
  out << "],\"children\":" << reference << '}';
}

}  // namespace gagp::migration
