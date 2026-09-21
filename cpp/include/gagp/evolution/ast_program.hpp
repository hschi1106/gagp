#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "gagp/core/bytecode.hpp"

namespace gagp::evo {

inline constexpr const char* k_ast_prefix_version_current = "ast-prefix-v2";

enum class RType {
  Int,
  Float,
  Bool,
  Char,
  String,
  IntList,
  FloatList,
  StringList,
  Any,
  Invalid,
};

enum class NodeKind {
  PROGRAM,
  BLOCK_NIL,
  BLOCK_CONS,
  ASSIGN,
  IF_STMT,
  FOR_RANGE,
  RETURN,
  CONST,
  VAR,
  NEG,
  NOT,
  ADD,
  SUB,
  MUL,
  DIV,
  MOD,
  LT,
  LE,
  GT,
  GE,
  EQ,
  NE,
  AND,
  OR,
  IF_EXPR,
  CALL_ABS,
  CALL_MIN,
  CALL_MAX,
  CALL_CLIP,
  CALL_IDIV0,
  CALL_IMOD0,
  CALL_LEN,
  CALL_CONCAT,
  CALL_SLICE,
  CALL_INDEX,
  CALL_APPEND,
  CALL_PREPEND,
  CALL_REVERSE,
  CALL_FIND,
  CALL_CONTAINS,
  CALL_CHAR_TO_STRING,
  CALL_STRING_TO_CHAR,
  CALL_ORD,
  CALL_CHR,
  CALL_IS_LETTER,
  CALL_IS_DIGIT,
  CALL_IS_SPACE,
  CALL_IS_VOWEL,
  CALL_TO_LOWER,
  CALL_TO_UPPER,
  CALL_TO_STRING,
  CALL_SINGLETON,
  BOUND_VAR,
  // Values 53 through 70 belonged to legacy specialized nodes. They remain
  // unassigned so ast-prefix-v2 rejects those numeric kinds.
  LET_REGION = 71,
  TRAVERSE,
  TRAVERSE_RANGE,
  REGION_VAR,
  CHECK_INT,
  CHECK_LIST,
  BOUNDED_REGION,
  COUNT,
};

struct AstNode {
  NodeKind kind = NodeKind::PROGRAM;
  int i0 = 0;
  int i1 = 0;
};

struct LexicalBinding {
  int id = 0;
  RType type = RType::Invalid;
};

struct LexicalRegion {
  std::size_t node_index = 0;
  int body_argument = 0;
  std::vector<LexicalBinding> bindings;
};

enum class TraversalDirection {
  Forward = 0,
  Reverse = 1,
};

struct TraversalSpec {
  std::size_t node_index = 0;
  TraversalDirection direction = TraversalDirection::Forward;
};

enum class FuelEvent {
  Operation,
  Bind,
  BranchTest,
  BranchMerge,
  StoreSequence,
  StoreStart,
  StoreBegin,
  StoreEnd,
  CheckStart,
  CheckBegin,
  CheckEnd,
  ObserveSequence,
  ClampBegin,
  ClampEnd,
  SetBegin,
  SetEnd,
  InitializeState,
  InitializeCursor,
  TestCursor,
  ReadElement,
  BindElement,
  ComputeIndex,
  UpdateState,
  AdvanceCursor,
  Repeat,
  Result,
};

struct FuelCharge {
  FuelEvent event = FuelEvent::Operation;
  std::uint32_t cost = 0;
};

struct NodeFuelSpec {
  std::size_t node_index = 0;
  std::vector<FuelCharge> charges;
};

enum class RegionCaptureKind { Lexical, Name };

// Indices name an enclosing lexical binder or an ordinary name-table entry.
// Lowering snapshots the physical caller local without evaluating a LOAD.
struct RegionCapture {
  RegionCaptureKind kind = RegionCaptureKind::Lexical;
  int index = 0;
};

struct RegionAstBinding {
  RegionValueSlot source;
  int binder_id = 0;
};

struct RegionAstPhase {
  std::uint32_t argument = 0;
  std::vector<RegionAstBinding> bindings;
};

struct BoundedRegionSpec {
  std::size_t node_index = 0;
  RegionPlan plan;
  std::vector<RegionCapture> parameters;
  // Canonical phase order: predicate, base, preparations, request expressions,
  // combine, and coordinate boundary. Arguments follow initial states/bounds.
  std::vector<RegionAstPhase> phases;
};

struct AstProgram {
  std::vector<AstNode> nodes;
  std::vector<std::string> names;
  std::vector<Value> consts;
  std::vector<LexicalRegion> lexical_regions;
  std::vector<TraversalSpec> traversal_specs;
  std::vector<NodeFuelSpec> fuel_specs;
  std::vector<BoundedRegionSpec> bounded_region_specs;
  std::string version = k_ast_prefix_version_current;
};

struct Limits {
  int max_expr_depth = 7;
  int max_stmts_per_block = 6;
  int max_total_nodes = 80;
  int max_for_k = 16;
  int max_call_args = 3;
};

std::string ast_to_string(const AstProgram& program);
std::string ast_cache_key(const AstProgram& program);

}  // namespace gagp::evo
