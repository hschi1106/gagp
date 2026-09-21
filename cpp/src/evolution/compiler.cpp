#include "gagp/evolution/compiler.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gagp/core/builtin.hpp"
#include "gagp/core/semantic_fuel.hpp"
#include "gagp/evolution/fuel_events.hpp"
#include "gagp/evolution/bounded_region.hpp"
#include "gagp/core/bytecode_verify.hpp"
#include "gagp/evolution/node_descriptor.hpp"
#include "subtree_utils.hpp"

namespace gagp::evo {

namespace {

Opcode op_name(NodeKind op) {
  switch (op) {
    case NodeKind::ADD: return Opcode::Add;
    case NodeKind::SUB: return Opcode::Sub;
    case NodeKind::MUL: return Opcode::Mul;
    case NodeKind::DIV: return Opcode::Div;
    case NodeKind::MOD: return Opcode::Mod;
    case NodeKind::LT: return Opcode::Lt;
    case NodeKind::LE: return Opcode::Le;
    case NodeKind::GT: return Opcode::Gt;
    case NodeKind::GE: return Opcode::Ge;
    case NodeKind::EQ: return Opcode::Eq;
    case NodeKind::NE: return Opcode::Ne;
    case NodeKind::NEG: return Opcode::Neg;
    case NodeKind::NOT: return Opcode::Not;
    default:
      throw std::runtime_error("prefix compile: unsupported opcode lowering for node kind");
  }
}

class Compiler {
 public:
  explicit Compiler(const std::vector<std::string>* preset_locals = nullptr,
                    const VerifiedAst* verified = nullptr)
      : verified_(verified) {
    if (preset_locals != nullptr) {
      for (const std::string& name : *preset_locals) {
        local(name);
      }
    }
  }

  BytecodeProgram build(const AstProgram& program) {
    if (program.version != k_ast_prefix_version_current) {
      throw std::runtime_error("unsupported ast prefix version");
    }
    if (program.nodes.empty() || program.nodes[0].kind != NodeKind::PROGRAM) {
      throw std::runtime_error("prefix compile: bad root");
    }
    if (verified_ != nullptr &&
        (verified_->subtree_end.size() != program.nodes.size() ||
         verified_->expression_types.size() != program.nodes.size() ||
         verified_->subtree_end[0] != program.nodes.size())) {
      throw std::invalid_argument("prefix compile: VerifiedAst does not match program shape");
    }
    if (!program.lexical_regions.empty() || !program.traversal_specs.empty() || !program.fuel_specs.empty() || !program.bounded_region_specs.empty()) {
      const auto structure = verify_ast_structure(program);
      if (!structure) throw std::invalid_argument("prefix compile: " + structure.diagnostic.message);
    }
    const std::size_t end = compile_block_prefix(program, 1);
    if (end != program.nodes.size()) {
      throw std::runtime_error("prefix compile: trailing tokens");
    }
    patch_jumps();
    BytecodeProgram out = finalize();
#ifndef NDEBUG
    const BytecodeVerifyResult verified = verify_bytecode(out);
    if (!verified) {
      throw std::runtime_error(
          std::string("compiler produced invalid bytecode (") +
          bytecode_verify_code_name(verified.diagnostic.code) + ") at " +
          verified.diagnostic.path + ": " + verified.diagnostic.message);
    }
#endif
    return out;
  }

 private:
  struct UnresolvedJump {
    int index = 0;
    std::string label;
  };

  BytecodeProgram finalize() {
    BytecodeProgram out;
    out.consts = consts_;
    out.code = code_;
    if (has_general_regions_ || has_source_fuel_) {
      out.instruction_fuel = fuel_;
      const auto validation = validate_semantic_fuel(out.code, out.instruction_fuel);
      if (!validation) throw std::invalid_argument("prefix compile: " + validation.message);
    }
    out.n_locals = static_cast<int>(var2idx_.size());
    out.var2idx = var2idx_;
    out.bounded_region_segments = bounded_region_segments_;
    return out;
  }

  int add_const(const Value& value) {
    consts_.push_back(value);
    return static_cast<int>(consts_.size()) - 1;
  }

  int local(const std::string& name) {
    auto it = var2idx_.find(name);
    if (it != var2idx_.end()) {
      return it->second;
    }
    const int idx = static_cast<int>(var2idx_.size());
    var2idx_[name] = idx;
    return idx;
  }

  std::string new_label(const std::string& prefix) {
    return prefix + "_" + std::to_string(label_counter_++);
  }

  std::string new_temp() { return std::string("\x00for_i_") + std::to_string(tmp_counter_++); }

  void push_binder(int name_id, int local_idx) {
    binder_stack_[name_id].push_back(local_idx);
  }

  void pop_binder(int name_id) {
    auto it = binder_stack_.find(name_id);
    if (it == binder_stack_.end() || it->second.empty()) {
      throw std::runtime_error("prefix compile: binder stack underflow");
    }
    it->second.pop_back();
  }

  int bound_local(const AstProgram& program, int name_id) const {
    auto it = binder_stack_.find(name_id);
    if (it == binder_stack_.end() || it->second.empty()) {
      throw std::runtime_error("prefix compile: undefined binder " + name_at(program, name_id));
    }
    return it->second.back();
  }

