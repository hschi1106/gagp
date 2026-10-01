#include "gagp/core/host_threads.hpp"
#include "../batch_workers.hpp"
#include "pack_internal.hpp"
#include "prep_internal.hpp"
#include "gagp/evolution/repro/gpu.hpp"

#include <algorithm>
#include <atomic>
#include <future>
#include <thread>
#include "../../runtime/payload/staging.hpp"
#include <chrono>
#include <cstdlib>
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
#include "owned_overlap.hpp"
#include "gagp/evolution/grammar/values.hpp"
#include "gagp/evolution/grammar/derivation_resources.hpp"
#include "../grammar/variation_internal.hpp"
#include "gagp/evolution/grammar/cache.hpp"
#include "gagp/evolution/grammar/random.hpp"

namespace gagp::evo::repro {
struct OwnedCompactedAnalysis {
  static bool transport(grammar::VariationContext& context,
      const std::vector<ProgramGenome>& before, const std::vector<ProgramGenome>& after,
      std::vector<grammar::WarmPopulationMember>& warmed) {
    if (warmed.size() != before.size() || payload::StagedPayloads::has_active_scope()) return false;
    std::vector<std::shared_ptr<grammar::VariationAnalysis>> analyses(before.size());
    std::vector<grammar::WarmPopulationMember> output(before.size());
    std::vector<std::string> input_names;
    for (const auto& input : context.grammar().inputs()) input_names.push_back(input.name);
    std::atomic<std::size_t> next{0};
    std::vector<std::future<void>> pending;
    const auto workers = gagp::host_thread_limit();
    for (unsigned worker = 0; worker < workers; ++worker)
      pending.push_back(std::async(gagp::host_launch_policy(), [&] {
        for (;;) {
          const auto i = next.fetch_add(1, std::memory_order_relaxed);
          if (i >= before.size()) break;
          try {
            if (!warmed[i].analysis || !warmed[i].reads || !warmed[i].reads->read_snapshot_unchanged()) continue;
            output[i].reads = std::make_shared<payload::StagedPayloads>();
            payload::StagedPayloads::Scope scope(*output[i].reads);
            analyses[i] = std::make_shared<grammar::VariationAnalysis>(grammar::variation_detail::remap_compacted_analysis(
                context.grammar(), *warmed[i].analysis, before[i].ast, after[i].ast));
            output[i].runtime_identity = grammar::runtime_cache_identity(after[i], input_names,
                context.grammar().execution_limits().fuel);
          } catch (const std::exception&) { analyses[i].reset(); }
        }
      }));
    for (auto& task : pending) task.get();
    std::vector<payload::StagedPayloads*> reads;
    for (std::size_t i = 0; i < before.size(); ++i) {
      if (!analyses[i] || !warmed[i].reads->read_snapshot_unchanged()) return false;
      reads.push_back(output[i].reads.get());
    }
    if (!payload::StagedPayloads::commit_all(reads)) return false;
    auto& cache = context.cache();
    for (std::size_t i = 0; i < before.size(); ++i) {
      auto key = cache.member_key(output[i].runtime_identity, context.requests());
      const auto found = cache.entries_.find(key);
      if (found != cache.entries_.end()) {
        ++cache.counters_.hits;
        output[i].analysis = found->second;
      } else {
        ++cache.counters_.misses;
        for (auto& site : analyses[i]->sites)
          if (site.compatibility_id == grammar::kNoGrammarId)
            site.compatibility_id = cache.registry_.intern(site.compatibility_key);
        output[i].analysis = cache.remember(std::move(key), std::move(analyses[i]));
      }
    }
    warmed = std::move(output);
    return true;
  }
};


struct GpuReproRunResources {
  std::shared_ptr<const grammar::CompiledGrammar> grammar;
  std::vector<grammar::GenerationRequest> requests;
  std::optional<grammar::ProjectedBudget> offspring_budget;
  std::mutex mutex;
  std::shared_ptr<const ConstantMutationDomains> domains;
  std::shared_ptr<GpuPhaseDonorSession> phase_donors;
  std::vector<std::weak_ptr<const ConstantMutationDomains>> proposal_domains;
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

bool same_requests(const std::vector<grammar::GenerationRequest>& a,
                   const std::vector<grammar::GenerationRequest>& b);

bool same_budget(const std::optional<grammar::ProjectedBudget>& a,
                 const std::optional<grammar::ProjectedBudget>& b) {
  return a.has_value() == b.has_value() &&
      (!a || (a->max_nodes == b->max_nodes && a->max_depth == b->max_depth));
}

void validate_run_resources(const std::shared_ptr<GpuReproRunResources>& resources,
                            const EvolutionConfig& cfg) {
  if (!resources || resources->grammar != cfg.compiled_grammar ||
      !same_budget(resources->offspring_budget, cfg.offspring_resource_budget) ||
      !same_requests(resources->requests, population_requests(cfg)))
    throw std::invalid_argument("compiled GPU run resource grammar/request mismatch");
}

std::vector<ProgramGenome> compact_prepared_population(const std::vector<ProgramGenome>& population) {
  if (population.size() < 32 || payload::StagedPayloads::has_active_scope())
    return compact_population_tables(population);
  std::vector<ProgramGenome> result(population.size());
  const auto workers = gagp::host_thread_limit();
  constexpr std::size_t batch = 128;
  detail::BatchWorkers team(workers);
  for (std::size_t begin = 0; begin < population.size(); begin += batch) {
    const auto count = std::min(batch, population.size() - begin);
    std::vector<std::unique_ptr<payload::StagedPayloads>> reads(count);
    std::vector<std::exception_ptr> errors(count);
    std::atomic<std::size_t> next{0};
    team.run([&] {
        for (;;) {
          const auto offset = next.fetch_add(1, std::memory_order_relaxed);
          if (offset >= count) break;
          try {
            reads[offset] = std::make_unique<payload::StagedPayloads>();
            payload::StagedPayloads::Scope scope(*reads[offset]);
            result[begin + offset] = compact_genome_tables(population[begin + offset]);
          } catch (...) { errors[offset] = std::current_exception(); }
        }
    });
    std::vector<payload::StagedPayloads*> transactions;
    for (const auto& read : reads) if (read) transactions.push_back(read.get());
    if (!payload::StagedPayloads::commit_all(transactions)) {
      for (std::size_t offset = 0; offset < count; ++offset)
        result[begin + offset] = compact_genome_tables(population[begin + offset]);
    } else {
      for (const auto& error : errors) if (error) std::rethrow_exception(error);
    }
  }
  return result;
}

GpuReproPreparedData prepare_backend_inputs(const std::vector<ProgramGenome>& population,
                                                      const EvolutionConfig& cfg,
                                                      std::uint64_t seed,
                                                      ReproductionStats* stats,
                                                      std::shared_ptr<grammar::VariationContext> context = nullptr,
                                                      std::shared_ptr<GpuReproRunResources> resources = nullptr,
                                                      CompiledVariationPass pass = CompiledVariationPass::Crossover) {
  require_reproduction_mode_supported(cfg, true);
  GpuReproPreparedData out;
  const auto prepare_t0 = std::chrono::steady_clock::now();
  if (!resources) resources = make_gpu_repro_run_resources(cfg);
  validate_run_resources(resources, cfg);
  out.run_resources = resources;
  if (population.empty() || population.size() != static_cast<std::size_t>(cfg.population_size))
    throw std::invalid_argument("compiled GPU preparation population size mismatch");
  // Retain parents across preparation and decoding, with room for donor children.
  // Bound retained analyses independently of the maximum supported population.
  const auto analysis_capacity = std::min<std::size_t>(4096,
      std::max<std::size_t>(128, population.size() * 4));
  if (!context) context = std::make_shared<grammar::VariationContext>(
      cfg.compiled_grammar, population_requests(cfg), analysis_capacity,
      cfg.offspring_resource_budget);
  std::future<std::vector<bool>> resource_proof;
  if (pass == CompiledVariationPass::Crossover && cfg.mutation_rate > 0 && cfg.mutation_subtree_prob > 0 &&
      cfg.offspring_resource_budget && !grammar::resource_charges_are_local(*cfg.compiled_grammar)) {
    std::vector<std::uint32_t> roots;
    for (const auto& request : context->requests()) roots.push_back(request.nonterminal);
    resource_proof = std::async(gagp::host_launch_policy(), [owner = cfg.compiled_grammar, roots] {
      return owner->resource_invariant_roots(roots);
    });
  }
  const bool selected_sites = std::getenv("GAGP_SELECTED_SITES") && gagp::host_thread_limit() == 1 &&
      context->requests().size() == 1 && !context->offspring_budget() &&
      !payload::StagedPayloads::has_active_scope() &&
      std::all_of(population.begin(), population.end(), [](const auto& genome) {
        return std::all_of(genome.ast.consts.begin(), genome.ast.consts.end(), [](const auto& value) {
          return value.tag == ValueTag::Int || value.tag == ValueTag::Float ||
              value.tag == ValueTag::Bool || value.tag == ValueTag::Char;
        });
      });
  std::vector<ProgramGenome> execution_parents;
  std::shared_ptr<const grammar::variation_detail::OwnedScalarPopulation> owned_parents;
  if (selected_sites && std::getenv("GAGP_OWNED_PREPARATION") &&
      !std::getenv("GAGP_NO_DERIVATION_CERTIFICATES")) {
    owned_parents = grammar::variation_detail::OwnedScalarPopulation::create(population, *context);
  } else if (selected_sites) {
    execution_parents.reserve(population.size());
    for (const auto& parent : population)
      execution_parents.push_back(grammar::variation_detail::certify_execution(parent, *context));
  }
  std::vector<grammar::WarmPopulationMember> warmed_members;
  if (!selected_sites) context->cache().warm_population(population, context->requests(), 20, &warmed_members);
  out.compiled_context = context;
  // Only the private mutation pass receives decode_compiled_pass output:
  // accepted children and certified fallbacks already have compact tables.
  // Keep warming/revalidation above so registry changes remain observable.
  const auto compacted_population = selected_sites || pass == CompiledVariationPass::Mutation
      ? std::vector<ProgramGenome>{} : compact_prepared_population(population);
  const auto& packed_population = owned_parents ? owned_parents->genomes() : selected_sites ? execution_parents : pass == CompiledVariationPass::Mutation
      ? population : compacted_population;
  // Compaction changes exact cache identities when unused table entries are
  // removed. Keep the original validation above (including unused payloads),
  // then prepare the actual packed representation in parallel rather than
  // reconstructing every changed parent serially during preprocessing.
  bool compacted_tables = false;
  for (std::size_t i = 0; i < population.size(); ++i)
    compacted_tables |= population[i].ast.names.size() != packed_population[i].ast.names.size() ||
        population[i].ast.consts.size() != packed_population[i].ast.consts.size();
  if (!selected_sites && compacted_tables && !OwnedCompactedAnalysis::transport(*context, population, packed_population, warmed_members))
    context->cache().warm_population(packed_population, context->requests(), 20, &warmed_members);
  out.config = make_gpu_repro_config(packed_population, cfg);
  out.config.seed = seed;
  out.config.compiled_pass = pass;
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
    domains = sample_constant_mutation_domains(domains, seed, population.size());
    if (domains != resources->domains) {
      auto& live = resources->proposal_domains;
      live.erase(std::remove_if(live.begin(), live.end(), [](const auto& weak) {
        return weak.expired();
      }), live.end());
      live.push_back(domains);
    }
  }
#ifdef GAGP_HAS_CUDA
  if (selected_sites && owned_parents && pass == CompiledVariationPass::Mutation &&
      std::getenv("GAGP_GPU_DONORS") && !resources->phase_donors)
    resources->phase_donors = make_gpu_phase_donor_session(context->grammar_owner());
#endif
  const PreprocessOutput prep = selected_sites ?
      preprocess_selected_population(packed_population, out.config, *context, domains,
          pass == CompiledVariationPass::Mutation, owned_parents.get(),
          pass == CompiledVariationPass::Mutation ? resources->phase_donors.get() : nullptr) :
      preprocess_warmed_population(packed_population, out.config, *context, domains,
          pass == CompiledVariationPass::Mutation, warmed_members);
  const auto prep_t1 = std::chrono::steady_clock::now();
  if (stats != nullptr) {
    stats->preprocess_ms += std::chrono::duration<double, std::milli>(prep_t1 - prep_t0).count();
    stats->gpu_donor_generated += prep.gpu_donor_generated;
    stats->gpu_donor_fallback += prep.gpu_donor_fallback;
    stats->gpu_donor_setup_ms += prep.gpu_donor_setup_ms;
    stats->gpu_donor_device_bytes = std::max(stats->gpu_donor_device_bytes,prep.gpu_donor_device_bytes);
  }

