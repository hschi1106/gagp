#include "gagp/evolution/repro/gpu.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <string>
#include <tuple>
#include <mutex>

#include "constant_prep.hpp"
#include <vector>

#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/selection.hpp"
#include "gagp/evolution/repro/pack.hpp"
#include "gagp/evolution/repro/prep.hpp"
#include "gpu/internal.hpp"
#include "compiled_decode.hpp"
#include "gagp/evolution/grammar/cache.hpp"
#include "gagp/evolution/grammar/random.hpp"

namespace gagp::evo::repro {

struct GpuReproRunResources {
  std::shared_ptr<const grammar::CompiledGrammar> grammar;
  grammar::GenerationRequest request;
  std::mutex mutex;
  std::shared_ptr<const ConstantMutationDomains> domains;
};

namespace {

#ifdef GAGP_HAS_CUDA
struct GpuReproRuntimeCache {
  GpuReproArena arena;
  GpuReproHostStaging staging;

  ~GpuReproRuntimeCache() {
    destroy_gpu_repro_host_staging(&staging);
    destroy_gpu_repro_arena(&arena);
  }
};

GpuReproRuntimeCache& gpu_runtime_cache() {
  // Register CUDA runtime teardown before the function-static arena destructor.
  // CPU-evaluation/GPU-reproduction runs otherwise construct this cache before
  // their first CUDA call, so cudart begins teardown before the arena can free
  // its retained allocations at process exit.
  static const bool cuda_runtime_ready = [] {
    std::string message;
    if (!initialize_gpu_repro_runtime(&message)) throw std::runtime_error(message);
    return true;
  }();
  (void)cuda_runtime_ready;
  static GpuReproRuntimeCache cache;
  return cache;
}
#endif

std::vector<double> extract_fitness(const std::vector<ScoredGenome>& scored) {
  std::vector<double> fitness;
  fitness.reserve(scored.size());
  for (const ScoredGenome& one : scored) {
    fitness.push_back(canonicalize_fitness_for_ranking(one.fitness));
  }
  return fitness;
}

std::vector<double> extract_fitness(const std::vector<ScoredGenomeRef>& scored) {
  std::vector<double> fitness;
  fitness.reserve(scored.size());
  for (const ScoredGenomeRef& one : scored) {
    fitness.push_back(canonicalize_fitness_for_ranking(one.fitness));
  }
  return fitness;
}

std::vector<ScoredGenomeRef> make_scored_refs(const std::vector<ScoredGenome>& scored) {
  std::vector<ScoredGenomeRef> refs;
  refs.reserve(scored.size());
  for (const ScoredGenome& one : scored) {
    refs.push_back(ScoredGenomeRef{&one.genome, one.fitness});
  }
  return refs;
}

}  // namespace

namespace {

bool same_request(const grammar::GenerationRequest& a, const grammar::GenerationRequest& b);

void validate_run_resources(const std::shared_ptr<GpuReproRunResources>& resources,
                            const EvolutionConfig& cfg) {
  if (!resources || resources->grammar != cfg.compiled_grammar ||
      !same_request(resources->request, cfg.generation_request.value_or(
          grammar::entry_request(*cfg.compiled_grammar))))
    throw std::invalid_argument("compiled GPU run resource grammar/request mismatch");
}

GpuReproPreparedData prepare_backend_inputs(const std::vector<ProgramGenome>& population,
                                                      const EvolutionConfig& cfg,
                                                      std::uint64_t seed,
                                                      ReproductionStats* stats,
                                                      std::shared_ptr<grammar::VariationContext> context = nullptr,
                                                      std::shared_ptr<GpuReproRunResources> resources = nullptr) {
  require_reproduction_mode_supported(cfg, true);
  GpuReproPreparedData out;
  const auto prepare_t0 = std::chrono::steady_clock::now();
  if (cfg.compiled_grammar) {
    if (!resources) resources = make_gpu_repro_run_resources(cfg);
    validate_run_resources(resources, cfg);
    out.run_resources = resources;
    if (population.empty() || population.size() != static_cast<std::size_t>(cfg.population_size))
      throw std::invalid_argument("compiled GPU preparation population size mismatch");
    if (!context) context = std::make_shared<grammar::VariationContext>(cfg.compiled_grammar,
        cfg.generation_request.value_or(grammar::entry_request(*cfg.compiled_grammar)));
    for (const auto& genome : population) (void)context->cache().analyze(genome, context->request());
    out.compiled_context = context;
  }
  if (resources && !cfg.compiled_grammar)
    throw std::invalid_argument("compiled GPU run resources require a compiled grammar");
  const std::vector<ProgramGenome> packed_population = compact_population_tables(population);
  out.config = make_gpu_repro_config(packed_population, cfg);
  out.config.seed = seed;
  if (context) {
    // Variation needs room for new table entries, beyond present occupancy.
    out.config.max_names = kGpuReproMaxNames;
    out.config.max_consts = kGpuReproMaxConsts;
  }
  if (out.config.max_names > kGpuReproMaxNames || out.config.max_consts > kGpuReproMaxConsts ||
      out.config.max_nodes > kGpuReproKernelMaxNodes) {
    throw std::runtime_error("gpu reproduction unsupported: kernel scratch limits exceeded");
  }
  const auto prepare_t1 = std::chrono::steady_clock::now();
  if (stats != nullptr) {
    stats->prepare_inputs_ms +=
        std::chrono::duration<double, std::milli>(prepare_t1 - prepare_t0).count();
  }

  const auto prep_t0 = std::chrono::steady_clock::now();
  std::shared_ptr<const ConstantMutationDomains> domains;
  if (resources) {
    std::lock_guard<std::mutex> lock(resources->mutex);
    if (!resources->domains)
      resources->domains = prepare_constant_mutation_domains(resources->grammar);
    domains = resources->domains;
  }
  const PreprocessOutput prep = context
      ? preprocess_population(packed_population, out.config, *context, domains)
      : preprocess_population(packed_population, out.config, cfg.grammar);
  const auto prep_t1 = std::chrono::steady_clock::now();
  if (stats != nullptr) {
    stats->preprocess_ms += std::chrono::duration<double, std::milli>(prep_t1 - prep_t0).count();
  }

  const auto pack_t0 = std::chrono::steady_clock::now();
  out.packed = pack_population(packed_population, prep, out.config);
  out.config = out.packed.config;
  if (context) out.preparation_counters = context->counters();
  const auto pack_t1 = std::chrono::steady_clock::now();
  if (stats != nullptr) {
    stats->pack_ms += std::chrono::duration<double, std::milli>(pack_t1 - pack_t0).count();
  }
  return out;
}

bool same_request(const grammar::GenerationRequest& a, const grammar::GenerationRequest& b) {
  if (a.nonterminal != b.nonterminal || a.type != b.type ||
      a.budget.max_nodes != b.budget.max_nodes || a.budget.max_depth != b.budget.max_depth ||
      a.visible_environment.size() != b.visible_environment.size()) return false;
  for (std::size_t i = 0; i < a.visible_environment.size(); ++i)
    if (a.visible_environment[i].name != b.visible_environment[i].name ||
        a.visible_environment[i].type != b.visible_environment[i].type) return false;
  return true;
}

bool same_config(const GpuReproConfig& a, const GpuReproConfig& b) {
  const auto fields = [](const GpuReproConfig& c) {
    return std::tie(c.compiled_pass, c.contract_mode, c.donor_pool_size_per_site,
        c.compiled_donor_count, c.compiled_occurrence_count, c.constant_domain_count,
        c.constant_value_count, c.constant_group_count, c.constant_origin_count,
        c.constant_stream_count, c.constant_root_count, c.population_size, c.pair_count,
        c.candidates_per_program, c.donor_pool_size_per_type, c.max_nodes,
        c.max_donor_nodes, c.max_names, c.max_consts, c.max_linear_rec_binders,
        c.max_asgp_dc_binders, c.max_asgp_dp1d_specs, c.max_asgp_dp2d_specs,
        c.tournament_k, c.max_expr_depth, c.max_for_k, c.mutation_ratio,
        c.mutation_subtree_ratio, c.seed);
  };
  return fields(a) == fields(b);
}

void validate_compiled_prepared(const std::vector<ScoredGenomeRef>& scored,
    const EvolutionConfig& cfg, const GpuReproPreparedData& prepared) {
  if (!prepared.run_resources || !prepared.compiled_context || !prepared.packed.compiled_sources ||
      prepared.packed.compiled_grammar != cfg.compiled_grammar ||
      prepared.compiled_context->grammar_owner() != cfg.compiled_grammar ||
      prepared.config.contract_mode != ReproductionContractMode::CompiledGrammar ||
      prepared.config.compiled_pass != CompiledVariationPass::Crossover ||
      !same_config(prepared.config, prepared.packed.config) ||
      !same_request(prepared.compiled_context->request(),
          cfg.generation_request.value_or(grammar::entry_request(*cfg.compiled_grammar))) ||
      prepared.config.population_size != cfg.population_size ||
      scored.size() != static_cast<std::size_t>(cfg.population_size) ||
      prepared.packed.compiled_sources->parents.size() != scored.size() ||
      prepared.config.tournament_k != std::max(1, std::min(cfg.population_size, cfg.selection_pressure)) ||
      prepared.config.mutation_ratio != cfg.mutation_rate ||
      prepared.config.mutation_subtree_ratio != cfg.mutation_subtree_prob)
    throw std::invalid_argument("compiled GPU preparation state mismatch");
  validate_run_resources(prepared.run_resources, cfg);
  {
    std::lock_guard<std::mutex> lock(prepared.run_resources->mutex);
    if (!prepared.packed.constant_mutation || !prepared.run_resources->domains ||
        prepared.packed.constant_mutation->grammar_domains != prepared.run_resources->domains)
      throw std::invalid_argument("compiled GPU preparation domain owner mismatch");
  }
  std::vector<std::string> inputs;
  for (const auto& input : cfg.compiled_grammar->inputs()) inputs.push_back(input.name);
  for (std::size_t i = 0; i < scored.size(); ++i) {
    if (!scored[i].genome) throw std::invalid_argument("compiled GPU preparation has a null source");
    ProgramGenome expected;
    expected.ast = prepared.packed.compiled_sources->parents[i];
    if (grammar::runtime_cache_identity(expected, inputs, cfg.fuel) !=
        grammar::runtime_cache_identity(*scored[i].genome, inputs, cfg.fuel))
      throw std::invalid_argument("compiled GPU preparation source identity mismatch");
  }
}

#ifdef GAGP_HAS_CUDA
ReproductionResult run_compiled_prepared(const std::vector<ScoredGenomeRef>& scored,
    const EvolutionConfig& cfg, const GpuReproPreparedData& prepared,
    ReproductionStats* stats) {
  GpuReproRuntimeCache& cache = gpu_runtime_cache();
  ReproductionResult out;
  if (stats) out.stats = *stats;
  auto& context = *prepared.compiled_context;
  context.counters() = prepared.preparation_counters;
  const auto run_pass = [&](const GpuReproPreparedData& pass, const std::vector<double>& fitness) {
    std::string message;
    const auto setup_start = std::chrono::steady_clock::now();
    if (!ensure_gpu_repro_arena_capacity(&cache.arena, pass.config, &message) ||
        !ensure_gpu_repro_host_staging_capacity(&cache.staging, pass.config, &message))
      throw std::runtime_error(message);
    out.stats.setup_ms += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - setup_start).count();
    if (!upload_gpu_repro_inputs(pass.packed, &cache.arena, &out.stats, &message) ||
        !launch_gpu_repro_kernels(&cache.arena, pass.config, fitness, &out.stats, &message))
      throw std::runtime_error(message);
    GpuReproChildView view;
    if (!copyback_gpu_repro_children(cache.arena, pass.config, &cache.staging, &view, &out.stats, &message))
      throw std::runtime_error(message);
    const auto decode_start = std::chrono::steady_clock::now();
    auto children = decode_compiled_pass(pass.packed, view, context);
    out.stats.decode_ms += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - decode_start).count();
    return children;
  };
  auto crossed = run_pass(prepared, extract_fitness(scored));
  if (cfg.mutation_rate == 0.0) {
    out.next_population = std::move(crossed);
  } else {
    grammar::GrammarRandom random(prepared.config.seed ^ UINT64_C(0xa0761d6478bd642f));
    auto mutation = prepare_backend_inputs(crossed, cfg, random.next(), &out.stats,
                                           prepared.compiled_context, prepared.run_resources);
    mutation.config.compiled_pass = CompiledVariationPass::Mutation;
    mutation.packed.config = mutation.config;
    out.next_population = run_pass(mutation, std::vector<double>(crossed.size(), 0.0));
  }
  out.stats.variation = context.counters();
  return out;
}
#endif

}  // namespace

