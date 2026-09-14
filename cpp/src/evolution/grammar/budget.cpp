#include "gagp/evolution/grammar/budget.hpp"

#include <algorithm>

namespace gagp::evo::grammar {
namespace {
struct SlotDepth {
  std::uint32_t expression = kNoGrammarId;
  std::uint32_t depth = kNoGrammarId;
};
struct Layer {
  std::uint32_t template_id;
  std::vector<std::uint32_t> costs;
};
class Budget {
 public:
  Budget(const CompiledGrammar& grammar, const HoleBudgetLookup& enclosing,
      const NonterminalBudgetLookup& nonterminal, const ExpressionBudgetOverride& override_cost)
      : grammar_(grammar), enclosing_(enclosing), nonterminal_(nonterminal), override_cost_(override_cost) {}
  std::uint32_t cost(std::uint32_t id, std::uint32_t depth) {
    const auto& node = grammar_.expressions().at(id);
    if (override_cost_) {
      const auto overridden = override_cost_(node, depth);
      if (overridden) return *overridden;
    }
    if (node.kind == ExpressionKind::Reference)
      return nonterminal_ ? nonterminal_(node.target, depth) :
          grammar_.nonterminals().at(node.target).minimum_nodes_by_depth.at(depth);
    if (node.kind == ExpressionKind::Hole) {
      for (auto i = layers_.rbegin(); i != layers_.rend(); ++i)
        if (i->template_id == node.template_id) return i->costs.at(node.target);
      if (enclosing_) {
        const auto value = enclosing_(node.template_id, node.target);
        if (value) return *value;
      }
      return node.children.empty() ? kNoGrammarId : cost(node.children[0], depth);
    }
    if (node.kind == ExpressionKind::Template) {
      if (node.children.empty()) return kNoGrammarId;
      std::vector<SlotDepth> slots(grammar_.templates().at(node.target).holes.size());
      gather(node.children[0], depth, node.target, slots);
      Layer layer{node.target, {}};
      for (const auto& slot : slots) {
        if (slot.expression == kNoGrammarId || slot.depth == kNoGrammarId) return kNoGrammarId;
        layer.costs.push_back(cost(slot.expression, slot.depth));
      }
      layers_.push_back(std::move(layer));
      const auto result = cost(node.children[0], depth);
      layers_.pop_back(); return result;
    }
    if (!depth) return kNoGrammarId;
    std::uint64_t result = 1;
    for (auto child : node.children) {
      const auto nodes = cost(child, depth - 1);
      if (nodes == kNoGrammarId) return kNoGrammarId;
      result += nodes;
      if (result > grammar_.search_limits().max_nodes) return kNoGrammarId;
    }
    return static_cast<std::uint32_t>(result);
  }

 private:
  void gather(std::uint32_t id, std::uint32_t depth, std::uint32_t template_id, std::vector<SlotDepth>& slots) {
    const auto& node = grammar_.expressions().at(id);
    if (node.kind == ExpressionKind::Reference) return;
    if (node.kind == ExpressionKind::Hole && node.template_id == template_id) {
      if (!node.children.empty()) {
        auto& slot = slots.at(node.target);
        slot.expression = node.children[0]; slot.depth = std::min(slot.depth, depth);
      }
      return;
    }
    const bool wrapper = node.kind == ExpressionKind::Template || node.kind == ExpressionKind::Hole;
    for (auto child : node.children) gather(child, wrapper ? depth : depth ? depth - 1 : 0, template_id, slots);
  }
  const CompiledGrammar& grammar_;
  const HoleBudgetLookup& enclosing_;
  const NonterminalBudgetLookup& nonterminal_;
  const ExpressionBudgetOverride& override_cost_;
  std::vector<Layer> layers_;
};
}  // namespace

std::uint32_t minimum_expression_nodes(const CompiledGrammar& grammar,
    std::uint32_t expression, std::uint32_t depth, const HoleBudgetLookup& enclosing,
    const NonterminalBudgetLookup& nonterminal, const ExpressionBudgetOverride& override_cost) {
  return Budget(grammar, enclosing, nonterminal, override_cost).cost(expression, depth);
}

}  // namespace gagp::evo::grammar
