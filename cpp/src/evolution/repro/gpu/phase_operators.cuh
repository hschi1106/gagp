#pragma once
#include "device/phase_variation.cuh"
#include <cstddef>
#include <cstdint>

// Per-child selection, variation and lowering of the native phase population.
// These bodies are shared verbatim by the CUDA kernels and the CPU reference
// owner, so both engines consume the same random draws in the same order and
// publish identical genotypes. Warp=true variants require one converged warp
// redundantly executing a child; only memory traffic and scans are spread over
// lanes, never the order of random draws or floating-point accumulation.
namespace gagp::evo::repro::phase_ops {
constexpr int capacity = 1024, code_capacity = 4*capacity;

struct TreePointers {
 PhaseTreeNode* nodes;int* origins;PhaseGeneSite* sites;Value* constants;PhaseGenerationResult* lengths;
 __host__ __device__ PhaseTreeView view(int row)const {return {nodes+std::size_t(row)*capacity,origins+std::size_t(row)*capacity,sites+std::size_t(row)*capacity,
   constants+std::size_t(row)*capacity,lengths[row].nodes,lengths[row].constants};}
 __host__ __device__ MutablePhaseTree output(int row)const{return {nodes+std::size_t(row)*capacity,origins+std::size_t(row)*capacity,
   sites+std::size_t(row)*capacity,constants+std::size_t(row)*capacity,capacity};}
};
// `scope` is the generation scope of the phase nonterminal; `locals` are the
// lowering locals (the phase binders, or the program inputs in whole-program mode).
struct DeviceSlot {PhaseGrammarView grammar;int root,scope,max_depth,logical_depth;ValueTag type;int local_count;ValueTag locals[64];};
struct OperatorStats {int parent=-1,crossover_attempted=0,crossover=0,crossover_rejected=0,mutation=0,changed=0,error=0,reusable_parent=-1;};
// Semantic operator policy (changes the search) plus the exact copy schedule.
struct OperatorPolicy {
 bool counted_sites=false;       // counted rank draw instead of reservoir draws
 bool uniform_phase=false;       // draw one variable phase before its sites
 double crossover_probability=1; // 1 consumes no gate draw
 int crossover_attempts=8;
 int mutation_max_nodes=0;      // 0: generation bounded only by phase capacity
 bool deferred_copy=false;       // exact: copy parent phases at publication
};
struct Site {int row=-1,node=-1;};

__host__ __device__ inline bool valid(const PhaseGenerationResult& r){return r.status==PhaseCompileStatus::Ok && r.nodes>0 && r.nodes<=capacity && r.constants>=0 && r.constants<=r.nodes;}
__host__ __device__ inline void copy_tree(TreePointers from,int source,TreePointers to,int target) {
 const auto r=from.lengths[source];to.lengths[target]={};if(!valid(r))return;
 const auto a=from.view(source);const auto b=to.output(target);
 for(int i=0;i<r.nodes;++i){b.nodes[i]=a.nodes[i];b.origins[i]=a.origins[i];b.sites[i]=a.sites[i];}
 for(int i=0;i<r.constants;++i)b.constants[i]=a.constants[i];
 to.lengths[target]=r;
}
__host__ __device__ inline int tournament(const double* fitness,int population,int pressure,DGrammarRandom& random) {
 int best=random.bounded(population);
 for(int i=1;i<pressure;++i){const int candidate=random.bounded(population);if(fitness[candidate]>fitness[best])best=candidate;}
 return best;
}
__host__ __device__ inline bool site_matches(const PhaseTreeView& tree,int i,int nt,int scope) {
 return tree.sites[i].nonterminal>=0 && !(tree.sites[i].scope&kPhaseFixedSite) &&
     (nt<0 || (tree.sites[i].nonterminal==nt && tree.sites[i].scope==scope));
}
__host__ __device__ inline Site choose_site(TreePointers trees,int program,int slots,DGrammarRandom& random,bool counted,int nt=-1,int scope=-1,int selected_slot=-1) {
 if(selected_slot < -1 || selected_slot>=slots)return {};
 const int first=selected_slot<0?0:selected_slot,last=selected_slot<0?slots:selected_slot+1;
 if(counted) {
  unsigned count=0;
  for(int k=first;k<last;++k) {
   const int row=program*slots+k;if(!valid(trees.lengths[row]))return {};
   const auto tree=trees.view(row);
   for(int i=0;i<tree.node_count;++i)if(site_matches(tree,i,nt,scope))++count;
  }
  if(!count)return {};
  unsigned rank=random.bounded(count);
  for(int k=first;k<last;++k) {
   const int row=program*slots+k;const auto tree=trees.view(row);
   for(int i=0;i<tree.node_count;++i)if(site_matches(tree,i,nt,scope)) {
     if(rank==0)return {row,i};--rank;
   }
  }
  return {};
 }
 Site selected;unsigned count=0;
 for(int k=first;k<last;++k) {
  const int row=program*slots+k;if(!valid(trees.lengths[row]))return {};
  const auto tree=trees.view(row);
  for(int i=0;i<tree.node_count;++i)if(site_matches(tree,i,nt,scope))
    if(random.bounded(++count)==0)selected={row,i};
 }
 return selected;
}

#ifdef __CUDA_ARCH__
__device__ inline void copy_tree_warp(TreePointers from,int source,TreePointers to,int target) {
 const int lane=threadIdx.x&31;const auto r=from.lengths[source];__syncwarp();if(lane==0)to.lengths[target]={};
 if(!valid(r)){__syncwarp();return;}
 const auto a=from.view(source);const auto b=to.output(target);
 for(int i=lane;i<r.nodes;i+=32){b.nodes[i]=a.nodes[i];b.origins[i]=a.origins[i];b.sites[i]=a.sites[i];}
 for(int i=lane;i<r.constants;i+=32)b.constants[i]=a.constants[i];
 __syncwarp();if(lane==0)to.lengths[target]=r;__syncwarp();
}
// Same count, the same single bounded draw and the same prefix-ordered rank.
__device__ inline Site choose_counted_site_warp(TreePointers trees,int program,int slots,DGrammarRandom& random,int nt,int scope,int selected_slot) {
 if(selected_slot < -1 || selected_slot>=slots)return {};
 const unsigned full=0xffffffffU;const int lane=threadIdx.x&31;
 const int first=selected_slot<0?0:selected_slot,last=selected_slot<0?slots:selected_slot+1;
 unsigned count=0;
 for(int k=first;k<last;++k) {
  const int row=program*slots+k;if(!valid(trees.lengths[row]))return {};
  const auto tree=trees.view(row);
  for(int chunk=0;chunk<tree.node_count;chunk+=32)
   count+=__popc(__ballot_sync(full,chunk+lane<tree.node_count && site_matches(tree,chunk+lane,nt,scope)));
 }
 if(!count)return {};
 unsigned rank=random.bounded(count);
 for(int k=first;k<last;++k) {
  const int row=program*slots+k;const auto tree=trees.view(row);
  for(int chunk=0;chunk<tree.node_count;chunk+=32) {
   unsigned mask=__ballot_sync(full,chunk+lane<tree.node_count && site_matches(tree,chunk+lane,nt,scope));const unsigned c=__popc(mask);
   if(rank<c){for(unsigned j=0;j<rank;++j)mask&=mask-1;return {row,chunk+__ffs(mask)-1};}
   rank-=c;
  }
 }
 return {};
}
#endif

// Engine dispatch: warp variants exist only in device code.
template<bool Warp> __host__ __device__ inline void copy_row(TreePointers from,int source,TreePointers to,int target) {
#ifdef __CUDA_ARCH__
 if constexpr(Warp){copy_tree_warp(from,source,to,target);return;}
#endif
 copy_tree(from,source,to,target);
}
template<bool Warp> __host__ __device__ inline Site draw_site(TreePointers trees,int program,int slots,DGrammarRandom& random,bool counted,int nt,int scope,int selected_slot) {
#ifdef __CUDA_ARCH__
 if constexpr(Warp) if(counted)return choose_counted_site_warp(trees,program,slots,random,nt,scope,selected_slot);
#endif
 return choose_site(trees,program,slots,random,counted,nt,scope,selected_slot);
}
template<bool Warp> __host__ __device__ inline int subtree_end(const PhaseTreeView& tree,int start) {
#ifdef __CUDA_ARCH__
 if constexpr(Warp)return phase_subtree_end_warp(tree,start);
#endif
 return phase_subtree_end(tree,start);
}
template<bool Warp> __host__ __device__ inline PhaseGenerationResult splice(const PhaseTreeView& base,int destination,
    const PhaseTreeView& donor,int source,MutablePhaseTree output,int max_depth,const PhaseGrammarView* projected,int logical_depth) {
#ifdef __CUDA_ARCH__
 if constexpr(Warp)return splice_phase_tree_warp(base,destination,donor,source,output,max_depth,projected,logical_depth);
#endif
 return splice_phase_tree<true>(base,destination,donor,source,output,max_depth,projected,logical_depth);
}

// A statement program must read only definitely assigned grammar locals.
template<bool Warp> __host__ __device__ inline bool locals_defined(const DeviceSlot& slot,const PhaseTreeNode* nodes,int count) {
 if(slot.type!=kPhaseStatement)return true;
#ifdef __CUDA_ARCH__
 if constexpr(Warp)__syncwarp();
#endif
 return phase_locals_defined(nodes,count);
}

template<bool Warp=false>
__host__ __device__ inline void crossover_child(int child,TreePointers input,TreePointers output,const DeviceSlot* slots,
    int slot_count,int population,int max_variable_nodes,const double* fitness,int pressure,std::uint64_t seed,
    const OperatorPolicy& policy,OperatorStats& stat) {
 DGrammarRandom random(seed ^ (UINT64_C(0x617089e168) + child));stat={};
 const int parent=tournament(fitness,population,pressure,random),donor=tournament(fitness,population,pressure,random);stat.parent=parent;
 int total=0;
 for(int k=0;k<slot_count;++k) {
  const int row=parent*slot_count+k;
  if(!valid(input.lengths[row])){stat.error=1;return;}
  total+=input.lengths[row].nodes;if(!policy.deferred_copy)copy_row<Warp>(input,row,output,child*slot_count+k);
 }
 // A gated skip publishes the selected parent before the ordinary mutation pass.
 if(policy.crossover_probability<1 && static_cast<double>(random.next()>>11)*0x1.0p-53>=policy.crossover_probability) {
  if(policy.deferred_copy)for(int k=0;k<slot_count;++k)copy_row<Warp>(input,parent*slot_count+k,output,child*slot_count+k);
  return;
 }
 stat.crossover_attempted=1;
 // Choose once per operator, not once per speculative retry.
 const int selected_slot=policy.uniform_phase?int(random.bounded(slot_count)):-1;
 // Each rejected attempt is explicit; fitness candidates and evaluation stay intact.
 for(int attempt=0;attempt<policy.crossover_attempts;++attempt) {
  // Only variation-disabled sites: the operator publishes the parent.
  const auto destination=draw_site<Warp>(input,parent,slot_count,random,policy.counted_sites,-1,-1,selected_slot);if(destination.node<0)break;
  const auto a=input.view(destination.row);const auto contract=a.sites[destination.node];
  const auto source=draw_site<Warp>(input,donor,slot_count,random,policy.counted_sites,contract.nonterminal,contract.scope,selected_slot);
  if(source.node<0){++stat.crossover_rejected;continue;}
  const auto b=input.view(source.row);const int ae=subtree_end<Warp>(a,destination.node),be=subtree_end<Warp>(b,source.node);
  if(ae<0 || be<0){stat.error=3;return;}
  if(total-(ae-destination.node)+(be-source.node)>max_variable_nodes){++stat.crossover_rejected;continue;}
  const int slot=destination.row%slot_count,target=child*slot_count+slot;
  const DeviceSlot s=slots[slot];
  const auto r=splice<Warp>(a,destination.node,b,source.node,output.output(target),s.max_depth,s.logical_depth?&s.grammar:nullptr,s.logical_depth);
  if(r.status==PhaseCompileStatus::Ok && locals_defined<Warp>(s,output.output(target).nodes,r.nodes)){
   output.lengths[target]=r;stat.crossover=1;
   if(policy.deferred_copy)for(int k=0;k<slot_count;++k)if(k!=slot)copy_row<Warp>(input,parent*slot_count+k,output,child*slot_count+k);
   return;
  }
  // Failed speculation may partially write output. Eager copies restore this
  // phase now; deferred copies restore every nonselected phase at publication.
  if(!policy.deferred_copy)copy_row<Warp>(input,destination.row,output,target);++stat.crossover_rejected;
 }
 if(policy.deferred_copy)for(int k=0;k<slot_count;++k)copy_row<Warp>(input,parent*slot_count+k,output,child*slot_count+k);
}

// Mutates the crossed rows in place; `output` holds one spliced row per child.
template<bool Warp=false>
__host__ __device__ inline void mutate_child(int child,TreePointers input,TreePointers output,TreePointers scratch,const DeviceSlot* slots,
    int slot_count,int max_variable_nodes,double probability,std::uint64_t seed,const OperatorPolicy& policy,OperatorStats& stat) {
 if(stat.error)return;
 int total=0;
 for(int k=0;k<slot_count;++k){const int row=child*slot_count+k;if(!valid(input.lengths[row])){stat.error=4;return;}total+=input.lengths[row].nodes;}
 DGrammarRandom random(seed ^ (UINT64_C(0x124ab512bf01)+child));
 if(static_cast<double>(random.next()>>11)*0x1.0p-53>=probability)return;
 const int selected_slot=policy.uniform_phase?int(random.bounded(slot_count)):-1;
 const auto destination=draw_site<Warp>(input,child,slot_count,random,policy.counted_sites,-1,-1,selected_slot);if(destination.node<0)return;
 const auto a=input.view(destination.row);const int end=subtree_end<Warp>(a,destination.node),depth=phase_node_depth(a,destination.node);
 if(end<0 || depth<0){stat.error=6;return;}
 // A private slot copy keeps grammar table pointers out of aliased memory.
 const auto site=a.sites[destination.node];const DeviceSlot slot=slots[destination.row%slot_count];auto tmp=scratch.output(child);
 const int incoming=slot.logical_depth?phase_projected_depth(slot.grammar,a.nodes,a.origins,a.node_count,destination.node,a.sites):0;
 if(incoming<0 || incoming>slot.logical_depth){stat.error=6;return;}
 const int logical=slot.logical_depth-incoming;
 // A larger whole-program budget does not enlarge the destination phase buffer.
 const int replaced=end-destination.node;
 const int whole=max_variable_nodes-(total-replaced),phase=capacity-(a.node_count-replaced);
 const int available=whole<phase?whole:phase,uncapped=available<capacity?available:capacity;
 const bool capped=policy.mutation_max_nodes>0 && policy.mutation_max_nodes<uncapped;
 const int bounded=capped?policy.mutation_max_nodes:uncapped;
 const auto generated=generate_phase<true,true,Warp>(slot.grammar,site.entry_expression,site.parent_scope,slot.max_depth-depth+1,
     bounded,random.next(),tmp.nodes,tmp.origins,tmp.constants,tmp.sites,logical);
 // A configured cap that leaves no feasible derivation skips this mutation.
 if(capped && generated.status==PhaseCompileStatus::Capacity)return;
 if(generated.status!=PhaseCompileStatus::Ok){stat.error=7;return;}
 scratch.lengths[child]=generated;
#ifdef __CUDA_ARCH__
 if constexpr(Warp)__syncwarp();
#endif
 // Splicing may partially write on failure, so its destination must never
 // alias the crossed input. Each child owns a separate temporary row.
 const int target=child;
 const auto r=splice<Warp>(a,destination.node,scratch.view(child),0,output.output(target),slot.max_depth,
     slot.logical_depth?&slot.grammar:nullptr,slot.logical_depth);
 if(r.status!=PhaseCompileStatus::Ok){stat.error=8;return;}
 // An undefined local read rejects this mutation; the crossed child stands.
 if(!locals_defined<Warp>(slot,output.output(target).nodes,r.nodes))return;
 output.lengths[target]=r;
 copy_row<Warp>(output,target,input,destination.row);
 stat.mutation=1;
}

__host__ __device__ inline void compare_child(int child,TreePointers parents,TreePointers children,int population,int slot_count,OperatorStats& stat) {
 if(stat.error)return;
 const int parent=stat.parent;if(parent<0 || parent>=population){stat.error=9;return;}
 bool equal=true,reusable=true;
 for(int k=0;k<slot_count && equal;++k) {
  const auto a=parents.view(parent*slot_count+k),b=children.view(child*slot_count+k);
  if(a.node_count!=b.node_count || a.constant_count!=b.constant_count){equal=false;break;}
  for(int i=0;i<a.node_count && equal;++i) {
   const auto x=a.nodes[i],y=b.nodes[i];equal=x.kind==y.kind && x.opcode==y.opcode && x.operand==y.operand &&
      x.arity==y.arity && x.fuel==y.fuel && x.merge_fuel==y.merge_fuel && a.origins[i]==b.origins[i];
  }
  for(int i=0;i<a.constant_count && equal;++i){const auto x=a.constants[i],y=b.constants[i];
   equal=x.tag==y.tag && (x.tag==ValueTag::Bool?x.b==y.b:x.i==y.i);
   reusable=reusable && x.i==y.i && x.b==y.b;
  }
 }
 stat.changed=!equal;
 stat.reusable_parent=equal && reusable?parent:-1;
}

__host__ __device__ inline PhaseCompileResult compile_row(TreePointers trees,const DeviceSlot* slots,int slot_count,int row,
    gpu_detail::DInstr* code,Value* constants) {
 const auto t=trees.view(row);const auto& slot=slots[row%slot_count];
 if(!valid(trees.lengths[row]))return {};
 return compile_phase_tree<true,true>(t.nodes,t.node_count,t.constants,t.constant_count,slot.locals,slot.local_count,slot.type,
     code+std::size_t(row)*code_capacity,code_capacity,constants+std::size_t(row)*capacity,capacity);
}
// Rank-based elite rule: highest fitness, then lowest source index.
inline int elite_rank(const double* fitness,int population,int candidate) {
 int rank=0;
 for(int i=0;i<population;++i)
  if(fitness[i]>fitness[candidate] || (fitness[i]==fitness[candidate] && i<candidate))++rank;
 return rank;
}
} // namespace gagp::evo::repro::phase_ops
