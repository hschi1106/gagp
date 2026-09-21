#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "gagp/evolution/ast_program.hpp"

namespace gagp::evo {

enum class NodeCategory {
  Program,
  Block,
  Statement,
  Expression,
};

enum class NodeIndexRole {
  Unused,
  Name,
  Constant,
  BinderId,
  DynamicArity,
};

enum class NodeMetadataKind {
  None,
  LexicalRegion,
  BoundedRegion,
};

enum class GrammarFeature {
  Always,
  StatementAssign,
  StatementIf,
  StatementForRange,
  StatementReturn,
  ExpressionConst,
  ExpressionVar,
  ExpressionIf,
  UnaryNeg,
  UnaryNot,
  BinaryAdd,
  BinarySub,
  BinaryMul,
  BinaryDiv,
  BinaryMod,
  BinaryLt,
  BinaryLe,
  BinaryGt,
  BinaryGe,
  BinaryEq,
  BinaryNe,
  BinaryAnd,
  BinaryOr,
  BuiltinAbs,
  BuiltinMin,
  BuiltinMax,
  BuiltinClip,
  BuiltinIdiv0,
  BuiltinImod0,
  BuiltinLen,
  BuiltinConcat,
  BuiltinSlice,
  BuiltinIndex,
  BuiltinAppend,
  BuiltinPrepend,
  BuiltinReverse,
  BuiltinFind,
  BuiltinContains,
  BuiltinCharToString,
  BuiltinStringToChar,
  BuiltinOrd,
  BuiltinChr,
  BuiltinIsLetter,
  BuiltinIsDigit,
  BuiltinIsSpace,
  BuiltinIsVowel,
  BuiltinToLower,
  BuiltinToUpper,
  BuiltinToString,
  BuiltinSingleton,
  GeneralRegion,
};

enum class NodeTypingRule {
  Program,
  Block,
  Assign,
  IfStatement,
  ForRange,
  Return,
  Constant,
  Variable,
  UnaryNumeric,
  UnaryBool,
  NumericBinary,
  OrderedComparison,
  Equality,
  BooleanBinary,
  Conditional,
  Builtin,
  BoundVariable,
  LetRegion,
  Traverse,
  RegionVariable,
  BoundedRegion,
};

struct NodeDescriptor {
  NodeKind kind;
  std::string_view source_name;
  std::string_view serialized_name;
  NodeCategory category;
  int prefix_arity;
  NodeIndexRole i0_role;
  NodeIndexRole i1_role;
  int builtin_id;
  int builtin_arity;
  NodeMetadataKind metadata;
  GrammarFeature grammar_feature;
  NodeTypingRule typing_rule;
  bool typed_subtree_eligible;

  constexpr bool is_builtin() const noexcept { return builtin_id >= 0; }
};

inline constexpr std::size_t k_node_kind_count = 60;

const std::array<NodeDescriptor, k_node_kind_count>& all_node_descriptors() noexcept;
const NodeDescriptor& node_descriptor(NodeKind kind);
// Traverse concrete nodes through this entrypoint so descriptor-sized argument
// layouts can validate their declared arity before any prefix walk.
inline int node_prefix_arity(const AstNode& node) {
  if (node.kind == NodeKind::BOUNDED_REGION) return node.i0;
  return node_descriptor(node.kind).prefix_arity;
}
bool is_known_node_kind(int value) noexcept;

}  // namespace gagp::evo
