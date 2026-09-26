#include "gagp/evolution/mutation.hpp"

#include <cmath>
#include <iterator>
#include <map>
#include <stdexcept>
#include <utility>

#include "gagp/evolution/grammar/donor.hpp"
#include "gagp/evolution/grammar/values.hpp"
#include "gagp/evolution/grammar/variation.hpp"
#include "variation_internal.hpp"

namespace gagp::evo {

ProgramGenome mutate(const ProgramGenome& genome, std::uint64_t seed,
    grammar::VariationContext& context, double mutation_subtree_prob) {
  using namespace grammar;
  if (!std::isfinite(mutation_subtree_prob) || mutation_subtree_prob < 0 || mutation_subtree_prob > 1)
    throw std::invalid_argument("compiled mutation subtree probability must be in [0,1]");
  ++context.counters().mutation_attempts;
  auto parent = variation_detail::certify(genome, context);
  const auto analysis = context.analyze(parent);
  GrammarRandom random(seed);
  const double draw = static_cast<double>(random.next() >> 11) * 0x1.0p-53;
  if (draw >= mutation_subtree_prob) {
    // Select a logical constant first, then apply its declared variation policy.
    std::map<std::uint32_t, std::vector<std::size_t>> groups;
    for (std::size_t i = 0; i < analysis->witness.nodes.size(); ++i) {
      const auto& origin = analysis->witness.nodes[i];
      if (origin.fixed || origin.expression == kNoGrammarId || parent.ast.nodes[i].kind != NodeKind::CONST)
        continue;
      if (context.grammar().expressions().at(origin.expression).kind == ExpressionKind::Constant)
        groups[origin.logical_instance].push_back(i);
    }
    if (!groups.empty()) {
      auto selected = groups.begin();
      std::advance(selected, random.bounded(groups.size()));
      const auto& origin = analysis->witness.nodes[selected->second.front()];
      const auto& expression = context.grammar().expressions().at(origin.expression);
      const auto& domain = context.grammar().constants().at(expression.target);
      if (domain.mutation == ConstantMutationPolicy::Keep) {
        ++context.counters().unchanged_children;
        return parent;
      }
      auto candidate = parent.ast;
      const auto index = static_cast<int>(candidate.consts.size());
      const auto& previous = parent.ast.consts.at(parent.ast.nodes[selected->second.front()].i0);
      candidate.consts.push_back(mutate_constant_value(domain,previous,random));
      for (auto node : selected->second) candidate.nodes[node].i0 = index;
      return variation_detail::accept(std::move(candidate), parent, context);
    }
    // No mutable constant production exists: regenerate an admitted nonterminal.
    // Fixed template constants never become perturbation candidates.
  }
  if (analysis->sites.empty()) return variation_detail::fallback(parent, context);
  const auto& site = analysis->sites[random.bounded(analysis->sites.size())];
  ContextualDonor donor;
  try {
    donor = generate_donor(context, random.next(), site, parent);
  } catch (const std::runtime_error&) {
    ++context.counters().generation_rejections;
    return variation_detail::fallback(parent, context);
  }
  return variation_detail::accept(variation_detail::splice(parent.ast, site,
      donor.genome.ast, donor.payload, site.occurrence_binder_ids.front()), parent, context);
}

}  // namespace gagp::evo
