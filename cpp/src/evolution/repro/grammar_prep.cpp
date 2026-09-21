#include "gagp/evolution/repro/prep.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
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

PreprocessOutput preprocess_population(const std::vector<ProgramGenome>& population,
                                       const GpuReproConfig& config,
                                       grammar::VariationContext& context,
                                       std::shared_ptr<const ConstantMutationDomains> domains) {
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
  for (std::size_t parent_index = 0; parent_index < population.size(); ++parent_index) {
    const ProgramGenome& parent = population[parent_index];
    out.population_identities.push_back(grammar::runtime_cache_identity(
        parent, input_names, context.grammar().execution_limits().fuel));

    const auto analysis = context.cache().analyze(parent, context.request());
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

    for (const std::size_t site_index : site_indices) {
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

      for (int attempt = 0; attempt < config.donor_pool_size_per_site; ++attempt) {
        grammar::ContextualDonor donor;
        try {
          donor = grammar::generate_donor(context.grammar(), random.next(), site);
        } catch (const std::runtime_error&) {
          ++context.counters().generation_rejections;
          continue;
        }
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

  out.compatibility_keys = context.cache().registry().keys();
  return out;
}

}  // namespace gagp::evo::repro
