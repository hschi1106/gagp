#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <limits>

#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/transition/bounded_regions.hpp"
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

    legacy::AstProgram string_contains;
    string_contains.version = "ast-prefix";
    string_contains.consts = {
        gagp::payload::make_string_value("abc"),
        gagp::payload::make_string_value("b")};
    string_contains.nodes = {
        {legacy::NodeKind::PROGRAM}, {legacy::NodeKind::BLOCK_CONS},
        {legacy::NodeKind::RETURN}, {legacy::NodeKind::CALL_CONTAINS},
        {legacy::NodeKind::CONST, 0}, {legacy::NodeKind::CONST, 1},
        {legacy::NodeKind::BLOCK_NIL}};
    if (legacy::verify(string_contains).return_type != gagp::evo::RType::Bool)
      throw std::runtime_error("legacy verifier rejected String substring contains");

    // A name remains one runtime slot across type-changing assignments. The
    // legacy loop annotation describes the incoming type, not a guarantee that
    // the body leaves its value unchanged. Type-based renaming would restore x's
    // original input value after this loop and silently change frozen behavior.
    legacy::AstProgram retyped_input;
    retyped_input.version = "ast-prefix";
    retyped_input.names = {"x", "i"};
    retyped_input.consts = {gagp::Value::from_int(1), gagp::Value::from_bool(true)};
    retyped_input.nodes = {
        {legacy::NodeKind::PROGRAM}, {legacy::NodeKind::BLOCK_CONS},
        {legacy::NodeKind::FOR_RANGE, 1}, {legacy::NodeKind::CONST, 0},
        {legacy::NodeKind::BLOCK_CONS}, {legacy::NodeKind::ASSIGN, 0},
        {legacy::NodeKind::CONST, 1}, {legacy::NodeKind::BLOCK_NIL},
        {legacy::NodeKind::BLOCK_CONS}, {legacy::NodeKind::RETURN},
        {legacy::NodeKind::VAR, 0}, {legacy::NodeKind::BLOCK_NIL}};
    const std::vector<gagp::evo::InputSpec> retyped_schema{{"x", gagp::evo::RType::Float}};
    for (int iterations : {0, 1, 2}) {
      retyped_input.consts[0] = gagp::Value::from_int(iterations);
      const auto lowered = gagp::evo::transition::lower_bounded_regions(retyped_input, retyped_schema);
      const auto checked = gagp::evo::verify_ast(lowered.ast, retyped_schema);
      if (!checked) throw std::runtime_error("type-changing legacy loop must remain migratable");
      const auto code = gagp::evo::compile_for_eval(lowered, checked.verified);
      const auto actual = gagp::execute_bytecode_cpu(
          code, {{code.var2idx.at("x"), gagp::Value::from_float(7.0)}}, 10000);
      if (actual.is_error || (iterations == 0 ?
          (actual.value.tag != gagp::ValueTag::Float || actual.value.f != 7.0) :
          (actual.value.tag != gagp::ValueTag::Bool || !actual.value.b)))
        throw std::runtime_error("migration split a type-changing input into different runtime slots");
    }

    const auto execute_int = [](const gagp::evo::ProgramGenome& genome) {
      const auto checked = gagp::evo::verify_ast(genome.ast, {});
      if (!checked)
        throw std::runtime_error("lowered bounded program failed verification");
      const auto code = gagp::evo::compile_for_eval(genome, checked.verified);
      const auto result = gagp::execute_bytecode_cpu(code, {}, 10000);
      if (result.is_error || result.value.tag != gagp::ValueTag::Int)
        throw std::runtime_error("lowered bounded program failed execution");
      return result.value.i;
    };

    legacy::AstProgram dc;
    dc.version = "ast-prefix";
    dc.names = {"xs", "n", "lo", "divide_n", "left", "right"};
    dc.consts = {
        gagp::payload::make_int_list_value({gagp::Value::from_int(1),
                                            gagp::Value::from_int(2),
                                            gagp::Value::from_int(3)}),
        gagp::Value::from_int(7), gagp::Value::from_int(99)};
    dc.nodes = {
        {legacy::NodeKind::PROGRAM}, {legacy::NodeKind::BLOCK_CONS},
        {legacy::NodeKind::RETURN}, {legacy::NodeKind::ASGP_DC},
        {legacy::NodeKind::CONST, 0}, {legacy::NodeKind::CONST, 1},
        {legacy::NodeKind::CONST, 2}, {legacy::NodeKind::BOUND_VAR, 4},
        {legacy::NodeKind::BLOCK_NIL}};
    dc.asgp_dc_binders = {{3, 0, 1, 2, 3, 4, 5}};
    const auto lowered_dc = gagp::evo::transition::lower_bounded_regions(dc);
    const auto& dc_limits = lowered_dc.ast.bounded_region_specs.at(0).plan.limits;
    if (dc_limits.frames != 3 || dc_limits.cells != 0)
      throw std::runtime_error("constant DC did not receive its proven frame bound");
    if (execute_int(lowered_dc) != 7)
      throw std::runtime_error("constant DC capacity changed execution");

    auto terminal_dc = dc;
    terminal_dc.consts[0] = gagp::payload::make_int_list_value({});
    if (gagp::evo::transition::lower_bounded_regions(terminal_dc)
            .ast.bounded_region_specs.at(0).plan.limits.frames != 1)
      throw std::runtime_error("empty DC did not retain its root frame");
    terminal_dc.consts[0] = gagp::payload::make_int_list_value(
        {gagp::Value::from_int(1)});
    if (gagp::evo::transition::lower_bounded_regions(terminal_dc)
            .ast.bounded_region_specs.at(0).plan.limits.frames != 1)
      throw std::runtime_error("length-one DC did not retain its root frame");

    dc.names.insert(dc.names.begin(), "source");
    dc.nodes[4] = {legacy::NodeKind::VAR, 0};
    dc.nodes[7].i0 = 5;
    dc.asgp_dc_binders = {{3, 1, 2, 3, 4, 5, 6}};
    const auto lowered_dynamic_dc = gagp::evo::transition::lower_bounded_regions(
        dc, {{"source", gagp::evo::RType::IntList}});
    if (lowered_dynamic_dc.ast.bounded_region_specs.at(0).plan.limits.frames !=
        gagp::Value::k_container_len_max)
      throw std::runtime_error("dynamic DC did not retain the full sequence bound");

    legacy::AstProgram dp1;
    dp1.version = "ast-prefix";
    dp1.names = {"solve", "state", "dependency"};
    dp1.consts = {gagp::Value::from_int(3), gagp::Value::from_int(7),
                  gagp::Value::from_int(-1)};
    dp1.nodes = {
        {legacy::NodeKind::PROGRAM}, {legacy::NodeKind::BLOCK_CONS},
        {legacy::NodeKind::RETURN}, {legacy::NodeKind::ASGP_DP1D},
        {legacy::NodeKind::CONST, 0}, {legacy::NodeKind::CONST, 1},
        {legacy::NodeKind::BOUND_VAR, 2}, {legacy::NodeKind::BLOCK_NIL}};
    dp1.asgp_dp1d_specs = {{3, 0, 5, 0, 2,
                            legacy::NodeKind::DP1_BACKWARD1, {1}, 0, 1, {2}}};
    const auto lowered_dp1 = gagp::evo::transition::lower_bounded_regions(dp1);
    const auto& dp1_limits = lowered_dp1.ast.bounded_region_specs.at(0).plan.limits;
    if (dp1_limits.cells != 6 || dp1_limits.frames != 7)
      throw std::runtime_error("DP1D did not receive its literal-domain bounds");
    dp1.names = {"solve", "state", "dependency1", "dependency2", "dependency3"};
    dp1.asgp_dp1d_specs = {{3, 0, 5, 0, 2,
                            legacy::NodeKind::DP1_BACKWARD3, {1, 2, 3},
                            0, 1, {2, 3, 4}}};
    const auto lowered_dp1_multi =
        gagp::evo::transition::lower_bounded_regions(dp1);
    if (execute_int(lowered_dp1_multi) != 7)
      throw std::runtime_error("multi-request DP1D capacity changed execution");
    dp1.asgp_dp1d_specs = {{3, std::numeric_limits<int>::min(),
                            std::numeric_limits<int>::max(), 0, 2,
                            legacy::NodeKind::DP1_BACKWARD3, {1, 2, 3},
                            0, 1, {2, 3, 4}}};
    const auto lowered_saturated_dp1 =
        gagp::evo::transition::lower_bounded_regions(dp1);
    const auto& saturated_dp1 =
        lowered_saturated_dp1.ast.bounded_region_specs.at(0).plan.limits;
    if (saturated_dp1.cells != std::numeric_limits<std::uint32_t>::max() ||
        saturated_dp1.frames != std::numeric_limits<std::uint32_t>::max())
      throw std::runtime_error("DP1D full-domain capacity did not saturate safely");

    legacy::AstProgram dp2;
    dp2.version = "ast-prefix";
    dp2.names = {"solve_i", "solve_j", "state_i", "state_j", "dependency"};
    dp2.consts = {gagp::Value::from_int(2), gagp::Value::from_int(2),
                  gagp::Value::from_int(7), gagp::Value::from_int(-1)};
    dp2.nodes = {
        {legacy::NodeKind::PROGRAM}, {legacy::NodeKind::BLOCK_CONS},
        {legacy::NodeKind::RETURN}, {legacy::NodeKind::ASGP_DP2D},
        {legacy::NodeKind::CONST, 0}, {legacy::NodeKind::CONST, 1},
        {legacy::NodeKind::CONST, 2}, {legacy::NodeKind::BOUND_VAR, 4},
        {legacy::NodeKind::BLOCK_NIL}};
    dp2.asgp_dp2d_specs = {{3, 0, 3, 0, 3, 0, 0, 3,
                            legacy::NodeKind::DP2_DIAGONAL_BACKWARD,
                            0, 1, 2, 3, {4}}};
    const auto lowered_dp2 = gagp::evo::transition::lower_bounded_regions(dp2);
    const auto& dp2_limits = lowered_dp2.ast.bounded_region_specs.at(0).plan.limits;
    if (dp2_limits.cells != 16 || dp2_limits.frames != 17)
      throw std::runtime_error("DP2D did not receive its literal-domain bounds");
    dp2.names.push_back("dependency2");
    dp2.asgp_dp2d_specs.at(0).dep_kind = legacy::NodeKind::DP2_CROSS_BACKWARD;
    dp2.asgp_dp2d_specs.at(0).transition_dep_names = {4, 5};
    const auto lowered_dp2_multi =
        gagp::evo::transition::lower_bounded_regions(dp2);
    (void)execute_int(lowered_dp2_multi);
    auto& saturated_dp2 = dp2.asgp_dp2d_specs.at(0);
    saturated_dp2.i_lo = saturated_dp2.j_lo = 0;
    saturated_dp2.i_hi = saturated_dp2.j_hi = 65535;
    saturated_dp2.base_i = saturated_dp2.base_j = 0;
    const auto lowered_saturated_dp2 =
        gagp::evo::transition::lower_bounded_regions(dp2);
    const auto& saturated_dp2_limits =
        lowered_saturated_dp2.ast.bounded_region_specs.at(0).plan.limits;
    if (saturated_dp2_limits.cells != std::numeric_limits<std::uint32_t>::max() ||
        saturated_dp2_limits.frames != std::numeric_limits<std::uint32_t>::max())
      throw std::runtime_error("DP2D product capacity did not saturate safely");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
