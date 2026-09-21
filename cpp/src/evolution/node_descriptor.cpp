#include "gagp/evolution/node_descriptor.hpp"

#include <stdexcept>

#include "gagp/core/builtin.hpp"

namespace gagp::evo {
namespace {

constexpr int k_no_builtin = -1;
#define NODE(kind, stable, category, arity, i0, metadata, grammar, typing, subtree) \
  NodeDescriptor{NodeKind::kind, #kind, stable, NodeCategory::category, arity, NodeIndexRole::i0, \
                 NodeIndexRole::Unused, k_no_builtin, -1, \
                 NodeMetadataKind::metadata, GrammarFeature::grammar, \
                 NodeTypingRule::typing, subtree}

#define BUILTIN(kind, stable, arity, id, grammar) \
  NodeDescriptor{NodeKind::kind, #kind, stable, NodeCategory::Expression, arity, NodeIndexRole::Unused, \
                 NodeIndexRole::Unused, static_cast<int>(BuiltinId::id), arity, \
                 NodeMetadataKind::None, GrammarFeature::grammar, \
                 NodeTypingRule::Builtin, true}

constexpr std::array<NodeDescriptor, k_node_kind_count> k_descriptors{{
    NODE(PROGRAM, "program", Program, 1, Unused, None, Always, Program, false),
    NODE(BLOCK_NIL, "block_nil", Block, 0, Unused, None, Always, Block, false),
    NODE(BLOCK_CONS, "block_cons", Block, 2, Unused, None, Always, Block, false),
    NODE(ASSIGN, "assign", Statement, 1, Name, None, StatementAssign, Assign, false),
    NODE(IF_STMT, "if_stmt", Statement, 3, Unused, None, StatementIf, IfStatement, false),
    NODE(FOR_RANGE, "for_range", Statement, 2, Name, None, StatementForRange, ForRange, false),
    NODE(RETURN, "return", Statement, 1, Unused, None, StatementReturn, Return, false),
    NODE(CONST, "const", Expression, 0, Constant, None, ExpressionConst, Constant, true),
    NODE(VAR, "var", Expression, 0, Name, None, ExpressionVar, Variable, true),
    NODE(NEG, "neg", Expression, 1, Unused, None, UnaryNeg, UnaryNumeric, true),
    NODE(NOT, "not", Expression, 1, Unused, None, UnaryNot, UnaryBool, true),
    NODE(ADD, "add", Expression, 2, Unused, None, BinaryAdd, NumericBinary, true),
    NODE(SUB, "sub", Expression, 2, Unused, None, BinarySub, NumericBinary, true),
    NODE(MUL, "mul", Expression, 2, Unused, None, BinaryMul, NumericBinary, true),
    NODE(DIV, "div", Expression, 2, Unused, None, BinaryDiv, NumericBinary, true),
    NODE(MOD, "mod", Expression, 2, Unused, None, BinaryMod, NumericBinary, true),
    NODE(LT, "lt", Expression, 2, Unused, None, BinaryLt, OrderedComparison, true),
    NODE(LE, "le", Expression, 2, Unused, None, BinaryLe, OrderedComparison, true),
    NODE(GT, "gt", Expression, 2, Unused, None, BinaryGt, OrderedComparison, true),
    NODE(GE, "ge", Expression, 2, Unused, None, BinaryGe, OrderedComparison, true),
    NODE(EQ, "eq", Expression, 2, Unused, None, BinaryEq, Equality, true),
    NODE(NE, "ne", Expression, 2, Unused, None, BinaryNe, Equality, true),
    NODE(AND, "and", Expression, 2, Unused, None, BinaryAnd, BooleanBinary, true),
    NODE(OR, "or", Expression, 2, Unused, None, BinaryOr, BooleanBinary, true),
    NODE(IF_EXPR, "if_expr", Expression, 3, Unused, None, ExpressionIf, Conditional, true),
    BUILTIN(CALL_ABS, "call_abs", 1, Abs, BuiltinAbs),
    BUILTIN(CALL_MIN, "call_min", 2, Min, BuiltinMin),
    BUILTIN(CALL_MAX, "call_max", 2, Max, BuiltinMax),
    BUILTIN(CALL_CLIP, "call_clip", 3, Clip, BuiltinClip),
    BUILTIN(CALL_IDIV0, "call_idiv0", 2, IDiv0, BuiltinIdiv0),
    BUILTIN(CALL_IMOD0, "call_imod0", 2, IMod0, BuiltinImod0),
    BUILTIN(CALL_LEN, "call_len", 1, Len, BuiltinLen),
    BUILTIN(CALL_CONCAT, "call_concat", 2, Concat, BuiltinConcat),
    BUILTIN(CALL_SLICE, "call_slice", 3, Slice, BuiltinSlice),
    BUILTIN(CALL_INDEX, "call_index", 2, Index, BuiltinIndex),
    BUILTIN(CALL_APPEND, "call_append", 2, Append, BuiltinAppend),
    BUILTIN(CALL_PREPEND, "call_prepend", 2, Prepend, BuiltinPrepend),
    BUILTIN(CALL_REVERSE, "call_reverse", 1, Reverse, BuiltinReverse),
    BUILTIN(CALL_FIND, "call_find", 2, Find, BuiltinFind),
    BUILTIN(CALL_CONTAINS, "call_contains", 2, Contains, BuiltinContains),
    BUILTIN(CALL_CHAR_TO_STRING, "call_char_to_string", 1, CharToString, BuiltinCharToString),
    BUILTIN(CALL_STRING_TO_CHAR, "call_string_to_char", 1, StringToChar, BuiltinStringToChar),
    BUILTIN(CALL_ORD, "call_ord", 1, Ord, BuiltinOrd),
    BUILTIN(CALL_CHR, "call_chr", 1, Chr, BuiltinChr),
    BUILTIN(CALL_IS_LETTER, "call_is_letter", 1, IsLetter, BuiltinIsLetter),
    BUILTIN(CALL_IS_DIGIT, "call_is_digit", 1, IsDigit, BuiltinIsDigit),
    BUILTIN(CALL_IS_SPACE, "call_is_space", 1, IsSpace, BuiltinIsSpace),
    BUILTIN(CALL_IS_VOWEL, "call_is_vowel", 1, IsVowel, BuiltinIsVowel),
    BUILTIN(CALL_TO_LOWER, "call_to_lower", 1, ToLower, BuiltinToLower),
    BUILTIN(CALL_TO_UPPER, "call_to_upper", 1, ToUpper, BuiltinToUpper),
    BUILTIN(CALL_TO_STRING, "call_to_string", 1, ToString, BuiltinToString),
    BUILTIN(CALL_SINGLETON, "call_singleton", 1, Singleton, BuiltinSingleton),
    NODE(BOUND_VAR, "bound_var", Expression, 0, Name, None, ExpressionVar, BoundVariable, false),
    NODE(LET_REGION, "let_region", Expression, 2, Unused, LexicalRegion, GeneralRegion,
         LetRegion, true),
    NODE(TRAVERSE, "traverse", Expression, 4, Unused, LexicalRegion, GeneralRegion,
         Traverse, true),
    NODE(TRAVERSE_RANGE, "traverse_range", Expression, 6, Unused, LexicalRegion,
         GeneralRegion, Traverse, true),
    NODE(REGION_VAR, "region_var", Expression, 0, BinderId, None, GeneralRegion,
         RegionVariable, false),
    NODE(CHECK_INT, "check_int", Expression, 1, Unused, None, GeneralRegion,
         Builtin, true),
    NODE(CHECK_LIST, "check_list", Expression, 1, Unused, None, GeneralRegion,
         Builtin, true),
    NODE(BOUNDED_REGION, "bounded_region", Expression, 0, DynamicArity, BoundedRegion,
         GeneralRegion, BoundedRegion, true),
}};

#undef BUILTIN
#undef NODE

constexpr bool descriptors_complete() {
  for (std::size_t i = 0; i < k_descriptors.size(); ++i) {
    const NodeDescriptor& descriptor = k_descriptors[i];
    if (descriptor.source_name.empty() || descriptor.serialized_name.empty() ||
        descriptor.prefix_arity < 0) {
      return false;
    }
    if (descriptor.is_builtin() != (descriptor.builtin_arity >= 0)) {
      return false;
    }
    if (descriptor.is_builtin() && descriptor.prefix_arity != descriptor.builtin_arity) {
      return false;
    }
    for (std::size_t j = 0; j < i; ++j)
      if (k_descriptors[j].kind == descriptor.kind) return false;
  }
  return true;
}

static_assert(descriptors_complete(),
              "every current NodeKind must have one complete descriptor");

}  // namespace

const std::array<NodeDescriptor, k_node_kind_count>& all_node_descriptors() noexcept {
  return k_descriptors;
}

const NodeDescriptor& node_descriptor(NodeKind kind) {
  const int value = static_cast<int>(kind);
  if (!is_known_node_kind(value)) {
    throw std::out_of_range("unknown NodeKind descriptor");
  }
  for (const NodeDescriptor& descriptor : k_descriptors)
    if (static_cast<int>(descriptor.kind) == value) return descriptor;
  throw std::out_of_range("unknown NodeKind descriptor");
}

bool is_known_node_kind(int value) noexcept {
  for (const NodeDescriptor& descriptor : k_descriptors)
    if (static_cast<int>(descriptor.kind) == value) return true;
  return false;
}

}  // namespace gagp::evo