  std::size_t expr_end_prefix(const AstProgram& program, std::size_t idx) const {
    (void)node_at(program, idx);
    if (verified_ != nullptr) {
      const std::size_t end = verified_->subtree_end[idx];
      if (end <= idx || end > program.nodes.size() ||
          verified_->expression_types[idx] == RType::Invalid) {
        throw std::invalid_argument(
            "prefix compile: VerifiedAst expression annotation is invalid");
      }
      return end;
    }
    std::size_t cur = idx + 1;
    for (int i = 0; i < node_prefix_arity(program.nodes[idx]); ++i) {
      cur = expr_end_prefix(program, cur);
    }
    return cur;
  }

  std::size_t compile_for_loop_body(const std::string& user_name, int bound_local, const AstProgram& program, std::size_t body_idx) {
    const int idx_0 = add_const(Value::from_int(0));
    const int idx_1 = add_const(Value::from_int(1));
    const int counter_i = local(new_temp());
    const int user_i = local(user_name);

    const std::string loop_label = new_label("for_loop");
    const std::string end_label = new_label("for_end");

    emit(Opcode::PushConst, idx_0, true);
    emit(Opcode::Store, counter_i, true);

    mark_label(loop_label);
    emit(Opcode::Load, counter_i, true);
    emit(Opcode::Load, bound_local, true);
    emit(Opcode::Lt);
    emit_jump(Opcode::JmpIfFalse, end_label);

    emit(Opcode::Load, counter_i, true);
    emit(Opcode::Store, user_i, true);

    const std::size_t next = compile_block_prefix(program, body_idx);

    emit(Opcode::Load, counter_i, true);
    emit(Opcode::PushConst, idx_1, true);
    emit(Opcode::Add);
    emit(Opcode::Store, counter_i, true);
    emit_jump(Opcode::Jmp, loop_label);
    mark_label(end_label);
    return next;
  }

  bool zero_event(const AstProgram& program, std::size_t index, FuelEvent event) const {
    const auto* spec = fuel_spec(program, index);
    return spec && event_cost(*spec, event) == 0;
  }

  struct LengthOffset {
    int source;
    int offset;
  };

  std::optional<LengthOffset> length_offset(const AstProgram& program,
                                           std::size_t index) const {
    const auto& node = program.nodes.at(index);
    if (node.kind == NodeKind::REGION_VAR) {
      const auto found = length_offsets_.find(node.i0);
      if (found != length_offsets_.end()) return found->second;
    }
    if (node.kind == NodeKind::CALL_LEN &&
        program.nodes.at(index + 1).kind == NodeKind::REGION_VAR)
      return LengthOffset{program.nodes[index + 1].i0, 0};
    if (node.kind == NodeKind::ADD || node.kind == NodeKind::SUB) {
      const auto left = length_offset(program, index + 1);
      const auto right = expr_end_prefix(program, index + 1);
      if (left && program.nodes[right].kind == NodeKind::CONST) {
        const auto value = const_at(program, program.nodes[right].i0);
        // Container lengths are 16-bit. Keep every inferred intermediate in
        // a small exact Int range despite the VM's double-based arithmetic.
        if (value.tag == ValueTag::Int && value.i >= -65535 && value.i <= 65535) {
          const auto offset = left->offset +
              (node.kind == NodeKind::ADD ? value.i : -value.i);
          if (offset >= -65535 && offset <= 65535)
            return LengthOffset{left->source, static_cast<int>(offset)};
        }
      }
    }
    return std::nullopt;
  }

  std::optional<int> zero_compared_length(const AstProgram& program,
                                         std::size_t index) const {
    const auto kind = program.nodes[index].kind;
    if (kind != NodeKind::EQ && kind != NodeKind::NE) return std::nullopt;
    const auto left = index + 1;
    const auto right = expr_end_prefix(program, left);
    for (const auto pair : {std::make_pair(left, right), std::make_pair(right, left)}) {
      const auto relation = length_offset(program, pair.first);
      if (relation && relation->offset == 0 &&
          program.nodes[pair.second].kind == NodeKind::CONST) {
        const auto value = const_at(program, program.nodes[pair.second].i0);
        if (value.tag == ValueTag::Int && value.i == 0) return relation->source;
      }
    }
    return std::nullopt;
  }

  bool zero_clamp_identity(const AstProgram& program, std::size_t owner,
      std::size_t source, std::size_t argument, FuelEvent event) const {
    if (!zero_event(program, owner, event)) return false;
    if (program.nodes[argument].kind == NodeKind::CONST) {
      const auto value = const_at(program, program.nodes[argument].i0);
      if (value.tag == ValueTag::Int && value.i == 0) return true;
    }
    const auto relation = length_offset(program, argument);
    return relation && program.nodes[source].kind == NodeKind::REGION_VAR &&
        relation->source == program.nodes[source].i0 &&
        (relation->offset == 0 ||
         (relation->offset == -1 && positive_lengths_.count(relation->source)));
  }

