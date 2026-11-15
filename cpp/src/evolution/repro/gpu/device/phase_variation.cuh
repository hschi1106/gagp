#pragma once
#include "phase_generate.cuh"

namespace gagp::evo::repro {
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
__host__ __device__ inline int phase_subtree_end(const PhaseTreeView& tree, int start);
__host__ __device__ inline bool phase_same_constant(const Value& x, const Value& y) {
  return x.tag == y.tag && (x.tag == ValueTag::Bool ? x.b == y.b : x.i == y.i);
}
// Walk a tree of a profile with repeated template holes (g.shared). Every
// later occurrence of a repeated hole is the first occurrence's derivation:
// fill-level expressions shift by the occurrence's delta and take its static
// operands (binder numbers, region entries), nodes below a reference and all
// constant values are copied, and every site inside a copy is marked fixed,
// so it counts toward site depth but variation never selects it.
// Rewrite=false (import): `in` must already hold exact copies; only out.sites
// is written (it may be in.sites' storage). Rewrite=true (after generation or
// variation): `out` must not alias `in`; the stale input copies are skipped
// and rebuilt, so the result may be longer or shorter than the input.
template<bool Rewrite>
__host__ __device__ inline PhaseGenerationResult share_phase_tree(const PhaseGrammarView& g, int root, int scope,
    const PhaseTreeView& in, MutablePhaseTree out) {
  PhaseGenerationResult result;
  if (!g.shared || !g.expressions || !g.nonterminals || !g.productions || !in.nodes || !in.origins || !out.sites ||
      (Rewrite && (!out.nodes || !out.origins || !out.constants)) || (in.constant_count && !in.constants) ||
      root < 0 || root >= g.expression_count || scope < 0 || scope > 15 || in.node_count <= 0 || in.node_count > 1024 ||
      in.constant_count < 0 || in.constant_count > in.node_count || out.capacity <= 0 || out.capacity > 1024)
    return result;
  // close >= 0 ends record `close`; close <= -2 ends template instance
  // `expression`, dropping the records it owns from index -2-close up (copies
  // inside a copy are never walked, so a record lives exactly as long as its
  // instance; a record of an enclosing instance may be opened inside it).
  struct Task { int expression, scope, close; PhaseGeneSite site; };
  // A first occurrence spans [begin,end) of the output.
  struct Record { int first, begin, end; };
  Task pending[192]; Record records[32];
  int pending_count = 1, record_count = 0, cursor = 0, written = 0, steps = 0;
  pending[0] = {root, scope, -1, {}};
  const PhaseTreeNode* source_nodes = Rewrite ? out.nodes : in.nodes;
  const int* source_origins = Rewrite ? out.origins : in.origins;
  const Value* source_constants = Rewrite ? out.constants : in.constants;
  while (pending_count) {
    if (++steps > 8192) return result;
    const auto task = pending[--pending_count];
    if (task.close >= 0) { records[task.close].end = written; continue; }
    if (task.close <= -2) {
      int kept = -2 - task.close;
      for (int r = kept; r < record_count; ++r)
        if (g.shared[records[r].first].owner != task.expression) records[kept++] = records[r];
      record_count = kept; continue;
    }
    if (task.expression < 0 || task.expression >= g.expression_count) return result;
    const auto expression = g.expressions[task.expression];
    if (expression.kind == PhaseExpressionKind::Reference) {
      if (expression.target < 0 || expression.target >= g.nonterminal_count || cursor >= in.node_count ||
          expression.scope_count < 0 || expression.scope_count > 8 || pending_count >= 192 || task.site.nonterminal >= 0) return result;
      const auto nt = g.nonterminals[expression.target];
      if (nt.scope_count != expression.scope_count || nt.production_offset < 0 || nt.production_count <= 0 ||
          nt.production_offset > g.production_count || nt.production_count > g.production_count - nt.production_offset)
        return result;
      int production = -1;
      for (int i = 0; i < nt.production_count && production < 0; ++i) {
        int e = g.productions[nt.production_offset + i].expression;
        for (int hop = 0; hop < 64 && e >= 0 && e < g.expression_count &&
             g.expressions[e].kind == PhaseExpressionKind::Alias; ++hop) e = g.expressions[e].children[0];
        if (e == in.origins[cursor]) production = g.productions[nt.production_offset + i].expression;
      }
      if (production < 0) return result;
      pending[pending_count++] = {production, expression.scope_count, -1,
          {expression.target, task.expression, task.scope, expression.scope_count | (nt.fixed ? kPhaseFixedSite : 0)}};
      continue;
    }
    if (expression.kind == PhaseExpressionKind::Alias) {
      const auto shared = g.shared[task.expression];
      if (shared.instance) {
        if (pending_count >= 191) return result;
        pending[pending_count++] = {task.expression, 0, -2 - record_count, {}};
      } else if (shared.first == task.expression) {
        if (record_count >= 32 || pending_count >= 191) return result;
        records[record_count] = {task.expression, written, -1};
        pending[pending_count++] = {-1, 0, record_count++, {}};
      } else if (shared.first >= 0) {
        // A later occurrence: never the first node of a production.
        if (task.site.nonterminal >= 0 || cursor >= in.node_count) return result;
        int r = record_count - 1;
        while (r >= 0 && (records[r].first != shared.first || records[r].end < 0)) --r;
        if (r < 0) return result;
        const int skipped = phase_subtree_end(in, cursor);
        const int begin = records[r].begin, length = records[r].end - begin;
        if (skipped < 0 || length <= 0) return result;
        if (!Rewrite && skipped - cursor != length) return result;
        if (Rewrite && length > out.capacity - written) { result.status = PhaseCompileStatus::Capacity; return result; }
        for (int i = 0; i < length; ++i) {
          auto node = source_nodes[begin + i]; int origin = source_origins[begin + i]; auto site = out.sites[begin + i];
          if (origin >= shared.low && origin <= shared.high) {
            origin += shared.delta;
            if (origin < 0 || origin >= g.expression_count) return result;
            const auto& model = g.expressions[origin].node;
            if (model.kind != node.kind || model.opcode != node.opcode || model.arity != node.arity || model.type != node.type) return result;
            if (node.kind != PhaseTreeKind::Constant) node.operand = model.operand;
          }
          if (site.nonterminal >= 0) {
            site.scope |= kPhaseFixedSite;
            if (site.entry_expression >= shared.low && site.entry_expression <= shared.high) site.entry_expression += shared.delta;
          }
          if (node.kind == PhaseTreeKind::Constant && (node.operand < 0 || node.operand >= (Rewrite ? result.constants : in.constant_count)))
            return result;
          if constexpr (Rewrite) {
            if (node.kind == PhaseTreeKind::Constant) {
              const Value value = out.constants[node.operand];
              node.operand = result.constants; out.constants[result.constants++] = value;
            }
            out.nodes[written + i] = node; out.origins[written + i] = origin;
          } else {
            const auto x = in.nodes[cursor + i];
            if (x.kind != node.kind || x.opcode != node.opcode || x.arity != node.arity || x.fuel != node.fuel ||
                x.merge_fuel != node.merge_fuel || x.type != node.type || in.origins[cursor + i] != origin) return result;
            if (node.kind == PhaseTreeKind::Constant) {
              if (x.operand < 0 || x.operand >= in.constant_count ||
                  !phase_same_constant(source_constants[x.operand], source_constants[node.operand])) return result;
            } else if (x.operand != node.operand) return result;
          }
          out.sites[written + i] = site;
        }
        cursor = skipped; written += length;
        continue;
      }
      // A template or hole wrapper passes a pending site to its first node.
      if (pending_count >= 192) return result;
      pending[pending_count++] = {expression.children[0], phase_child_scope(expression, 0, task.scope), -1, task.site};
      continue;
    }
    if (cursor >= in.node_count) return result;
    auto node = in.nodes[cursor];
    if (in.origins[cursor] != task.expression || node.kind != expression.node.kind ||
        node.opcode != expression.node.opcode || node.arity != expression.node.arity ||
        node.fuel != expression.node.fuel || node.merge_fuel != expression.node.merge_fuel || node.type != expression.node.type ||
        node.arity < 0 || node.arity > kPhaseMaxArity) return result;
    if (expression.kind != PhaseExpressionKind::Constant && node.operand != expression.node.operand) return result;
    if constexpr (Rewrite) {
      if (written >= out.capacity) { result.status = PhaseCompileStatus::Capacity; return result; }
      if (node.kind == PhaseTreeKind::Constant) {
        if (node.operand < 0 || node.operand >= in.constant_count) return result;
        out.constants[result.constants] = in.constants[node.operand]; node.operand = result.constants++;
      }
      out.nodes[written] = node; out.origins[written] = task.expression;
    }
    out.sites[written++] = task.site; ++cursor;
    for (int i = node.arity - 1; i >= 0; --i) {
      if (pending_count >= 192) return result;
      pending[pending_count++] = {expression.children[i], phase_child_scope(expression, i, task.scope), -1, {}};
    }
  }
  if (cursor != in.node_count || record_count) return result;
  result.nodes = written; result.constants = Rewrite ? result.constants : in.constant_count;
  result.status = PhaseCompileStatus::Ok; return result;
}

// Annotate a private generated tree using its construction expression IDs.
// This is not canonical grammar reconstruction and is not an import boundary.
// Checking every traversed production/expression guards implementation mistakes
// and establishes precise nonterminal sites for subsequent GPU-only splices.
// A profile with repeated holes also checks that every later occurrence is an
// exact copy (see share_phase_tree); that needs the constant table.
__host__ __device__ inline bool annotate_phase_tree(const PhaseGrammarView& g, int root,
    int scope, const PhaseTreeNode* nodes, const int* origins, int count,
    PhaseGeneSite* sites, const Value* constants=nullptr, int constant_count=0) {
  if (g.shared) {
    const PhaseTreeView in{nodes, origins, sites, constants, count, constant_count};
    const auto r = share_phase_tree<false>(g, root, scope, in, {nullptr, nullptr, sites, nullptr, 1024});
    return r.status == PhaseCompileStatus::Ok && r.nodes == count;
  }
  if (!nodes || !origins || !sites || !g.expressions || !g.nonterminals || !g.productions ||
      root < 0 || root >= g.expression_count || scope < 0 || scope > 15 || count <= 0 || count > 1024)
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
          expression.scope_count < 0 || expression.scope_count > 8 ||
          pending_count >= 192 || task.site.nonterminal >= 0) return false; // one site per node
      const auto nt = g.nonterminals[expression.target];
      if (nt.scope_count != expression.scope_count || nt.production_offset < 0 || nt.production_count <= 0 ||
          nt.production_offset > g.production_count || nt.production_count > g.production_count - nt.production_offset)
        return false;
      // The first node's origin names the production, through any template
      // wrappers at its root (the profile keeps resolved roots unique).
      int production = -1;
      for (int i = 0; i < nt.production_count && production < 0; ++i) {
        int e = g.productions[nt.production_offset + i].expression;
        for (int hop = 0; hop < 64 && e >= 0 && e < g.expression_count &&
             g.expressions[e].kind == PhaseExpressionKind::Alias; ++hop) e = g.expressions[e].children[0];
        if (e == origins[cursor]) production = g.productions[nt.production_offset + i].expression;
      }
      if (production < 0) return false;
      pending[pending_count++] = {production, expression.scope_count,
          {expression.target, task.expression, task.scope, expression.scope_count | (nt.fixed ? kPhaseFixedSite : 0)}};
      continue;
    }
    if (expression.kind == PhaseExpressionKind::Alias) {
      // A template or hole wrapper passes a pending site to its first node.
      if (pending_count >= 192) return false;
      pending[pending_count++] = {expression.children[0], phase_child_scope(expression, 0, task.scope), task.site};
      continue;
    }
    const auto node = nodes[cursor];
    if (origins[cursor] != task.expression || node.kind != expression.node.kind ||
        node.opcode != expression.node.opcode || node.arity != expression.node.arity ||
        node.fuel != expression.node.fuel || node.merge_fuel != expression.node.merge_fuel || node.type != expression.node.type ||
        node.arity < 0 || node.arity > kPhaseMaxArity) return false;
    // Every operand but a constant's table index is static.
    if (expression.kind != PhaseExpressionKind::Constant && node.operand != expression.node.operand) return false;
    sites[cursor++] = task.site;
    for (int i = node.arity - 1; i >= 0; --i) {
      if (pending_count >= 192) return false;
      pending[pending_count++] = {expression.children[i], phase_child_scope(expression, i, task.scope), {}};
    }
  }
  return cursor == count;
}

