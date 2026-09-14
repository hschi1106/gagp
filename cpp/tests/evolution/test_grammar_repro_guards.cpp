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
    "compiled grammar GPU reproduction is unavailable until Goal 07";

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

void test_dispatcher_guards_both_score_representations() {
  EvolutionConfig config = compiled_config();
  config.reproduction_backend = ReproductionBackend::Gpu;
  std::mt19937_64 rng(7);

  expect_invalid_argument(
      [&] { (void)gagp::evo::repro::run_reproduction_backend(
                std::vector<ScoredGenome>{}, config, rng); },
      kGpuGuardMessage);
  expect_invalid_argument(
      [&] { (void)gagp::evo::repro::run_reproduction_backend(
                std::vector<ScoredGenomeRef>{}, config, rng); },
      kGpuGuardMessage);

  config.reproduction_backend = ReproductionBackend::Cpu;
  config.cpu_repro_ablation = CpuReproAblation::GpuSelection;
  expect_invalid_argument(
      [&] { (void)gagp::evo::repro::run_reproduction_backend(
                std::vector<ScoredGenome>{}, config, rng); });
  expect_invalid_argument(
      [&] { (void)gagp::evo::repro::run_reproduction_backend(
                std::vector<ScoredGenomeRef>{}, config, rng); });
}

void test_direct_gpu_entry_points_guard_before_empty_or_cuda_paths() {
  EvolutionConfig config = compiled_config();
  config.reproduction_backend = ReproductionBackend::Cpu;
  std::mt19937_64 rng(11);
  const GpuReproPreparedData prepared;

  expect_invalid_argument(
      [&] { (void)gagp::evo::repro::prepare_gpu_repro_backend_inputs(
                std::vector<ProgramGenome>{}, config, 13); },
      kGpuGuardMessage);
  expect_invalid_argument(
      [&] { (void)gagp::evo::repro::run_gpu_repro_backend(
                std::vector<ScoredGenome>{}, config, rng); },
      kGpuGuardMessage);
  expect_invalid_argument(
      [&] { (void)gagp::evo::repro::run_gpu_repro_backend(
                std::vector<ScoredGenomeRef>{}, config, rng); },
      kGpuGuardMessage);
  expect_invalid_argument(
      [&] { (void)gagp::evo::repro::run_gpu_repro_backend_prepared(
                std::vector<ScoredGenome>{}, config, prepared); },
      kGpuGuardMessage);
  expect_invalid_argument(
      [&] { (void)gagp::evo::repro::run_gpu_repro_backend_prepared(
                std::vector<ScoredGenomeRef>{}, config, prepared); },
      kGpuGuardMessage);
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
void test_low_level_cuda_entries_guard_before_runtime_access() {
  gagp::evo::repro::GpuReproConfig config;
  config.contract_mode = ReproductionContractMode::CompiledGrammar;
  std::string message = "unchanged";

  check(!gagp::evo::repro::require_legacy_gpu_contract(config, &message) &&
        message == kGpuGuardMessage,
      "inline GPU contract guard did not report the compiled-mode diagnostic");

  message = "unchanged";
  check(!gagp::evo::repro::ensure_gpu_repro_arena_capacity(
            nullptr, config, &message) && message == kGpuGuardMessage,
      "arena capacity checked a null arena before compiled mode");

  message = "unchanged";
  check(!gagp::evo::repro::ensure_gpu_repro_host_staging_capacity(
            nullptr, config, &message) && message == kGpuGuardMessage,
      "host staging capacity checked a null staging pointer before compiled mode");

  PackedHostData packed;
  packed.config = config;
  message = "unchanged";
  check(!gagp::evo::repro::upload_gpu_repro_inputs(
            packed, nullptr, nullptr, &message) && message == kGpuGuardMessage,
      "GPU upload accessed a null arena before compiled mode");

  message = "unchanged";
  check(!gagp::evo::repro::launch_gpu_repro_kernels(
            nullptr, config, {}, nullptr, &message) && message == kGpuGuardMessage,
      "GPU launch accessed a null arena before compiled mode");

  gagp::evo::repro::GpuReproArena arena;
  message = "unchanged";
  check(!gagp::evo::repro::copyback_gpu_repro_children(
            arena, config, nullptr, nullptr, nullptr, &message) &&
        message == kGpuGuardMessage,
      "GPU copyback checked null outputs or selected a device before compiled mode");
}
#endif

void test_overlap_guards_and_eligibility() {
  EvolutionConfig config = compiled_config();
  config.eval_engine = gagp::evo::EvalEngine::GPU;
  config.reproduction_backend = ReproductionBackend::Gpu;
  config.repro_overlap = true;
  if (gagp::evo::gpu_reproduction_overlap_enabled(config)) {
    throw std::runtime_error("compiled grammar must disable GPU reproduction overlap");
  }

  expect_invalid_argument(
      [&] { (void)gagp::evo::start_gpu_reproduction_overlap({}, config, 17); },
      kGpuGuardMessage);
  expect_invalid_argument(
      [&] {
        (void)gagp::evo::finish_gpu_reproduction_overlap(
            static_cast<std::future<OverlapPrepared>*>(nullptr), {}, {}, config);
      },
      kGpuGuardMessage);
}

void test_evolve_guards_before_case_validation() {
  EvolutionConfig config = compiled_config();
  config.reproduction_backend = ReproductionBackend::Gpu;
  expect_invalid_argument(
      [&] { (void)gagp::evo::evolve_population({}, config); },
      kGpuGuardMessage);
}

}  // namespace

int main() {
  try {
    test_dispatcher_guards_both_score_representations();
    test_direct_gpu_entry_points_guard_before_empty_or_cuda_paths();
    test_prepared_payload_guards_without_config_grammar_pointer();
    test_decode_guards_before_empty_scored_pointer_access();
#ifdef GAGP_HAS_CUDA
    test_low_level_cuda_entries_guard_before_runtime_access();
#endif
    test_overlap_guards_and_eligibility();
    test_evolve_guards_before_case_validation();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