  bool proven_integer(const AstProgram& program, std::size_t index) const {
    const auto& node = program.nodes.at(index);
    if (node.kind == NodeKind::CHECK_INT || node.kind == NodeKind::CALL_LEN) return true;
    if (node.kind == NodeKind::CONST) return const_at(program, node.i0).tag == ValueTag::Int;
    if (node.kind == NodeKind::REGION_VAR) return integer_binders_.count(node.i0) != 0;
    if (node.kind == NodeKind::ADD || node.kind == NodeKind::SUB) {
      const auto right = expr_end_prefix(program, index + 1);
      return proven_integer(program, index + 1) && proven_integer(program, right);
    }
    return false;
  }

  std::optional<int> zero_capture(const AstProgram& program, std::size_t owner,
      std::size_t argument, FuelEvent store_event) const {
    const auto& node = program.nodes.at(argument);
    if (node.kind != NodeKind::REGION_VAR || !zero_event(program, owner, store_event) ||
        !zero_event(program, argument, FuelEvent::Operation)) return std::nullopt;
    const auto found = binder_stack_.find(-node.i0 - 1);
    if (found == binder_stack_.end() || found->second.empty()) return std::nullopt;
    return found->second.back();
  }

  std::size_t compile_region_prefix(const AstProgram& program, std::size_t idx) {
    has_general_regions_ = true;
    const LexicalRegion* region = nullptr;
    for (const auto& row : program.lexical_regions) {
      if (row.node_index == idx) { region = &row; break; }
    }
    if (!region) throw std::invalid_argument("prefix compile: missing lexical region");
    if (program.nodes[idx].kind == NodeKind::LET_REGION) {
      const int slot = local(new_temp());
      const auto body = compile_expr_prefix(program, idx + 1);
      fuel_event(program, idx, FuelEvent::Bind, [&] { emit(Opcode::Store, slot, true); });
      const int binding = region->bindings.at(0).id;
      push_binder(-binding - 1, slot);
      if (proven_integer(program, idx + 1)) integer_binders_.insert(binding);
      if (const auto relation = length_offset(program, idx + 1))
        length_offsets_[binding] = *relation;
      std::optional<std::pair<int, std::optional<int>>> observed;
      if (program.nodes[idx + 1].kind == NodeKind::CALL_LEN &&
          program.nodes[idx + 2].kind == NodeKind::REGION_VAR) {
        const int source = program.nodes[idx + 2].i0;
        const auto found = observed_lengths_.find(source);
        observed = std::make_pair(source, found == observed_lengths_.end() ?
            std::optional<int>{} : std::optional<int>{found->second});
        observed_lengths_[source] = slot;
      }
      const auto end = compile_expr_prefix(program, body);
      if (observed) {
        if (observed->second) observed_lengths_[observed->first] = *observed->second;
        else observed_lengths_.erase(observed->first);
      }
      integer_binders_.erase(binding);
      length_offsets_.erase(binding);
      pop_binder(-binding - 1);
      return end;
    }
    const TraversalSpec* traversal = nullptr;
    for (const auto& row : program.traversal_specs) {
      if (row.node_index == idx) { traversal = &row; break; }
    }
    if (!traversal) throw std::invalid_argument("prefix compile: missing traversal specification");
    const bool ranged = program.nodes[idx].kind == NodeKind::TRAVERSE_RANGE;
    const bool reverse = traversal->direction == TraversalDirection::Reverse;
    const auto source_arg = idx + 1;
    const auto start_arg = expr_end_prefix(program, source_arg);
    const auto begin_arg = expr_end_prefix(program, start_arg);
    const auto end_arg = ranged ? expr_end_prefix(program, begin_arg) : begin_arg;
    const bool decrement_at_read = reverse &&
        zero_event(program, idx, FuelEvent::InitializeCursor) &&
        zero_event(program, idx, FuelEvent::AdvanceCursor);
    bool constant_begin = !ranged && zero_event(program, idx, FuelEvent::SetBegin);
    if (ranged && program.nodes[begin_arg].kind == NodeKind::CONST) {
      const auto value = const_at(program, program.nodes[begin_arg].i0);
      constant_begin = value.tag == ValueTag::Int && value.i == 0 &&
          zero_event(program, begin_arg, FuelEvent::Operation) &&
          zero_event(program, idx, FuelEvent::StoreBegin) &&
          zero_event(program, idx, FuelEvent::CheckBegin) &&
          zero_event(program, idx, FuelEvent::ClampBegin);
    }
    const auto source_alias = zero_capture(program, idx, source_arg, FuelEvent::StoreSequence);
    const auto start_alias = zero_capture(program, idx, start_arg, FuelEvent::StoreStart);
    const auto end_alias = ranged && zero_clamp_identity(program, idx, source_arg,
        end_arg, FuelEvent::ClampEnd) ?
        zero_capture(program, idx, end_arg, FuelEvent::StoreEnd) : std::nullopt;
    std::optional<int> length_alias;
    if (zero_event(program, idx, FuelEvent::ObserveSequence) &&
        program.nodes[source_arg].kind == NodeKind::REGION_VAR) {
      const auto found = observed_lengths_.find(program.nodes[source_arg].i0);
      if (found != observed_lengths_.end()) length_alias = found->second;
    }
    const int xs = source_alias ? *source_alias : local(new_temp());
    const int start = start_alias ? *start_alias : local(new_temp());
    // Only a proven identity clamp permits sharing immutable endpoint storage.
    const int begin = constant_begin ? -1 : local(new_temp());
    const int limit = end_alias ? *end_alias : local(new_temp());
    const int length = length_alias ? *length_alias : local(new_temp());
    const int counter = local(new_temp());
    const int element = local(new_temp()), index = local(new_temp()), state = local(new_temp());
    const auto emit_begin = [&] {
      if (constant_begin) emit(Opcode::PushConst, add_const(Value::from_int(0)), true);
      else emit(Opcode::Load, begin, true);
    };
    auto next = source_arg;
    if (source_alias) next = start_arg;
    else {
      next = compile_expr_prefix(program, next);
      fuel_event(program, idx, FuelEvent::StoreSequence, [&] { emit(Opcode::Store, xs, true); });
    }
    if (start_alias) next = begin_arg;
    else {
      next = compile_expr_prefix(program, next);
      fuel_event(program, idx, FuelEvent::StoreStart, [&] { emit(Opcode::Store, start, true); });
    }
    if (ranged) {
      if (constant_begin) next = expr_end_prefix(program, next);
      else {
        next = compile_expr_prefix(program, next);
        fuel_event(program, idx, FuelEvent::StoreBegin, [&] { emit(Opcode::Store, begin, true); });
      }
      if (end_alias) next = expr_end_prefix(program, next);
      else {
        next = compile_expr_prefix(program, next);
        fuel_event(program, idx, FuelEvent::StoreEnd, [&] { emit(Opcode::Store, limit, true); });
      }
    }
    // A dominating checked/constructed Int needs no repeated zero-cost validation.
    const int check_slots[] = {start, begin, limit};
    const FuelEvent check_events[] = {FuelEvent::CheckStart, FuelEvent::CheckBegin, FuelEvent::CheckEnd};
    const std::size_t check_arguments[] = {start_arg, begin_arg, end_arg};
    for (int role = 0; role < (ranged ? 3 : 1); ++role) {
      const int slot = check_slots[role];
      const auto event = check_events[role];
      const auto argument = check_arguments[role];
      if (zero_event(program, idx, event) && proven_integer(program, argument)) continue;
      fuel_event(program, idx, event, [&] {
        emit(Opcode::Load, slot, true);
        emit(Opcode::CheckInt);
        emit(Opcode::Store, slot, true);
      });
    }
    if (!length_alias) fuel_event(program, idx, FuelEvent::ObserveSequence, [&] {
      emit(Opcode::Load, xs, true);
      emit(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Len), true, 1, true);
      emit(Opcode::Store, length, true);
    });
    if (ranged) {
      // Comparisons avoid narrowing an extreme endpoint through numeric builtins.
      for (int slot : {begin, limit}) {
        const auto event = slot == begin ? FuelEvent::ClampBegin : FuelEvent::ClampEnd;
        const auto argument = slot == begin ? begin_arg : end_arg;
        // A charged clamp remains observable even when its result is known.
        if (zero_clamp_identity(program, idx, source_arg, argument, event)) continue;
        const auto relation = length_offset(program, argument);
        const bool upper_redundant = zero_event(program, idx, event) && relation &&
            program.nodes[source_arg].kind == NodeKind::REGION_VAR &&
            relation->source == program.nodes[source_arg].i0 && relation->offset <= 0;
        fuel_event(program, idx, event, [&] {
          const auto nonnegative = new_label("range_nonnegative");
          const auto clamped = new_label("range_clamped");
          emit(Opcode::Load, slot, true);
          emit(Opcode::PushConst, add_const(Value::from_int(0)), true);
          emit(Opcode::Lt);
          emit_jump(Opcode::JmpIfFalse, nonnegative);
          emit(Opcode::PushConst, add_const(Value::from_int(0)), true);
          emit(Opcode::Store, slot, true);
          emit_jump(Opcode::Jmp, clamped);
          mark_label(nonnegative);
          if (!upper_redundant) {
            emit(Opcode::Load, slot, true);
            emit(Opcode::Load, length, true);
            emit(Opcode::Gt);
            emit_jump(Opcode::JmpIfFalse, clamped);
            emit(Opcode::Load, length, true);
            emit(Opcode::Store, slot, true);
          }
          mark_label(clamped);
        });
      }
    } else {
    if (!constant_begin) fuel_event(program, idx, FuelEvent::SetBegin, [&] {
      emit(Opcode::PushConst, add_const(Value::from_int(0)), true);
      emit(Opcode::Store, begin, true);
    });
    fuel_event(program, idx, FuelEvent::SetEnd, [&] {
      emit(Opcode::Load, length, true);
      emit(Opcode::Store, limit, true);
    });
    }
    const auto body = compile_expr_prefix(program, next);
    fuel_event(program, idx, FuelEvent::InitializeState, [&] {
      emit(Opcode::Store, state, true);
    });
    fuel_event(program, idx, FuelEvent::InitializeCursor, [&] {
      if (reverse) emit(Opcode::Load, limit, true);
      else emit_begin();
      if (reverse && !decrement_at_read) {
        emit(Opcode::PushConst, add_const(Value::from_int(1)), true);
        emit(Opcode::Sub);
      }
      emit(Opcode::Store, counter, true);
    });
    const auto loop = new_label("traverse_loop"), done = new_label("traverse_done");
    mark_label(loop);
    fuel_event(program, idx, FuelEvent::TestCursor, [&] {
      emit(Opcode::Load, counter, true);
      if (reverse) emit_begin();
      else emit(Opcode::Load, limit, true);
      emit(reverse ? (decrement_at_read ? Opcode::Gt : Opcode::Ge) : Opcode::Lt);
      emit_jump(Opcode::JmpIfFalse, done);
    });
    fuel_event(program, idx, FuelEvent::ReadElement, [&] {
      // With zero cursor setup/advance charges, decrement immediately before
      // reading. The event still charges before the first fallible operation.
      if (decrement_at_read) {
        emit(Opcode::Load, counter, true);
        emit(Opcode::PushConst, add_const(Value::from_int(1)), true);
        emit(Opcode::Sub);
        emit(Opcode::Store, counter, true);
      }
      emit(Opcode::Load, xs, true);
      emit(Opcode::Load, counter, true);
      emit(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Index), true, 2, true);
    });
    fuel_event(program, idx, FuelEvent::BindElement, [&] {
      emit(Opcode::Store, element, true);
    });
    fuel_event(program, idx, FuelEvent::ComputeIndex, [&] {
      emit(Opcode::Load, start, true);
      emit(Opcode::Load, counter, true);
      emit(Opcode::Add);
      emit(Opcode::Store, index, true);
    });
    const std::vector<int> slots{element, index, state};
    for (std::size_t i = 0; i < slots.size(); ++i)
      push_binder(-region->bindings.at(i).id - 1, slots[i]);
    const auto end = compile_expr_prefix(program, body);
    for (std::size_t i = slots.size(); i > 0; --i)
      pop_binder(-region->bindings.at(i - 1).id - 1);
    fuel_event(program, idx, FuelEvent::UpdateState, [&] { emit(Opcode::Store, state, true); });
    if (!decrement_at_read) fuel_event(program, idx, FuelEvent::AdvanceCursor, [&] {
      emit(Opcode::Load, counter, true);
      emit(Opcode::PushConst, add_const(Value::from_int(1)), true);
      emit(reverse ? Opcode::Sub : Opcode::Add);
      emit(Opcode::Store, counter, true);
    });
    fuel_event(program, idx, FuelEvent::Repeat, [&] {
      emit_jump(Opcode::Jmp, loop);
    });
    mark_label(done);
    fuel_event(program, idx, FuelEvent::Result, [&] {
      emit(Opcode::Load, state, true);
    });
    return end;
  }

