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
enum class PhaseTreeKind : int { Constant, Local, Operation, If, And, Or, Program, Block, Nil, Assign, Return, ForRange, Var, Let, Bound };
// Completion tag of a statement; never a runtime value tag.
constexpr ValueTag kPhaseStatement = static_cast<ValueTag>(0xF0);
constexpr int kPhaseNamedLocals = 16;
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
template<bool SharedNodes = false, bool CompactFrames = false>
__host__ __device__ inline PhaseCompileResult compile_phase_tree(
    const PhaseTreeNode* nodes, int node_count,
    const Value* constants, int constant_count,
    const ValueTag* local_types, int local_count, ValueTag expected,
    gpu_detail::DInstr* code, int code_capacity, Value* output_constants, int constant_capacity) {
  using namespace phase_compile_detail;
  static_assert(!CompactFrames || SharedNodes, "compact frames use immutable node indices");
  if constexpr (CompactFrames) {
    // The generic helper accepts larger code buffers. Preserve that contract
    // instead of narrowing it to the native owner's bounded compact indices.
    if (node_count > 4096 || code_capacity > 32767)
      return compile_phase_tree<SharedNodes, false>(nodes,node_count,constants,constant_count,
          local_types,local_count,expected,code,code_capacity,output_constants,constant_capacity);
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
  Loop loops[16];
  // Lexical let binders, mirroring the native compiler: the binder temporary
  // precedes the value expression, and every temporary allocated inside the
  // let is recycled LIFO once its body finishes.
  struct LetScope { int index, slot, live_begin, saved_slot; ValueTag saved_type; };
  LetScope lets[16]; int let_depth=0;
  int env_slot[8]; ValueTag env_type[8];
  for (int i=0; i<8; ++i) { env_slot[i]=-1; env_type[i]=ValueTag::Invalid; }
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
      } else if (node.kind==PhaseTreeKind::Bound && node.operand>=0 && node.operand<8 && env_slot[node.operand]>=0) {
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
          if (loop_depth==16) { out.result.status=PhaseCompileStatus::Capacity; return out.result; }
          auto& loop=loops[loop_depth++]; loop.live_begin=live_count;
          loop.bound=temporary(); if (loop.bound<0) return out.result;
        } else if (node.kind==PhaseTreeKind::Let) {
          if (node.operand<0 || node.operand>=8) return out.result;
          if (let_depth==16) { out.result.status=PhaseCompileStatus::Capacity; return out.result; }
          auto& scope=lets[let_depth++]; scope.index=node.operand; scope.live_begin=live_count; general=true;
          scope.slot=temporary(); if (scope.slot<0) return out.result;
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
      return out.result;
    }
    auto& frame=frames[depth-1];
    const auto node=[&]() {
      if constexpr (SharedNodes) return nodes[frame.source.index];
      else return frame.source.node;
    }();
    frame.args[frame.seen++]=completed;
    const bool branch=node.kind==PhaseTreeKind::If || node.kind==PhaseTreeKind::And || node.kind==PhaseTreeKind::Or;
    if (branch && frame.seen==1) {
      if (completed!=ValueTag::Bool) return out.result;
      frame.first_jump=out.result.code_count;
      if (!out.emit(node.kind==PhaseTreeKind::Or ? Opcode::JmpIfTrue : Opcode::JmpIfFalse,
                    node.fuel,0,0,gpu_detail::DINSTR_HAS_A)) return out.result;
      --out.stack;
    } else if (node.kind==PhaseTreeKind::If && frame.seen==2) {
      frame.second_jump=out.result.code_count;
      if (!out.emit(Opcode::Jmp,node.merge_fuel,0,0,gpu_detail::DINSTR_HAS_A)) return out.result;
      out.code[frame.first_jump].a=out.result.code_count;
      out.stack=frame.base_stack;
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
      env_slot[scope.index]=scope.slot; env_type[scope.index]=completed;
    }
    if (frame.seen<node.arity) { have_value=false; continue; }
    if (node.kind==PhaseTreeKind::If) {
      if (frame.args[1]!=frame.args[2]) return out.result;
      out.code[frame.second_jump].a=out.result.code_count;
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
}  // namespace gagp::evo::repro
