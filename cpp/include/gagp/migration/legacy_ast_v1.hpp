#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "gagp/evolution/ast_program.hpp"
#include "gagp/evolution/input_spec.hpp"

namespace gagp::migration::legacy_v1 {

// Frozen numeric layout of the release-1 AST.  These values belong to the
// offline migration reader and must never be used by production dispatch.
enum class NodeKind : int {
  PROGRAM = 0, BLOCK_NIL, BLOCK_CONS, ASSIGN, IF_STMT, FOR_RANGE, RETURN,
  CONST, VAR, NEG, NOT, ADD, SUB, MUL, DIV, MOD, LT, LE, GT, GE, EQ, NE,
  AND, OR, IF_EXPR, CALL_ABS, CALL_MIN, CALL_MAX, CALL_CLIP, CALL_IDIV0,
  CALL_IMOD0, CALL_LEN, CALL_CONCAT, CALL_SLICE, CALL_INDEX, CALL_APPEND,
  CALL_PREPEND, CALL_REVERSE, CALL_FIND, CALL_CONTAINS, CALL_CHAR_TO_STRING,
  CALL_STRING_TO_CHAR, CALL_ORD, CALL_CHR, CALL_IS_LETTER, CALL_IS_DIGIT,
  CALL_IS_SPACE, CALL_IS_VOWEL, CALL_TO_LOWER, CALL_TO_UPPER, CALL_TO_STRING,
  CALL_SINGLETON, BOUND_VAR, MAP_LIST, FILTER_LIST, LINEAR_REC, ASGP_DC,
  ASGP_DP1D, ASGP_DP2D, DP1_BACKWARD1, DP1_BACKWARD2, DP1_BACKWARD3,
  DP1_FORWARD1, DP1_FORWARD2, DP1_FORWARD3, DP2_CROSS_BACKWARD,
  DP2_CROSS_FORWARD, DP2_DIAGONAL_BACKWARD, DP2_DIAGONAL_FORWARD,
  DP2_NEIGHBORHOOD_BACKWARD3, DP2_NEIGHBORHOOD_FORWARD3, LET_REGION,
  TRAVERSE, TRAVERSE_RANGE, REGION_VAR, CHECK_INT, CHECK_LIST,
  BOUNDED_REGION, COUNT,
};

static_assert(static_cast<int>(NodeKind::LINEAR_REC) == 55);
static_assert(static_cast<int>(NodeKind::ASGP_DC) == 56);
static_assert(static_cast<int>(NodeKind::DP2_NEIGHBORHOOD_FORWARD3) == 70);
static_assert(static_cast<int>(NodeKind::LET_REGION) == 71);

struct AstNode {
  NodeKind kind = NodeKind::PROGRAM;
  int i0 = 0;
  int i1 = 0;
};

enum class ListTypeTag { Int = 1, Float = 2, String = 3 };

struct LinearRecBinders {
  std::size_t node_index = 0;
  int elem_name = 0;
  int accum_name = 0;
  int index_name = 0;
};

struct AsgpDcBinders {
  std::size_t node_index = 0;
  int solve_xs_name = 0;
  int solve_n_name = 0;
  int solve_lo_name = 0;
  int divide_n_name = 0;
  int combine_left_name = 0;
  int combine_right_name = 0;
};

struct AsgpDp1dSpec {
  std::size_t node_index = 0;
  int lo = 0;
  int hi = 0;
  int base_state = 0;
  int boundary_const = 0;
  NodeKind dep_kind = NodeKind::DP1_BACKWARD1;
  std::vector<int> dep_offsets;
  int solve_state_name = 0;
  int transition_state_name = 0;
  std::vector<int> transition_dep_names;
};

struct AsgpDp2dSpec {
  std::size_t node_index = 0;
  int i_lo = 0;
  int i_hi = 0;
  int j_lo = 0;
  int j_hi = 0;
  int base_i = 0;
  int base_j = 0;
  int boundary_const = 0;
  NodeKind dep_kind = NodeKind::DP2_CROSS_BACKWARD;
  int solve_i_name = 0;
  int solve_j_name = 0;
  int transition_i_name = 0;
  int transition_j_name = 0;
  std::vector<int> transition_dep_names;
};

struct AstProgram {
  std::vector<AstNode> nodes;
  std::vector<std::string> names;
  std::vector<Value> consts;
  std::vector<LinearRecBinders> linear_rec_binders;
  std::vector<AsgpDcBinders> asgp_dc_binders;
  std::vector<AsgpDp1dSpec> asgp_dp1d_specs;
  std::vector<AsgpDp2dSpec> asgp_dp2d_specs;
  std::vector<evo::LexicalRegion> lexical_regions;
  std::vector<evo::TraversalSpec> traversal_specs;
  std::vector<evo::NodeFuelSpec> fuel_specs;
  std::vector<evo::BoundedRegionSpec> bounded_region_specs;
  std::string version = "ast-prefix";
};

struct VerifiedAst {
  std::vector<std::size_t> subtree_end;
  std::vector<evo::RType> expression_types;
  evo::RType return_type = evo::RType::Invalid;
};

int prefix_arity(const AstProgram& program, std::size_t node_index);
evo::NodeKind current_kind(NodeKind kind);
VerifiedAst verify(const AstProgram& program,
                   const std::vector<evo::InputSpec>& inputs = {});

}  // namespace gagp::migration::legacy_v1
