#include "gagp/core/bytecode_verify.hpp"
#include "gagp/core/semantic_fuel.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <optional>
#include <memory>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gagp/core/builtin.hpp"

namespace gagp {

const char* bytecode_verify_code_name(BytecodeVerifyCode code) noexcept {
  switch (code) {
    case BytecodeVerifyCode::Ok: return "ok";
    case BytecodeVerifyCode::InvalidLocalCount: return "invalid_local_count";
    case BytecodeVerifyCode::InvalidConstant: return "invalid_constant";
    case BytecodeVerifyCode::UnknownOpcode: return "unknown_opcode";
    case BytecodeVerifyCode::MissingOperand: return "missing_operand";
    case BytecodeVerifyCode::InvalidConstantIndex: return "invalid_constant_index";
    case BytecodeVerifyCode::InvalidLocalIndex: return "invalid_local_index";
    case BytecodeVerifyCode::InvalidBuiltinId: return "invalid_builtin_id";
    case BytecodeVerifyCode::InvalidBuiltinArity: return "invalid_builtin_arity";
    case BytecodeVerifyCode::InvalidJumpTarget: return "invalid_jump_target";
    case BytecodeVerifyCode::InvalidPrivateOpcode: return "invalid_private_opcode";
    case BytecodeVerifyCode::InvalidSegmentIndex: return "invalid_segment_index";
    case BytecodeVerifyCode::StackUnderflow: return "stack_underflow";
    case BytecodeVerifyCode::StackJoinMismatch: return "stack_join_mismatch";
    case BytecodeVerifyCode::InvalidFallthrough: return "invalid_fallthrough";
    case BytecodeVerifyCode::InvalidVarMapping: return "invalid_var_mapping";
    case BytecodeVerifyCode::InvalidBinderLocal: return "invalid_binder_local";
    case BytecodeVerifyCode::InvalidSegmentMetadata: return "invalid_segment_metadata";
    case BytecodeVerifyCode::InvalidFuelSchedule: return "invalid_fuel_schedule";
    case BytecodeVerifyCode::ResourceLimit: return "resource_limit";
  }
  return "unknown";
}

namespace {

enum class AbstractType : std::uint8_t {
  Unknown,
  Int,
  Float,
  Bool,
  Char,
  String,
  IntList,
  FloatList,
  StringList,
};

struct AbstractLocals {
  std::unordered_map<int, AbstractType> known_locals;
  std::set<int> initialized_locals;
};

struct AbstractState {
  std::vector<AbstractType> stack;
  // Only reached states own locals. Instruction propagation shares the immutable
  // environment; Store and joins detach before mutation, including loop backedges.
  std::shared_ptr<AbstractLocals> locals;

