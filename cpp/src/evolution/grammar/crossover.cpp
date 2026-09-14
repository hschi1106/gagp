#include "gagp/evolution/crossover.hpp"

#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/evolution/grammar/random.hpp"
#include "variation_internal.hpp"

namespace gagp::evo {

std::pair<ProgramGenome, ProgramGenome> crossover(const ProgramGenome& parent_a,
    const ProgramGenome& parent_b, std::uint64_t seed, grammar::VariationContext& context) {
  using namespace grammar;
  ++context.counters().crossover_attempts;
  // Invalid parents are errors, never fallback candidates. Supplied provenance
  // is ignored, including on a reproduction attempt with no compatible pair.
  auto a = variation_detail::certify(parent_a, context);
  auto b = variation_detail::certify(parent_b, context);
  const auto analysis_a = context.cache().analyze(a, context.request());
  const auto analysis_b = context.cache().analyze(b, context.request());
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
      if (!donor_fits(site_a, site_b.materialized_nodes, site_b.materialized_depth, site_b.template_nesting) ||
          !donor_fits(site_b, site_a.materialized_nodes, site_a.materialized_depth, site_a.template_nesting)) {
        ++context.counters().budget_rejections;
        continue;
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
  auto child_a = variation_detail::splice(a.ast, *selected_a, b.ast, selected_b->occurrences.front());
  auto child_b = variation_detail::splice(b.ast, *selected_b, a.ast, selected_a->occurrences.front());
  return {variation_detail::accept(std::move(child_a), a, context),
      variation_detail::accept(std::move(child_b), b, context)};
}

}  // namespace gagp::evo
