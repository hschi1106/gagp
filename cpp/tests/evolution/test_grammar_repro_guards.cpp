#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/lifecycle.hpp"
#include "gagp/evolution/repro/backend.hpp"
#include "gagp/evolution/repro/gpu.hpp"
#include "gagp/evolution/repro/pack.hpp"
#ifdef GAGP_HAS_CUDA
#include "../../src/evolution/repro/gpu/internal.hpp"
#endif

namespace {

using gagp::evo::EvolutionConfig;
using gagp::evo::OverlapPrepared;
using gagp::evo::ProgramGenome;
using gagp::evo::ScoredGenome;
using gagp::evo::ScoredGenomeRef;
using gagp::evo::grammar::CompiledGrammar;
using gagp::evo::repro::CpuReproAblation;
using gagp::evo::repro::GpuReproPreparedData;
using gagp::evo::repro::GpuReproChildView;
using gagp::evo::repro::PackedHostData;
using gagp::evo::repro::ReproductionContractMode;
using gagp::evo::repro::ReproductionBackend;

constexpr const char* kGpuGuardMessage =
    "legacy GPU reproduction entry point cannot consume compiled grammar state";

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void expect_invalid_argument(const std::function<void()>& action,
                             const char* expected_message = nullptr) {
  try {
    action();
  } catch (const std::invalid_argument& error) {
    if (expected_message != nullptr && error.what() != std::string(expected_message)) {
      throw std::runtime_error("unexpected invalid_argument: " + std::string(error.what()));
    }
    return;
  }
  throw std::runtime_error("expected invalid_argument");
}

EvolutionConfig compiled_config() {
  EvolutionConfig config;
  config.compiled_grammar = std::make_shared<const CompiledGrammar>(
      gagp::evo::grammar::compile_grammar(gagp::evo::grammar::parse_definition(R"({
        "format_version":"grammar-definition-v1",
        "entry":{"nonterminal":"Root","type":"Int"},
        "search_limits":{"max_nodes":5,"max_depth":4},
        "execution_limits":{"fuel":20000},
        "nonterminals":[{"id":"Root","type":"Int","scope":[],
          "alternatives":[{"id":"value","weight":1,
            "expression":{"constant":{"type":"Int","values":["0"]}}}]}]
      })")));
  return config;
}

void test_synchronous_gpu_mode_is_supported_and_validation_is_preserved() {
  EvolutionConfig config = compiled_config();
  config.reproduction_backend = ReproductionBackend::Gpu;
  gagp::evo::repro::require_reproduction_mode_supported(config);
  gagp::evo::repro::require_reproduction_mode_supported(config, true);

  config.cpu_repro_ablation = CpuReproAblation::GpuSelection;
  expect_invalid_argument(
      [&] { gagp::evo::repro::require_reproduction_mode_supported(config); },
      "compiled grammar reproduction does not support legacy CPU ablations");

  config.cpu_repro_ablation = CpuReproAblation::None;
  --config.fuel;
  expect_invalid_argument(
      [&] { gagp::evo::repro::require_reproduction_mode_supported(config, true); },
      "compiled grammar execution fuel must match EvolutionConfig fuel");

  config = compiled_config();
  config.mutation_rate = 1.01;
  expect_invalid_argument(
      [&] { gagp::evo::repro::require_reproduction_mode_supported(config); },
      "compiled reproduction mutation probabilities must be in [0,1]");

  config = compiled_config();
  config.generation_request = gagp::evo::grammar::entry_request(*config.compiled_grammar);
  config.generation_request->budget.max_nodes = 0;
  expect_invalid_argument(
      [&] { gagp::evo::repro::require_reproduction_mode_supported(config, true); },
      "generation request budget must be positive and within the compiled grammar limits");
}

void test_empty_synchronous_gpu_entry_points_do_not_require_cuda() {
  EvolutionConfig config = compiled_config();
  config.reproduction_backend = ReproductionBackend::Gpu;
  std::mt19937_64 rng(11);

  check(gagp::evo::repro::run_gpu_repro_backend(
            std::vector<ScoredGenome>{}, config, rng).next_population.empty(),
        "empty owned-score GPU reproduction returned children");
  check(gagp::evo::repro::run_gpu_repro_backend(
            std::vector<ScoredGenomeRef>{}, config, rng).next_population.empty(),
        "empty borrowed-score GPU reproduction returned children");
  check(gagp::evo::repro::run_reproduction_backend(
            std::vector<ScoredGenome>{}, config, rng).next_population.empty(),
        "empty owned-score GPU dispatcher returned children");
  check(gagp::evo::repro::run_reproduction_backend(
            std::vector<ScoredGenomeRef>{}, config, rng).next_population.empty(),
        "empty borrowed-score GPU dispatcher returned children");

  expect_invalid_argument(
      [&] { (void)gagp::evo::repro::prepare_gpu_repro_backend_inputs(
                std::vector<ProgramGenome>{}, config, 13); });
}

void test_prepared_compiled_mode_requires_matching_state_before_cuda() {
  EvolutionConfig config = compiled_config();
  const GpuReproPreparedData prepared;
  expect_invalid_argument(
      [&] { (void)gagp::evo::repro::run_gpu_repro_backend_prepared(
                std::vector<ScoredGenome>{}, config, prepared); },
      "compiled GPU preparation state mismatch");
  expect_invalid_argument(
      [&] { (void)gagp::evo::repro::run_gpu_repro_backend_prepared(
                std::vector<ScoredGenomeRef>{}, config, prepared); },
      "compiled GPU preparation state mismatch");
}

void test_prepared_payload_guards_without_config_grammar_pointer() {
  EvolutionConfig config;

  GpuReproPreparedData prepared_config_mode;
  prepared_config_mode.config.contract_mode =
      ReproductionContractMode::CompiledGrammar;
  expect_invalid_argument(
      [&] { (void)gagp::evo::repro::run_gpu_repro_backend_prepared(
                std::vector<ScoredGenome>{}, config, prepared_config_mode); },
      kGpuGuardMessage);
  expect_invalid_argument(
      [&] { (void)gagp::evo::repro::run_gpu_repro_backend_prepared(
                std::vector<ScoredGenomeRef>{}, config, prepared_config_mode); },
      kGpuGuardMessage);

  GpuReproPreparedData packed_config_mode;
  packed_config_mode.packed.config.contract_mode =
      ReproductionContractMode::CompiledGrammar;
  expect_invalid_argument(
      [&] { (void)gagp::evo::repro::run_gpu_repro_backend_prepared(
                std::vector<ScoredGenome>{}, config, packed_config_mode); },
      kGpuGuardMessage);
  expect_invalid_argument(
      [&] { (void)gagp::evo::repro::run_gpu_repro_backend_prepared(
                std::vector<ScoredGenomeRef>{}, config, packed_config_mode); },
      kGpuGuardMessage);

  GpuReproPreparedData packed_owner;
  packed_owner.packed.compiled_grammar = compiled_config().compiled_grammar;
  expect_invalid_argument(
      [&] { (void)gagp::evo::repro::run_gpu_repro_backend_prepared(
                std::vector<ScoredGenome>{}, config, packed_owner); },
      kGpuGuardMessage);
  expect_invalid_argument(
      [&] { (void)gagp::evo::repro::run_gpu_repro_backend_prepared(
                std::vector<ScoredGenomeRef>{}, config, packed_owner); },
      kGpuGuardMessage);
}

void test_decode_guards_before_empty_scored_pointer_access() {
  EvolutionConfig config;

  PackedHostData packed_mode;
  packed_mode.config.contract_mode = ReproductionContractMode::CompiledGrammar;
  GpuReproChildView legacy_copyback;
  expect_invalid_argument(
      [&] { (void)gagp::evo::repro::decode_gpu_repro_children(
                packed_mode, legacy_copyback, std::vector<ScoredGenome>{}, config); },
      kGpuGuardMessage);
  expect_invalid_argument(
      [&] { (void)gagp::evo::repro::decode_gpu_repro_children(
                packed_mode, legacy_copyback, std::vector<ScoredGenomeRef>{}, config); },
      kGpuGuardMessage);

  PackedHostData legacy_packed;
  GpuReproChildView copyback_mode;
  copyback_mode.config.contract_mode = ReproductionContractMode::CompiledGrammar;
  expect_invalid_argument(
      [&] { (void)gagp::evo::repro::decode_gpu_repro_children(
                legacy_packed, copyback_mode, std::vector<ScoredGenome>{}, config); },
      kGpuGuardMessage);
  expect_invalid_argument(
      [&] { (void)gagp::evo::repro::decode_gpu_repro_children(
                legacy_packed, copyback_mode, std::vector<ScoredGenomeRef>{}, config); },
      kGpuGuardMessage);

  PackedHostData packed_owner;
  packed_owner.compiled_grammar = compiled_config().compiled_grammar;
  expect_invalid_argument(
      [&] { (void)gagp::evo::repro::decode_gpu_repro_children(
                packed_owner, legacy_copyback, std::vector<ScoredGenome>{}, config); },
      kGpuGuardMessage);
  expect_invalid_argument(
      [&] { (void)gagp::evo::repro::decode_gpu_repro_children(
                packed_owner, legacy_copyback, std::vector<ScoredGenomeRef>{}, config); },
      kGpuGuardMessage);
}

#ifdef GAGP_HAS_CUDA
void test_compiled_arena_capacity_identity() {
  using namespace gagp::evo::repro;
  GpuReproConfig have;
  have.contract_mode = ReproductionContractMode::CompiledGrammar;
  have.compiled_donor_count = 7;
  have.compiled_occurrence_count = 11;
  auto need = have;
  check(gpu_repro_config_fits_capacity(need, have), "identical compiled arena failed reuse");
  need.compiled_donor_count = 8;
  check(!gpu_repro_config_fits_capacity(need, have), "compiled donor growth reused a short arena");
  need = have;
  need.compiled_occurrence_count = 12;
  check(!gpu_repro_config_fits_capacity(need, have), "occurrence growth reused a short arena");
  for (int GpuReproConfig::* field : {&GpuReproConfig::constant_domain_count,
       &GpuReproConfig::constant_value_count, &GpuReproConfig::constant_group_count,
       &GpuReproConfig::constant_origin_count, &GpuReproConfig::constant_stream_count}) {
    need = have;
    ++(need.*field);
    check(!gpu_repro_config_fits_capacity(need, have), "constant table growth reused a short arena");
  }
  need = have;
  need.compiled_donor_count = need.compiled_occurrence_count = 0;
  check(gpu_repro_config_fits_capacity(need, have), "smaller compiled tables could not reuse capacity");
  need.contract_mode = ReproductionContractMode::Legacy;
  check(!gpu_repro_config_fits_capacity(need, have), "arena reuse crossed incompatible donor layouts");
}

void test_low_level_cuda_entries_guard_before_runtime_access() {
  gagp::evo::repro::GpuReproConfig config;
  config.contract_mode = ReproductionContractMode::CompiledGrammar;
  std::string message = "unchanged";
  const std::string invalid_transport =
      "gpu reproduction unsupported: invalid compiled transport configuration";

  check(!gagp::evo::repro::require_legacy_gpu_contract(config, &message) &&
        message == kGpuGuardMessage,
      "inline GPU contract guard did not report the compiled-mode diagnostic");

  message = "unchanged";
  check(!gagp::evo::repro::ensure_gpu_repro_arena_capacity(
            nullptr, config, &message) && message == invalid_transport,
      "arena capacity checked a null arena before compiled mode");

  message = "unchanged";
  check(!gagp::evo::repro::ensure_gpu_repro_host_staging_capacity(
            nullptr, config, &message) && message == invalid_transport,
      "host staging capacity checked a null staging pointer before compiled mode");

  PackedHostData packed;
  packed.config = config;
  message = "unchanged";
  check(!gagp::evo::repro::upload_gpu_repro_inputs(
            packed, nullptr, nullptr, &message) && message == invalid_transport,
      "GPU upload accessed a null arena before compiled mode");

  message = "unchanged";
  check(!gagp::evo::repro::launch_gpu_repro_kernels(
            nullptr, config, {}, nullptr, &message) && message == invalid_transport,
      "GPU launch accessed a null arena before compiled mode");

  gagp::evo::repro::GpuReproArena arena;
  message = "unchanged";
  check(!gagp::evo::repro::copyback_gpu_repro_children(
            arena, config, nullptr, nullptr, nullptr, &message) &&
        message == invalid_transport,
      "GPU copyback checked null outputs or selected a device before compiled mode");
}
#endif

void test_overlap_guards_and_eligibility() {
  EvolutionConfig config = compiled_config();
  check(!gagp::evo::gpu_reproduction_overlap_enabled(config),
        "CPU evaluation unexpectedly enabled GPU reproduction overlap");

  config.eval_engine = gagp::evo::EvalEngine::GPU;
  check(!gagp::evo::gpu_reproduction_overlap_enabled(config),
        "CPU reproduction unexpectedly enabled GPU reproduction overlap");

  config.reproduction_backend = ReproductionBackend::Gpu;
  check(!gagp::evo::gpu_reproduction_overlap_enabled(config),
        "disabled overlap flag unexpectedly enabled GPU reproduction overlap");

  config.repro_overlap = true;
  check(gagp::evo::gpu_reproduction_overlap_enabled(config),
        "compiled grammar did not enable eligible GPU reproduction overlap");

  EvolutionConfig cpu_eval = config;
  cpu_eval.eval_engine = gagp::evo::EvalEngine::CPU;
  check(!gagp::evo::gpu_reproduction_overlap_enabled(cpu_eval),
        "overlap eligibility ignored the GPU evaluation requirement");

  EvolutionConfig cpu_repro = config;
  cpu_repro.reproduction_backend = ReproductionBackend::Cpu;
  check(!gagp::evo::gpu_reproduction_overlap_enabled(cpu_repro),
        "overlap eligibility ignored the GPU reproduction requirement");

  expect_invalid_argument(
      [&] {
        std::future<OverlapPrepared> missing;
        (void)gagp::evo::finish_gpu_reproduction_overlap(
            &missing, {}, {}, config);
      },
      "GPU reproduction overlap future is not valid");
  expect_invalid_argument(
      [&] {
        (void)gagp::evo::finish_gpu_reproduction_overlap(
            static_cast<std::future<OverlapPrepared>*>(nullptr), {}, {}, config);
      },
      "GPU reproduction overlap future is not valid");

  std::future<OverlapPrepared> empty_worker =
      gagp::evo::start_gpu_reproduction_overlap({}, config, 17);
  expect_invalid_argument(
      [&] { (void)empty_worker.get(); });
}

}  // namespace

int main() {
  try {
    test_synchronous_gpu_mode_is_supported_and_validation_is_preserved();
    test_empty_synchronous_gpu_entry_points_do_not_require_cuda();
    test_prepared_compiled_mode_requires_matching_state_before_cuda();
    test_prepared_payload_guards_without_config_grammar_pointer();
    test_decode_guards_before_empty_scored_pointer_access();
#ifdef GAGP_HAS_CUDA
    test_compiled_arena_capacity_identity();
    test_low_level_cuda_entries_guard_before_runtime_access();
#endif
    test_overlap_guards_and_eligibility();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