  bool subtree_contains_bounded_region(const AstProgram& program, std::size_t idx) const {
    const std::size_t end = expr_end_prefix(program, idx);
    for (std::size_t i = idx; i < end; ++i) {
      if (program.nodes[i].kind == NodeKind::BOUNDED_REGION) return true;
    }
    return false;
  }

  RegionPhase compile_bounded_phase(const AstProgram& program,
                                    const RegionAstPhase& binding,
                                    std::size_t begin, std::size_t end) {
    if (subtree_contains_bounded_region(program, begin))
      throw std::invalid_argument("prefix compile: nested structured region in phase");
    // Ordinary inputs must enter through explicit Parameter bank bindings.
    for (std::size_t i = begin; i < end; ++i)
      if (program.nodes[i].kind == NodeKind::VAR)
        throw std::invalid_argument("prefix compile: implicit variable capture in region phase");
    Compiler phase;
    RegionPhase out;
    for (const auto& item : binding.bindings) {
      const int local_idx = phase.local(phase.new_temp());
      phase.push_binder(-item.binder_id - 1, local_idx);
      out.bindings.push_back({item.source, local_idx});
    }
    if (phase.compile_expr_prefix(program, begin) != end)
      throw std::invalid_argument("prefix compile: region phase trailing tokens");
    phase.patch_jumps();
    out.program.consts = std::move(phase.consts_);
    out.program.code = std::move(phase.code_);
    out.program.n_locals = static_cast<int>(phase.var2idx_.size());
    if (phase.has_general_regions_ || phase.has_source_fuel_) {
      out.program.instruction_fuel = std::move(phase.fuel_);
      const auto valid = validate_semantic_fuel(out.program.code, out.program.instruction_fuel);
      if (!valid) throw std::invalid_argument("prefix phase compile: " + valid.message);
    }
    return out;
  }