std::shared_ptr<GpuReproRunResources> make_gpu_repro_run_resources(const EvolutionConfig& cfg) {
  require_reproduction_mode_supported(cfg, true);
  if (!cfg.compiled_grammar)
    throw std::invalid_argument("compiled GPU run resources require a compiled grammar");
  auto resources = std::make_shared<GpuReproRunResources>();
  resources->grammar = cfg.compiled_grammar;
  resources->request = cfg.generation_request.value_or(grammar::entry_request(*cfg.compiled_grammar));
  return resources;
}

void append_gpu_repro_run_payload_roots(
    const std::shared_ptr<GpuReproRunResources>& resources, std::vector<Value>* roots) {
  if (!resources) return;
  if (!roots) throw std::invalid_argument("GPU run payload roots output is null");
  std::lock_guard<std::mutex> lock(resources->mutex);
  if (resources->domains)
    roots->insert(roots->end(), resources->domains->values.begin(), resources->domains->values.end());
}

GpuReproPreparedData prepare_gpu_repro_backend_inputs(
    const std::vector<ProgramGenome>& population, const EvolutionConfig& cfg,
    std::uint64_t seed, ReproductionStats* stats,
    std::shared_ptr<GpuReproRunResources> resources) {
  return prepare_backend_inputs(population, cfg, seed, stats, nullptr, std::move(resources));
}