  AbstractLocals& mutable_locals() {
    if (!locals) locals = std::make_shared<AbstractLocals>();
    else if (!locals.unique()) locals = std::make_shared<AbstractLocals>(*locals);
    return *locals;
  }
};

struct CodeSummary {
  std::size_t max_stack_depth = 0;
  bool has_reachable_return = false;
};

bool is_public_value_tag(ValueTag tag) {
  return tag == ValueTag::Int || tag == ValueTag::Float || tag == ValueTag::Bool ||
         tag == ValueTag::Char || tag == ValueTag::String || tag == ValueTag::IntList ||
         tag == ValueTag::FloatList || tag == ValueTag::StringList;
}

bool is_valid_char(std::int64_t value) {
  return value >= 0 && value <= 0x10FFFF && !(value >= 0xD800 && value <= 0xDFFF);
}

AbstractType abstract_type(ValueTag tag) {
  switch (tag) {
    case ValueTag::Int: return AbstractType::Int;
    case ValueTag::Float: return AbstractType::Float;
    case ValueTag::Bool: return AbstractType::Bool;
    case ValueTag::Char: return AbstractType::Char;
    case ValueTag::String: return AbstractType::String;
    case ValueTag::IntList: return AbstractType::IntList;
    case ValueTag::FloatList: return AbstractType::FloatList;
    case ValueTag::StringList: return AbstractType::StringList;
    case ValueTag::FallbackToken:
    case ValueTag::Invalid: return AbstractType::Unknown;
  }
  return AbstractType::Unknown;
}

bool is_numeric(AbstractType type) {
  return type == AbstractType::Int || type == AbstractType::Float;
}

bool is_typed_list(AbstractType type) {
  return type == AbstractType::IntList || type == AbstractType::FloatList ||
         type == AbstractType::StringList;
}

bool is_known_opcode(Opcode op) {
  switch (op) {
    case Opcode::PushConst:
    case Opcode::Load:
    case Opcode::Store:
    case Opcode::Neg:
    case Opcode::Not:
    case Opcode::Add:
    case Opcode::Sub:
    case Opcode::Mul:
    case Opcode::Div:
    case Opcode::Mod:
    case Opcode::Lt:
    case Opcode::Le:
    case Opcode::Gt:
    case Opcode::Ge:
    case Opcode::Eq:
    case Opcode::Ne:
    case Opcode::Jmp:
    case Opcode::JmpIfFalse:
    case Opcode::JmpIfTrue:
    case Opcode::CallBuiltin:
    case Opcode::Return:
    case Opcode::CheckList:
    case Opcode::CheckInt:
    case Opcode::EmptyList:
    case Opcode::EmptyListLike:
    case Opcode::BoundedRegion:
      return true;
  }
  return false;
}

bool is_private_opcode(Opcode op) {
  return op == Opcode::CheckList || op == Opcode::CheckInt || op == Opcode::EmptyList ||
         op == Opcode::EmptyListLike || op == Opcode::BoundedRegion;
}

bool is_structured_opcode(Opcode op) {
  return op == Opcode::BoundedRegion;
}

int expected_builtin_arity(BuiltinId id) {
  switch (id) {
    case BuiltinId::Abs:
    case BuiltinId::Len:
    case BuiltinId::Reverse:
    case BuiltinId::IsInt:
    case BuiltinId::CharToString:
    case BuiltinId::StringToChar:
    case BuiltinId::Ord:
    case BuiltinId::Chr:
    case BuiltinId::IsLetter:
    case BuiltinId::IsDigit:
    case BuiltinId::IsSpace:
    case BuiltinId::IsVowel:
    case BuiltinId::ToLower:
    case BuiltinId::ToUpper:
    case BuiltinId::ToString:
    case BuiltinId::Singleton: return 1;
    case BuiltinId::Min:
    case BuiltinId::Max:
    case BuiltinId::Concat:
    case BuiltinId::Index:
    case BuiltinId::Append:
    case BuiltinId::Find:
    case BuiltinId::Contains:
    case BuiltinId::IDiv0:
    case BuiltinId::IMod0:
    case BuiltinId::Prepend: return 2;
    case BuiltinId::Clip:
    case BuiltinId::Slice: return 3;
  }
  return -1;
}

AbstractType builtin_result_type(BuiltinId id, const std::vector<AbstractType>& args) {
  switch (id) {
    case BuiltinId::Abs:
    case BuiltinId::Min:
    case BuiltinId::Max:
    case BuiltinId::Clip:
    case BuiltinId::Concat:
    case BuiltinId::Slice:
    case BuiltinId::Append:
    case BuiltinId::Prepend:
    case BuiltinId::Reverse:
      return args.empty() ? AbstractType::Unknown : args.front();
    case BuiltinId::Len:
    case BuiltinId::Find:
    case BuiltinId::Ord:
    case BuiltinId::IDiv0:
    case BuiltinId::IMod0: return AbstractType::Int;
    case BuiltinId::Contains:
    case BuiltinId::IsInt:
    case BuiltinId::IsLetter:
    case BuiltinId::IsDigit:
    case BuiltinId::IsSpace:
    case BuiltinId::IsVowel: return AbstractType::Bool;
    case BuiltinId::CharToString:
    case BuiltinId::ToString: return AbstractType::String;
    case BuiltinId::StringToChar:
    case BuiltinId::Chr:
    case BuiltinId::ToLower:
    case BuiltinId::ToUpper: return AbstractType::Char;
    case BuiltinId::Index:
      if (args.empty()) return AbstractType::Unknown;
      if (args.front() == AbstractType::String) return AbstractType::Char;
      if (args.front() == AbstractType::IntList) return AbstractType::Int;
      if (args.front() == AbstractType::FloatList) return AbstractType::Float;
      if (args.front() == AbstractType::StringList) return AbstractType::String;
      return AbstractType::Unknown;
    case BuiltinId::Singleton:
      if (args.empty()) return AbstractType::Unknown;
      if (args.front() == AbstractType::Char) return AbstractType::String;
      if (args.front() == AbstractType::Int) return AbstractType::IntList;
      if (args.front() == AbstractType::Float) return AbstractType::FloatList;
      if (args.front() == AbstractType::String) return AbstractType::StringList;
      return AbstractType::Unknown;
  }
  return AbstractType::Unknown;
}

class Verifier {
 public:
  Verifier(const BytecodeProgram& program, const BytecodeVerifyOptions& options)
      : program_(program), options_(options) {}

  BytecodeVerifyResult run() {
    if (!check_segment_limit()) return result_;
    const auto fuel = validate_semantic_fuel(program_.code, program_.instruction_fuel);
    if (!fuel) {
      fail(BytecodeVerifyCode::InvalidFuelSchedule, fuel.instruction_index, "$.instruction_fuel", fuel.message);
      return result_;
    }
    CodeSummary main_summary;
    if (!verify_code(program_.consts, program_.code, program_.n_locals, program_.var2idx,
                     {}, "$.code", true, true, std::nullopt, &main_summary)) {
      return result_;
    }
    result_.verified.max_stack_depth = main_summary.max_stack_depth;
    result_.verified.has_reachable_return = main_summary.has_reachable_return;
    if (!verify_segments()) return result_;
    result_.ok = true;
    result_.diagnostic = BytecodeVerifyDiagnostic{};
    return result_;
  }

