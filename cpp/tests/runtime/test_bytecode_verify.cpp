#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "gagp/core/builtin.hpp"
#include "gagp/core/bytecode_verify.hpp"

namespace {

using namespace gagp;

Instr ins(Opcode op) { return Instr{op, 0, 0, false, false}; }
Instr ins_a(Opcode op, int a) { return Instr{op, a, 0, true, false}; }
Instr ins_ab(Opcode op, int a, int b) { return Instr{op, a, b, true, true}; }

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << "\n";
  return condition;
}

bool expect_code(const BytecodeProgram& program, BytecodeVerifyCode code,
                 const std::string& label,
                 const BytecodeVerifyOptions& options = BytecodeVerifyOptions{}) {
  const BytecodeVerifyResult result = verify_bytecode(program, options);
  return check(!result, label + " should fail") &&
         check(result.diagnostic.code == code,
               label + " expected " + bytecode_verify_code_name(code) + " but got " +
                   bytecode_verify_code_name(result.diagnostic.code) + " at " +
                   result.diagnostic.path + ": " + result.diagnostic.message);
}

BytecodeProgram constant_program() {
  BytecodeProgram program;
  program.consts = {Value::from_int(7)};
  program.code = {ins_a(Opcode::PushConst, 0), ins(Opcode::Return)};
  return program;
}

}  // namespace

int main() {
  using namespace gagp;

  BytecodeProgram program = constant_program();
  BytecodeVerifyResult result = verify_bytecode(program);
  if (!check(result.ok && result.verified.has_reachable_return &&
                 result.verified.max_stack_depth == 1,
             "simple constant bytecode verifies")) return 1;

  program.code[0].has_a = false;
  if (!expect_code(program, BytecodeVerifyCode::MissingOperand, "missing operand")) return 1;

  program = constant_program();
  program.code[0].a = 2;
  if (!expect_code(program, BytecodeVerifyCode::InvalidConstantIndex, "constant range")) return 1;

  program = constant_program();
  program.code[0].op = static_cast<Opcode>(255);
  if (!expect_code(program, BytecodeVerifyCode::UnknownOpcode, "unknown opcode")) return 1;

  program = constant_program();
  program.n_locals = 1;
  program.code[0] = ins_a(Opcode::Load, 1);
  if (!expect_code(program, BytecodeVerifyCode::InvalidLocalIndex, "local range")) return 1;

  program = constant_program();
  program.n_locals = -1;
  if (!expect_code(program, BytecodeVerifyCode::InvalidLocalCount, "negative local count")) return 1;

  program = constant_program();
  program.consts[0] = Value::invalid();
  if (!expect_code(program, BytecodeVerifyCode::InvalidConstant, "private constant tag")) return 1;

  program = constant_program();
  program.consts[0] = Value::from_char(0xD800);
  if (!expect_code(program, BytecodeVerifyCode::InvalidConstant, "invalid Char scalar")) return 1;

  program = constant_program();
  program.n_locals = 1;
  program.var2idx["x"] = 1;
  if (!expect_code(program, BytecodeVerifyCode::InvalidVarMapping, "variable mapping range")) return 1;

  program = constant_program();
  program.code = {ins_a(Opcode::Jmp, 3)};
  if (!expect_code(program, BytecodeVerifyCode::InvalidJumpTarget, "jump range")) return 1;

  program = constant_program();
  program.code = {ins(Opcode::Add), ins(Opcode::Return)};
  if (!expect_code(program, BytecodeVerifyCode::StackUnderflow, "stack underflow")) return 1;

  program = constant_program();
  program.code = {ins_ab(Opcode::CallBuiltin, 999, 1), ins(Opcode::Return)};
  if (!expect_code(program, BytecodeVerifyCode::InvalidBuiltinId, "builtin id")) return 1;

  program.code = {ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Abs), 2),
                  ins(Opcode::Return)};
  if (!expect_code(program, BytecodeVerifyCode::InvalidBuiltinArity, "builtin arity")) return 1;

  program.consts = {Value::from_bool(true), Value::from_int(1)};
  program.code = {ins_a(Opcode::PushConst, 0), ins_a(Opcode::JmpIfFalse, 4),
                  ins_a(Opcode::PushConst, 1), ins_a(Opcode::Jmp, 5),
                  ins_a(Opcode::Jmp, 5), ins_a(Opcode::Jmp, 5)};
  if (!expect_code(program, BytecodeVerifyCode::StackJoinMismatch, "join depth")) return 1;

  program.consts.push_back(Value::from_float(1.0));
  program.code = {ins_a(Opcode::PushConst, 0), ins_a(Opcode::JmpIfFalse, 4),
                  ins_a(Opcode::PushConst, 1), ins_a(Opcode::Jmp, 5),
                  ins_a(Opcode::PushConst, 2), ins_a(Opcode::Jmp, 5)};
  if (!expect_code(program, BytecodeVerifyCode::StackJoinMismatch, "join type")) return 1;

  program = constant_program();
  program.code = {ins_a(Opcode::PushConst, 0)};
  if (!expect_code(program, BytecodeVerifyCode::InvalidFallthrough, "main fallthrough")) return 1;

  program.consts = {Value::from_bool(true)};
  program.code = {ins_a(Opcode::PushConst, 0), ins(Opcode::Neg)};
  if (!check(verify_bytecode(program).ok, "guaranteed runtime TypeError remains valid bytecode")) return 1;

  program.consts.clear();
  program.code = {ins_a(Opcode::Jmp, 0)};
  if (!check(verify_bytecode(program).ok, "timeout-only loop remains valid bytecode")) return 1;

  for (int removed_opcode = 25; removed_opcode <= 27; ++removed_opcode) {
    program = constant_program();
    program.code[0].op = static_cast<Opcode>(removed_opcode);
    if (!expect_code(program, BytecodeVerifyCode::UnknownOpcode,
                     "reserved opcode " + std::to_string(removed_opcode))) {
      return 1;
    }
  }

  program = constant_program();
  BytecodeVerifyOptions limited;
  limited.max_instructions_per_code = 1;
  if (!expect_code(program, BytecodeVerifyCode::ResourceLimit, "instruction limit", limited)) return 1;

  return 0;
}