  std::size_t compile_bounded_prefix(const AstProgram& program, std::size_t owner) {
    const auto* spec = lookup_bounded_region_spec(program, owner);
    if (!spec) throw std::invalid_argument("prefix compile: missing bounded region metadata");
    BoundedRegionSegment segment;
    segment.plan = spec->plan;
    std::size_t cursor = owner + 1;
    const auto operands = spec->plan.state_types.size() + spec->plan.bound_operand_count;
    for (std::size_t i = 0; i < operands; ++i)
      cursor = compile_expr_prefix(program, cursor);
    for (const auto& capture : spec->parameters) {
      if (capture.kind == RegionCaptureKind::Name) {
        segment.parameter_locals.push_back(local(name_at(program, capture.index)));
      } else {
        const auto found = binder_stack_.find(-capture.index - 1);
        if (found == binder_stack_.end() || found->second.empty())
          throw std::invalid_argument("prefix compile: undefined bounded region capture");
        segment.parameter_locals.push_back(found->second.back());
      }
    }
    for (std::size_t ordinal = 0; ordinal < spec->phases.size(); ++ordinal) {
      const auto end = expr_end_prefix(program, cursor);
      auto phase = compile_bounded_phase(program, spec->phases[ordinal], cursor, end);
      switch (bounded_region_phase_kind(spec->plan, ordinal)) {
        case RegionPhaseKind::BasePredicate: segment.base_predicate = std::move(phase); break;
        case RegionPhaseKind::BaseBody: segment.base_body = std::move(phase); break;
        case RegionPhaseKind::Preparation: segment.preparations.push_back(std::move(phase)); break;
        case RegionPhaseKind::Request: segment.request_expressions.push_back(std::move(phase)); break;
        case RegionPhaseKind::Combine: segment.combine = std::move(phase); break;
        case RegionPhaseKind::Boundary: segment.boundary = std::move(phase); break;
      }
      cursor = end;
    }
    const auto verified = verify_bounded_region_segment(segment, static_cast<int>(var2idx_.size()));
    if (!verified) throw std::invalid_argument("prefix compile: " + verified.diagnostic.message);
    const auto index = static_cast<int>(bounded_region_segments_.size());
    bounded_region_segments_.push_back(std::move(segment));
    emit(Opcode::BoundedRegion, index, true);
    return cursor;
  }