  BytecodeVerifyResult run_bounded_segment(const BoundedRegionSegment& segment,
                                           int caller_n_locals) {
    if (options_.max_segments != 0 && options_.max_segments < 1) {
      fail(BytecodeVerifyCode::ResourceLimit, 0, "$.segments.bounded_region",
           "segment count exceeds configured limit");
      return result_;
    }
    if (!verify_bounded_segment(segment, caller_n_locals,
                                "$.segments.bounded_region[0]")) {
      return result_;
    }
    result_.ok = true;
    result_.diagnostic = BytecodeVerifyDiagnostic{};
    return result_;
  }

 private:
  bool fail(BytecodeVerifyCode code, std::size_t instruction_index,
            std::string path, std::string message) {
    result_.ok = false;
    result_.diagnostic = BytecodeVerifyDiagnostic{
        code, instruction_index, std::move(path), std::move(message)};
    return false;
  }

  bool check_segment_limit() {
    const std::size_t segments = program_.bounded_region_segments.size();
    if (options_.max_segments != 0 && segments > options_.max_segments) {
      return fail(BytecodeVerifyCode::ResourceLimit, 0, "$.segments",
                  "segment count exceeds configured limit");
    }
    return true;
  }

  bool validate_value(const Value& value, std::size_t index, const std::string& path) {
    if (!is_public_value_tag(value.tag)) {
      return fail(BytecodeVerifyCode::InvalidConstant, index, path,
                  "constant uses a non-public value tag");
    }
    if (value.tag == ValueTag::Char && !is_valid_char(value.i)) {
      return fail(BytecodeVerifyCode::InvalidConstant, index, path,
                  "Char constant is not a Unicode scalar value");
    }
    return true;
  }

  bool validate_maps(int n_locals,
                     const std::unordered_map<std::string, int>& var2idx,
                     const std::string& owner_path) {
    std::set<int> mapped_locals;
    for (const auto& item : var2idx) {
      if (item.second < 0 || item.second >= n_locals) {
        return fail(BytecodeVerifyCode::InvalidVarMapping, 0, owner_path + ".var2idx",
                    "variable mapping local is out of range");
      }
      if (!mapped_locals.insert(item.second).second) {
        return fail(BytecodeVerifyCode::InvalidVarMapping, 0, owner_path + ".var2idx",
                    "multiple variable names alias one local");
      }
    }
    return true;
  }

  bool validate_instruction(const Instr& ins, std::size_t ip,
                            std::size_t code_size, std::size_t const_count,
                            int n_locals, bool allow_structured,
                            const std::string& code_path) {
    if (!is_known_opcode(ins.op)) {
      return fail(BytecodeVerifyCode::UnknownOpcode, ip,
                  code_path + "[" + std::to_string(ip) + "].op", "unknown opcode value");
    }
    const Opcode op = ins.op;
    if (is_private_opcode(op) && !options_.allow_private_opcodes) {
      return fail(BytecodeVerifyCode::InvalidPrivateOpcode, ip,
                  code_path + "[" + std::to_string(ip) + "].op",
                  "private opcode is disabled for this verification profile");
    }
    if (is_structured_opcode(op) && !allow_structured) {
      return fail(BytecodeVerifyCode::InvalidPrivateOpcode, ip,
                  code_path + "[" + std::to_string(ip) + "].op",
                  "bounded region calls are forbidden inside phase programs");
    }

    const bool requires_a = op == Opcode::PushConst || op == Opcode::Load || op == Opcode::Store ||
                            op == Opcode::Jmp || op == Opcode::JmpIfFalse || op == Opcode::JmpIfTrue ||
                            op == Opcode::CallBuiltin || op == Opcode::EmptyList ||
                            is_structured_opcode(op);
    if (requires_a && !ins.has_a) {
      return fail(BytecodeVerifyCode::MissingOperand, ip,
                  code_path + "[" + std::to_string(ip) + "].a",
                  std::string(opcode_name(op)) + " requires operand a");
    }
    if (op == Opcode::CallBuiltin && !ins.has_b) {
      return fail(BytecodeVerifyCode::MissingOperand, ip,
                  code_path + "[" + std::to_string(ip) + "].b",
                  "CALL_BUILTIN requires operand b");
    }
    if (op == Opcode::PushConst && (ins.a < 0 || static_cast<std::size_t>(ins.a) >= const_count)) {
      return fail(BytecodeVerifyCode::InvalidConstantIndex, ip,
                  code_path + "[" + std::to_string(ip) + "].a", "constant index is out of range");
    }
    if ((op == Opcode::Load || op == Opcode::Store) && (ins.a < 0 || ins.a >= n_locals)) {
      return fail(BytecodeVerifyCode::InvalidLocalIndex, ip,
                  code_path + "[" + std::to_string(ip) + "].a", "local index is out of range");
    }
    if ((op == Opcode::Jmp || op == Opcode::JmpIfFalse || op == Opcode::JmpIfTrue) &&
        (ins.a < 0 || static_cast<std::size_t>(ins.a) > code_size)) {
      return fail(BytecodeVerifyCode::InvalidJumpTarget, ip,
                  code_path + "[" + std::to_string(ip) + "].a", "jump target is out of range");
    }
    if (op == Opcode::CallBuiltin) {
      BuiltinId id = BuiltinId::Abs;
      if (!builtin_id_from_int(ins.a, id)) {
        return fail(BytecodeVerifyCode::InvalidBuiltinId, ip,
                    code_path + "[" + std::to_string(ip) + "].a", "unknown builtin id");
      }
      if (ins.b != expected_builtin_arity(id)) {
        return fail(BytecodeVerifyCode::InvalidBuiltinArity, ip,
                    code_path + "[" + std::to_string(ip) + "].b",
                    std::string(builtin_name(id)) + " has the wrong bytecode arity");
      }
    }
    if (op == Opcode::EmptyList && (ins.a < 1 || ins.a > 3)) {
      return fail(BytecodeVerifyCode::InvalidConstant, ip,
                  code_path + "[" + std::to_string(ip) + "].a", "invalid typed-list tag");
    }
    if (op == Opcode::BoundedRegion &&
        (ins.a < 0 || static_cast<std::size_t>(ins.a) >=
                          program_.bounded_region_segments.size())) {
      return fail(BytecodeVerifyCode::InvalidSegmentIndex, ip,
                  code_path + "[" + std::to_string(ip) + "].a",
                  "bounded-region segment index is out of range");
    }
    if (op == Opcode::BoundedRegion) {
      try {
        validate_region_plan(
            program_.bounded_region_segments[static_cast<std::size_t>(ins.a)].plan);
      } catch (const std::invalid_argument& error) {
        return fail(BytecodeVerifyCode::InvalidSegmentMetadata, ip,
                    "$.segments.bounded_region[" + std::to_string(ins.a) +
                        "].plan",
                    error.what());
      }
    }
    return true;
  }

