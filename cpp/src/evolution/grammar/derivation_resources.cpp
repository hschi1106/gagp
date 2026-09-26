#include "gagp/evolution/grammar/derivation_resources.hpp"

#include <utility>
#include <map>

#include "gagp/evolution/grammar/compiled.hpp"

namespace gagp::evo::grammar {

bool resource_charges_are_local(const CompiledGrammar& grammar) {
  using Key = std::pair<NodeKind, std::vector<std::pair<int, std::uint32_t>>>;
  std::map<Key, ResourceCharge> charges;
  for (const auto& expression : grammar.expressions()) {
    NodeKind kind;
    switch (expression.kind) {
      case ExpressionKind::Reference:
      case ExpressionKind::Template:
      case ExpressionKind::Hole: continue;
      case ExpressionKind::Constant: kind = NodeKind::CONST; break;
      case ExpressionKind::Input:
      case ExpressionKind::Local: kind = NodeKind::VAR; break;
      case ExpressionKind::Bound: kind = NodeKind::REGION_VAR; break;
      case ExpressionKind::Control:
        kind = PrimitiveCatalog::standard().control_signatures().at(expression.target).lowering_node;
        break;
      case ExpressionKind::Primitive: {
        const auto node = PrimitiveCatalog::standard().at(expression.target).lowering_node;
        if (!node) return false;
        kind = *node; break;
      }
      case ExpressionKind::Structured:
        if (grammar.structured_contracts().at(expression.target).family != StructuredFamily::BoundedRegion)
          return false;
        kind = NodeKind::BOUNDED_REGION; break;
    }
    Key key{kind, {}};
    for (const auto& fuel : expression.fuel_charges)
      key.second.emplace_back(static_cast<int>(fuel.event), fuel.cost);
    const auto inserted = charges.emplace(std::move(key), expression.resource_charge);
    const auto& previous = inserted.first->second;
    const auto& current = expression.resource_charge;
    if (previous.nodes != current.nodes || previous.depth != current.depth ||
        previous.resets_depth != current.resets_depth) return false;
  }
  return true;
}

}  // namespace gagp::evo::grammar