  void emit(Opcode op, int a = 0, bool has_a = false, int b = 0, bool has_b = false) {
    code_.push_back(Instr{op, a, b, has_a, has_b});
    fuel_.push_back(operation_cost_);
  }

  void emit_jump(Opcode op, const std::string& label) {
    emit(op, 0, true, 0, false);
    unresolved_.push_back({static_cast<int>(code_.size()) - 1, label});
  }

  void mark_label(const std::string& name) { labels_[name] = static_cast<int>(code_.size()); }

  void patch_jumps() {
    for (const UnresolvedJump& jump : unresolved_) {
      auto it = labels_.find(jump.label);
      if (it == labels_.end()) {
        throw std::runtime_error("undefined label");
      }
      code_[static_cast<std::size_t>(jump.index)].a = it->second;
      code_[static_cast<std::size_t>(jump.index)].has_a = true;
    }
  }

  const AstNode& node_at(const AstProgram& program, std::size_t idx) const {
    if (idx >= program.nodes.size()) {
      throw std::runtime_error("prefix compile: node index out of range");
    }
    return program.nodes[idx];
  }

  const std::string& name_at(const AstProgram& program, int idx) const {
    if (idx < 0 || static_cast<std::size_t>(idx) >= program.names.size()) {
      throw std::runtime_error("prefix compile: name index out of range");
    }
    return program.names[static_cast<std::size_t>(idx)];
  }

  const Value& const_at(const AstProgram& program, int idx) const {
    if (idx < 0 || static_cast<std::size_t>(idx) >= program.consts.size()) {
      throw std::runtime_error("prefix compile: const index out of range");
    }
    return program.consts[static_cast<std::size_t>(idx)];
  }

  const NodeFuelSpec* fuel_spec(const AstProgram& program, std::size_t idx) const {
    if (program.fuel_specs.empty()) return nullptr;
    if (fuel_program_ != &program) {
      fuel_specs_.assign(program.nodes.size(), nullptr);
      for (const auto& spec : program.fuel_specs) {
        if (spec.node_index >= fuel_specs_.size())
          throw std::invalid_argument("prefix compile: source fuel owner is out of bounds");
        fuel_specs_[spec.node_index] = &spec;
      }
      fuel_program_ = &program;
    }
    return fuel_specs_.at(idx);
  }

  static std::uint32_t event_cost(const NodeFuelSpec& spec, FuelEvent event) {
    for (const auto& charge : spec.charges) if (charge.event == event) return charge.cost;
    return 1;
  }

  template <class Emit>
  void fuel_event(const AstProgram& program, std::size_t idx, FuelEvent event, Emit body) {
    const auto begin = fuel_.size();
    body();
    if (const auto* spec = fuel_spec(program, idx)) {
      if (fuel_.size() == begin) throw std::logic_error("source fuel event has no lowering anchor");
      fuel_[begin] = event_cost(*spec, event);
      for (auto i = begin + 1; i < fuel_.size(); ++i) fuel_[i] = 0;
      has_source_fuel_ = true;
    }
  }

  std::size_t compile_expr_prefix(const AstProgram& program, std::size_t idx) {
    const auto saved = operation_cost_;
    operation_cost_ = 1;
    if (const auto* spec = fuel_spec(program, idx)) {
      has_source_fuel_ = true;
      if (supports_fuel_event(program.nodes.at(idx).kind, FuelEvent::Operation))
        operation_cost_ = event_cost(*spec, FuelEvent::Operation);
    }
    const auto end = compile_expr_impl(program, idx);
    operation_cost_ = saved;
    return end;
  }