  bool merge_state(AbstractState& existing, const AbstractState& incoming,
                   std::size_t ip, const std::string& code_path,
                   bool track_initialization, bool* changed) {
    if (existing.stack.size() != incoming.stack.size()) {
      return fail(BytecodeVerifyCode::StackJoinMismatch, ip,
                  code_path + "[" + std::to_string(ip) + "]",
                  "control-flow join has inconsistent stack depth");
    }
    *changed = false;
    for (std::size_t i = 0; i < existing.stack.size(); ++i) {
      AbstractType& dst = existing.stack[i];
      const AbstractType src = incoming.stack[i];
      if (dst == src || dst == AbstractType::Unknown) continue;
      if (src == AbstractType::Unknown) {
        dst = AbstractType::Unknown;
        *changed = true;
        continue;
      }
      return fail(BytecodeVerifyCode::StackJoinMismatch, ip,
                  code_path + "[" + std::to_string(ip) + "]",
                  "control-flow join has inconsistent stack types");
    }
    if (existing.locals == incoming.locals) return true;
    auto& destination = existing.mutable_locals();
    const auto& source = *incoming.locals;
    if (!track_initialization) {
      for (auto it = destination.known_locals.begin();
           it != destination.known_locals.end();) {
        const auto incoming_it = source.known_locals.find(it->first);
        if (incoming_it == source.known_locals.end() ||
            incoming_it->second != it->second) {
          it = destination.known_locals.erase(it);
          *changed = true;
        } else {
          ++it;
        }
      }
    } else {
      for (int local : source.initialized_locals) {
        const bool already_initialized =
            destination.initialized_locals.count(local) != 0;
        if (!already_initialized) {
          destination.initialized_locals.insert(local);
          const auto incoming_type = source.known_locals.find(local);
          if (incoming_type != source.known_locals.end()) {
            destination.known_locals[local] = incoming_type->second;
          } else {
            destination.known_locals.erase(local);
          }
          *changed = true;
          continue;
        }
        const auto existing_type = destination.known_locals.find(local);
        const auto incoming_type = source.known_locals.find(local);
        if (existing_type != destination.known_locals.end() &&
            (incoming_type == source.known_locals.end() ||
             incoming_type->second != existing_type->second)) {
          destination.known_locals.erase(existing_type);
          *changed = true;
        }
      }
    }
    return true;
  }

  bool enqueue(std::size_t target, const AbstractState& state,
               std::vector<AbstractState>& states, std::vector<bool>& seen,
               std::deque<std::size_t>& work, const std::string& code_path,
               bool track_initialization) {
    if (!seen[target]) {
      states[target] = state;
      seen[target] = true;
      work.push_back(target);
      return true;
    }
    bool changed = false;
    if (!merge_state(states[target], state, target, code_path,
                     track_initialization, &changed)) return false;
    if (changed) work.push_back(target);
    return true;
  }

