#include "gagp/evolution/grammar/derivation_resources.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <string>
#include <tuple>

#include "gagp/evolution/grammar/compiled.hpp"
#include "gagp/serialization/region_plan_json.hpp"

namespace gagp::evo::grammar {
namespace {
struct AnalysisLimit {};
using Pair = std::pair<std::uint32_t, std::uint32_t>;
using Label = std::tuple<NodeKind, int, std::string,
    std::vector<std::pair<int, std::uint32_t>>, std::size_t>;

class ProductAnalysis {
 public:
  ProductAnalysis(const CompiledGrammar& grammar, std::size_t limit)
      : grammar_(grammar), remaining_(limit), closures_(grammar.expressions().size()),
        closure_ready_(closures_.size()), labels_(closures_.size()), label_ready_(closures_.size()) {}

  ResourceInvarianceCertificate run(const std::vector<std::uint32_t>& roots) {
    std::vector<std::vector<std::uint32_t>> root_leaves;
    for (auto root : roots) {
      std::vector<std::uint32_t> leaves;
      for (auto production : grammar_.nonterminals().at(root).productions) {
        const auto& values = closure(grammar_.productions()[production].expression);
        leaves.insert(leaves.end(), values.begin(), values.end());
      }
      unique(leaves);
      root_leaves.push_back(std::move(leaves));
    }
    const auto representatives = quotient(root_leaves);
    std::vector<std::vector<std::size_t>> root_pairs;
    for (auto& leaves : root_leaves) {
      for (auto& id : leaves) id = representatives[id];
      unique(leaves);
      root_pairs.push_back(pairs(leaves, leaves));
    }
    for (std::size_t i = 0; i < states_.size(); ++i) {
      const auto pair = states_[i].pair;
      const auto& a = grammar_.expressions()[pair.first];
      const auto& b = grammar_.expressions()[pair.second];
      std::vector<std::vector<std::size_t>> children;
      for (std::size_t j = 0; j < a.children.size(); ++j)
        children.push_back(pairs(closure(a.children[j]), closure(b.children[j])));
      groups_total_ += children.size();
      if (groups_total_ > 2000000) throw AnalysisLimit{};
      states_[i].children = std::move(children);
    }
    // Each group is an OR of matching child pairs; a state is an AND of groups.
    struct Group { std::size_t owner; bool satisfied = false; };
    std::vector<Group> groups;
    std::vector<std::vector<std::size_t>> reverse(states_.size());
    std::vector<std::size_t> missing(states_.size());
    std::vector<bool> live(states_.size()), bad(states_.size());
    std::deque<std::size_t> pending;
    for (std::size_t i = 0; i < states_.size(); ++i) {
      missing[i] = states_[i].children.size();
      if (!missing[i]) { live[i] = true; pending.push_back(i); }
      for (const auto& children : states_[i].children) {
        tick();
        const auto group = groups.size(); groups.push_back({i, false});
        for (auto child : children) { tick(); reverse[child].push_back(group); }
      }
    }
    while (!pending.empty()) {
      const auto child = pending.front(); pending.pop_front();
      for (auto index : reverse[child]) {
        tick(); auto& group = groups[index];
        if (group.satisfied) continue;
        group.satisfied = true;
        if (!--missing[group.owner]) { live[group.owner] = true; pending.push_back(group.owner); }
      }
    }
    for (std::size_t i = 0; i < states_.size(); ++i) {
      const auto& a = grammar_.expressions()[states_[i].pair.first].resource_charge;
      const auto& b = grammar_.expressions()[states_[i].pair.second].resource_charge;
      if (live[i] && (a.nodes != b.nodes || a.depth != b.depth || a.resets_depth != b.resets_depth)) {
        bad[i] = true; pending.push_back(i);
      }
    }
    while (!pending.empty()) {
      const auto child = pending.front(); pending.pop_front();
      for (auto index : reverse[child]) {
        tick(); const auto owner = groups[index].owner;
        if (live[owner] && !bad[owner]) { bad[owner] = true; pending.push_back(owner); }
      }
    }
    ResourceInvarianceCertificate result;
    result.complete = true; result.product_states = states_.size();
    for (const auto& values : root_pairs) {
      bool productive = false, conflict = false;
      for (auto value : values) { productive |= live[value]; conflict |= bad[value]; }
      result.roots.push_back(productive && !conflict);
    }
    return result;
  }
 private:
  void tick() { if (!remaining_) throw AnalysisLimit{}; --remaining_; }
  template<class T> static void unique(std::vector<T>& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
  }
  const std::vector<std::uint32_t>& closure(std::uint32_t start) {
    if (closure_ready_.at(start)) return closures_[start];
    std::vector<std::uint32_t> pending{start}, leaves;
    std::vector<bool> seen(closures_.size());
    while (!pending.empty()) {
      tick(); const auto id = pending.back(); pending.pop_back();
      if (seen.at(id)) continue;
      seen[id] = true;
      const auto& e = grammar_.expressions()[id];
      if (e.kind == ExpressionKind::Reference) {
        for (auto p : grammar_.nonterminals().at(e.target).productions)
          pending.push_back(grammar_.productions()[p].expression);
      } else if (e.kind == ExpressionKind::Template || e.kind == ExpressionKind::Hole) {
        if (e.children.size() != 1) throw AnalysisLimit{};
        pending.push_back(e.children.front());
      } else leaves.push_back(id);
    }
    unique(leaves);
    closure_items_ += leaves.size();
    if (closure_items_ > 2000000) throw AnalysisLimit{};
    closures_[start] = std::move(leaves); closure_ready_[start] = true;
    return closures_[start];
  }
  std::vector<std::uint32_t> quotient(
      const std::vector<std::vector<std::uint32_t>>& roots) {
    // Bisimulation of the relaxed, weighted tree grammar. Keep labels AND
    // charges in the initial partition; refine by each ordered child's set of
    // classes until no class splits. This preserves both overlap and costs,
    // including productive recursive languages.
    std::vector<std::uint32_t> reachable;
    std::vector<bool> seen(closures_.size());
    const auto enqueue = [&](std::uint32_t id) {
      if (!seen[id]) { seen[id] = true; reachable.push_back(id); }
    };
    for (const auto& values : roots) for (auto id : values) enqueue(id);
    for (std::size_t i = 0; i < reachable.size(); ++i)
      for (auto child : grammar_.expressions()[reachable[i]].children)
        for (auto id : closure(child)) { tick(); enqueue(id); }
    using Initial = std::tuple<Label, std::uint64_t, std::uint64_t, bool>;
    std::map<Initial, std::uint32_t> initial;
    std::vector<std::uint32_t> classes(closures_.size(), kNoGrammarId);
    for (auto id : reachable) {
      tick(); const auto& cost = grammar_.expressions()[id].resource_charge;
      const Initial key{label(id), cost.nodes, cost.depth, cost.resets_depth};
      classes[id] = initial.emplace(key, initial.size()).first->second;
    }
    auto count = initial.size();
    for (;;) {
      using Signature = std::pair<std::uint32_t, std::vector<std::vector<std::uint32_t>>>;
      std::map<Signature, std::uint32_t> partitions;
      auto next = classes;
      for (auto id : reachable) {
        tick(); Signature key{classes[id], {}};
        for (auto child : grammar_.expressions()[id].children) {
          std::vector<std::uint32_t> alternatives;
          for (auto leaf : closure(child)) { tick(); alternatives.push_back(classes[leaf]); }
          unique(alternatives); key.second.push_back(std::move(alternatives));
        }
        next[id] = partitions.emplace(std::move(key), partitions.size()).first->second;
      }
      classes = std::move(next);
      if (partitions.size() == count) break;
      count = partitions.size();
    }
    std::vector<std::uint32_t> first(count, kNoGrammarId), result(closures_.size(), kNoGrammarId);
    for (auto id : reachable) {
      auto& representative = first[classes[id]];
      if (representative == kNoGrammarId) representative = id;
      result[id] = representative;
    }
    // All reachable child closures have been visited above. Future product
    // traversal therefore sees only representatives, never mixed partitions.
    for (std::size_t i = 0; i < closures_.size(); ++i) if (closure_ready_[i]) {
      for (auto& id : closures_[i]) id = result[id];
      unique(closures_[i]);
    }
    return result;
  }
  const Label& label(std::uint32_t id) {
    if (label_ready_[id]) return labels_[id];
    tick(); const auto& e = grammar_.expressions()[id];
    NodeKind kind = NodeKind::CONST;
    std::string extra;
    switch (e.kind) {
      case ExpressionKind::Constant: break;
      case ExpressionKind::Input: kind = NodeKind::VAR; extra = grammar_.inputs().at(e.target).name; break;
      case ExpressionKind::Local: kind = NodeKind::VAR; extra = grammar_.locals().at(e.target).name; break;
      case ExpressionKind::Bound: kind = NodeKind::REGION_VAR; break;
      case ExpressionKind::Control:
        kind = PrimitiveCatalog::standard().control_signatures().at(e.target).lowering_node; break;
      case ExpressionKind::Primitive: {
        const auto node = PrimitiveCatalog::standard().at(e.target).lowering_node;
        if (!node) throw AnalysisLimit{};
        kind = *node; break;
      }
      case ExpressionKind::Structured: {
        const auto& contract = grammar_.structured_contracts().at(e.target);
        if (contract.family != StructuredFamily::BoundedRegion || !contract.plan) throw AnalysisLimit{};
        kind = NodeKind::BOUNDED_REGION;
        extra = canonical_json(serialization::encode_region_plan(*contract.plan)); break;
      }
      default: throw AnalysisLimit{};
    }
    std::vector<std::pair<int, std::uint32_t>> fuel;
    for (const auto& charge : e.fuel_charges) fuel.emplace_back(static_cast<int>(charge.event), charge.cost);
    std::sort(fuel.begin(), fuel.end());
    labels_[id] = {kind, e.category == NodeCategory::Expression ? static_cast<int>(e.type) : -1,
        std::move(extra), std::move(fuel), e.children.size()};
    label_ready_[id] = true;
    return labels_[id];
  }
  std::vector<std::size_t> pairs(const std::vector<std::uint32_t>& left,
      const std::vector<std::uint32_t>& right) {
    std::vector<std::size_t> result;
    for (auto a : left) for (auto b : right) {
      tick(); if (label(a) != label(b)) continue;
      Pair pair{std::min(a, b), std::max(a, b)};
      auto found = ids_.find(pair);
      if (found == ids_.end()) {
        if (states_.size() == 100000) throw AnalysisLimit{};
        found = ids_.emplace(pair, states_.size()).first;
        states_.push_back({pair, {}});
      }
      if (++edges_ > 2000000) throw AnalysisLimit{};
      result.push_back(found->second);
    }
    unique(result); return result;
  }
  struct State { Pair pair; std::vector<std::vector<std::size_t>> children; };
  const CompiledGrammar& grammar_;
  std::size_t remaining_, edges_ = 0, groups_total_ = 0, closure_items_ = 0;
  std::vector<std::vector<std::uint32_t>> closures_;
  std::vector<bool> closure_ready_;
  std::vector<Label> labels_;
  std::vector<bool> label_ready_;
  std::map<Pair, std::size_t> ids_;
  std::vector<State> states_;
};
}  // namespace

ResourceInvarianceCertificate certify_resource_invariance(const CompiledGrammar& grammar,
    const std::vector<std::uint32_t>& roots, std::size_t work_limit) {
  for (auto root : roots)
    if (root >= grammar.nonterminals().size()) throw std::invalid_argument("unknown resource certificate root");
  try { return ProductAnalysis(grammar, work_limit).run(roots); }
  catch (const AnalysisLimit&) {
    ResourceInvarianceCertificate result; result.roots.resize(roots.size(), false); return result;
  }
}
std::vector<bool> CompiledGrammar::resource_invariant_roots(
    const std::vector<std::uint32_t>& roots) const {
  for (auto root : roots)
    if (root >= nonterminals_.size()) throw std::invalid_argument("unknown resource certificate root");
  std::lock_guard<std::mutex> lock(executable_cache_->mutex);
  std::vector<std::uint32_t> missing;
  for (auto root : roots)
    if (!executable_cache_->resource_roots.count(root)) missing.push_back(root);
  std::sort(missing.begin(), missing.end());
  missing.erase(std::unique(missing.begin(), missing.end()), missing.end());
  if (!missing.empty()) {
    const auto certificate = certify_resource_invariance(*this, missing);
    for (std::size_t i = 0; i < missing.size(); ++i)
      executable_cache_->resource_roots.emplace(missing[i],
          certificate.complete && certificate.roots[i]);
  }
  std::vector<bool> result;
  for (auto root : roots) result.push_back(executable_cache_->resource_roots.at(root));
  return result;
}
}  // namespace gagp::evo::grammar
