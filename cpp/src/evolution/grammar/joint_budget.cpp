#include "gagp/evolution/grammar/joint_budget.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace gagp::evo::grammar {
namespace {
using Frontier = std::vector<JointResourceCost>;
bool dominates(const JointResourceCost& a,const JointResourceCost& b) {
  return a.physical_nodes <= b.physical_nodes && a.projected.nodes <= b.projected.nodes &&
      a.projected.carried_depth <= b.projected.carried_depth && a.projected.reset_depth <= b.projected.reset_depth;
}
struct Slot { std::uint32_t expression = kNoGrammarId, depth = kNoGrammarId; };
struct Layer { std::uint32_t id; std::vector<JointResourceCost> slots; };

class JointBudget {
 public:
  JointBudget(const CompiledGrammar& grammar,const GenerationRequest& request,ProjectedBudget limits,std::size_t capacity)
      : grammar_(grammar),request_(request),limits_(limits),capacity_(capacity),
        tables_(grammar.nonterminals().size(),std::vector<Frontier>(request.budget.max_depth+1)) {
    if (!capacity_) throw std::invalid_argument("joint resource frontier capacity must be positive");
  }
  Frontier run() {
    const auto& entry = grammar_.nonterminals().at(request_.nonterminal);
    const bool wrap = entry.category == NodeCategory::Expression;
    const auto depth = request_.budget.max_depth - (wrap ? 3 : 0);
    for (std::uint32_t d = 0; d <= depth; ++d) {
      bool changed;
      do {
        changed = false;
        for (const auto& production : grammar_.productions()) {
          if (!production.generates(request_.stage)) continue;
          auto candidates = expression(production.expression,d);
          auto& target = tables_.at(production.nonterminal).at(d);
          for (const auto& candidate : candidates) {
            const auto before = target.size();
            if (insert(target,candidate)) {
              stored_ -= before; stored_ += target.size();
              if (stored_ > capacity_) throw std::runtime_error("joint resource table capacity exceeded");
              changed = true;
            }
          }
        }
      } while (changed);
    }
    Frontier result;
    for (auto cost : tables_.at(entry.id).at(depth)) {
      if (wrap) {
        // PROGRAM(BLOCK_CONS(RETURN(expression),BLOCK_NIL)). Its implicit
        // physical envelope also has default unit projected charges.
        const auto returned = parent({}, {cost});
        const auto block = parent({}, {returned,parent({}, {})});
        cost = parent({}, {block});
      }
      insert(result,cost);
    }
    std::sort(result.begin(),result.end(),[&](const auto& a,const auto& b) {
      work();
      if (a.physical_nodes != b.physical_nodes) return a.physical_nodes < b.physical_nodes;
      if (a.projected.nodes != b.projected.nodes) return a.projected.nodes < b.projected.nodes;
      if (a.projected.carried_depth != b.projected.carried_depth) return a.projected.carried_depth < b.projected.carried_depth;
      return a.projected.reset_depth < b.projected.reset_depth;
    });
    return result;
  }