  bool verify_code(const std::vector<Value>& consts, const std::vector<Instr>& code,
                   int n_locals, const std::unordered_map<std::string, int>& var2idx,
                   const std::unordered_map<int, AbstractType>& initial_locals,
                   const std::string& code_path, bool require_return,
                   bool allow_structured,
                   std::optional<AbstractType> expected_success_type,
                   CodeSummary* summary) {
    const std::string owner_path = code_path.substr(0, code_path.size() - 5);
    if (n_locals < 0) {
      return fail(BytecodeVerifyCode::InvalidLocalCount, 0, owner_path + ".n_locals",
                  "n_locals must be non-negative");
    }
    if ((options_.max_instructions_per_code != 0 && code.size() > options_.max_instructions_per_code) ||
        (options_.max_constants_per_code != 0 && consts.size() > options_.max_constants_per_code) ||
        (options_.max_locals_per_code != 0 &&
         static_cast<std::size_t>(n_locals) > options_.max_locals_per_code)) {
      return fail(BytecodeVerifyCode::ResourceLimit, 0, owner_path,
                  "bytecode resource count exceeds configured limit");
    }
    if (!validate_maps(n_locals, var2idx, owner_path)) return false;
    for (std::size_t i = 0; i < consts.size(); ++i) {
      if (!validate_value(consts[i], i, owner_path + ".consts[" + std::to_string(i) + "]")) return false;
    }
    std::vector<bool> jump_entries(expected_success_type ? code.size() + 1 : 0, false);
    for (std::size_t ip = 0; ip < code.size(); ++ip) {
      if (!validate_instruction(code[ip], ip, code.size(), consts.size(), n_locals,
                                allow_structured, code_path)) return false;
      if (expected_success_type &&
          (code[ip].op == Opcode::Jmp || code[ip].op == Opcode::JmpIfFalse ||
           code[ip].op == Opcode::JmpIfTrue)) {
        jump_entries[static_cast<std::size_t>(code[ip].a)] = true;
      }
    }

    std::vector<AbstractState> states(code.size() + 1);
    std::vector<bool> seen(code.size() + 1, false);
    std::deque<std::size_t> work;
    AbstractState start;
    auto& start_locals = start.mutable_locals();
    for (const auto& item : initial_locals) {
      if (item.first < 0 || item.first >= n_locals) {
        return fail(BytecodeVerifyCode::InvalidBinderLocal, 0, owner_path + ".binder_locals",
                    "preset binder local is out of range");
      }
      if (expected_success_type) start_locals.initialized_locals.insert(item.first);
      if (item.second != AbstractType::Unknown) start_locals.known_locals[item.first] = item.second;
    }
    states[0] = start;
    seen[0] = true;
    work.push_back(0);

    while (!work.empty()) {
      const std::size_t ip = work.front();
      work.pop_front();
      AbstractState state = states[ip];
      summary->max_stack_depth = std::max(summary->max_stack_depth, state.stack.size());
      result_.verified.max_stack_depth =
          std::max(result_.verified.max_stack_depth, summary->max_stack_depth);
      if (options_.max_stack_depth != 0 && state.stack.size() > options_.max_stack_depth) {
        return fail(BytecodeVerifyCode::ResourceLimit, ip, code_path,
                    "operand stack exceeds configured limit");
      }
      if (ip == code.size()) {
        if (require_return) {
          return fail(BytecodeVerifyCode::InvalidFallthrough, ip, code_path,
                      "main bytecode has a reachable path without RETURN");
        }
        if (state.stack.size() != 1) {
          return fail(BytecodeVerifyCode::InvalidFallthrough, ip, code_path,
                      "phase fallthrough must produce exactly one value");
        }
        if (expected_success_type && state.stack.back() != *expected_success_type) {
          return fail(BytecodeVerifyCode::InvalidFallthrough, ip, code_path,
                      "phase success value does not have the required exact type");
        }
        continue;
      }

      const Instr& ins = code[ip];
      const Opcode op = ins.op;
      auto require_stack = [&](std::size_t count) -> bool {
        if (state.stack.size() >= count) return true;
        fail(BytecodeVerifyCode::StackUnderflow, ip,
             code_path + "[" + std::to_string(ip) + "]",
             std::string(opcode_name(op)) + " can underflow the operand stack");
        return false;
      };
      bool guaranteed_runtime_error = false;

      if (op == Opcode::PushConst) {
        state.stack.push_back(abstract_type(consts[static_cast<std::size_t>(ins.a)].tag));
      } else if (op == Opcode::Load) {
        if (expected_success_type && state.locals->initialized_locals.count(ins.a) == 0) {
          guaranteed_runtime_error = true;
        }
        const auto local = state.locals->known_locals.find(ins.a);
        if (!guaranteed_runtime_error) {
          state.stack.push_back(local == state.locals->known_locals.end()
                                    ? AbstractType::Unknown
                                    : local->second);
        }
      } else if (op == Opcode::Store) {
        if (!require_stack(1)) return false;
        state.mutable_locals();
        if (state.stack.back() == AbstractType::Unknown) {
          state.locals->known_locals.erase(ins.a);
        } else {
          state.locals->known_locals[ins.a] = state.stack.back();
        }
        if (expected_success_type) state.locals->initialized_locals.insert(ins.a);
        state.stack.pop_back();
      } else if (op == Opcode::CheckList || op == Opcode::CheckInt) {
        if (!require_stack(1)) return false;
        const AbstractType type = state.stack.back();
        if (type != AbstractType::Unknown &&
            ((op == Opcode::CheckList && !is_typed_list(type)) ||
             (op == Opcode::CheckInt && type != AbstractType::Int))) {
          guaranteed_runtime_error = true;
        } else if (expected_success_type && op == Opcode::CheckInt &&
                   type == AbstractType::Unknown) {
          state.stack.back() = AbstractType::Int;
        }
      } else if (op == Opcode::EmptyList) {
        state.stack.push_back(ins.a == 1 ? AbstractType::IntList
                                         : (ins.a == 2 ? AbstractType::FloatList
                                                       : AbstractType::StringList));
      } else if (op == Opcode::EmptyListLike) {
        if (!require_stack(1)) return false;
        const AbstractType source = state.stack.back();
        if (source != AbstractType::Unknown && !is_typed_list(source)) {
          guaranteed_runtime_error = true;
        } else if (source == AbstractType::Unknown) {
          state.stack.back() = AbstractType::Unknown;
        }
      } else if (op == Opcode::Neg || op == Opcode::Not) {
        if (!require_stack(1)) return false;
        const AbstractType operand = state.stack.back();
        if (operand != AbstractType::Unknown &&
            ((op == Opcode::Neg && !is_numeric(operand)) ||
             (op == Opcode::Not && operand != AbstractType::Bool))) {
          guaranteed_runtime_error = true;
        } else {
          state.stack.back() = op == Opcode::Not ? AbstractType::Bool : operand;
        }
      } else if (op == Opcode::Add || op == Opcode::Sub || op == Opcode::Mul ||
                 op == Opcode::Div || op == Opcode::Mod) {
        if (!require_stack(2)) return false;
        const AbstractType rhs = state.stack.back();
        state.stack.pop_back();
        const AbstractType lhs = state.stack.back();
        state.stack.pop_back();
        const bool definitely_invalid = expected_success_type
            ? ((lhs != AbstractType::Unknown && !is_numeric(lhs)) ||
               (rhs != AbstractType::Unknown && !is_numeric(rhs)) ||
               (lhs != AbstractType::Unknown && rhs != AbstractType::Unknown &&
                lhs != rhs))
            : (lhs != AbstractType::Unknown && rhs != AbstractType::Unknown &&
               (!is_numeric(lhs) || lhs != rhs));
        // With no jump entering this instruction, an immediately preceding
        // PUSH_CONST is the only possible producer of the right operand.
        // Division/modulo by numeric zero cannot reach a successful return,
        // regardless of the left operand's type. Keep the bytecode and fuel
        // unchanged; this only excludes an impossible success from type proof.
        bool literal_zero_divisor = false;
        if (expected_success_type && (op == Opcode::Div || op == Opcode::Mod) &&
            ip > 0 && !jump_entries[ip] && code[ip - 1].op == Opcode::PushConst) {
          const Value& divisor = consts[static_cast<std::size_t>(code[ip - 1].a)];
          literal_zero_divisor = (divisor.tag == ValueTag::Int && divisor.i == 0) ||
                                 (divisor.tag == ValueTag::Float && divisor.f == 0.0);
        }
        if (definitely_invalid || literal_zero_divisor) {
          guaranteed_runtime_error = true;
        } else {
          AbstractType out = AbstractType::Unknown;
          if (expected_success_type) {
            const AbstractType known =
                lhs != AbstractType::Unknown ? lhs : rhs;
            if (is_numeric(known)) {
              out = op == Opcode::Div ? AbstractType::Float : known;
            }
          } else if (lhs == rhs && is_numeric(lhs)) {
            out = op == Opcode::Div ? AbstractType::Float : lhs;
          }
          state.stack.push_back(out);
        }
      } else if (op == Opcode::Lt || op == Opcode::Le || op == Opcode::Gt ||
                 op == Opcode::Ge || op == Opcode::Eq || op == Opcode::Ne) {
        if (!require_stack(2)) return false;
        const AbstractType rhs = state.stack.back();
        state.stack.pop_back();
        const AbstractType lhs = state.stack.back();
        state.stack.pop_back();
        const bool ordering = op == Opcode::Lt || op == Opcode::Le || op == Opcode::Gt || op == Opcode::Ge;
        if (lhs != AbstractType::Unknown && rhs != AbstractType::Unknown &&
            (lhs != rhs || (ordering && !is_numeric(lhs)))) {
          guaranteed_runtime_error = true;
        } else {
          state.stack.push_back(AbstractType::Bool);
        }
      } else if (op == Opcode::JmpIfFalse || op == Opcode::JmpIfTrue) {
        if (!require_stack(1)) return false;
        const AbstractType cond = state.stack.back();
        state.stack.pop_back();
        if (cond != AbstractType::Unknown && cond != AbstractType::Bool) {
          guaranteed_runtime_error = true;
        }
      } else if (op == Opcode::CallBuiltin) {
        const std::size_t argc = static_cast<std::size_t>(ins.b);
        if (!require_stack(argc)) return false;
        std::vector<AbstractType> args(state.stack.end() - static_cast<std::ptrdiff_t>(argc),
                                       state.stack.end());
        state.stack.resize(state.stack.size() - argc);
        BuiltinId id = BuiltinId::Abs;
        (void)builtin_id_from_int(ins.a, id);
        state.stack.push_back(builtin_result_type(id, args));
      } else if (op == Opcode::BoundedRegion) {
        const BoundedRegionSegment& segment =
            program_.bounded_region_segments[static_cast<std::size_t>(ins.a)];
        const std::size_t state_count = segment.plan.state_types.size();
        const std::size_t argc = state_count + segment.plan.bound_operand_count;
        if (!require_stack(argc)) return false;
        for (std::size_t i = 0; i < state_count; ++i) {
          const AbstractType actual = state.stack[state.stack.size() - argc + i];
          const AbstractType expected = abstract_type(segment.plan.state_types[i]);
          if (actual != AbstractType::Unknown && actual != expected) {
            guaranteed_runtime_error = true;
          }
        }
        for (std::size_t i = state_count; i < argc; ++i) {
          const AbstractType actual = state.stack[state.stack.size() - argc + i];
          if (actual != AbstractType::Unknown && actual != AbstractType::Int) {
            guaranteed_runtime_error = true;
          }
        }
        state.stack.resize(state.stack.size() - argc);
        if (!guaranteed_runtime_error) {
          state.stack.push_back(abstract_type(segment.plan.result_type));
        }
      } else if (op == Opcode::Return) {
        if (!require_stack(1)) return false;
        if (expected_success_type && state.stack.back() != *expected_success_type) {
          return fail(BytecodeVerifyCode::InvalidFallthrough, ip, code_path,
                      "phase RETURN value does not have the required exact type");
        }
        summary->has_reachable_return = true;
        continue;
      }

      summary->max_stack_depth = std::max(summary->max_stack_depth, state.stack.size());
      if (guaranteed_runtime_error) continue;
      if (op == Opcode::Jmp) {
        if (!enqueue(static_cast<std::size_t>(ins.a), state, states, seen,
                     work, code_path, expected_success_type.has_value())) {
          return false;
        }
      } else if (op == Opcode::JmpIfFalse || op == Opcode::JmpIfTrue) {
        if (!enqueue(static_cast<std::size_t>(ins.a), state, states, seen,
                     work, code_path, expected_success_type.has_value()) ||
            !enqueue(ip + 1, state, states, seen, work, code_path,
                     expected_success_type.has_value())) {
          return false;
        }
      } else {
        if (!enqueue(ip + 1, state, states, seen, work, code_path,
                     expected_success_type.has_value())) {
          return false;
        }
      }
    }
    return true;
  }