  const auto pack_t0 = std::chrono::steady_clock::now();
  out.packed = owned_parents ? pack_owned_population(*owned_parents, prep, out.config) : selected_sites ? pack_population(packed_population, prep, out.config) :
      pack_warmed_population(packed_population, prep, out.config, warmed_members);
  if (selected_sites) {
    auto certificates = std::make_shared<PreparedParentCertificates>();
    certificates->sources = out.packed.compiled_sources;
    certificates->context = context;
    certificates->admitted_parents = std::move(execution_parents);
    certificates->owned_parents = std::move(owned_parents);
    out.parent_certificates = std::move(certificates);
  }
  if (!selected_sites && warmed_members.size() == packed_population.size()) {
    auto certificates = std::make_shared<PreparedParentCertificates>();
    certificates->sources = out.packed.compiled_sources;
    certificates->context = context;
    certificates->analyses = std::move(warmed_members);
    certificates->metadata.reserve(packed_population.size());
    for (const auto& member : packed_population) certificates->metadata.push_back(member.meta);
    out.parent_certificates = std::move(certificates);
  }
  out.config = out.packed.config;
  if (resource_proof.valid()) (void)resource_proof.get();
  if (context) out.preparation_counters = context->counters();
  const auto pack_t1 = std::chrono::steady_clock::now();
  if (stats != nullptr) {
    stats->pack_ms += std::chrono::duration<double, std::milli>(pack_t1 - pack_t0).count();
  }
  return out;
}

bool same_request(const grammar::GenerationRequest& a, const grammar::GenerationRequest& b) {
  if (a.nonterminal != b.nonterminal || a.type != b.type || a.stage != b.stage ||
      a.budget.max_nodes != b.budget.max_nodes || a.budget.max_depth != b.budget.max_depth ||
      a.visible_environment.size() != b.visible_environment.size()) return false;
  for (std::size_t i = 0; i < a.visible_environment.size(); ++i)
    if (a.visible_environment[i].name != b.visible_environment[i].name ||
        a.visible_environment[i].type != b.visible_environment[i].type) return false;
  return true;
}

bool same_requests(const std::vector<grammar::GenerationRequest>& a,
                   const std::vector<grammar::GenerationRequest>& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (!same_request(a[i], b[i])) return false;
  return true;
}

bool same_config(const GpuReproConfig& a, const GpuReproConfig& b) {
  const auto fields = [](const GpuReproConfig& c) {
    return std::tie(c.compiled_pass, c.donor_pool_size_per_site,
        c.compiled_donor_count, c.compiled_occurrence_count, c.constant_domain_count,
        c.constant_value_count, c.constant_group_count, c.constant_origin_count,
        c.constant_stream_count, c.constant_root_count, c.population_size, c.pair_count,
        c.candidates_per_program, c.donor_pool_size_per_type, c.max_nodes,
        c.max_donor_nodes, c.max_names, c.max_consts,
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
      !same_budget(prepared.compiled_context->offspring_budget(), cfg.offspring_resource_budget) ||
      prepared.config.compiled_pass != CompiledVariationPass::Crossover ||
      !same_config(prepared.config, prepared.packed.config) ||
      !same_requests(prepared.compiled_context->requests(),
          population_requests(cfg)) ||
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
    const auto domains = prepared.packed.constant_mutation
        ? prepared.packed.constant_mutation->grammar_domains : nullptr;
    const auto base = prepared.run_resources->domains;
    const auto registered = [&] {
      return std::any_of(prepared.run_resources->proposal_domains.begin(),
          prepared.run_resources->proposal_domains.end(), [&](const auto& weak) {
            return weak.lock() == domains;
          });
    };
    if (!domains || !base ||
        (base->has_sequence_domains && domains == base) ||
        (domains != base && (domains->base_domains != base ||
          domains->proposal_seed != prepared.config.seed ||
          domains->proposals_per_domain != scored.size() || !registered())))
      throw std::invalid_argument("compiled GPU preparation domain owner mismatch");
  }
  std::vector<std::string> inputs;
  for (const auto& input : cfg.compiled_grammar->inputs()) inputs.push_back(input.name);
  for (std::size_t i = 0; i < scored.size(); ++i) {
    if (!scored[i].genome) throw std::invalid_argument("compiled GPU preparation has a null source");
    const auto& expected = prepared.packed.compiled_sources->parents[i];
    const ProgramGenome compacted_scored = compact_genome_tables(*scored[i].genome);
    if (grammar::runtime_cache_identity(expected, inputs, cfg.fuel) !=
        grammar::runtime_cache_identity(compacted_scored, inputs, cfg.fuel))
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
    auto children = decode_compiled_pass(pass.packed, view, context, pass.parent_certificates.get());
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
                                           prepared.compiled_context, prepared.run_resources,
                                           CompiledVariationPass::Mutation);
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
  if (cfg.generation_request->budget.max_nodes >
      static_cast<std::uint32_t>(kGpuReproKernelMaxNodes)) {
    throw std::invalid_argument(
        "gpu reproduction mode capacity exceeded: generation request max_nodes=" +
        std::to_string(cfg.generation_request->budget.max_nodes) +
        " exceeds kernel maximum " + std::to_string(kGpuReproKernelMaxNodes) +
        " (grammar search_limits.max_nodes=" +
        std::to_string(cfg.compiled_grammar->search_limits().max_nodes) + ")");
  }
#ifdef GAGP_HAS_CUDA
  std::string device_message;
  int device_id = -1;
  if (!initialize_gpu_repro_runtime(&device_message) ||
      !select_gpu_repro_device(&device_id, &device_message)) {
    throw std::runtime_error(
        "gpu reproduction mode device preflight failed: " + device_message);
  }
#else
  throw std::runtime_error(
      "gpu reproduction mode device preflight failed: CUDA is unavailable in this build");
#endif
  auto resources = std::make_shared<GpuReproRunResources>();
  resources->grammar = cfg.compiled_grammar;
  resources->requests = population_requests(cfg);
  resources->offspring_budget = cfg.offspring_resource_budget;
  return resources;
}

void append_gpu_repro_run_payload_roots(
    const std::shared_ptr<GpuReproRunResources>& resources, std::vector<Value>* roots) {
  if (!resources) return;
  if (!roots) throw std::invalid_argument("GPU run payload roots output is null");
  std::lock_guard<std::mutex> lock(resources->mutex);
  if (resources->domains)
    roots->insert(roots->end(), resources->domains->values.begin(), resources->domains->values.end());
  for (const auto& weak : resources->proposal_domains)
    if (const auto domains = weak.lock())
      roots->insert(roots->end(), domains->values.begin(), domains->values.end());
}

GpuReproPreparedData prepare_gpu_repro_backend_inputs(
    const std::vector<ProgramGenome>& population, const EvolutionConfig& cfg,
    std::uint64_t seed, ReproductionStats* stats,
    std::shared_ptr<GpuReproRunResources> resources) {
  return prepare_backend_inputs(population, cfg, seed, stats, nullptr, std::move(resources));
}

OwnedGpuReproOverlap start_owned_gpu_repro_overlap(
    std::vector<ProgramGenome> population, const EvolutionConfig& config,
    std::uint64_t seed, std::shared_ptr<GpuReproRunResources> resources) {
  require_reproduction_mode_supported(config, true);
  OwnedGpuReproOverlap out;
  out.population = std::make_shared<const std::vector<ProgramGenome>>(std::move(population));
  out.completion = std::async(gagp::host_launch_policy(),
      [population = out.population, config, seed, resources]() {
    auto reads = std::make_shared<payload::StagedPayloads>();
    {
      // Registry contents are mutable even though the owning ASTs are const.
      // Record all constants, including unused entries, before preparation.
      payload::StagedPayloads::Scope scope(*reads);
      for (const auto& genome : *population)
        for (const auto& value : genome.ast.consts)
          (void)grammar::canonical_constant_encoding(value);
    }
    ReproductionStats stats;
    auto prepared = std::make_shared<const GpuReproPreparedData>(
        prepare_backend_inputs(*population, config, seed, &stats, nullptr, resources));
    return std::function<ReproductionResult(const std::vector<double>&)>(
        [population, config, seed, resources, prepared, reads, stats,
         consumed = std::make_shared<std::atomic<bool>>(false)](
            const std::vector<double>& fitness) mutable -> ReproductionResult {
      if (consumed->exchange(true))
        throw std::logic_error("owned GPU overlap continuation already consumed");
      const auto scored = rank_population_refs(*population, fitness, false);
#ifdef GAGP_HAS_CUDA
      if (!reads->read_snapshot_unchanged()) {
        const auto refreshed = prepare_backend_inputs(
            *population, config, seed, &stats, nullptr, resources);
        return run_compiled_prepared(scored, config, refreshed, &stats);
      }
      return run_compiled_prepared(scored, config, *prepared, &stats);
#else
      throw std::runtime_error("gpu reproduction requested but CUDA is unavailable in this build");
#endif
    });
  });
  return out;
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
  validate_compiled_prepared(scored, cfg, prepared);
#ifndef GAGP_HAS_CUDA
  (void)scored;
  (void)cfg;
  (void)prepared;
  (void)stats;
  throw std::runtime_error("gpu reproduction requested but CUDA is unavailable in this build");
#else
  return run_compiled_prepared(scored, cfg, prepared, stats);
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
  if (scored.empty()) return ReproductionResult{};
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
  // This call owns the just-prepared population and never exposes prepared
  // state to a caller. Preparation already validates imports, config/resources,
  // compact sources and payload identities. Replay validation is needed only
  // when a caller can replace or mutate that state between prepare and run.
  return run_compiled_prepared(scored, cfg, prepared, &prep_stats);
#endif
}

}  // namespace gagp::evo::repro
