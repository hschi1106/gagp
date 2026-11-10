#include "gagp/evolution/crossover.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <unordered_map>

#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/evolution/grammar/random.hpp"
#include "variation_internal.hpp"

namespace gagp::evo::grammar::variation_detail {

void CrossoverGroupBounds::include(const VariationSite& site) {
  ordinary = ordinary && site.materialized_nodes > 0 && site.materialized_depth > 0;
  max_nodes = std::max(max_nodes, site.materialized_nodes);
  max_depth = std::max(max_depth, site.materialized_depth);
  max_nesting = std::max(max_nesting, site.template_nesting);
  min_nodes = initialized ? std::min(min_nodes, site.replacement_budget.max_nodes) : site.replacement_budget.max_nodes;
  min_depth = initialized ? std::min(min_depth, site.replacement_budget.max_depth) : site.replacement_budget.max_depth;
  min_nesting = initialized ? std::min(min_nesting, site.remaining_template_nesting) : site.remaining_template_nesting;
  initialized = true;
}

bool CrossoverGroupBounds::all_fit(const VariationSite& site) const {
  return initialized && ordinary && donor_fits(site, max_nodes, max_depth, max_nesting) &&
      site.materialized_nodes > 0 && site.materialized_depth > 0 &&
      site.materialized_nodes <= min_nodes && site.materialized_depth <= min_depth &&
      site.template_nesting <= min_nesting;
}

namespace {
std::pair<ProgramGenome, ProgramGenome> crossover_analyzed(const ProgramGenome& a,
    const ProgramGenome& b, const std::shared_ptr<const VariationAnalysis>& analysis_a,
    const std::shared_ptr<const VariationAnalysis>& analysis_b,
    std::uint64_t seed, VariationContext& context, SelectedCrossoverSites* selected) {
  if (selected) selected->reset();
  using namespace grammar;
  GrammarRandom random(seed);
  const VariationSite* selected_a = nullptr;
  const VariationSite* selected_b = nullptr;
  std::uint64_t eligible = 0;
  // Uniform over every feasible pair, with one rank draw.
  struct Group {
    std::vector<const VariationSite*> sites;
    CrossoverGroupBounds bounds;
  };
  std::unordered_map<std::string, Group> groups;
  for (const auto& site : analysis_b->sites)
    if (!site.compatibility_key.empty()) {
      auto& group = groups[site.compatibility_key];
      group.sites.push_back(&site);
      group.bounds.include(site);
    }
  std::vector<std::uint64_t> row_counts(analysis_a->sites.size(), 0);
  std::vector<bool> all_fit(analysis_a->sites.size(), false);
  const auto fits = [](const auto& a, const auto& b) {
    return donor_fits(a, b) && donor_fits(b, a);
  };
  for (std::size_t row = 0; row < analysis_a->sites.size(); ++row) {
    const auto& site = analysis_a->sites[row];
    const auto group = groups.find(site.compatibility_key);
    context.counters().contract_rejections += analysis_b->sites.size() -
        (group == groups.end() ? 0 : group->second.sites.size());
    if (group == groups.end()) continue;
    all_fit[row] = group->second.bounds.all_fit(site);
    if (all_fit[row]) row_counts[row] = group->second.sites.size();
    else for (const auto* donor : group->second.sites) {
      if (fits(site, *donor)) ++row_counts[row];
      else ++context.counters().budget_rejections;
    }
    if (row_counts[row] > std::numeric_limits<std::uint64_t>::max() - eligible)
      throw std::overflow_error("crossover feasible pair count overflow");
    eligible += row_counts[row];
  }
  if (eligible) {
    auto rank = random.bounded(eligible);
    for (std::size_t row = 0; row < row_counts.size(); ++row) {
      if (rank >= row_counts[row]) { rank -= row_counts[row]; continue; }
      selected_a = &analysis_a->sites[row];
      const auto& group = groups.at(selected_a->compatibility_key).sites;
      if (all_fit[row]) selected_b = group[rank];
      else for (const auto* donor : group) {
        if (!fits(*selected_a, *donor)) continue;
        if (rank == 0) { selected_b = donor; break; }
        --rank;
      }
      break;
    }
    if (!selected_a || !selected_b) throw std::logic_error("counted crossover lost feasible rank");
  }
  if (!selected_a) return {variation_detail::fallback(a, context), variation_detail::fallback(b, context)};
  if (selected) selected->emplace(*selected_a, *selected_b);
  auto child_a = variation_detail::splice(a.ast, *selected_a, b.ast, selected_b->occurrences.front(), selected_b->occurrence_binder_ids.front());
  auto child_b = variation_detail::splice(b.ast, *selected_b, a.ast, selected_a->occurrences.front(), selected_a->occurrence_binder_ids.front());
  return {variation_detail::accept(std::move(child_a), a, context),
      variation_detail::accept(std::move(child_b), b, context)};
}

}  // namespace

std::pair<ProgramGenome, ProgramGenome> crossover_with_sites(const ProgramGenome& parent_a,
    const ProgramGenome& parent_b, std::uint64_t seed, VariationContext& context,
    SelectedCrossoverSites* selected) {
  ++context.counters().crossover_attempts;
  if (selected) selected->reset();
  // Public mutable inputs always undergo complete admission before site use.
  std::shared_ptr<const VariationAnalysis> analysis_a, analysis_b;
  auto a = certify(parent_a, context, &analysis_a);
  auto b = certify(parent_b, context, &analysis_b);
  return crossover_analyzed(a, b, analysis_a, analysis_b, seed, context, selected);
}

std::unique_ptr<const CertifiedCrossoverParent> CertifiedCrossoverParent::create_scalar(
    const ProgramGenome& parent, VariationContext& context) {
  for (const auto& value : parent.ast.consts)
    if (value.tag != ValueTag::Int && value.tag != ValueTag::Float && value.tag != ValueTag::Bool)
      return nullptr;
  auto owned = std::unique_ptr<CertifiedCrossoverParent>(new CertifiedCrossoverParent);
  owned->genome_ = certify(parent, context, &owned->analysis_);
  owned->context_ = &context;
  return owned;
}

std::pair<ProgramGenome, ProgramGenome> crossover_certified(
    const CertifiedCrossoverParent& a, const CertifiedCrossoverParent& b,
    std::uint64_t seed, VariationContext& context) {
  if (a.context_ != &context || b.context_ != &context)
    throw std::invalid_argument("certified crossover parents belong to another context");
  ++context.counters().crossover_attempts;
  return crossover_analyzed(a.genome_, b.genome_, a.analysis_, b.analysis_,
      seed, context, nullptr);
}

}  // namespace gagp::evo::grammar::variation_detail

namespace gagp::evo {
std::pair<ProgramGenome, ProgramGenome> crossover(const ProgramGenome& parent_a,
    const ProgramGenome& parent_b, std::uint64_t seed, grammar::VariationContext& context) {
  return grammar::variation_detail::crossover_with_sites(parent_a, parent_b, seed, context, nullptr);
}
}  // namespace gagp::evo