  bool verify_region_phase(const RegionPhase& phase, const RegionPlan& plan,
                           RegionPhaseKind kind, std::uint32_t preparation,
                           ValueTag output_type, const std::string& path) {
    if (!phase.program.var2idx.empty()) {
      return fail(BytecodeVerifyCode::InvalidVarMapping, 0,
                  path + ".program.var2idx",
                  "generic region phases use explicit slot bindings only");
    }
    if (!phase.program.binder_locals.empty()) {
      return fail(BytecodeVerifyCode::InvalidBinderLocal, 0,
                  path + ".program.binder_locals",
                  "generic region phases use explicit slot bindings only");
    }
    const auto fuel = validate_semantic_fuel(
        phase.program.code, phase.program.instruction_fuel);
    if (!fuel) {
      return fail(BytecodeVerifyCode::InvalidFuelSchedule,
                  fuel.instruction_index, path + ".program.instruction_fuel",
                  fuel.message);
    }

    std::set<std::pair<int, std::uint32_t>> sources;
    std::set<int> destinations;
    std::unordered_map<int, AbstractType> initial;
    for (std::size_t i = 0; i < phase.bindings.size(); ++i) {
      const RegionPhaseBinding& binding = phase.bindings[i];
      const std::string binding_path =
          path + ".bindings[" + std::to_string(i) + "]";
      if (binding.local < 0 || binding.local >= phase.program.n_locals) {
        return fail(BytecodeVerifyCode::InvalidBinderLocal, 0,
                    binding_path + ".local",
                    "region phase binding local is out of range");
      }
      const auto source_key = std::make_pair(
          static_cast<int>(binding.source.bank), binding.source.slot);
      if (!sources.insert(source_key).second) {
        return fail(BytecodeVerifyCode::InvalidSegmentMetadata, 0,
                    binding_path + ".source",
                    "region phase source slot is bound more than once");
      }
      if (!destinations.insert(binding.local).second) {
        return fail(BytecodeVerifyCode::InvalidBinderLocal, 0,
                    binding_path + ".local",
                    "region phase bindings alias one destination local");
      }
      ValueTag type = ValueTag::Invalid;
      try {
        type = region_slot_type(plan, kind, binding.source, preparation);
      } catch (const std::invalid_argument& error) {
        return fail(BytecodeVerifyCode::InvalidSegmentMetadata, 0,
                    binding_path + ".source", error.what());
      }
      initial.emplace(binding.local, abstract_type(type));
    }

    CodeSummary summary;
    if (!verify_code(phase.program.consts, phase.program.code,
                     phase.program.n_locals, phase.program.var2idx, initial,
                     path + ".program.code", false, false,
                     abstract_type(output_type), &summary)) {
      return false;
    }
    result_.verified.phase_program_count += 1;
    return true;
  }

