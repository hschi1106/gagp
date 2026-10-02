#pragma once
#include "phase_generate.cuh"

namespace gagp::evo::repro {
// Internal construction metadata, never an external grammar certificate.
// The current profile requires identity reference scope maps and no binders,
// aliases, templates or coupled holes inside a phase. Each position denotes a
// distinct logical site, even when two subtrees have identical contents.
struct PhaseGeneSite {
  int nonterminal = -1;
  int entry_expression = -1;
  int parent_scope = 0;
  int scope = 0;
};
struct PhaseTreeView {
  const PhaseTreeNode* nodes = nullptr;
  const int* origins = nullptr;
  const PhaseGeneSite* sites = nullptr;
  const Value* constants = nullptr;
  int node_count = 0, constant_count = 0;
};
struct MutablePhaseTree {
  PhaseTreeNode* nodes;
  int* origins;
  PhaseGeneSite* sites;
  Value* constants;
  int capacity;
};
// Annotate a private generated tree using its construction expression IDs.
// This is not canonical grammar reconstruction and is not an import boundary.
// Checking every traversed production/expression guards implementation mistakes
// and establishes precise nonterminal sites for subsequent GPU-only splices.
__device__ inline bool annotate_phase_tree(const PhaseGrammarView& g, int root,
    int scope, const PhaseTreeNode* nodes, const int* origins, int count,
    PhaseGeneSite* sites) {
  if (!nodes || !origins || !sites || !g.expressions || !g.nonterminals || !g.productions ||
      root < 0 || root >= g.expression_count || scope < 0 || scope > 8 || count <= 0 || count > 1024)
    return false;
  struct Task { int expression, scope; PhaseGeneSite site; };
  Task pending[192]; int pending_count = 1, cursor = 0, steps = 0;
  pending[0] = {root, scope, {}};
  while (pending_count) {
    if (++steps > 8192 || cursor >= count) return false;
    const auto task = pending[--pending_count];
    if (task.expression < 0 || task.expression >= g.expression_count) return false;
    const auto expression = g.expressions[task.expression];
    if (expression.kind == PhaseExpressionKind::Reference) {
      if (expression.target < 0 || expression.target >= g.nonterminal_count ||
          expression.scope_count < 0 || expression.scope_count > task.scope ||
          pending_count >= 192 || task.site.nonterminal >= 0) return false; // no zero-node aliases
      for (int i = 0; i < expression.scope_count; ++i)
        if (expression.scope[i] != i) return false;
      const auto nt = g.nonterminals[expression.target];
      if (nt.scope_count != expression.scope_count || nt.production_offset < 0 || nt.production_count <= 0 ||
          nt.production_offset > g.production_count || nt.production_count > g.production_count - nt.production_offset)
        return false;
      bool found = false;
      for (int i = 0; i < nt.production_count; ++i)
        found |= g.productions[nt.production_offset + i].expression == origins[cursor];
      if (!found) return false;
      pending[pending_count++] = {origins[cursor], expression.scope_count,
          {expression.target, task.expression, task.scope, expression.scope_count}};
      continue;
    }
    const auto node = nodes[cursor];
    if (origins[cursor] != task.expression || node.kind != expression.node.kind ||
        node.opcode != expression.node.opcode || node.arity != expression.node.arity ||
        node.fuel != expression.node.fuel || node.merge_fuel != expression.node.merge_fuel ||
        node.arity < 0 || node.arity > 3) return false;
    if (expression.kind == PhaseExpressionKind::Bound) {
      if (expression.target < 0 || expression.target >= task.scope || node.operand != expression.target)
        return false;
    } else if (expression.kind == PhaseExpressionKind::Operation && node.operand != expression.node.operand)
      return false;
    sites[cursor++] = task.site;
    for (int i = node.arity - 1; i >= 0; --i) {
      if (pending_count >= 192) return false;
      pending[pending_count++] = {expression.children[i], task.scope, {}};
    }
  }
  return cursor == count;
}

__device__ inline int phase_subtree_end(const PhaseTreeView& tree, int start) {
  if (!tree.nodes || start < 0 || start >= tree.node_count || tree.node_count > 1024) return -1;
  int pending = 1;
  for (int i = start; i < tree.node_count; ++i) {
    const int arity = tree.nodes[i].arity;
    if (arity < 0 || arity > 3) return -1;
    pending += arity - 1;
    if (!pending) return i + 1;
  }
  return -1;
}
__device__ inline int phase_node_depth(const PhaseTreeView& tree, int position) {
  if (!tree.nodes || position < 0 || position >= tree.node_count || tree.node_count > 1024) return -1;
  int pending[64]; int depth = 1; pending[0] = 1;
  for (int i = 0; i <= position; ++i) {
    while (depth && !pending[depth - 1]) --depth;
    if (!depth) return -1;
    if (i == position) return depth;
    --pending[depth - 1];
    const int arity = tree.nodes[i].arity;
    if (arity < 0 || arity > 3) return -1;
    if (arity) { if (depth == 64) return -1; pending[depth++] = arity; }
  }
  return -1;
}
__device__ inline int phase_tree_depth(const PhaseTreeNode* nodes, int count) {
  int pending[64]; int depth = 1, maximum = 1; pending[0] = 1;
  for (int i = 0; i < count; ++i) {
    while (depth && !pending[depth - 1]) --depth;
    if (!depth) return -1;
    if (depth > maximum) maximum = depth;
    --pending[depth - 1];
    const int arity = nodes[i].arity;
    if (arity < 0 || arity > 3) return -1;
    if (arity) {
      if (depth == 64) return -1;
      pending[depth++] = arity;
    }
  }
  while (depth && !pending[depth - 1]) --depth;
  return depth == 0 ? maximum : -1;
}

// Output must not alias either immutable input. The run owner supplies distinct
// generation buffers; this primitive never retains pointers across calls.
// Same type alone is insufficient: nonterminal and lexical scope must agree.
__device__ inline PhaseGenerationResult splice_phase_tree(const PhaseTreeView& base,
    int destination, const PhaseTreeView& donor, int source,
    MutablePhaseTree output, int max_depth) {
  PhaseGenerationResult result;
  if (!base.nodes || !base.origins || !base.sites || !donor.nodes || !donor.origins || !donor.sites ||
      !output.nodes || !output.origins || !output.sites || !output.constants ||
      output.capacity <= 0 || output.capacity > 1024 || max_depth <= 0 || max_depth > 64 ||
      base.node_count <= 0 || base.node_count > 1024 || donor.node_count <= 0 || donor.node_count > 1024 ||
      base.constant_count < 0 || base.constant_count > base.node_count || donor.constant_count < 0 || donor.constant_count > donor.node_count ||
      (base.constant_count && !base.constants) || (donor.constant_count && !donor.constants) ||
      destination < 0 || destination >= base.node_count || source < 0 || source >= donor.node_count)
    return result;
  const auto a = base.sites[destination], b = donor.sites[source];
  if (a.nonterminal < 0 || a.nonterminal != b.nonterminal || a.scope != b.scope) return result;
  const int end = phase_subtree_end(base, destination), donor_end = phase_subtree_end(donor, source);
  if (end < 0 || donor_end < 0) return result;
  const int length = base.node_count - (end - destination) + donor_end - source;
  if (length > output.capacity) { result.status = PhaseCompileStatus::Capacity; return result; }
  for (int i = 0; i < length; ++i) {
    const bool incoming = i >= destination && i < destination + donor_end - source;
    const auto& tree = incoming ? donor : base;
    const int index = incoming ? source + i - destination : i < destination ? i : i + (end - destination) - (donor_end - source);
    auto node = tree.nodes[index];
    if (node.kind == PhaseTreeKind::Constant) {
      if (node.operand < 0 || node.operand >= tree.constant_count) return result;
      node.operand = result.constants;
      output.constants[result.constants++] = tree.constants[tree.nodes[index].operand];
    }
    output.nodes[i] = node; output.origins[i] = tree.origins[index]; output.sites[i] = tree.sites[index];
  }
  // The replaced root belongs to the destination's grammar invocation. Descendant
  // sites retain their own identity scope contracts, never pointer-based aliases.
  output.sites[destination] = a;
  const int depth = phase_tree_depth(output.nodes, length);
  if (depth < 0) return result;
  if (depth > max_depth) { result.status = PhaseCompileStatus::Capacity; return result; }
  result.nodes = length; result.status = PhaseCompileStatus::Ok; return result;
}
} // namespace gagp::evo::repro
