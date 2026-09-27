#include "prep_internal.hpp"
#include "../../runtime/payload/staging.hpp"
#include "gagp/evolution/repro/mutation_schedule.hpp"
#include "gagp/evolution/repro/prep.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "gagp/evolution/grammar/cache.hpp"
#include "gagp/evolution/grammar/donor.hpp"
#include "gagp/evolution/grammar/random.hpp"
#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/evolution/repro/pack.hpp"
#include "../subtree_utils.hpp"
#include "constant_prep.hpp"

namespace gagp::evo::repro {
namespace {

constexpr std::size_t kMaxCompiledPrepItems = 1'000'000;

int as_int(std::size_t value, const char* field) {
  if (value > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    throw std::invalid_argument(std::string(field) + " exceeds signed integer capacity");
  return static_cast<int>(value);
}

int as_int(std::uint32_t value, const char* field) {
  if (value > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
    throw std::invalid_argument(std::string(field) + " exceeds signed integer capacity");
  return static_cast<int>(value);
}

void require_room(std::size_t current, std::size_t additional, const char* field) {
  if (additional > kMaxCompiledPrepItems || current > kMaxCompiledPrepItems - additional)
    throw std::invalid_argument(std::string(field) + " exceeds compiled preparation limit");
}

std::vector<std::size_t> shuffled_site_indices(
    std::size_t count, grammar::GrammarRandom* random) {
  std::vector<std::size_t> indices(count);
  std::iota(indices.begin(), indices.end(), std::size_t{0});
  for (std::size_t remaining = indices.size(); remaining > 1; --remaining) {
    const std::size_t other = static_cast<std::size_t>(random->bounded(remaining));
    std::swap(indices[remaining - 1], indices[other]);
  }
  return indices;
}

CandidateRange make_candidate(const grammar::VariationSite& site,
                              std::size_t occurrence_offset,
                              std::size_t donor_offset) {
  if (site.occurrences.empty())
    throw std::logic_error("compiled variation candidate has no occurrences");
  if (site.compatibility_id == grammar::kNoGrammarId)
    throw std::logic_error("compiled variation candidate has no compatibility ID");

  CandidateRange candidate;
  candidate.start = as_int(site.occurrences.front().begin, "candidate start");
  candidate.stop = as_int(site.occurrences.front().end, "candidate stop");
  candidate.tag = static_cast<int>(site.category == NodeCategory::Program
                                       ? CandidateTag::Program
                                       : CandidateTag::Expr);
  candidate.aux = static_cast<int>(site.type);
  candidate.compatibility_id = site.compatibility_id;
  candidate.crossover_closed = site.crossover_closed;
  candidate.occurrence_offset = as_int(occurrence_offset, "candidate occurrence offset");
  candidate.occurrence_count = as_int(site.occurrences.size(), "candidate occurrence count");
  candidate.replacement_max_nodes =
      as_int(site.replacement_budget.max_nodes, "candidate replacement node budget");
  candidate.replacement_max_depth =
      as_int(site.replacement_budget.max_depth, "candidate replacement depth budget");
  candidate.remaining_template_nesting =
      as_int(site.remaining_template_nesting, "candidate template nesting budget");
  candidate.materialized_nodes =
      as_int(site.materialized_nodes, "candidate materialized nodes");
  candidate.materialized_depth =
      as_int(site.materialized_depth, "candidate materialized depth");
  candidate.template_nesting =
      as_int(site.template_nesting, "candidate template nesting");
  candidate.donor_offset = as_int(donor_offset, "candidate donor offset");
  candidate.has_projected_allowance = site.has_projected_allowance;
  candidate.projected_allowance = site.projected_allowance;
  candidate.projected_resources = site.projected_resources;
  return candidate;
}

ProgramGenome extract_donor_fragment(const grammar::ContextualDonor& donor) {
  AstProgram empty;
  ProgramGenome fragment;
  fragment.ast = subtree::replace_subtree(
      empty, 0, 0, donor.genome.ast, donor.payload.begin, donor.payload.end);
  fragment = compact_genome_tables(fragment);
  fragment.derivation.reset();
  return fragment;
}

bool shares_owner(
    const std::shared_ptr<const grammar::CompiledGrammar>& left,
    const std::shared_ptr<const grammar::CompiledGrammar>& right) {
  return left && right && !left.owner_before(right) && !right.owner_before(left);
}

}  // namespace

static PreprocessOutput preprocess_population_impl(const std::vector<ProgramGenome>& population,
                                       const GpuReproConfig& config,
                                       grammar::VariationContext& context,
                                       std::shared_ptr<const ConstantMutationDomains> domains,
                                       bool prepare_donors,
                                       const std::vector<grammar::WarmPopulationMember>* handoff) {
  if (config.population_size <= 0 || config.population_size > 65536 ||
      static_cast<std::size_t>(config.population_size) != population.size())
    throw std::invalid_argument("compiled grammar preprocessing population size mismatch");
  if (config.candidates_per_program <= 0 || config.candidates_per_program > 65536)
    throw std::invalid_argument("compiled grammar candidates_per_program must be in [1,65536]");
  if (config.donor_pool_size_per_site <= 0 || config.donor_pool_size_per_site > 64)
    throw std::invalid_argument("compiled grammar donor_pool_size_per_site must be in [1,64]");
  if (config.max_nodes <= 0 ||
      static_cast<std::uint32_t>(config.max_nodes) != context.request().budget.max_nodes)
    throw std::invalid_argument("compiled grammar max_nodes must match the generation request");
  if (config.max_expr_depth <= 0 ||
      static_cast<std::uint32_t>(config.max_expr_depth) != context.request().budget.max_depth)
    throw std::invalid_argument("compiled grammar max_expr_depth must match the generation request");
  for (const auto& parent : population) {
    if (parent.ast.nodes.size() > static_cast<std::size_t>(config.max_nodes))
      throw std::invalid_argument("compiled grammar parent exceeds configured max_nodes");
  }

  if (!domains)
    domains = prepare_constant_mutation_domains(context.grammar_owner());
  if (!shares_owner(domains->grammar_owner, context.grammar_owner()))
    throw std::invalid_argument(
        "constant mutation domains do not match the compiled grammar owner");
  domains = sample_constant_mutation_domains(domains, config.seed, population.size());
  auto constants = std::make_shared<ConstantMutationTable>();
  constants->grammar_domains = std::move(domains);
  PreprocessOutput out;
  out.prepared_max_nodes = config.max_nodes;
  out.prepared_max_depth = config.max_expr_depth;
  out.compiled_grammar = context.grammar_owner();
  out.constant_mutation = constants;
  out.population_identities.reserve(population.size());
  out.subtree_ends.resize(population.size());
  out.candidates.resize(population.size());

  std::vector<std::string> input_names;
  input_names.reserve(context.grammar().inputs().size());
  for (const auto& input : context.grammar().inputs()) input_names.push_back(input.name);

  grammar::GrammarRandom random(config.seed);
  std::size_t donor_attempts = 0;
  const std::size_t pool_window = std::min<std::size_t>(1024,
      std::max<std::size_t>(128, population.size()));
  std::size_t preview_begin = 0, preview_end = 0;
  std::vector<grammar::DonorPoolJob> planned_jobs;
  std::vector<std::pair<std::size_t, std::size_t>> planned_sites;
  std::optional<std::vector<grammar::DonorPool>> planned_pools;
  std::size_t planned_cursor = 0;
  struct PreviewAnalysis {
    std::shared_ptr<const grammar::VariationAnalysis> analysis;
    std::string identity;
    std::shared_ptr<payload::StagedPayloads> reads;
  };
  std::vector<PreviewAnalysis> preview_analyses;
  const auto warmed = [&](std::size_t index) -> const grammar::WarmPopulationMember* {
    if (!handoff || handoff->size() != population.size()) return nullptr;
    const auto& member = (*handoff)[index];
    return member.analysis && member.reads && member.reads->read_snapshot_unchanged()
        ? &member : nullptr;
  };

  const auto prefetch = [&](std::size_t begin) {
    preview_begin = begin;
    preview_end = std::min(population.size(), begin + pool_window);
    planned_jobs.clear(); planned_sites.clear(); planned_pools.reset(); planned_cursor = 0;
    preview_analyses.clear();
    if (!prepare_donors || config.compiled_pass != CompiledVariationPass::Mutation ||
        config.mutation_ratio <= 0.0 || std::thread::hardware_concurrency() < 2 ||
        config.donor_pool_size_per_site < 2 || population.size() < pool_window) return;
    // Stop before the next selected parent would exceed the batch API's donor
    // storage bound. Sparse mutation schedules can use the full window; dense
    // schedules retain bounded parallel batches rather than falling back en masse.
    const auto max_jobs = (std::size_t{1048576} / context.request().budget.max_nodes) /
        static_cast<std::size_t>(config.donor_pool_size_per_site);
    if (max_jobs < 2) return;
    // Preview the existing schedule with a copied RNG. No main RNG, output
    // offsets, mutation counters or stream order change during speculation.
    auto preview_random = random;
    preview_analyses.resize(std::min(pool_window, population.size() - begin));
    ConstantMutationTable preview_constants;
    preview_constants.grammar_domains = constants->grammar_domains;
    try {
      for (auto index = begin; index < preview_end; ++index) {
        auto& saved = preview_analyses[index - begin];
        if (const auto* member = warmed(index)) {
          saved.analysis = member->analysis;
          saved.identity = member->runtime_identity;
          saved.reads = member->reads;
        } else if (!payload::StagedPayloads::has_active_scope()) {
          saved.reads = std::make_unique<payload::StagedPayloads>();
          payload::StagedPayloads::Scope scope(*saved.reads);
          saved.analysis = context.analyze(population[index], &saved.identity);
        } else {
          saved.analysis = context.analyze(population[index]);
        }
        const auto& analysis = saved.analysis;
        append_constant_mutation_stream(preview_constants, population[index].ast,
            analysis->verified, analysis->witness);
        const auto selected = std::min(analysis->sites.size(),
            static_cast<std::size_t>(config.candidates_per_program));
        auto sites = shuffled_site_indices(analysis->sites.size(), &preview_random);
        sites.resize(selected);
        const int anticipated = anticipated_mutation_candidate<grammar::GrammarRandom>(
            config.seed, static_cast<int>(index), config.mutation_ratio,
            config.mutation_subtree_ratio, selected, preview_constants.streams.back().group_count > 0);
        if (anticipated >= 0 && planned_jobs.size() == max_jobs) {
          preview_end = index;
          preview_analyses.resize(index - begin);
          break;
        }
        for (std::size_t rank = 0; rank < sites.size(); ++rank) {
          std::vector<std::uint64_t> seeds;
          for (int attempt = 0; attempt < config.donor_pool_size_per_site; ++attempt) {
            const auto seed = preview_random.next();
            if (static_cast<int>(rank) == anticipated) seeds.push_back(seed);
          }
          if (!seeds.empty()) {
            planned_sites.emplace_back(index, sites[rank]);
            planned_jobs.push_back({&population[index], analysis->sites[sites[rank]], std::move(seeds)});
          }
        }
      }
      planned_pools = grammar::try_generate_donor_pools(context, planned_jobs);
    } catch (const std::exception&) {
      // The batch API publishes no payloads until successful return. Replaying
      // the original interleaved loop preserves errors and collision behavior.
      planned_pools.reset();
    }
  };
  for (std::size_t parent_index = 0; parent_index < population.size(); ++parent_index) {
    if (parent_index == preview_end) {
      if (planned_pools && planned_cursor != planned_pools->size())
        throw std::logic_error("prefetched donor schedule did not consume its pools");
      prefetch(parent_index);
    }
    const ProgramGenome& parent = population[parent_index];
    std::string parent_identity;
    std::shared_ptr<const grammar::VariationAnalysis> analysis;
    const auto preview_index = parent_index - preview_begin;
    if (preview_index < preview_analyses.size()) {
      auto& saved = preview_analyses[preview_index];
      // The population is const for this call, but registry payloads are mutable.
      // Only reuse an analysis after validating its exact read dependencies.
      if (saved.analysis && saved.reads &&
          saved.reads->read_snapshot_unchanged()) {
        analysis = std::move(saved.analysis);
        parent_identity = std::move(saved.identity);
      }
      saved.reads.reset();
    }
    if (!analysis) {
      if (const auto* member = warmed(parent_index)) {
        analysis = member->analysis;
        parent_identity = member->runtime_identity;
      } else analysis = context.analyze(parent, &parent_identity);
    }
    out.population_identities.push_back(std::move(parent_identity));
    out.parent_constant_streams.push_back(as_int(constants->streams.size(), "parent constant stream"));
    append_constant_mutation_stream(*constants, parent.ast, analysis->verified, analysis->witness);
    out.subtree_ends[parent_index] = analysis->verified.subtree_end;
    if (analysis->sites.size() > kMaxCompiledPrepItems)
      throw std::invalid_argument("compiled grammar site count exceeds compiled preparation limit");
    const std::size_t selected_count = std::min(analysis->sites.size(),
        static_cast<std::size_t>(config.candidates_per_program));
    const std::size_t donors_per_site =
        static_cast<std::size_t>(config.donor_pool_size_per_site);
    if (selected_count > kMaxCompiledPrepItems / donors_per_site)
      throw std::invalid_argument("compiled grammar donor count exceeds compiled preparation limit");
    const std::size_t selected_donor_attempts = selected_count * donors_per_site;
    require_room(donor_attempts, selected_donor_attempts,
                 "compiled grammar donor count");
    donor_attempts += selected_donor_attempts;

    auto site_indices = shuffled_site_indices(analysis->sites.size(), &random);
    site_indices.resize(selected_count);
    out.candidates[parent_index].reserve(site_indices.size());

    const int anticipated_candidate = config.compiled_pass == CompiledVariationPass::Mutation ?
        anticipated_mutation_candidate<grammar::GrammarRandom>(config.seed,
            static_cast<int>(parent_index), config.mutation_ratio,
            config.mutation_subtree_ratio, selected_count,
            constants->streams.back().group_count > 0) : -1;
    std::size_t candidate_rank = 0;
    for (const std::size_t site_index : site_indices) {
      const bool needs_donors = prepare_donors &&
          (config.compiled_pass != CompiledVariationPass::Mutation ||
           static_cast<int>(candidate_rank) == anticipated_candidate);
      ++candidate_rank;
      const grammar::VariationSite& site = analysis->sites[site_index];
      if (site.occurrence_binder_ids.size() != site.occurrences.size())
        throw std::logic_error(
            "compiled variation candidate binder mappings do not align with occurrences");
      require_room(out.occurrences.size(), site.occurrences.size(),
                   "compiled grammar occurrence count");

      CandidateRange candidate =
          make_candidate(site, out.occurrences.size(), out.donor_pool.size());
      std::size_t formal_arity = 0;
      if (!site.occurrence_binder_ids.empty())
        formal_arity = site.occurrence_binder_ids.front().size();
      for (std::size_t occurrence_index = 0;
           occurrence_index < site.occurrences.size(); ++occurrence_index) {
        const auto& occurrence = site.occurrences[occurrence_index];
        const auto& binder_ids = site.occurrence_binder_ids[occurrence_index];
        if (binder_ids.size() != formal_arity)
          throw std::logic_error(
              "compiled variation candidate occurrences have different binder arities");
        require_room(out.occurrence_binder_ids.size(), binder_ids.size(),
                     "compiled grammar occurrence binder count");
        out.occurrences.push_back(CandidateOccurrence{
            as_int(occurrence.begin, "candidate occurrence start"),
            as_int(occurrence.end, "candidate occurrence stop"),
            as_int(out.occurrence_binder_ids.size(),
                   "candidate occurrence binder offset"),
            as_int(binder_ids.size(), "candidate occurrence binder count")});
        out.occurrence_binder_ids.insert(
            out.occurrence_binder_ids.end(), binder_ids.begin(), binder_ids.end());
      }

      std::vector<std::uint64_t> donor_seeds;
      for (int attempt = 0; attempt < config.donor_pool_size_per_site; ++attempt) {
        const auto donor_seed = random.next();
        if (needs_donors) donor_seeds.push_back(donor_seed);
      }
      grammar::DonorPool donor_pool;
      if (planned_pools && needs_donors) {
        if (planned_cursor >= planned_pools->size() ||
            planned_sites.at(planned_cursor) != std::make_pair(parent_index, site_index) ||
            planned_jobs.at(planned_cursor).seeds != donor_seeds)
          throw std::logic_error("prefetched donor schedule differs from sequential generation");
        donor_pool = std::move((*planned_pools)[planned_cursor++]);
      } else donor_pool = grammar::generate_donor_pool(context, donor_seeds, site, parent);
      for (auto& prepared_donor : donor_pool) {
        if (!prepared_donor) {
          ++context.counters().generation_rejections;
          continue;
        }
        auto& donor = *prepared_donor;
        require_room(out.donor_pool.size(), 1, "compiled grammar donor count");
        if (!donor.frame.binder_ids.empty() &&
            donor.frame.binder_ids != site.occurrence_binder_ids.front())
          throw std::logic_error(
              "compiled donor formal binder IDs differ from its source occurrence");
        const auto& donor_binder_ids = site.occurrence_binder_ids.front();
        require_room(out.donor_binder_ids.size(), donor_binder_ids.size(),
                     "compiled grammar donor binder count");
        ProgramGenome fragment = extract_donor_fragment(donor);
        if (!donor.genome.derivation || donor.payload.end > donor.genome.derivation->nodes.size())
          throw std::logic_error("compiled donor lost its verified constant witness");
        grammar::DerivationMetadata fragment_witness;
        fragment_witness.nodes.assign(
            donor.genome.derivation->nodes.begin() + donor.payload.begin,
            donor.genome.derivation->nodes.begin() + donor.payload.end);
        out.donor_constant_streams.push_back(as_int(constants->streams.size(), "donor constant stream"));
        append_constant_mutation_stream(*constants, fragment.ast, fragment_witness);
        std::string donor_identity = grammar::runtime_cache_identity(
            fragment, input_names, context.grammar().execution_limits().fuel);
        out.donor_pool.push_back(DonorProgram{std::move(fragment.ast), site.type});
        out.donor_contracts.push_back(DonorContract{
            site.compatibility_id,
            as_int(donor.nodes, "donor materialized nodes"),
            as_int(donor.depth, "donor materialized depth"),
            as_int(donor.template_nesting, "donor template nesting"),
            as_int(out.donor_binder_ids.size(), "donor binder offset"),
            as_int(donor_binder_ids.size(), "donor binder count")});
        out.donor_binder_ids.insert(
            out.donor_binder_ids.end(), donor_binder_ids.begin(), donor_binder_ids.end());
        out.donor_identities.push_back(std::move(donor_identity));
        ++candidate.donor_count;
      }
      out.candidates[parent_index].push_back(candidate);
    }
  }

  if (planned_pools && planned_cursor != planned_pools->size())
    throw std::logic_error("prefetched donor schedule has unconsumed pools");
  out.compatibility_keys = context.cache().registry().keys();
  return out;
}

PreprocessOutput preprocess_population(const std::vector<ProgramGenome>& population,
    const GpuReproConfig& config, grammar::VariationContext& context,
    std::shared_ptr<const ConstantMutationDomains> domains, bool prepare_donors) {
  return preprocess_population_impl(population, config, context, std::move(domains), prepare_donors, nullptr);
}

PreprocessOutput preprocess_warmed_population(const std::vector<ProgramGenome>& population,
    const GpuReproConfig& config, grammar::VariationContext& context,
    std::shared_ptr<const ConstantMutationDomains> domains, bool prepare_donors,
    const std::vector<grammar::WarmPopulationMember>& handoff) {
  return preprocess_population_impl(population, config, context, std::move(domains), prepare_donors, &handoff);
}

}  // namespace gagp::evo::repro