  bool verify_bounded_segment(const BoundedRegionSegment& segment,
                              int caller_n_locals,
                              const std::string& path) {
    if (caller_n_locals < 0) {
      return fail(BytecodeVerifyCode::InvalidLocalCount, 0,
                  path + ".caller_n_locals",
                  "caller n_locals must be non-negative");
    }
    try {
      validate_region_plan(segment.plan);
    } catch (const std::invalid_argument& error) {
      return fail(BytecodeVerifyCode::InvalidSegmentMetadata, 0,
                  path + ".plan", error.what());
    }

    if (segment.parameter_locals.size() !=
        segment.plan.parameter_types.size()) {
      return fail(BytecodeVerifyCode::InvalidSegmentMetadata, 0,
                  path + ".parameter_locals",
                  "parameter locals must match the plan parameter types");
    }
    std::set<int> parameter_locals;
    for (std::size_t i = 0; i < segment.parameter_locals.size(); ++i) {
      const int local = segment.parameter_locals[i];
      if (local < 0 || local >= caller_n_locals) {
        return fail(BytecodeVerifyCode::InvalidLocalIndex, 0,
                    path + ".parameter_locals[" + std::to_string(i) + "]",
                    "region parameter local is out of caller range");
      }
      if (!parameter_locals.insert(local).second) {
        return fail(BytecodeVerifyCode::InvalidSegmentMetadata, 0,
                    path + ".parameter_locals[" + std::to_string(i) + "]",
                    "region parameter locals must be distinct");
      }
    }

    const bool coordinates =
        segment.plan.progress == RegionProgressKind::Coordinates;
    if (coordinates != segment.boundary.has_value()) {
      return fail(BytecodeVerifyCode::InvalidSegmentMetadata, 0,
                  path + ".boundary",
                  coordinates
                      ? "coordinate regions require a boundary phase"
                      : "sequence regions must not contain a boundary phase");
    }
    if (segment.preparations.size() != segment.plan.preparations.size()) {
      return fail(BytecodeVerifyCode::InvalidSegmentMetadata, 0,
                  path + ".preparations",
                  "preparation phase count does not match the plan");
    }
    if (segment.request_expressions.size() !=
        segment.plan.request_expression_types.size()) {
      return fail(BytecodeVerifyCode::InvalidSegmentMetadata, 0,
                  path + ".request_expressions",
                  "request expression phase count does not match the plan");
    }

    if (segment.boundary &&
        !verify_region_phase(*segment.boundary, segment.plan,
                             RegionPhaseKind::Boundary, 0,
                             segment.plan.result_type, path + ".boundary")) {
      return false;
    }
    if (!verify_region_phase(segment.base_predicate, segment.plan,
                             RegionPhaseKind::BasePredicate, 0,
                             ValueTag::Bool, path + ".base_predicate") ||
        !verify_region_phase(segment.base_body, segment.plan,
                             RegionPhaseKind::BaseBody, 0,
                             segment.plan.result_type, path + ".base_body")) {
      return false;
    }
    for (std::size_t i = 0; i < segment.preparations.size(); ++i) {
      if (!verify_region_phase(segment.preparations[i], segment.plan,
                               RegionPhaseKind::Preparation,
                               static_cast<std::uint32_t>(i),
                               segment.plan.preparations[i].type,
                               path + ".preparations[" +
                                   std::to_string(i) + "]")) {
        return false;
      }
    }
    for (std::size_t i = 0; i < segment.request_expressions.size(); ++i) {
      if (!verify_region_phase(segment.request_expressions[i], segment.plan,
                               RegionPhaseKind::Request, 0,
                               segment.plan.request_expression_types[i],
                               path + ".request_expressions[" +
                                   std::to_string(i) + "]")) {
        return false;
      }
    }
    return verify_region_phase(segment.combine, segment.plan,
                               RegionPhaseKind::Combine, 0,
                               segment.plan.result_type, path + ".combine");
  }

  bool verify_segments() {
    for (std::size_t i = 0; i < program_.bounded_region_segments.size(); ++i) {
      if (!verify_bounded_segment(
              program_.bounded_region_segments[i], program_.n_locals,
              "$.segments.bounded_region[" + std::to_string(i) + "]")) {
        return false;
      }
    }
    return true;
  }

  const BytecodeProgram& program_;
  const BytecodeVerifyOptions& options_;
  BytecodeVerifyResult result_;
};

}  // namespace

BytecodeVerifyResult verify_bytecode(const BytecodeProgram& program,
                                     const BytecodeVerifyOptions& options) {
  return Verifier(program, options).run();
}

BytecodeVerifyResult verify_bounded_region_segment(
    const BoundedRegionSegment& segment, int caller_n_locals,
    const BytecodeVerifyOptions& options) {
  static const BytecodeProgram empty_program;
  return Verifier(empty_program, options).run_bounded_segment(
      segment, caller_n_locals);
}

}  // namespace gagp
