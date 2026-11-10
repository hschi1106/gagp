#pragma once
#include "gagp/runtime/gpu/device_types_gpu.hpp"
#include "gagp/core/builtin.hpp"
#include <type_traits>

namespace gagp::evo::repro {
// Lowering prototype only: this proves bounded, typed phase bytecode. It does
// NOT establish grammar membership. Callers must separately admit the source.
// Statement kinds (whole-program phases only): Program and Block sequence
// their children, Nil is an empty block, Assign/ForRange name a variable by
// operand (>=0 grammar local, <0 preset input slot -1-operand), Var reads a
// grammar local. If also lowers if_stmt with statement branches.
// Let (value, body) binds lexical index `operand` for its body; Bound reads a
// lexical index: the innermost active Let binder, otherwise binder local
// `operand` of a region phase.
// Region (operands..., phases...) is a bounded region of grammar region table
// entry `operand`: operands lower in place, each phase lowers as a separate
// program whose locals start with the phase bindings (see PhaseRegionInfo).
// Traverse (source, start, [begin, end,] initial, body) is the native
// traversal primitive of region table entry `operand` (see PhaseRegionInfo).
enum class PhaseTreeKind : int { Constant, Local, Operation, If, And, Or, Program, Block, Nil, Assign, Return, ForRange, Var, Let, Bound, Region, Traverse };
// Completion tag of a statement; never a runtime value tag.
constexpr ValueTag kPhaseStatement = static_cast<ValueTag>(0xF0);
constexpr int kPhaseNamedLocals = 16;
// Open traversals, lets, loops and analysed value ifs on one lowering path.
constexpr int kPhaseWalks = 8, kPhaseLets = 16, kPhaseLoops = 16, kPhaseIfs = 16;
// Widest node: a bounded region with its operands and phases.
constexpr int kPhaseMaxArity = 8;
// Bounded regions per lowered program and the lowering record marker below.
constexpr int kPhaseMaxRegions = 4;
constexpr std::uint8_t kPhaseRegionRecord = 0xFF;
// Lexical binder numbers of a whole-program phase (Let, Traverse and Bound
// operands); a region phase numbers its own bindings from zero.
constexpr int kPhaseBinders = 16;
// Source fuel events, numbered as the native FuelEvent enumeration.
enum class PhaseFuelEvent : int {
  Operation, Bind, BranchTest, BranchMerge, StoreSequence, StoreStart, StoreBegin, StoreEnd,
  CheckStart, CheckBegin, CheckEnd, ObserveSequence, ClampBegin, ClampEnd, SetBegin, SetEnd,
  InitializeState, InitializeCursor, TestCursor, ReadElement, BindElement, ComputeIndex,
  UpdateState, AdvanceCursor, Repeat, Result, Count
};
enum class PhaseCaptureKind : int { Input, Local, Bound };
// Static shape of one grammar bounded region. Captures name an evaluator input
// position, a grammar local, or a binder number; phase p sees phase_bindings[p]
// binder locals typed binding_types[p][...] and yields phase_type[p].
// A traversal entry (traversal != 0) instead types its children by
// operand_types, its element/index/state bindings by binding_types[0] and its
// result by phase_type[0]; the body binds numbers binders[0..2]. With a source
// fuel profile, each lowering event charges event_cost on its first
// instruction and nothing on the rest; without one every instruction costs 1.
struct PhaseRegionInfo {
  int traversal = 0;  // 0: bounded region, 1: forward, 2: reverse traversal
  bool ranged = false, fuel_profile = false;
  int binders[3]{};
  unsigned event_cost[static_cast<int>(PhaseFuelEvent::Count)]{};
  int operands = 0, phases = 0, captures = 0;
  ValueTag operand_types[kPhaseMaxArity]{};
  PhaseCaptureKind capture_kind[kPhaseMaxArity]{};
  int capture_target[kPhaseMaxArity]{};
  int phase_bindings[kPhaseMaxArity]{};
  ValueTag phase_type[kPhaseMaxArity]{};
  ValueTag binding_types[kPhaseMaxArity][kPhaseMaxArity]{};
};
struct PhaseTreeNode {
  PhaseTreeKind kind = PhaseTreeKind::Constant;
  Opcode opcode = Opcode::PushConst;
  int operand = 0;
  int arity = 0;
  unsigned fuel = 1;
  unsigned merge_fuel = 1;
  // Grammar result type of the producing expression. It is a function of the
  // node's origin (which genotype equality compares), so it is never hashed.
  ValueTag type = ValueTag::Invalid;
};
enum class PhaseCompileStatus : int { Ok, Invalid, Unsupported, Capacity };
struct PhaseCompileResult {
  PhaseCompileStatus status = PhaseCompileStatus::Invalid;
  int code_count = 0;
  int constant_count = 0;
  int stack_bound = 0;
  ValueTag result_type = ValueTag::Invalid;
  // Scalar/view operations and forward typed branches prove view execution.
  // Allocating list operations and empty list literals explicitly clear this
  // proof and use the ordinary payload-aware GPU executor.
  bool typed_views_proven = false;
  // The complete operation_type whitelist excludes container equality. This
  // certificate is published only after every source node and type was checked.
  bool private_intlists_proven = false;
  // Final local count, and 1+slot of each grammar local (0: never used),
  // allocated in the native compiler's first-use order.
  int local_count = 0;
  std::int8_t named_slots[kPhaseNamedLocals] = {};
  // A lexical binder or checked conversion was lowered: like the native
  // compiler, the program keeps its per-instruction fuel table even when unit.
  bool general_regions = false;
  // With bounded regions, code_count/constant_count also cover the region
  // records that follow the main code (main_* counts): per region, in
  // completion order, a header {a: region table entry, b: phases, fuel:
  // captures}, one {a: caller local} per capture, then per phase a header
  // {a: code, b: constants, fuel: locals | 1<<16 if general} and its code.
  // Phase constants follow the main constants in the same order.
  int region_count = 0, main_code_count = 0, main_constant_count = 0;
};
namespace phase_compile_detail {
using gpu_detail::DInstr;
struct Output {
  DInstr* code;
  Value* constants;
  int code_capacity, constant_capacity;
  PhaseCompileResult result;
  int stack = 0;
  __host__ __device__ bool emit(Opcode op, unsigned fuel, int a=0, int b=0, unsigned flags=0) {
    if (result.code_count >= code_capacity) { result.status=PhaseCompileStatus::Capacity; return false; }
    if (fuel > 0x7fffffffU) { result.status=PhaseCompileStatus::Invalid; return false; }
    code[result.code_count++] = {static_cast<std::uint8_t>(op),static_cast<std::uint8_t>(flags),a,b,fuel};
    return true;
  }
  __host__ __device__ int add(Value value) {
    if (result.constant_count >= constant_capacity) { result.status=PhaseCompileStatus::Capacity; return -1; }
    constants[result.constant_count]=value;
    return result.constant_count++;
  }
  __host__ __device__ void grow(int by) { stack+=by; if (stack>result.stack_bound) result.stack_bound=stack; }
  __host__ __device__ bool push(Value value, unsigned fuel) {
    const int index=add(value);
    if (index<0) return false;
    if (!emit(Opcode::PushConst,fuel,index,0,gpu_detail::DINSTR_HAS_A)) return false;
    ++stack; if (stack>result.stack_bound) result.stack_bound=stack;
    return true;
  }
};
// Types the typed-view and private-IntList evaluator certificates cover.
__host__ __device__ inline bool certified_type(ValueTag tag) {
  return tag==ValueTag::Int || tag==ValueTag::Bool || tag==ValueTag::IntList;
}
__host__ __device__ inline ValueTag operation_type(const PhaseTreeNode& node, const ValueTag* args) {
  const auto integer=ValueTag::Int, boolean=ValueTag::Bool, list=ValueTag::IntList;
  if (node.opcode==Opcode::Neg || node.opcode==Opcode::CheckInt)
    return node.arity==1 && args[0]==integer ? integer : ValueTag::Invalid;
  if (node.opcode==Opcode::Not)
    return node.arity==1 && args[0]==boolean ? boolean : ValueTag::Invalid;
  if (node.opcode==Opcode::CheckList)
    return node.arity==1 && args[0]==list ? list : ValueTag::Invalid;
  switch (node.opcode) {
    case Opcode::Add: case Opcode::Sub: case Opcode::Mul: case Opcode::Mod:
      return node.arity==2 && args[0]==integer && args[1]==integer ? integer : ValueTag::Invalid;
    case Opcode::Lt: case Opcode::Le: case Opcode::Gt: case Opcode::Ge:
      return node.arity==2 && args[0]==integer && args[1]==integer ? boolean : ValueTag::Invalid;
    case Opcode::Eq: case Opcode::Ne:
      return node.arity==2 && args[0]==args[1] && (args[0]==integer || args[0]==boolean) ? boolean : ValueTag::Invalid;
    case Opcode::CallBuiltin: break;
    default: return ValueTag::Invalid;
  }
  switch (static_cast<BuiltinId>(node.operand)) {
    case BuiltinId::TruncDiv0: case BuiltinId::TruncMod0: case BuiltinId::IDiv0: case BuiltinId::IMod0: case BuiltinId::Min: case BuiltinId::Max:
      return node.arity==2 && args[0]==integer && args[1]==integer ? integer : ValueTag::Invalid;
    case BuiltinId::Clip:
      return node.arity==3 && args[0]==integer && args[1]==integer && args[2]==integer ? integer : ValueTag::Invalid;
    case BuiltinId::IsLetter: return node.arity==1 && args[0]==integer ? boolean : ValueTag::Invalid;
    case BuiltinId::Singleton: return node.arity==1 && args[0]==integer ? list : ValueTag::Invalid;
    case BuiltinId::Concat: return node.arity==2 && args[0]==list && args[1]==list ? list : ValueTag::Invalid;
    case BuiltinId::Len: return node.arity==1 && args[0]==list ? integer : ValueTag::Invalid;
    case BuiltinId::IndexClampedOrZero: case BuiltinId::IndexNonnegative: case BuiltinId::Index: return node.arity==2 && args[0]==list && args[1]==integer ? integer : ValueTag::Invalid;
    case BuiltinId::Slice: return node.arity==3 && args[0]==list && args[1]==integer && args[2]==integer ? list : ValueTag::Invalid;
    default: return ValueTag::Invalid;
  }
}
}  // namespace phase_compile_detail

// Native definite assignment (ast_type_verify): a grammar local is readable
// after an Assign, inside a ForRange body naming it, and after an if_stmt only
// when both branches assigned it; loop bodies never define it afterwards.
// Inputs are always defined. Variation rejects children that fail this check.
__host__ __device__ inline bool phase_locals_defined(const PhaseTreeNode* nodes, int count) {
  struct Frame { int index, seen; unsigned saved, branch; };
  Frame frames[64]; int depth=0; unsigned mask=0;
  const auto bit=[](int operand) { return operand>=0 && operand<kPhaseNamedLocals ? 1U<<operand : 0U; };
  for (int i=0; i<count; ++i) {
    const auto node=nodes[i];
    if (node.kind==PhaseTreeKind::Var && !(mask & bit(node.operand))) return false;
    if (node.arity>0) {
      if (depth==64) return false;
      frames[depth++]={i,0,mask,0}; continue;
    }
    while (depth) {
      auto& frame=frames[depth-1]; const auto owner=nodes[frame.index]; ++frame.seen;
      if (owner.kind==PhaseTreeKind::ForRange && frame.seen==1) { frame.saved=mask; mask|=bit(owner.operand); }
      else if (owner.kind==PhaseTreeKind::If && owner.type==kPhaseStatement) {
        if (frame.seen==1) frame.saved=mask;
        else if (frame.seen==2) { frame.branch=mask; mask=frame.saved; }
        else mask&=frame.branch;
      }
      if (frame.seen<owner.arity) break;
      if (owner.kind==PhaseTreeKind::Assign) mask|=bit(owner.operand);
      else if (owner.kind==PhaseTreeKind::ForRange) mask=frame.saved;
      --depth;
    }
  }
  return depth==0;
}

// One thread per phase. All cursors/output writes are capacity checked; depth is
// bounded independently of input metadata. Invalid/unsupported output is never
// executable. No device allocation, recursive C++ call, or run-time JIT occurs.
// Regions=false lowers one region phase (no nested regions); the Regions=true
// instantiation calls it once per phase, so no recursive call arises.
template<bool SharedNodes = false, bool CompactFrames = false, bool Regions = true>
__host__ __device__ inline PhaseCompileResult compile_phase_tree(
    const PhaseTreeNode* nodes, int node_count,
    const Value* constants, int constant_count,
    const ValueTag* local_types, int local_count, ValueTag expected,
    gpu_detail::DInstr* code, int code_capacity, Value* output_constants, int constant_capacity,
    const PhaseRegionInfo* regions = nullptr, int region_info_count = 0) {
  using namespace phase_compile_detail;
  static_assert(!CompactFrames || SharedNodes, "compact frames use immutable node indices");
  if constexpr (CompactFrames) {
    // The generic helper accepts larger code buffers. Preserve that contract
    // instead of narrowing it to the native owner's bounded compact indices.
    if (node_count > 4096 || code_capacity > 32767)
      return compile_phase_tree<SharedNodes, false, Regions>(nodes,node_count,constants,constant_count,
          local_types,local_count,expected,code,code_capacity,output_constants,constant_capacity,regions,region_info_count);
  }
  Output out{code,output_constants,code_capacity,constant_capacity,{}};
  if (expected==kPhaseStatement && nodes && node_count>0 && !phase_locals_defined(nodes,node_count)) return out.result;
  if (!nodes || !code || !output_constants || node_count<=0 || node_count>4096 ||
      constant_count<0 || (constant_count && !constants) || local_count<0 || local_count>64 ||
      (local_count && !local_types) || code_capacity<=0 || constant_capacity<=0) return out.result;
  struct CopiedNode { PhaseTreeNode node; };
  // Source nodes remain immutable for this lowering invocation. Compact frames
  // retain their indices, not copied nodes. At most4096 consumed source nodes
  // bound the stack values; signed jump fields retain the -1 unset sentinel.
  using FrameIndex = typename std::conditional<CompactFrames, std::int16_t, int>::type;
  using ChildCount = typename std::conditional<CompactFrames, std::uint8_t, int>::type;
  struct SharedNode { FrameIndex index; };
  using StoredNode = typename std::conditional<SharedNodes, SharedNode, CopiedNode>::type;
  struct Frame { StoredNode source; FrameIndex base_stack, first_jump, second_jump; ValueTag args[3]; ChildCount seen; };
  // Bounded regions in completion order (the native segment order): the
  // phase subtree bounds and each capture's caller local.
  struct RegionRecord { int info, captures[kPhaseMaxArity], bounds[kPhaseMaxArity+1]; };
  RegionRecord records[Regions ? kPhaseMaxRegions : 1]; int region_count=0;
  static_assert(!CompactFrames || sizeof(Frame)==12, "compact lowering frame layout");
  Frame frames[64]; int depth=0, cursor=0;
  ValueTag completed=ValueTag::Invalid;
  bool have_value=false,typed_views=true,private_intlists=true,general=false;
  // Statement lowering state, mirroring the native compiler: preset locals
  // first, grammar locals on first use, loop temporaries recycled LIFO once
  // their for_range finishes. Untouched by expression-only phases.
  int total_locals=local_count, live_count=0, free_count=0, loop_depth=0;
  unsigned named_mask=0; std::int8_t named[kPhaseNamedLocals], live[64], freed[64];
  struct Loop { int bound, counter, head, end_jump, one, live_begin; };
  Loop loops[kPhaseLoops];
  // Lexical let binders, mirroring the native compiler: the binder temporary
  // precedes the value expression, and every temporary allocated inside the
  // let is recycled LIFO once its body finishes.
  // Native traversal lowering analyses (compiler.cpp), keyed by binder number
  // instead of native binder identity: an Int-valued binder, a binder equal to
  // len(source)+offset, the binder holding len(source) and a source proven
  // nonempty in the current branch. Entering a binder saves and clears the
  // facts of its number, which a hidden outer binder may still own, and
  // leaving restores them, so the facts follow native identities exactly.
  // Only traversal lowering reads them; without one they are never computed.
  struct Facts { bool integer, positive; int length_source, length_offset, observed; };
  struct LetScope { int index, slot, live_begin, saved_slot, node, observed_source, saved_observed; ValueTag saved_type; Facts saved; };
  LetScope lets[kPhaseLets]; int let_depth=0;
  int env_slot[kPhaseBinders]; ValueTag env_type[kPhaseBinders]; Facts facts[kPhaseBinders];
  for (int i=0; i<kPhaseBinders; ++i) { env_slot[i]=-1; env_type[i]=ValueTag::Invalid; facts[i]={false,false,-1,0,-1}; }
  bool analyze=false;
  if constexpr (Regions) for (int r=0; regions && r<region_info_count; ++r) analyze|=regions[r].traversal!=0;
  struct IfFact { int frame, source; bool positive_then, already; };
  IfFact ifs[kPhaseIfs]; int if_count=0;
  struct TraverseScope {
    int info, live_begin, args[6], xs, start, begin, limit, length, counter, element, index, state, loop_head, done_jump;
    bool source_alias, start_alias, end_alias, length_alias, constant_begin, decrement;
    int saved_slot[3]; ValueTag saved_type[3]; Facts saved[3];
  };
  TraverseScope walks[kPhaseWalks]; int walk_depth=0;
  const auto fresh_local=[&]() -> int {
    if (total_locals>=64) { out.result.status=PhaseCompileStatus::Capacity; return -1; }
    return total_locals++;
  };
  const auto variable_slot=[&](int operand) -> int {
    if (operand<0) return -1-operand<local_count ? -1-operand : -1;
    if (operand>=kPhaseNamedLocals) return -1;
    if (!(named_mask>>operand & 1U)) {
      const int slot=fresh_local(); if (slot<0) return -1;
      named[operand]=static_cast<std::int8_t>(slot); named_mask|=1U<<operand;
    }
    return named[operand];
  };
  const auto temporary=[&]() -> int {
    const int slot=free_count ? freed[--free_count] : fresh_local();
    if (slot>=0) live[live_count++]=static_cast<std::int8_t>(slot);
    return slot;
  };
  // The operands are on the stack. Like the native compiler, resolve the
  // captures, set the phase subtrees aside and emit BoundedRegion.
  const auto finish_region=[&](const PhaseTreeNode& node) -> bool {
    if constexpr (!Regions) { out.result.status=PhaseCompileStatus::Unsupported; return false; }
    else {
      if (region_count==kPhaseMaxRegions) { out.result.status=PhaseCompileStatus::Capacity; return false; }
      const auto& info=regions[node.operand]; auto& record=records[region_count];
      record.info=node.operand;
      for (int c=0; c<info.captures; ++c) {
        const int target=info.capture_target[c]; int slot=-1;
        if (info.capture_kind[c]==PhaseCaptureKind::Input) slot=target>=0 ? variable_slot(-1-target) : -1;
        else if (info.capture_kind[c]==PhaseCaptureKind::Local) slot=variable_slot(target);
        else if (target>=0 && target<kPhaseBinders) slot=env_slot[target];
        if (slot<0) return false;
        record.captures[c]=slot;
      }
      for (int p=0; p<info.phases; ++p) {
        record.bounds[p]=cursor; int pending=1;
        while (pending) {
          if (cursor>=node_count) return false;
          const int arity=nodes[cursor++].arity;
          if (arity<0 || arity>kPhaseMaxArity) return false;
          pending+=arity-1;
        }
      }
      record.bounds[info.phases]=cursor;
      if (!out.emit(Opcode::BoundedRegion,node.fuel,region_count,0,gpu_detail::DINSTR_HAS_A)) return false;
      out.stack-=info.operands; out.grow(1); ++region_count;
      typed_views=private_intlists=false; completed=node.type;
      return true;
    }
  };
  const auto subtree_end=[&](int i) -> int {
    int pending=1;
    while (pending) {
      if (i<0 || i>=node_count) return -1;
      const int arity=nodes[i++].arity;
      if (arity<0 || arity>kPhaseMaxArity) return -1;
      pending+=arity-1;
    }
    return i;
  };
  // Binder number of a lexical read in the main code, else -1.
  const auto binder_at=[&](int i) -> int {
    if (i<0 || i>=node_count) return -1;
    const auto& n=nodes[i];
    return n.kind==PhaseTreeKind::Bound && n.operand>=0 && n.operand<kPhaseBinders && env_slot[n.operand]>=0 ? n.operand : -1;
  };
  const auto int_constant=[&](int i, std::int64_t& value) -> bool {
    if (i<0 || i>=node_count) return false;
    const auto& n=nodes[i];
    if (n.kind!=PhaseTreeKind::Constant || n.operand<0 || n.operand>=constant_count || constants[n.operand].tag!=ValueTag::Int) return false;
    value=constants[n.operand].i; return true;
  };
  const auto is_len=[&](int i) {
    const auto& n=nodes[i];
    return n.kind==PhaseTreeKind::Operation && n.opcode==Opcode::CallBuiltin && n.operand==int(BuiltinId::Len) && n.arity==1;
  };
  const auto is_add_sub=[&](int i) {
    const auto& n=nodes[i];
    return n.kind==PhaseTreeKind::Operation && (n.opcode==Opcode::Add || n.opcode==Opcode::Sub) && n.arity==2;
  };
  struct Length { int source, offset; };
  // Native length_offset: a binder, len(binder) or such a value plus/minus
  // small Int constants along the left spine.
  const auto length_offset=[&](int i, Length& result) -> bool {
    int spine[64]; int n=0;
    while (i>=0 && i<node_count && is_add_sub(i)) { if (n==64) return false; spine[n++]=i++; }
    Length relation{};
    const int binder=binder_at(i);
    if (binder>=0) {
      if (facts[binder].length_source<0) return false;
      relation={facts[binder].length_source,facts[binder].length_offset};
    } else if (i>=0 && i+1<node_count && is_len(i) && binder_at(i+1)>=0) relation={binder_at(i+1),0};
    else return false;
    while (n) {
      const int owner=spine[--n]; std::int64_t value;
      if (!int_constant(subtree_end(owner+1),value) || value<-65535 || value>65535) return false;
      const std::int64_t offset=relation.offset+(nodes[owner].opcode==Opcode::Add ? value : -value);
      if (offset<-65535 || offset>65535) return false;
      relation.offset=static_cast<int>(offset);
    }
    result=relation; return true;
  };
  const auto proven_integer=[&](int i) -> bool {
    int pending[64]; int n=0; pending[n++]=i;
    while (n) {
      const int j=pending[--n];
      if (j<0 || j>=node_count) return false;
      const auto& node=nodes[j]; std::int64_t value;
      if (node.kind==PhaseTreeKind::Operation && (node.opcode==Opcode::CheckInt || is_len(j))) continue;
      if (node.kind==PhaseTreeKind::Constant) { if (!int_constant(j,value)) return false; continue; }
      if (node.kind==PhaseTreeKind::Bound) { const int b=binder_at(j); if (b<0 || !facts[b].integer) return false; continue; }
      if (!is_add_sub(j) || n+2>64) return false;
      pending[n++]=subtree_end(j+1); pending[n++]=j+1;
    }
    return true;
  };
  // Native zero_compared_length: the source of `len(source)+0 ==/!= 0`.
  const auto zero_compared_length=[&](int i) -> int {
    if (i<0 || i>=node_count) return -1;
    const auto& node=nodes[i];
    if (node.kind!=PhaseTreeKind::Operation || (node.opcode!=Opcode::Eq && node.opcode!=Opcode::Ne) || node.arity!=2) return -1;
    const int left=i+1, right=subtree_end(left);
    const int pairs[2][2]={{left,right},{right,left}};
    for (const auto& pair : pairs) {
      Length relation; std::int64_t value;
      if (length_offset(pair[0],relation) && relation.offset==0 && int_constant(pair[1],value) && value==0) return relation.source;
    }
    return -1;
  };
  const auto zero_event=[&](const PhaseRegionInfo& info, PhaseFuelEvent event) {
    return info.fuel_profile && info.event_cost[static_cast<int>(event)]==0;
  };
  // An uncharged lexical operand stored by an uncharged event aliases its binder.
  const auto zero_capture=[&](const PhaseRegionInfo& info, int argument, PhaseFuelEvent store) -> int {
    const int binder=binder_at(argument);
    return binder>=0 && zero_event(info,store) && nodes[argument].fuel==0 ? env_slot[binder] : -1;
  };
  const auto zero_clamp_identity=[&](const PhaseRegionInfo& info, int source, int argument, PhaseFuelEvent event) -> bool {
    if (!zero_event(info,event)) return false;
    std::int64_t value;
    if (int_constant(argument,value) && value==0) return true;
    Length relation; const int binder=binder_at(source);
    return length_offset(argument,relation) && binder>=0 && relation.source==binder &&
        (relation.offset==0 || (relation.offset==-1 && facts[binder].positive));
  };
  // One source fuel event of a traversal: its first instruction carries the
  // event charge and the rest none; without a fuel profile each costs one.
  bool event_profile=false; unsigned event_first=1; int event_count=0;
  const auto event=[&](const PhaseRegionInfo& info, PhaseFuelEvent which) {
    event_profile=info.fuel_profile; event_first=event_profile ? info.event_cost[static_cast<int>(which)] : 1U; event_count=0;
  };
  const auto put=[&](Opcode op, int a, int b, unsigned flags, int delta) -> bool {
    const unsigned fuel=event_profile ? (event_count++ ? 0U : event_first) : 1U;
    if (!out.emit(op,fuel,a,b,flags)) return false;
    if (delta>0) out.grow(delta); else out.stack+=delta;
    return true;
  };
  const auto put_load=[&](int slot) { return put(Opcode::Load,slot,0,gpu_detail::DINSTR_HAS_A,1); };
  const auto put_store=[&](int slot) { return put(Opcode::Store,slot,0,gpu_detail::DINSTR_HAS_A,-1); };
  const auto put_int=[&](std::int64_t value) {
    const int index=out.add(Value::from_int(value));
    return index>=0 && put(Opcode::PushConst,index,0,gpu_detail::DINSTR_HAS_A,1);
  };
  const auto put_jump=[&](Opcode op, int delta) -> int {
    const int at=out.result.code_count;
    return put(op,0,0,gpu_detail::DINSTR_HAS_A,delta) ? at : -1;
  };
  // Native compile_region_prefix for a traversal, after child `c` completed or
  // was skipped. Children: source, start, [begin, end,] initial state, body.
  const auto walk_after=[&](TraverseScope& walk, int c) -> bool {
    using E=PhaseFuelEvent;
    const auto& info=regions[walk.info];
    const bool reverse=info.traversal==2; const int arity=info.ranged ? 6 : 4;
    const auto begin_value=[&]() { return walk.constant_begin ? put_int(0) : put_load(walk.begin); };
    if (c==0 && !walk.source_alias) { event(info,E::StoreSequence); if (!put_store(walk.xs)) return false; }
    if (c==1 && !walk.start_alias) { event(info,E::StoreStart); if (!put_store(walk.start)) return false; }
    if (info.ranged && c==2 && !walk.constant_begin) { event(info,E::StoreBegin); if (!put_store(walk.begin)) return false; }
    if (info.ranged && c==3 && !walk.end_alias) { event(info,E::StoreEnd); if (!put_store(walk.limit)) return false; }
    if (c==(info.ranged ? 3 : 1)) {
      // A dominating checked/constructed Int needs no repeated uncharged check.
      const int slots[3]={walk.start,walk.begin,walk.limit};
      const E checks[3]={E::CheckStart,E::CheckBegin,E::CheckEnd};
      const int arguments[3]={walk.args[1],walk.args[2],walk.args[info.ranged ? 3 : 2]};
      for (int role=0; role<(info.ranged ? 3 : 1); ++role) {
        if (zero_event(info,checks[role]) && proven_integer(arguments[role])) continue;
        event(info,checks[role]);
        if (!put_load(slots[role]) || !put(Opcode::CheckInt,0,0,0,0) || !put_store(slots[role])) return false;
      }
      if (!walk.length_alias) {
        event(info,E::ObserveSequence);
        if (!put_load(walk.xs) || !put(Opcode::CallBuiltin,int(BuiltinId::Len),1,gpu_detail::DINSTR_HAS_A|gpu_detail::DINSTR_HAS_B,0) ||
            !put_store(walk.length)) return false;
      }
      if (info.ranged) {
        for (int k=0; k<2; ++k) {
          const int slot=k ? walk.limit : walk.begin, argument=k ? walk.args[3] : walk.args[2];
          const E clamp=k ? E::ClampEnd : E::ClampBegin;
          if (zero_clamp_identity(info,walk.args[0],argument,clamp)) continue;
          Length relation; const int source=binder_at(walk.args[0]);
          const bool related=length_offset(argument,relation);
          const bool upper_redundant=zero_event(info,clamp) && related && source>=0 && relation.source==source && relation.offset<=0;
          event(info,clamp);
          if (!put_load(slot) || !put_int(0) || !put(Opcode::Lt,0,0,0,-1)) return false;
          const int nonnegative=put_jump(Opcode::JmpIfFalse,-1); if (nonnegative<0) return false;
          if (!put_int(0) || !put_store(slot)) return false;
          const int clamped=put_jump(Opcode::Jmp,0); if (clamped<0) return false;
          out.code[nonnegative].a=out.result.code_count;
          int above=-1;
          if (!upper_redundant) {
            if (!put_load(slot) || !put_load(walk.length) || !put(Opcode::Gt,0,0,0,-1)) return false;
            above=put_jump(Opcode::JmpIfFalse,-1); if (above<0) return false;
            if (!put_load(walk.length) || !put_store(slot)) return false;
          }
          out.code[clamped].a=out.result.code_count;
          if (above>=0) out.code[above].a=out.result.code_count;
        }
      } else {
        if (!walk.constant_begin) { event(info,E::SetBegin); if (!put_int(0) || !put_store(walk.begin)) return false; }
        event(info,E::SetEnd);
        if (!put_load(walk.length) || !put_store(walk.limit)) return false;
      }
    }
    if (c==arity-2) {
      event(info,E::InitializeState); if (!put_store(walk.state)) return false;
      event(info,E::InitializeCursor);
      if (!(reverse ? put_load(walk.limit) : begin_value())) return false;
      if (reverse && !walk.decrement && (!put_int(1) || !put(Opcode::Sub,0,0,0,-1))) return false;
      if (!put_store(walk.counter)) return false;
      walk.loop_head=out.result.code_count;
      event(info,E::TestCursor);
      if (!put_load(walk.counter) || !(reverse ? begin_value() : put_load(walk.limit)) ||
          !put(reverse ? (walk.decrement ? Opcode::Gt : Opcode::Ge) : Opcode::Lt,0,0,0,-1)) return false;
      walk.done_jump=put_jump(Opcode::JmpIfFalse,-1); if (walk.done_jump<0) return false;
      event(info,E::ReadElement);
      if (walk.decrement && (!put_load(walk.counter) || !put_int(1) || !put(Opcode::Sub,0,0,0,-1) || !put_store(walk.counter))) return false;
      if (!put_load(walk.xs) || !put_load(walk.counter) ||
          !put(Opcode::CallBuiltin,int(BuiltinId::Index),2,gpu_detail::DINSTR_HAS_A|gpu_detail::DINSTR_HAS_B,-1)) return false;
      event(info,E::BindElement); if (!put_store(walk.element)) return false;
      event(info,E::ComputeIndex);
      if (!put_load(walk.start) || !put_load(walk.counter) || !put(Opcode::Add,0,0,0,-1) || !put_store(walk.index)) return false;
      const int slots[3]={walk.element,walk.index,walk.state};
      for (int k=0; k<3; ++k) {
        const int number=info.binders[k];
        walk.saved_slot[k]=env_slot[number]; walk.saved_type[k]=env_type[number]; walk.saved[k]=facts[number];
        env_slot[number]=slots[k]; env_type[number]=info.binding_types[0][k]; facts[number]={false,false,-1,0,-1};
      }
    }
    if (c==arity-1) {
      for (int k=2; k>=0; --k) {
        const int number=info.binders[k];
        env_slot[number]=walk.saved_slot[k]; env_type[number]=walk.saved_type[k]; facts[number]=walk.saved[k];
      }
      event(info,E::UpdateState); if (!put_store(walk.state)) return false;
      if (!walk.decrement) {
        event(info,E::AdvanceCursor);
        if (!put_load(walk.counter) || !put_int(1) || !put(reverse ? Opcode::Sub : Opcode::Add,0,0,0,-1) || !put_store(walk.counter)) return false;
      }
      event(info,E::Repeat); if (!put(Opcode::Jmp,walk.loop_head,0,gpu_detail::DINSTR_HAS_A,0)) return false;
      out.code[walk.done_jump].a=out.result.code_count;
      event(info,E::Result); if (!put_load(walk.state)) return false;
    }
    event_profile=false;
    return true;
  };
  // Skip the children native lowering never compiles (aliased or constant
  // operands), running their completion steps.
  const auto walk_skip=[&](TraverseScope& walk, Frame& frame) -> bool {
    const auto& info=regions[walk.info];
    for (;;) {
      const int c=frame.seen;
      const bool skipped=(c==0 && walk.source_alias) || (c==1 && walk.start_alias) ||
          (info.ranged && c==2 && walk.constant_begin) || (info.ranged && c==3 && walk.end_alias);
      if (!skipped) return true;
      cursor=walk.args[c+1];
      if (!walk_after(walk,c)) return false;
      ++frame.seen;
    }
  };
  for (;;) {
    if (!have_value) {
      if (cursor>=node_count) return out.result;
      const auto node=nodes[cursor++];
      if (node.fuel>0x7fffffffU || node.merge_fuel>0x7fffffffU) return out.result;
      if (node.kind==PhaseTreeKind::Constant) {
        if (node.arity!=0 || node.operand<0 || node.operand>=constant_count) return out.result;
        const auto value=constants[node.operand];
        if (value.tag==ValueTag::Invalid || value.tag==ValueTag::FallbackToken) { out.result.status=PhaseCompileStatus::Unsupported; return out.result; }
        // Only scalar and canonical empty IntList constants keep the certificates.
        if (!(value.tag==ValueTag::Int || value.tag==ValueTag::Bool ||
              (value.tag==ValueTag::IntList && Value::container_len(value)==0))) typed_views=private_intlists=false;
        if(value.tag==ValueTag::IntList)typed_views=false;
        if (!out.push(value,node.fuel)) return out.result;
        completed=value.tag; have_value=true;
      } else if (node.kind==PhaseTreeKind::Bound && node.operand>=0 && node.operand<kPhaseBinders && env_slot[node.operand]>=0) {
        if (node.arity!=0) return out.result;
        completed=env_type[node.operand];
        if (node.type!=ValueTag::Invalid && node.type!=completed) return out.result;
        if (!out.emit(Opcode::Load,node.fuel,env_slot[node.operand],0,gpu_detail::DINSTR_HAS_A)) return out.result;
        out.grow(1); typed_views=private_intlists=false; general=have_value=true;
      } else if (node.kind==PhaseTreeKind::Local || node.kind==PhaseTreeKind::Bound) {
        if (node.arity!=0 || node.operand<0 || node.operand>=local_count) return out.result;
        general|=node.kind==PhaseTreeKind::Bound;
        completed=local_types[node.operand];
        if (completed==ValueTag::Invalid || completed==ValueTag::FallbackToken) { out.result.status=PhaseCompileStatus::Unsupported; return out.result; }
        if (!certified_type(completed)) typed_views=private_intlists=false;
        if (!out.emit(Opcode::Load,node.fuel,node.operand,0,gpu_detail::DINSTR_HAS_A)) return out.result;
        ++out.stack; if (out.stack>out.result.stack_bound) out.result.stack_bound=out.stack;
        have_value=true;
      } else if (node.kind==PhaseTreeKind::Nil) {
        if (node.arity!=0) return out.result;
        typed_views=private_intlists=false; completed=kPhaseStatement; have_value=true;
      } else if (node.kind==PhaseTreeKind::Var) {
        if (node.arity!=0 || node.operand<0) return out.result;
        if (node.type==ValueTag::Invalid || node.type==ValueTag::FallbackToken || node.type==kPhaseStatement) {
          out.result.status=PhaseCompileStatus::Unsupported; return out.result;
        }
        const int slot=variable_slot(node.operand); if (slot<0) return out.result;
        if (!out.emit(Opcode::Load,node.fuel,slot,0,gpu_detail::DINSTR_HAS_A)) return out.result;
        out.grow(1); typed_views=private_intlists=false; completed=node.type; have_value=true;
      } else if (node.kind==PhaseTreeKind::Traverse) {
        if (!Regions || !analyze || node.operand<0 || node.operand>=region_info_count || !regions[node.operand].traversal) {
          out.result.status=PhaseCompileStatus::Unsupported; return out.result;
        }
        const auto& info=regions[node.operand];
        if (node.arity!=(info.ranged ? 6 : 4)) return out.result;
        for (int k=0; k<3; ++k) if (info.binders[k]<0 || info.binders[k]>=kPhaseBinders) return out.result;
        if (depth==64 || walk_depth==kPhaseWalks) { out.result.status=PhaseCompileStatus::Capacity; return out.result; }
        auto& walk=walks[walk_depth++]; walk.info=node.operand; walk.live_begin=live_count;
        int child=cursor;
        for (int c=0; c<node.arity; ++c) { if (child<0) return out.result; walk.args[c]=child; child=subtree_end(child); }
        if (child<0) return out.result;
        using E=PhaseFuelEvent;
        const int source=walk.args[0], begin_argument=walk.args[2], end_argument=walk.args[info.ranged ? 3 : 2];
        walk.decrement=info.traversal==2 && zero_event(info,E::InitializeCursor) && zero_event(info,E::AdvanceCursor);
        walk.constant_begin=!info.ranged && zero_event(info,E::SetBegin);
        std::int64_t value;
        if (info.ranged && nodes[begin_argument].kind==PhaseTreeKind::Constant)
          walk.constant_begin=int_constant(begin_argument,value) && value==0 && nodes[begin_argument].fuel==0 &&
              zero_event(info,E::StoreBegin) && zero_event(info,E::CheckBegin) && zero_event(info,E::ClampBegin);
        const int source_alias=zero_capture(info,source,E::StoreSequence);
        const int start_alias=zero_capture(info,walk.args[1],E::StoreStart);
        const int end_alias=info.ranged && zero_clamp_identity(info,source,end_argument,E::ClampEnd) ?
            zero_capture(info,end_argument,E::StoreEnd) : -1;
        int length_alias=-1;
        if (zero_event(info,E::ObserveSequence) && binder_at(source)>=0) length_alias=facts[binder_at(source)].observed;
        walk.source_alias=source_alias>=0; walk.start_alias=start_alias>=0; walk.end_alias=end_alias>=0; walk.length_alias=length_alias>=0;
        // Native temporary order; immutable aliased operands share storage.
        walk.xs=walk.source_alias ? source_alias : temporary();
        walk.start=walk.start_alias ? start_alias : temporary();
        walk.begin=walk.constant_begin ? -1 : temporary();
        walk.limit=walk.end_alias ? end_alias : temporary();
        walk.length=walk.length_alias ? length_alias : temporary();
        walk.counter=temporary(); walk.element=temporary(); walk.index=temporary(); walk.state=temporary();
        if (walk.xs<0 || walk.start<0 || (!walk.constant_begin && walk.begin<0) || walk.limit<0 || walk.length<0 ||
            walk.counter<0 || walk.element<0 || walk.index<0 || walk.state<0) return out.result;
        general=true; typed_views=private_intlists=false;
        StoredNode stored;
        if constexpr (SharedNodes) stored.index=cursor-1;
        else stored.node=node;
        frames[depth++]={stored,static_cast<FrameIndex>(out.stack),-1,-1,{},0};
        if (!walk_skip(walk,frames[depth-1])) return out.result;
        continue;
      } else if (node.kind==PhaseTreeKind::Region) {
        if (!regions || node.operand<0 || node.operand>=region_info_count) { out.result.status=PhaseCompileStatus::Unsupported; return out.result; }
        const auto& info=regions[node.operand];
        if (info.phases<1 || info.operands<0 || info.captures<0 || info.captures>kPhaseMaxArity ||
            node.arity!=info.operands+info.phases || node.arity>kPhaseMaxArity) return out.result;
        if (info.operands==0) {
          // Without operands the main code sees a leaf.
          if (!finish_region(node)) return out.result;
          have_value=true;
        } else {
          if (depth==64) { out.result.status=PhaseCompileStatus::Capacity; return out.result; }
          StoredNode source;
          if constexpr (SharedNodes) source.index=cursor-1;
          else source.node=node;
          frames[depth++]={source,static_cast<FrameIndex>(out.stack),-1,-1,{},0};
          continue;
        }
      } else {
        if (node.arity<1 || node.arity>3) return out.result;
        if (node.kind==PhaseTreeKind::If) { if (node.arity!=3) return out.result; }
        else if (node.kind==PhaseTreeKind::And || node.kind==PhaseTreeKind::Or) { if (node.arity!=2) return out.result; }
        else if (node.kind==PhaseTreeKind::Program || node.kind==PhaseTreeKind::Assign || node.kind==PhaseTreeKind::Return) {
          if (node.arity!=1) return out.result; typed_views=private_intlists=false;
        } else if (node.kind==PhaseTreeKind::Block || node.kind==PhaseTreeKind::ForRange || node.kind==PhaseTreeKind::Let) {
          if (node.arity!=2) return out.result; typed_views=private_intlists=false;
        } else if (node.kind!=PhaseTreeKind::Operation) { out.result.status=PhaseCompileStatus::Unsupported; return out.result; }
        if (depth==64) { out.result.status=PhaseCompileStatus::Capacity; return out.result; }
        if (node.kind==PhaseTreeKind::ForRange) {
          // The bound temporary precedes the bound expression's own locals.
          if (loop_depth==kPhaseLoops) { out.result.status=PhaseCompileStatus::Capacity; return out.result; }
          auto& loop=loops[loop_depth++]; loop.live_begin=live_count;
          loop.bound=temporary(); if (loop.bound<0) return out.result;
        } else if (node.kind==PhaseTreeKind::Let) {
          if (node.operand<0 || node.operand>=kPhaseBinders) return out.result;
          if (let_depth==kPhaseLets) { out.result.status=PhaseCompileStatus::Capacity; return out.result; }
          auto& scope=lets[let_depth++]; scope.index=node.operand; scope.live_begin=live_count; scope.node=cursor-1; general=true;
          scope.slot=temporary(); if (scope.slot<0) return out.result;
        }
        if (analyze && node.kind==PhaseTreeKind::If && node.type!=kPhaseStatement) {
          const int source=zero_compared_length(cursor);
          if (source>=0) {
            if (if_count==kPhaseIfs) { out.result.status=PhaseCompileStatus::Capacity; return out.result; }
            ifs[if_count++]={depth,source,nodes[cursor].opcode==Opcode::Ne,facts[source].positive};
          }
        }
        StoredNode source;
        if constexpr (SharedNodes) source.index=cursor-1;
        else source.node=node;
        frames[depth++]={source,static_cast<FrameIndex>(out.stack),-1,-1,{},0};
        continue;
      }
    }
    if (depth==0) {
      if (cursor!=node_count || out.stack!=(expected==kPhaseStatement ? 0 : 1) || completed!=expected) return out.result;
      if (out.result.stack_bound>gpu_detail::MAX_STACK) { out.result.status=PhaseCompileStatus::Capacity; return out.result; }
      out.result.status=PhaseCompileStatus::Ok; out.result.result_type=completed;
      out.result.typed_views_proven=typed_views;
      out.result.private_intlists_proven=private_intlists;
      out.result.local_count=total_locals; out.result.general_regions=general;
      for (int g=0; g<kPhaseNamedLocals; ++g)
        out.result.named_slots[g]=named_mask>>g & 1U ? static_cast<std::int8_t>(named[g]+1) : 0;
      out.result.main_code_count=out.result.code_count; out.result.main_constant_count=out.result.constant_count;
      if constexpr (Regions) {
        out.result.region_count=region_count;
        for (int r=0; r<region_count; ++r) {
          const auto& record=records[r]; const auto& info=regions[record.info];
          if (!out.emit(static_cast<Opcode>(kPhaseRegionRecord),info.captures,record.info,info.phases)) return out.result;
          for (int c=0; c<info.captures; ++c)
            if (!out.emit(static_cast<Opcode>(kPhaseRegionRecord),0,record.captures[c])) return out.result;
          for (int p=0; p<info.phases; ++p) {
            const int header=out.result.code_count;
            if (!out.emit(static_cast<Opcode>(kPhaseRegionRecord),0)) return out.result;
            const auto phase=compile_phase_tree<SharedNodes,CompactFrames,false>(nodes+record.bounds[p],record.bounds[p+1]-record.bounds[p],
                constants,constant_count,info.binding_types[p],info.phase_bindings[p],info.phase_type[p],
                out.code+out.result.code_count,code_capacity-out.result.code_count,
                out.constants+out.result.constant_count,constant_capacity-out.result.constant_count);
            if (phase.status!=PhaseCompileStatus::Ok) { out.result.status=phase.status; return out.result; }
            out.code[header].a=phase.code_count; out.code[header].b=phase.constant_count;
            out.code[header].fuel=static_cast<unsigned>(phase.local_count)|(phase.general_regions ? 1U<<16 : 0U);
            out.result.code_count+=phase.code_count; out.result.constant_count+=phase.constant_count;
            if (phase.stack_bound>out.result.stack_bound) out.result.stack_bound=phase.stack_bound;
          }
        }
      }
      return out.result;
    }
    auto& frame=frames[depth-1];
    const auto node=[&]() {
      if constexpr (SharedNodes) return nodes[frame.source.index];
      else return frame.source.node;
    }();
    if (frame.seen<3) frame.args[frame.seen]=completed;
    ++frame.seen;
    if (node.kind==PhaseTreeKind::Traverse) {
      auto& walk=walks[walk_depth-1]; const auto& info=regions[walk.info];
      if (completed!=info.operand_types[frame.seen-1] || !walk_after(walk,frame.seen-1)) return out.result;
      if (frame.seen<node.arity) {
        if (!walk_skip(walk,frame)) return out.result;
        have_value=false; continue;
      }
      while (live_count>walk.live_begin) freed[free_count++]=live[--live_count];
      --walk_depth; completed=info.phase_type[0]; --depth; continue;
    }
    if (node.kind==PhaseTreeKind::Region) {
      const auto& info=regions[node.operand];
      if (completed!=info.operand_types[frame.seen-1]) return out.result;
      if (frame.seen<info.operands) { have_value=false; continue; }
      if (!finish_region(node)) return out.result;
      --depth; continue;
    }
    const bool branch=node.kind==PhaseTreeKind::If || node.kind==PhaseTreeKind::And || node.kind==PhaseTreeKind::Or;
    if (branch && frame.seen==1) {
      if (completed!=ValueTag::Bool) return out.result;
      frame.first_jump=out.result.code_count;
      if (!out.emit(node.kind==PhaseTreeKind::Or ? Opcode::JmpIfTrue : Opcode::JmpIfFalse,
                    node.fuel,0,0,gpu_detail::DINSTR_HAS_A)) return out.result;
      --out.stack;
      if (if_count && ifs[if_count-1].frame==depth-1 && ifs[if_count-1].positive_then) facts[ifs[if_count-1].source].positive=true;
    } else if (node.kind==PhaseTreeKind::If && frame.seen==2) {
      frame.second_jump=out.result.code_count;
      if (!out.emit(Opcode::Jmp,node.merge_fuel,0,0,gpu_detail::DINSTR_HAS_A)) return out.result;
      out.code[frame.first_jump].a=out.result.code_count;
      out.stack=frame.base_stack;
      if (if_count && ifs[if_count-1].frame==depth-1) {
        const auto fact=ifs[if_count-1];
        if (!fact.already) facts[fact.source].positive=false;
        if (!fact.positive_then) facts[fact.source].positive=true;
      }
    }
    if (node.kind==PhaseTreeKind::ForRange && frame.seen==1) {
      // Native for_range: validate the stored bound (a non-Int or negative
      // bound reaches Neg(true), a runtime error), then count from zero.
      using gpu_detail::DINSTR_HAS_A; using gpu_detail::DINSTR_HAS_B;
      auto& loop=loops[loop_depth-1];
      if (completed==kPhaseStatement) return out.result;
      if (!out.emit(Opcode::Store,1,loop.bound,0,DINSTR_HAS_A)) return out.result;
      out.stack=frame.base_stack;
      if (!out.emit(Opcode::Load,1,loop.bound,0,DINSTR_HAS_A)) return out.result;
      out.grow(1);
      if (!out.emit(Opcode::CallBuiltin,1,int(BuiltinId::IsInt),1,DINSTR_HAS_A|DINSTR_HAS_B)) return out.result;
      const int bad=out.result.code_count;
      if (!out.emit(Opcode::JmpIfFalse,1,0,0,DINSTR_HAS_A)) return out.result;
      out.stack=frame.base_stack;
      if (!out.emit(Opcode::Load,1,loop.bound,0,DINSTR_HAS_A)) return out.result;
      out.grow(1);
      if (!out.push(Value::from_int(0),1) || !out.emit(Opcode::Lt,1)) return out.result;
      const int valid=out.result.code_count;
      if (!out.emit(Opcode::JmpIfFalse,1,0,0,DINSTR_HAS_A)) return out.result;
      out.stack=frame.base_stack; out.code[bad].a=out.result.code_count;
      if (!out.push(Value::from_bool(true),1) || !out.emit(Opcode::Neg,1)) return out.result;
      out.code[valid].a=out.result.code_count; out.stack=frame.base_stack;
      const int zero=out.add(Value::from_int(0)); loop.one=out.add(Value::from_int(1));
      if (zero<0 || loop.one<0) return out.result;
      loop.counter=temporary(); const int user=variable_slot(node.operand);
      if (loop.counter<0 || user<0) return out.result;
      if (!out.emit(Opcode::PushConst,1,zero,0,DINSTR_HAS_A)) return out.result;
      out.grow(1);
      if (!out.emit(Opcode::Store,1,loop.counter,0,DINSTR_HAS_A)) return out.result;
      out.stack=frame.base_stack; loop.head=out.result.code_count;
      if (!out.emit(Opcode::Load,1,loop.counter,0,DINSTR_HAS_A) || !out.emit(Opcode::Load,1,loop.bound,0,DINSTR_HAS_A)) return out.result;
      out.grow(2);
      if (!out.emit(Opcode::Lt,1)) return out.result;
      loop.end_jump=out.result.code_count;
      if (!out.emit(Opcode::JmpIfFalse,1,0,0,DINSTR_HAS_A)) return out.result;
      out.stack=frame.base_stack;
      if (!out.emit(Opcode::Load,1,loop.counter,0,DINSTR_HAS_A)) return out.result;
      out.grow(1);
      if (!out.emit(Opcode::Store,1,user,0,DINSTR_HAS_A)) return out.result;
      out.stack=frame.base_stack;
    }
    if (node.kind==PhaseTreeKind::Let && frame.seen==1) {
      auto& scope=lets[let_depth-1];
      if (completed==kPhaseStatement) return out.result;
      if (!out.emit(Opcode::Store,node.fuel,scope.slot,0,gpu_detail::DINSTR_HAS_A)) return out.result;
      out.stack=frame.base_stack;
      scope.saved_slot=env_slot[scope.index]; scope.saved_type=env_type[scope.index];
      scope.observed_source=-1;
      if (analyze) {
        const int value=scope.node+1; Facts fresh{false,false,-1,0,-1}; Length relation;
        fresh.integer=proven_integer(value);
        if (length_offset(value,relation)) { fresh.length_source=relation.source; fresh.length_offset=relation.offset; }
        if (is_len(value)) scope.observed_source=binder_at(value+1);
        scope.saved=facts[scope.index]; facts[scope.index]=fresh;
        if (scope.observed_source>=0) {
          scope.saved_observed=facts[scope.observed_source].observed; facts[scope.observed_source].observed=scope.slot;
        }
      }
      env_slot[scope.index]=scope.slot; env_type[scope.index]=completed;
    }
    if (frame.seen<node.arity) { have_value=false; continue; }
    if (node.kind==PhaseTreeKind::If) {
      if (frame.args[1]!=frame.args[2]) return out.result;
      out.code[frame.second_jump].a=out.result.code_count;
      if (if_count && ifs[if_count-1].frame==depth-1) {
        if (!ifs[if_count-1].already) facts[ifs[if_count-1].source].positive=false;
        --if_count;
      }
    } else if (node.kind==PhaseTreeKind::Program) {
      if (completed!=kPhaseStatement) return out.result;
    } else if (node.kind==PhaseTreeKind::Block) {
      if (frame.args[0]!=kPhaseStatement || frame.args[1]!=kPhaseStatement) return out.result;
    } else if (node.kind==PhaseTreeKind::Assign || node.kind==PhaseTreeKind::Return) {
      // The grammar fixes the stored/returned type; the native compiler
      // allocates an assigned local only after its value expression.
      if (completed!=node.type || completed==kPhaseStatement) return out.result;
      if (node.kind==PhaseTreeKind::Assign) {
        const int slot=variable_slot(node.operand); if (slot<0) return out.result;
        if (!out.emit(Opcode::Store,node.fuel,slot,0,gpu_detail::DINSTR_HAS_A)) return out.result;
      } else if (!out.emit(Opcode::Return,node.fuel)) return out.result;
      out.stack=frame.base_stack; completed=kPhaseStatement;
    } else if (node.kind==PhaseTreeKind::ForRange) {
      if (completed!=kPhaseStatement) return out.result;
      const auto loop=loops[--loop_depth];
      using gpu_detail::DINSTR_HAS_A;
      if (!out.emit(Opcode::Load,1,loop.counter,0,DINSTR_HAS_A) || !out.emit(Opcode::PushConst,1,loop.one,0,DINSTR_HAS_A)) return out.result;
      out.grow(2);
      if (!out.emit(Opcode::Add,1) || !out.emit(Opcode::Store,1,loop.counter,0,DINSTR_HAS_A) ||
          !out.emit(Opcode::Jmp,1,loop.head,0,DINSTR_HAS_A)) return out.result;
      out.code[loop.end_jump].a=out.result.code_count; out.stack=frame.base_stack;
      while (live_count>loop.live_begin) freed[free_count++]=live[--live_count];
    } else if (node.kind==PhaseTreeKind::Let) {
      if (completed==kPhaseStatement || (node.type!=ValueTag::Invalid && node.type!=completed)) return out.result;
      const auto scope=lets[--let_depth];
      if (analyze) {
        if (scope.observed_source>=0) facts[scope.observed_source].observed=scope.saved_observed;
        facts[scope.index]=scope.saved;
      }
      env_slot[scope.index]=scope.saved_slot; env_type[scope.index]=scope.saved_type;
      while (live_count>scope.live_begin) freed[free_count++]=live[--live_count];
    } else if (node.kind==PhaseTreeKind::And || node.kind==PhaseTreeKind::Or) {
      if (completed!=ValueTag::Bool) return out.result;
      if (!out.emit(Opcode::Not,node.fuel) || !out.emit(Opcode::Not,node.fuel)) return out.result;
      const int finish=out.result.code_count;
      if (!out.emit(Opcode::Jmp,node.fuel,0,0,gpu_detail::DINSTR_HAS_A)) return out.result;
      out.code[frame.first_jump].a=out.result.code_count;
      out.stack=frame.base_stack;
      if (!out.push(Value::from_bool(node.kind==PhaseTreeKind::Or),node.fuel)) return out.result;
      out.code[finish].a=out.result.code_count;
    } else {
      completed=operation_type(node,frame.args);
      if (completed==ValueTag::Invalid) {
        // Outside the certified set the typed grammar fixes the result type.
        if (node.type==ValueTag::Invalid || node.type==ValueTag::FallbackToken) { out.result.status=PhaseCompileStatus::Unsupported; return out.result; }
        completed=node.type;typed_views=private_intlists=false;
      } else if (node.type!=ValueTag::Invalid && node.type!=completed) return out.result;
      if (node.opcode==Opcode::CallBuiltin) {
        if(node.operand==int(BuiltinId::Concat) || node.operand==int(BuiltinId::Singleton))typed_views=false;
        if (!out.emit(node.opcode,node.fuel,node.operand,node.arity,gpu_detail::DINSTR_HAS_A|gpu_detail::DINSTR_HAS_B)) return out.result;
      } else if (!out.emit(node.opcode,node.fuel)) return out.result;
      general|=node.opcode==Opcode::CheckInt || node.opcode==Opcode::CheckList;
      out.stack-=node.arity-1;
    }
    --depth;
  }
}
// nvcc emits device code for the region-phase instantiations, which only other
// instantiations of the same template call, once they are named here.
__host__ __device__ inline void phase_compile_region_instantiations() {
  (void)&compile_phase_tree<true, true, false>; (void)&compile_phase_tree<true, false, false>;
  (void)&compile_phase_tree<false, false, false>;
}
}  // namespace gagp::evo::repro