  std::size_t compile_expr_impl(const AstProgram& program, std::size_t idx) {
    const AstNode& node = node_at(program, idx);
    switch (node.kind) {
      case NodeKind::BOUNDED_REGION:
        return compile_bounded_prefix(program, idx);
      case NodeKind::CHECK_INT:
      case NodeKind::CHECK_LIST: {
        has_general_regions_ = true;
        const auto end = compile_expr_prefix(program, idx + 1);
        emit(node.kind == NodeKind::CHECK_INT ? Opcode::CheckInt : Opcode::CheckList);
        return end;
      }
      case NodeKind::REGION_VAR: {
        has_general_regions_ = true;
        if (node.i0 < 0 || node.i0 == std::numeric_limits<int>::max())
          throw std::invalid_argument("prefix compile: invalid lexical binder ID");
        const auto it = binder_stack_.find(-node.i0 - 1);
        if (it == binder_stack_.end() || it->second.empty())
          throw std::invalid_argument("prefix compile: undefined lexical binder");
        emit(Opcode::Load, it->second.back(), true);
        return idx + 1;
      }
      case NodeKind::LET_REGION:
      case NodeKind::TRAVERSE:
      case NodeKind::TRAVERSE_RANGE:
        return compile_region_prefix(program, idx);
      case NodeKind::CONST:
        emit(Opcode::PushConst, add_const(const_at(program, node.i0)), true);
        return idx + 1;
      case NodeKind::VAR:
        emit(Opcode::Load, local(name_at(program, node.i0)), true);
        return idx + 1;
      case NodeKind::BOUND_VAR:
        emit(Opcode::Load, bound_local(program, node.i0), true);
        return idx + 1;
      case NodeKind::NEG:
      case NodeKind::NOT: {
        const std::size_t next = compile_expr_prefix(program, idx + 1);
        emit(node.kind == NodeKind::NEG ? Opcode::Neg : Opcode::Not);
        return next;
      }
      case NodeKind::AND: {
        const std::string false_label = new_label("and_false");
        const std::string end_label = new_label("and_end");
        std::size_t next = compile_expr_prefix(program, idx + 1);
        emit_jump(Opcode::JmpIfFalse, false_label);
        next = compile_expr_prefix(program, next);
        emit(Opcode::Not);
        emit(Opcode::Not);
        emit_jump(Opcode::Jmp, end_label);
        mark_label(false_label);
        emit(Opcode::PushConst, add_const(Value::from_bool(false)), true);
        mark_label(end_label);
        return next;
      }
      case NodeKind::OR: {
        const std::string true_label = new_label("or_true");
        const std::string end_label = new_label("or_end");
        std::size_t next = compile_expr_prefix(program, idx + 1);
        emit_jump(Opcode::JmpIfTrue, true_label);
        next = compile_expr_prefix(program, next);
        emit(Opcode::Not);
        emit(Opcode::Not);
        emit_jump(Opcode::Jmp, end_label);
        mark_label(true_label);
        emit(Opcode::PushConst, add_const(Value::from_bool(true)), true);
        mark_label(end_label);
        return next;
      }
      case NodeKind::ADD:
      case NodeKind::SUB:
      case NodeKind::MUL:
      case NodeKind::DIV:
      case NodeKind::MOD:
      case NodeKind::LT:
      case NodeKind::LE:
      case NodeKind::GT:
      case NodeKind::GE:
      case NodeKind::EQ:
      case NodeKind::NE: {
        std::size_t next = compile_expr_prefix(program, idx + 1);
        next = compile_expr_prefix(program, next);
        emit(op_name(node.kind));
        return next;
      }
      case NodeKind::IF_EXPR: {
        const std::string else_label = new_label("ifexpr_else");
        const std::string end_label = new_label("ifexpr_end");
        std::size_t next = compile_expr_prefix(program, idx + 1);
        fuel_event(program, idx, FuelEvent::BranchTest, [&] { emit_jump(Opcode::JmpIfFalse, else_label); });
        const auto nonempty_source = zero_compared_length(program, idx + 1);
        const bool positive_then = program.nodes[idx + 1].kind == NodeKind::NE;
        const bool already_positive = nonempty_source &&
            positive_lengths_.count(*nonempty_source);
        if (nonempty_source && positive_then) positive_lengths_.insert(*nonempty_source);
        next = compile_expr_prefix(program, next);
        if (nonempty_source && !already_positive) positive_lengths_.erase(*nonempty_source);
        fuel_event(program, idx, FuelEvent::BranchMerge, [&] { emit_jump(Opcode::Jmp, end_label); });
        mark_label(else_label);
        if (nonempty_source && !positive_then) positive_lengths_.insert(*nonempty_source);
        next = compile_expr_prefix(program, next);
        if (nonempty_source && !already_positive) positive_lengths_.erase(*nonempty_source);
        mark_label(end_label);
        return next;
      }
      case NodeKind::CALL_ABS:
      case NodeKind::CALL_MIN:
      case NodeKind::CALL_MAX:
      case NodeKind::CALL_CLIP:
      case NodeKind::CALL_IDIV0:
      case NodeKind::CALL_IMOD0:
      case NodeKind::CALL_LEN:
      case NodeKind::CALL_CONCAT:
      case NodeKind::CALL_SLICE:
      case NodeKind::CALL_INDEX:
      case NodeKind::CALL_APPEND:
      case NodeKind::CALL_PREPEND:
      case NodeKind::CALL_REVERSE:
      case NodeKind::CALL_FIND:
      case NodeKind::CALL_CONTAINS:
      case NodeKind::CALL_CHAR_TO_STRING:
      case NodeKind::CALL_STRING_TO_CHAR:
      case NodeKind::CALL_ORD:
      case NodeKind::CALL_CHR:
      case NodeKind::CALL_IS_LETTER:
      case NodeKind::CALL_IS_DIGIT:
      case NodeKind::CALL_IS_SPACE:
      case NodeKind::CALL_IS_VOWEL:
      case NodeKind::CALL_TO_LOWER:
      case NodeKind::CALL_TO_UPPER:
      case NodeKind::CALL_TO_STRING:
      case NodeKind::CALL_SINGLETON: {
        std::size_t next = idx + 1;
        const int argc = node_prefix_arity(node);
        for (int i = 0; i < argc; ++i) {
          next = compile_expr_prefix(program, next);
        }
        const NodeDescriptor& descriptor = node_descriptor(node.kind);
        if (!descriptor.is_builtin() || descriptor.builtin_arity != argc) {
          throw std::runtime_error("prefix compile: invalid builtin descriptor");
        }
        emit(Opcode::CallBuiltin, descriptor.builtin_id, true, argc, true);
        return next;
      }
      default:
        throw std::runtime_error("prefix compile: expected expr node");
    }
  }

