#include "gagp/evolution/crossover.hpp"

#include <stdexcept>

#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/evolution/grammar/random.hpp"
#include "variation_internal.hpp"

namespace gagp::evo::grammar::variation_detail {

std::pair<ProgramGenome, ProgramGenome> crossover_with_sites(const ProgramGenome& parent_a,
    const ProgramGenome& parent_b, std::uint64_t seed, VariationContext& context,
    SelectedCrossoverSites* selected) {
  if (selected) selected->reset();
  using namespace grammar;
  ++context.counters().crossover_attempts;
  // Invalid parents are errors, never fallback candidates. Supplied provenance
  // is ignored, including on a reproduction attempt with no compatible pair.
  auto a = variation_detail::certify(parent_a, context);
  auto b = variation_detail::certify(parent_b, context);
  const auto analysis_a = context.analyze(a);
  const auto analysis_b = context.analyze(b);
  GrammarRandom random(seed);
  const VariationSite* selected_a = nullptr;
  const VariationSite* selected_b = nullptr;
  std::uint64_t eligible = 0;
  for (const auto& site_a : analysis_a->sites) {
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

}  // namespace gagp::evo::grammar::variation_detail

namespace gagp::evo {
std::pair<ProgramGenome, ProgramGenome> crossover(const ProgramGenome& parent_a,
    const ProgramGenome& parent_b, std::uint64_t seed, grammar::VariationContext& context) {
  return grammar::variation_detail::crossover_with_sites(parent_a, parent_b, seed, context, nullptr);
}
}  // namespace gagp::evo