 private:
  bool fits(const JointResourceCost& cost) const {
    return cost.physical_nodes <= request_.budget.max_nodes && cost.projected.nodes <= limits_.max_nodes &&
        cost.projected.carried_depth <= limits_.max_depth && cost.projected.reset_depth <= limits_.max_depth;
  }
  bool insert(Frontier& frontier,const JointResourceCost& cost) {
    if (!fits(cost)) return false;
    for (const auto& old : frontier) { work(); if (dominates(old,cost)) return false; }
    frontier.erase(std::remove_if(frontier.begin(),frontier.end(),[&](const auto& old){
      work(); return dominates(cost,old);
    }),frontier.end());
    if (frontier.size() >= capacity_) throw std::runtime_error("joint resource frontier capacity exceeded");
    frontier.push_back(cost); return true;
  }
  JointResourceCost parent(ResourceCharge own,const std::vector<JointResourceCost>& children) {
    work();
    JointResourceCost result{1,{own.nodes,own.resets_depth ? 0 : own.depth,own.resets_depth ? own.depth : 0}};
    for (const auto& child : children) {
      work();
      // Node/charge bounds follow from 65536 physical nodes and per-node
      // authored capacities. Wide arithmetic avoids intermediate truncation.
      const auto physical = static_cast<std::uint64_t>(result.physical_nodes) + child.physical_nodes;
      result.physical_nodes = static_cast<std::uint32_t>(std::min<std::uint64_t>(physical,UINT32_MAX));
      result.projected.nodes += child.projected.nodes;
      if (own.resets_depth) result.projected.reset_depth = std::max(result.projected.reset_depth,child.projected.peak(own.depth));
      else {
        result.projected.carried_depth = std::max(result.projected.carried_depth,own.depth+child.projected.carried_depth);
        result.projected.reset_depth = std::max(result.projected.reset_depth,child.projected.reset_depth);
      }
    }
    return result;
  }
  void gather(std::uint32_t id,std::uint32_t depth,std::uint32_t owner,std::vector<Slot>& slots) {
    work();
    const auto& node = grammar_.expressions().at(id);
    if (node.kind == ExpressionKind::Reference) return;
    if (node.kind == ExpressionKind::Hole && node.template_id == owner) {
      if (!node.children.empty()) {
        auto& slot = slots.at(node.target);
        slot.expression = node.children[0]; slot.depth = std::min(slot.depth,depth);
      }
      return;
    }
    const bool wrapper = node.kind == ExpressionKind::Template || node.kind == ExpressionKind::Hole;
    for (auto child : node.children) gather(child,wrapper ? depth : depth ? depth-1 : 0,owner,slots);
  }
  Frontier expression(std::uint32_t id,std::uint32_t depth) {
    work();
    const auto& node = grammar_.expressions().at(id);
    if (node.kind == ExpressionKind::Reference) return tables_.at(node.target).at(depth);
    if (node.kind == ExpressionKind::Hole) {
      for (auto i=layers_.rbegin();i!=layers_.rend();++i)
        if (i->id == node.template_id) return {i->slots.at(node.target)};
      return node.children.empty() ? Frontier{} : expression(node.children[0],depth);
    }
    if (node.kind == ExpressionKind::Template) {
      if (node.children.empty()) return {};
      std::vector<Slot> slots(grammar_.templates().at(node.target).holes.size());
      gather(node.children[0],depth,node.target,slots);
      std::vector<Frontier> options;
      for (const auto& slot : slots) {
        if (slot.expression == kNoGrammarId || slot.depth == kNoGrammarId) return {};
        options.push_back(expression(slot.expression,slot.depth));
        if (options.back().empty()) return {};
      }
      Frontier result;
      Layer layer{node.target,{}};
      assignments(node.children[0],depth,options,0,layer,result);
      return result;
    }
    if (!depth) return {};
    std::vector<Frontier> children;
    for (auto child : node.children) {
      children.push_back(expression(child,depth-1));
      if (children.back().empty()) return {};
    }
    Frontier result;
    std::vector<JointResourceCost> chosen;
    combinations(node.resource_charge,children,0,chosen,result);
    return result;
  }
  void combinations(ResourceCharge own,const std::vector<Frontier>& children,std::size_t index,
      std::vector<JointResourceCost>& chosen,Frontier& result) {
    work();
    if (index == children.size()) { insert(result,parent(own,chosen)); return; }
    for (const auto& cost : children[index]) {
      chosen.push_back(cost);
      if (fits(parent(own,chosen))) combinations(own,children,index+1,chosen,result);
      chosen.pop_back();
    }
  }
  void assignments(std::uint32_t body,std::uint32_t depth,const std::vector<Frontier>& options,std::size_t index,
      Layer& layer,Frontier& result) {
    work();
    if (index == options.size()) {
      layers_.push_back(layer);
      auto values = expression(body,depth);
      layers_.pop_back();
      for (const auto& value : values) insert(result,value);
      return;
    }
    for (const auto& cost : options[index]) {
      layer.slots.push_back(cost); assignments(body,depth,options,index+1,layer,result); layer.slots.pop_back();
    }
  }
  const CompiledGrammar& grammar_;
  const GenerationRequest& request_;
  ProjectedBudget limits_;
  void work() {
    if (++work_ > 64'000'000) throw std::runtime_error("joint resource construction work capacity exceeded");
  }
  std::size_t capacity_,stored_=0,work_=0;
  std::vector<std::vector<Frontier>> tables_;
  std::vector<Layer> layers_;
};
}  // namespace

std::vector<JointResourceCost> joint_resource_frontier(const CompiledGrammar& grammar,
    const GenerationRequest& request,ProjectedBudget projected_budget,std::size_t maximum_states) {
  (void)validate_request(grammar,request);
  grammar.require_executable(request.nonterminal);
  return JointBudget(grammar,request,projected_budget,maximum_states).run();
}

}  // namespace gagp::evo::grammar
