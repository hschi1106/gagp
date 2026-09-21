#include <iostream>
#include <stdexcept>
#include <algorithm>

#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/transition/linear_rec.hpp"
#include "gagp/migration/legacy_ast_v1.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/payload/payload.hpp"

int main() {
  namespace legacy = gagp::migration::legacy_v1;
  try {
    legacy::AstProgram source;
    source.version = "ast-prefix";
    source.names = {"element", "accumulator", "index"};
    source.consts = {
        gagp::payload::make_int_list_value({}), gagp::Value::from_int(0),
        gagp::Value::from_int(7)};
    source.nodes = {
        {legacy::NodeKind::PROGRAM}, {legacy::NodeKind::BLOCK_CONS},
        {legacy::NodeKind::RETURN}, {legacy::NodeKind::LINEAR_REC},
        {legacy::NodeKind::CONST, 0}, {legacy::NodeKind::CONST, 1},
        {legacy::NodeKind::CONST, 2}, {legacy::NodeKind::BOUND_VAR, 1},
        {legacy::NodeKind::CONST, 2}, {legacy::NodeKind::BLOCK_NIL}};
    source.linear_rec_binders = {{3, 0, 1, 2}};

    const auto checked = legacy::verify(source);
    if (checked.return_type != gagp::evo::RType::Int)
      throw std::runtime_error("legacy verifier did not infer the LinearRec result");

    const auto migrated = gagp::evo::transition::lower_linear_rec(source);
    if (migrated.ast.version != gagp::evo::k_ast_prefix_version_current)
      throw std::runtime_error("migration did not emit the current AST version");
    const auto has_kind = [&](gagp::evo::NodeKind kind) {
      return std::any_of(migrated.ast.nodes.begin(), migrated.ast.nodes.end(),
                         [&](const auto& node) { return node.kind == kind; });
    };
    if (!has_kind(gagp::evo::NodeKind::LET_REGION) ||
        !has_kind(gagp::evo::NodeKind::TRAVERSE_RANGE) ||
        !migrated.ast.bounded_region_specs.empty())
      throw std::runtime_error("legacy LinearRec was not lowered to general AST regions");
    const auto verified = gagp::evo::verify_ast(migrated.ast, {});
    if (!verified)
      throw std::runtime_error("migrated AST failed current verification: " +
                               verified.diagnostic.message);
    const auto bytecode = gagp::evo::compile_for_eval(
        migrated, verified.verified);
    const auto result = gagp::execute_bytecode_cpu(bytecode, {}, 1000);
    if (result.is_error || result.value.tag != gagp::ValueTag::Int ||
        result.value.i != 7)
      throw std::runtime_error("migrated LinearRec behavior changed");

    source.linear_rec_binders.clear();
    bool rejected = false;
    try {
      (void)legacy::verify(source);
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    if (!rejected)
      throw std::runtime_error("legacy verifier accepted missing metadata");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