__host__ __device__ inline int phase_subtree_end(const PhaseTreeView& tree, int start) {
  if (!tree.nodes || start < 0 || start >= tree.node_count || tree.node_count > 1024) return -1;
  int pending = 1;
  for (int i = start; i < tree.node_count; ++i) {
    const int arity = tree.nodes[i].arity;
    if (arity < 0 || arity > kPhaseMaxArity) return -1;
    pending += arity - 1;
    if (!pending) return i + 1;
  }
  return -1;
}
__host__ __device__ inline int phase_node_depth(const PhaseTreeView& tree, int position) {
  if (!tree.nodes || position < 0 || position >= tree.node_count || tree.node_count > 1024) return -1;
  int pending[64]; int depth = 1; pending[0] = 1;
  for (int i = 0; i <= position; ++i) {
    while (depth && !pending[depth - 1]) --depth;
    if (!depth) return -1;
    if (i == position) return depth;
    --pending[depth - 1];
    const int arity = tree.nodes[i].arity;
    if (arity < 0 || arity > kPhaseMaxArity) return -1;
    if (arity) { if (depth == 64) return -1; pending[depth++] = arity; }
  }
  return -1;
}
__host__ __device__ inline int phase_tree_depth(const PhaseTreeNode* nodes, int count) {
  int pending[64]; int depth = 1, maximum = 1; pending[0] = 1;
  for (int i = 0; i < count; ++i) {
    while (depth && !pending[depth - 1]) --depth;
    if (!depth) return -1;
    if (depth > maximum) maximum = depth;
    --pending[depth - 1];
    const int arity = nodes[i].arity;
    if (arity < 0 || arity > kPhaseMaxArity) return -1;
    if (arity) {
      if (depth == 64) return -1;
      pending[depth++] = arity;
    }
  }
  while (depth && !pending[depth - 1]) --depth;
  return depth == 0 ? maximum : -1;
}