ReproductionResult run_gpu_repro_backend_prepared(const std::vector<ScoredGenome>& scored,
                                                  const EvolutionConfig& cfg,
                                                  const GpuReproPreparedData& prepared,
                                                  ReproductionStats* stats) {
  require_reproduction_mode_supported(cfg, true);
  const std::vector<ScoredGenomeRef> scored_refs = make_scored_refs(scored);
  return run_gpu_repro_backend_prepared(scored_refs, cfg, prepared, stats);
}

ReproductionResult run_gpu_repro_backend_prepared(const std::vector<ScoredGenomeRef>& scored,
                                                  const EvolutionConfig& cfg,
                                                  const GpuReproPreparedData& prepared,
                                                  ReproductionStats* stats) {
  require_reproduction_mode_supported(cfg, true);
  if (cfg.compiled_grammar) validate_compiled_prepared(scored, cfg, prepared);
  else if (prepared.run_resources || prepared.compiled_context ||
      prepared.config.contract_mode != ReproductionContractMode::Legacy ||
      prepared.packed.config.contract_mode != ReproductionContractMode::Legacy || prepared.packed.compiled_grammar ||
      prepared.packed.compiled_sources || prepared.packed.constant_mutation ||
      !prepared.packed.parent_constant_streams.empty() || !prepared.packed.donor_constant_streams.empty())
    throw std::invalid_argument("legacy GPU reproduction entry point cannot consume compiled grammar state");
#ifndef GAGP_HAS_CUDA
  (void)scored;
  (void)cfg;
  (void)prepared;
  (void)stats;
  throw std::runtime_error("gpu reproduction requested but CUDA is unavailable in this build");
#else
  if (cfg.compiled_grammar) return run_compiled_prepared(scored, cfg, prepared, stats);
  if (scored.empty()) {
    return ReproductionResult{};
  }
  if (static_cast<int>(scored.size()) != prepared.config.population_size) {
    throw std::runtime_error("gpu reproduction prepared population size mismatch");
  }

  GpuReproRuntimeCache& cache = gpu_runtime_cache();
  std::string message;
  const auto setup_t0 = std::chrono::steady_clock::now();
  if (!ensure_gpu_repro_arena_capacity(&cache.arena, prepared.config, &message) ||
      !ensure_gpu_repro_host_staging_capacity(&cache.staging, prepared.config, &message)) {
    throw std::runtime_error(message);
  }
  const auto setup_t1 = std::chrono::steady_clock::now();

  ReproductionResult out;
  if (stats != nullptr) {
    out.stats = *stats;
  }
  out.stats.setup_ms += std::chrono::duration<double, std::milli>(setup_t1 - setup_t0).count();

  try {
    if (!upload_gpu_repro_inputs(prepared.packed, &cache.arena, &out.stats, &message)) {
      throw std::runtime_error(message);
    }
    if (!launch_gpu_repro_kernels(&cache.arena, prepared.config, extract_fitness(scored), &out.stats, &message)) {
      throw std::runtime_error(message);
    }
    GpuReproChildView copyback;
    if (!copyback_gpu_repro_children(cache.arena, prepared.config, &cache.staging, &copyback, &out.stats, &message)) {
      throw std::runtime_error(message);
    }
    const auto decode_t0 = std::chrono::steady_clock::now();
    out.next_population = decode_gpu_repro_children(prepared.packed, copyback, scored, cfg);
    const auto decode_t1 = std::chrono::steady_clock::now();
    out.stats.decode_ms += std::chrono::duration<double, std::milli>(decode_t1 - decode_t0).count();
  } catch (...) {
    throw;
  }
  return out;
#endif
}