  std::size_t compile_block_prefix(const AstProgram& program, std::size_t idx) {
    const AstNode& node = node_at(program, idx);
    if (node.kind == NodeKind::BLOCK_NIL) {
      return idx + 1;
    }
    if (node.kind != NodeKind::BLOCK_CONS) {
      throw std::runtime_error("prefix compile: expected block node");
    }
    const std::size_t next = compile_stmt_prefix(program, idx + 1);
    return compile_block_prefix(program, next);
  }

  std::size_t compile_stmt_prefix(const AstProgram& program, std::size_t idx) {
    const AstNode& node = node_at(program, idx);
    if (node.kind == NodeKind::ASSIGN) {
      const std::size_t next = compile_expr_prefix(program, idx + 1);
      emit(Opcode::Store, local(name_at(program, node.i0)), true);
      return next;
    }
    if (node.kind == NodeKind::RETURN) {
      const std::size_t next = compile_expr_prefix(program, idx + 1);
      emit(Opcode::Return);
      return next;
    }
    if (node.kind == NodeKind::IF_STMT) {
      const std::string else_label = new_label("if_else");
      const std::string end_label = new_label("if_end");
      std::size_t next = compile_expr_prefix(program, idx + 1);
      emit_jump(Opcode::JmpIfFalse, else_label);
      next = compile_block_prefix(program, next);
      emit_jump(Opcode::Jmp, end_label);
      mark_label(else_label);
      next = compile_block_prefix(program, next);
      mark_label(end_label);
      return next;
    }
    if (node.kind == NodeKind::FOR_RANGE) {
      const int bound_local = local(new_temp());
      const std::string valid_label = new_label("for_valid");
      const std::string bad_label = new_label("for_bad");
      std::size_t next = compile_expr_prefix(program, idx + 1);
      emit(Opcode::Store, bound_local, true);
      emit(Opcode::Load, bound_local, true);
      emit(Opcode::CallBuiltin, static_cast<int>(gagp::BuiltinId::IsInt), true, 1, true);
      emit_jump(Opcode::JmpIfFalse, bad_label);
      emit(Opcode::Load, bound_local, true);
      emit(Opcode::PushConst, add_const(Value::from_int(0)), true);
      emit(Opcode::Lt);
      emit_jump(Opcode::JmpIfFalse, valid_label);
      mark_label(bad_label);
      emit(Opcode::PushConst, add_const(Value::from_bool(true)), true);
      emit(Opcode::Neg);
      mark_label(valid_label);
      return compile_for_loop_body(name_at(program, node.i0), bound_local, program, next);
    }
    throw std::runtime_error("prefix compile: expected stmt node");
  }

  std::vector<Value> consts_;
  std::vector<Instr> code_;
  std::vector<std::uint32_t> fuel_;
  std::uint32_t operation_cost_ = 1;
  bool has_source_fuel_ = false;
  mutable const AstProgram* fuel_program_ = nullptr;
  mutable std::vector<const NodeFuelSpec*> fuel_specs_;
  std::vector<UnresolvedJump> unresolved_;
  std::unordered_map<std::string, int> labels_;
  std::unordered_map<std::string, int> var2idx_;
  std::unordered_map<int, std::vector<int>> binder_stack_;
  std::set<int> integer_binders_;
  std::unordered_map<int, int> observed_lengths_;
  std::unordered_map<int, LengthOffset> length_offsets_;
  std::set<int> positive_lengths_;
  std::vector<BoundedRegionSegment> bounded_region_segments_;
  bool has_general_regions_ = false;
  int label_counter_ = 0;
  int tmp_counter_ = 0;
  const VerifiedAst* verified_ = nullptr;
};

}  // namespace

BytecodeProgram compile_for_eval(const ProgramGenome& genome,
                                 const std::vector<std::string>& preset_locals) {
  Compiler compiler(&preset_locals);
  return compiler.build(genome.ast);
}

BytecodeProgram compile_for_eval(const ProgramGenome& genome,
                                 const VerifiedAst& verified,
                                 const std::vector<std::string>& preset_locals) {
  Compiler compiler(&preset_locals, &verified);
  return compiler.build(genome.ast);
}

}  // namespace gagp::evo
