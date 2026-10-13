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
  // Same uniform distribution over every feasible pair, but one rank draw
  // instead of one reservoir draw per pair. The deterministic RNG trajectory
  // is deliberately distinct. Nonlocal projected-resource trials retain legacy.
  const bool counted = std::getenv("GAGP_CPU_COUNTED_PAIRS") &&
      (!context.offspring_budget() ||
       (std::all_of(analysis_a->sites.begin(), analysis_a->sites.end(),
            [](const auto& site) { return site.has_projected_allowance; }) &&
        std::all_of(analysis_b->sites.begin(), analysis_b->sites.end(),
            [](const auto& site) { return site.has_projected_allowance; })));
  if (counted) {
    std::unordered_map<std::string, std::vector<const VariationSite*>> groups;
    for (const auto& site : analysis_b->sites)
      if (!site.compatibility_key.empty()) groups[site.compatibility_key].push_back(&site);
    std::vector<std::uint64_t> row_counts(analysis_a->sites.size(), 0);
    const auto fits = [](const auto& a, const auto& b) {
      return donor_fits(a, b) && donor_fits(b, a);
    };
    for (std::size_t row = 0; row < analysis_a->sites.size(); ++row) {
      const auto& site = analysis_a->sites[row];
      const auto group = groups.find(site.compatibility_key);
      context.counters().contract_rejections += analysis_b->sites.size() -
          (group == groups.end() ? 0 : group->second.size());
      if (group == groups.end()) continue;
      for (const auto* donor : group->second) {
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
        for (const auto* donor : groups.at(selected_a->compatibility_key)) {
          if (!fits(*selected_a, *donor)) continue;
          if (rank == 0) { selected_b = donor; break; }
          --rank;
        }
        break;
      }
      if (!selected_a || !selected_b) throw std::logic_error("counted crossover lost feasible rank");
    }
  } else for (const auto& site_a : analysis_a->sites) {
    for (const auto& site_b : analysis_b->sites) {
      if (!compatible_sites(site_a, site_b)) {
        ++context.counters().contract_rejections;
        continue;
      }
      if (!donor_fits(site_a, site_b) || !donor_fits(site_b, site_a)) {
        ++context.counters().budget_rejections;
        continue;
      }
      if (context.offspring_budget() && !(site_a.has_projected_allowance && site_b.has_projected_allowance)) {
        // Charges can change when a splice selects a different canonical
        // ancestor production. Check the whole child, not independent subtree
        // minima or a transplanted source witness.
        bool invalid_contract = false;
        const auto fits = [&](const ProgramGenome& base, const VariationSite& destination,
                              const ProgramGenome& donor, const VariationSite& source,
                              const GenerationRequest& request) {
          ProgramGenome child;
          child.ast = variation_detail::splice(base.ast, destination, donor.ast,
              source.occurrences.front(), source.occurrence_binder_ids.front(), destination.crossover_closed);
          try {
            // Trial children need native verification, canonical membership,
            // lowering and resource accounting, but not their future variation
            // sites. Selected children still receive full acceptance below.
            return context.offspring_budget()->accepts(
                reconstruct_derivation(context.grammar(), child, request).resources->subtree());
          } catch (const std::invalid_argument&) {
            invalid_contract = true;
            return false;
          }
        };
        if (!fits(a, site_a, b, site_b, analysis_a->witness.request) ||
            !fits(b, site_b, a, site_a, analysis_b->witness.request)) {
          if (invalid_contract) ++context.counters().contract_rejections;
          else ++context.counters().budget_rejections;
          continue;
        }
      }
      // Reservoir selection is uniform over feasible pairs without storing the
      // Cartesian product or biasing toward contracts with fewer alternatives.
      if (random.bounded(++eligible) == 0) {
        selected_a = &site_a;
        selected_b = &site_b;
      }
    }
  }
  if (!selected_a) return {variation_detail::fallback(a, context), variation_detail::fallback(b, context)};
  if (selected) selected->emplace(*selected_a, *selected_b);
  auto child_a = variation_detail::splice(a.ast, *selected_a, b.ast, selected_b->occurrences.front(), selected_b->occurrence_binder_ids.front(), selected_a->crossover_closed);
  auto child_b = variation_detail::splice(b.ast, *selected_b, a.ast, selected_a->occurrences.front(), selected_a->occurrence_binder_ids.front(), selected_b->crossover_closed);
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