ReproductionResult run_gpu_repro_backend(const std::vector<ScoredGenome>& scored,
                                         const EvolutionConfig& cfg,
                                         std::mt19937_64& rng,
                                         std::shared_ptr<GpuReproRunResources> resources) {
  require_reproduction_mode_supported(cfg, true);
  const std::vector<ScoredGenomeRef> scored_refs = make_scored_refs(scored);
  return run_gpu_repro_backend(scored_refs, cfg, rng, std::move(resources));
}

ReproductionResult run_gpu_repro_backend(const std::vector<ScoredGenomeRef>& scored,
                                         const EvolutionConfig& cfg,
                                         std::mt19937_64& rng,
                                         std::shared_ptr<GpuReproRunResources> resources) {
  require_reproduction_mode_supported(cfg, true);
  if (cfg.compiled_grammar && scored.empty()) return ReproductionResult{};
#ifndef GAGP_HAS_CUDA
  (void)scored;
  (void)cfg;
  (void)rng;
  throw std::runtime_error("gpu reproduction requested but CUDA is unavailable in this build");
#else
  if (scored.empty()) {
    return ReproductionResult{};
  }

  std::vector<ProgramGenome> population;
  population.reserve(scored.size());
  for (const ScoredGenomeRef& one : scored) {
    if (!one.genome) throw std::invalid_argument("GPU reproduction has a null source");
    population.push_back(*one.genome);
  }
  ReproductionStats prep_stats;
  const GpuReproPreparedData prepared = prepare_gpu_repro_backend_inputs(population, cfg, rng(), &prep_stats, std::move(resources));
  return run_gpu_repro_backend_prepared(scored, cfg, prepared, &prep_stats);
#endif
}

}  // namespace gagp::evo::repro