// Count only independently mutable grammar invocation roots. Literal nodes
// introduced inside a production and all lowering scaffolds carry no charge.
// Position returns incoming depth; the selected site's own charge is excluded.
__host__ __device__ inline int phase_site_depth(const PhaseTreeNode* nodes,
    const PhaseGeneSite* sites,int count,int position=-1) {
  if(!nodes || !sites || count<=0 || count>1024 || position>=count)return -1;
  int pending[64],incoming[64];int depth=1,maximum=0;pending[0]=1;incoming[0]=0;
  for(int i=0;i<count;++i) {
    while(depth && !pending[depth-1])--depth;
    if(!depth)return -1;
    if(i==position)return incoming[depth-1];
    const int arity=nodes[i].arity;if(arity<0 || arity>kPhaseMaxArity)return -1;
    const int value=incoming[depth-1]+(sites[i].nonterminal>=0?1:0);
    maximum=value>maximum?value:maximum;--pending[depth-1];
    if(arity) {if(depth==64)return -1;pending[depth]=arity;incoming[depth++]=value;}
  }
  while(depth && !pending[depth-1])--depth;
  return depth?-1:maximum;
}

// Output must not alias either immutable input. The run owner supplies distinct
// generation buffers; this primitive never retains pointers across calls.
// Same type alone is insufficient: nonterminal and lexical scope must agree.
template<bool FusedDepth = false>
__host__ __device__ inline PhaseGenerationResult splice_phase_tree(const PhaseTreeView& base,
    int destination, const PhaseTreeView& donor, int source,
    MutablePhaseTree output, int max_depth, int site_depth=0) {
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
  int pending[64], depth = 1, maximum = 1;
  if constexpr (FusedDepth) pending[0] = 1;
  for (int i = 0; i < length; ++i) {
    const bool incoming = i >= destination && i < destination + donor_end - source;
    const auto& tree = incoming ? donor : base;
    const int index = incoming ? source + i - destination : i < destination ? i : i + (end - destination) - (donor_end - source);
    auto node = tree.nodes[index];
    if constexpr (FusedDepth) {
      while (depth && !pending[depth - 1]) --depth;
      if (!depth || node.arity < 0 || node.arity > kPhaseMaxArity) return result;
      maximum = depth > maximum ? depth : maximum;
      --pending[depth - 1];
      if (node.arity) {
        if (depth == 64) return result;
        pending[depth++] = node.arity;
      }
    }
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
  if constexpr (FusedDepth) {
    while (depth && !pending[depth - 1]) --depth;
    if (depth) return result;
  } else {
    maximum = phase_tree_depth(output.nodes, length);
    if (maximum < 0) return result;
  }
  if (maximum > max_depth) { result.status = PhaseCompileStatus::Capacity; return result; }
  if(site_depth) {
    const int height=phase_site_depth(output.nodes,output.sites,length);
    if(height<0)return result;
    if(height>site_depth){result.status=PhaseCompileStatus::Capacity;return result;}
  }
  result.nodes = length; result.status = PhaseCompileStatus::Ok; return result;
}
#ifdef __CUDACC__
// Converged-warp variants used by warp-per-child GPU variation. Every lane holds identical
// arguments; loads/stores are lane-strided while arity/depth checks keep the
// original prefix order. Successful results are identical to the serial path;
// failures still return the same status (output contents are then unused).
__device__ inline int phase_subtree_end_warp(const PhaseTreeView& tree, int start) {
  if (!tree.nodes || start < 0 || start >= tree.node_count || tree.node_count > 1024) return -1;
  const unsigned full = 0xffffffffU; const int lane = threadIdx.x & 31;
  int pending = 1;
  for (int chunk = start; chunk < tree.node_count; chunk += 32) {
    const int i = chunk + lane; const bool live = i < tree.node_count;
    const int arity = live ? tree.nodes[i].arity : 1;
    const bool invalid = live && (arity < 0 || arity > kPhaseMaxArity);
    int scan = live ? arity - 1 : 0;
    for (int offset = 1; offset < 32; offset <<= 1) { const int up = __shfl_up_sync(full, scan, offset); if (lane >= offset) scan += up; }
    const bool hit = live && !invalid && pending + scan == 0;
    const unsigned events = __ballot_sync(full, invalid || hit);
    if (events) { const int j = __ffs(events) - 1; return __shfl_sync(full, invalid, j) ? -1 : chunk + j + 1; }
    pending += __shfl_sync(full, scan, 31);
  }
  return -1;
}
__device__ inline PhaseGenerationResult splice_phase_tree_warp(const PhaseTreeView& base,
    int destination, const PhaseTreeView& donor, int source,
    MutablePhaseTree output, int max_depth, int site_depth=0) {
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
  const int end = phase_subtree_end_warp(base, destination), donor_end = phase_subtree_end_warp(donor, source);
  if (end < 0 || donor_end < 0) return result;
  const int length = base.node_count - (end - destination) + donor_end - source;
  if (length > output.capacity) { result.status = PhaseCompileStatus::Capacity; return result; }
  const unsigned full = 0xffffffffU; const int lane = threadIdx.x & 31;
  int pending[64], depth = 1, maximum = 1; pending[0] = 1;
  for (int chunk = 0; chunk < length; chunk += 32) {
    const int i = chunk + lane; const bool live = i < length;
    const bool incoming = i >= destination && i < destination + donor_end - source;
    const auto& tree = incoming ? donor : base;
    const int index = incoming ? source + i - destination : i < destination ? i : i + (end - destination) - (donor_end - source);
    PhaseTreeNode node{}; node.arity = 0;
    if (live) node = tree.nodes[index];
    const int lanes = min(32, length - chunk);
    bool bad = false;
    for (int j = 0; j < lanes; ++j) {
      const int arity = __shfl_sync(full, node.arity, j);
      while (depth && !pending[depth - 1]) --depth;
      if (!depth || arity < 0 || arity > kPhaseMaxArity) { bad = true; break; }
      maximum = depth > maximum ? depth : maximum;
      --pending[depth - 1];
      if (arity) { if (depth == 64) { bad = true; break; } pending[depth++] = arity; }
    }
    if (bad) return result;
    const bool constant = live && node.kind == PhaseTreeKind::Constant;
    if (__any_sync(full, constant && (node.operand < 0 || node.operand >= tree.constant_count))) return result;
    const unsigned constants = __ballot_sync(full, constant);
    if (constant) {
      const int slot = result.constants + __popc(constants & ((1U << lane) - 1U));
      output.constants[slot] = tree.constants[node.operand];
      node.operand = slot;
    }
    result.constants += __popc(constants);
    if (live) { output.nodes[i] = node; output.origins[i] = tree.origins[index]; output.sites[i] = tree.sites[index]; }
  }
  __syncwarp();
  output.sites[destination] = a;
  __syncwarp();
  while (depth && !pending[depth - 1]) --depth;
  if (depth) return result;
  if (maximum > max_depth) { result.status = PhaseCompileStatus::Capacity; return result; }
  if(site_depth) {
    const int height=phase_site_depth(output.nodes,output.sites,length);
    if(height<0)return result;
    if(height>site_depth){result.status=PhaseCompileStatus::Capacity;return result;}
  }
  result.nodes = length; result.status = PhaseCompileStatus::Ok; return result;
}
#endif
} // namespace gagp::evo::repro
