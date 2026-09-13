#include "gagp/evolution/grammar/catalog.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace gagp::evo::grammar {

const std::array<RType, 8>& value_types() {
  static constexpr std::array<RType, 8> types{RType::Int, RType::Float, RType::Bool, RType::Char,
      RType::String, RType::IntList, RType::FloatList, RType::StringList};
  return types;
}

std::string_view type_name(RType type) {
  switch (type) {
    case RType::Int: return "Int";
    case RType::Float: return "Float";
    case RType::Bool: return "Bool";
    case RType::Char: return "Char";
    case RType::String: return "String";
    case RType::IntList: return "IntList";
    case RType::FloatList: return "FloatList";
    case RType::StringList: return "StringList";
    default: throw std::invalid_argument("grammar types must be exact public value types");
  }
}

RType parse_type(std::string_view name) {
  for (auto type : value_types()) if (type_name(type) == name) return type;
  throw std::invalid_argument("unknown grammar value type: " + std::string(name));
}

namespace {
std::string key_for(std::string_view operation, const std::vector<RType>& args, RType result) {
  std::string key(operation);
  key += '(';
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (i) key += ',';
    key += type_name(args[i]);
  }
  key += ")->";
  key += type_name(result);
  return key;
}
}  // namespace

PrimitiveCatalog::PrimitiveCatalog() {
  const auto control = [&](std::string key, NodeKind node, NodeCategory result,
      std::vector<ControlSlot> arguments, bool name = false) {
    controls_.push_back({0, std::move(key), node, result, std::move(arguments), name});
  };
  const ControlSlot block{NodeCategory::Block, RType::Invalid};
  const ControlSlot statement{NodeCategory::Statement, RType::Invalid};
  control("program(Block)->Program", NodeKind::PROGRAM, NodeCategory::Program, {block});
  control("block_nil()->Block", NodeKind::BLOCK_NIL, NodeCategory::Block, {});
  control("block_cons(Statement,Block)->Block", NodeKind::BLOCK_CONS, NodeCategory::Block, {statement, block});
  control("if_stmt(Bool,Block,Block)->Statement", NodeKind::IF_STMT, NodeCategory::Statement,
      {{NodeCategory::Expression, RType::Bool}, block, block});
  control("for_range(Int,Block)->Statement", NodeKind::FOR_RANGE, NodeCategory::Statement,
      {{NodeCategory::Expression, RType::Int}, block}, true);
  for (auto type : value_types()) {
    control("assign(" + std::string(type_name(type)) + ")->Statement", NodeKind::ASSIGN,
        NodeCategory::Statement, {{NodeCategory::Expression, type}}, true);
    control("return(" + std::string(type_name(type)) + ")->Statement", NodeKind::RETURN,
        NodeCategory::Statement, {{NodeCategory::Expression, type}});
  }
  std::sort(controls_.begin(), controls_.end(), [](const auto& a, const auto& b) { return a.key < b.key; });
  for (std::size_t i = 0; i < controls_.size(); ++i) controls_[i].id = static_cast<std::uint32_t>(i);
  const auto add = [&](std::string operation, std::vector<RType> arguments, RType result,
                       std::optional<NodeKind> node, std::vector<RegionSlot> regions = {}) {
    PrimitiveSignature signature;
    signature.key = key_for(operation, arguments, result);
    signature.operation = std::move(operation);
    signature.arguments = std::move(arguments);
    signature.result = result;
    signature.lowering_node = node;
    signature.regions = std::move(regions);
    signatures_.push_back(std::move(signature));
  };
  for (RType type : value_types()) {
    add("constant", {}, type, NodeKind::CONST);
    add("input", {}, type, NodeKind::VAR);
    add("bound", {}, type, NodeKind::BOUND_VAR);
    add("if", {RType::Bool, type, type}, type, NodeKind::IF_EXPR);
    add("eq", {type, type}, RType::Bool, NodeKind::EQ);
    add("ne", {type, type}, RType::Bool, NodeKind::NE);
    for (RType bound : value_types())
      add("let", {bound, type}, type, std::nullopt, {{1, {{"value", bound}}}});
  }
  for (RType type : {RType::Int, RType::Float}) {
    for (auto entry : {std::pair{"add", NodeKind::ADD}, {"sub", NodeKind::SUB},
                       {"mul", NodeKind::MUL}, {"mod", NodeKind::MOD}})
      add(entry.first, {type, type}, type, entry.second);
    add("div", {type, type}, RType::Float, NodeKind::DIV);
    add("neg", {type}, type, NodeKind::NEG);
    for (auto entry : {std::pair{"lt", NodeKind::LT}, {"le", NodeKind::LE},
                       {"gt", NodeKind::GT}, {"ge", NodeKind::GE}})
      add(entry.first, {type, type}, RType::Bool, entry.second);
    add("abs", {type}, type, NodeKind::CALL_ABS);
    add("min", {type, type}, type, NodeKind::CALL_MIN);
    add("max", {type, type}, type, NodeKind::CALL_MAX);
    add("clip", {type, type, type}, type, NodeKind::CALL_CLIP);
    add("to_string", {type}, RType::String, NodeKind::CALL_TO_STRING);
  }
  add("not", {RType::Bool}, RType::Bool, NodeKind::NOT);
  add("and", {RType::Bool, RType::Bool}, RType::Bool, NodeKind::AND);
  add("or", {RType::Bool, RType::Bool}, RType::Bool, NodeKind::OR);
  add("idiv0", {RType::Int, RType::Int}, RType::Int, NodeKind::CALL_IDIV0);
  add("imod0", {RType::Int, RType::Int}, RType::Int, NodeKind::CALL_IMOD0);
  for (auto sequence : {std::pair{RType::String, RType::Char}, {RType::IntList, RType::Int},
                        {RType::FloatList, RType::Float}, {RType::StringList, RType::String}}) {
    const auto list = sequence.first, element = sequence.second;
    add("len", {list}, RType::Int, NodeKind::CALL_LEN);
    add("concat", {list, list}, list, NodeKind::CALL_CONCAT);
    add("slice", {list, RType::Int, RType::Int}, list, NodeKind::CALL_SLICE);
    add("index", {list, RType::Int}, element, NodeKind::CALL_INDEX);
    add("reverse", {list}, list, NodeKind::CALL_REVERSE);
    add("singleton", {element}, list, NodeKind::CALL_SINGLETON);
    if (list != RType::String) {
      add("append", {list, element}, list, NodeKind::CALL_APPEND);
      add("prepend", {list, element}, list, NodeKind::CALL_PREPEND);
    }
    for (RType state : value_types())
      add("traverse", {list, RType::Int, state, state}, state, std::nullopt,
          {{3, {{"element", element}, {"index", RType::Int}, {"accumulator", state}}}});
  }
  add("find", {RType::String, RType::String}, RType::Int, NodeKind::CALL_FIND);
  add("contains", {RType::String, RType::String}, RType::Bool, NodeKind::CALL_CONTAINS);
  add("char_to_string", {RType::Char}, RType::String, NodeKind::CALL_CHAR_TO_STRING);
  add("string_to_char", {RType::String}, RType::Char, NodeKind::CALL_STRING_TO_CHAR);
  add("ord", {RType::Char}, RType::Int, NodeKind::CALL_ORD);
  add("chr", {RType::Int}, RType::Char, NodeKind::CALL_CHR);
  for (auto entry : {std::pair{"is_letter", NodeKind::CALL_IS_LETTER}, {"is_digit", NodeKind::CALL_IS_DIGIT},
                     {"is_space", NodeKind::CALL_IS_SPACE}, {"is_vowel", NodeKind::CALL_IS_VOWEL}})
    add(entry.first, {RType::Char}, RType::Bool, entry.second);
  add("to_lower", {RType::Char}, RType::Char, NodeKind::CALL_TO_LOWER);
  add("to_upper", {RType::Char}, RType::Char, NodeKind::CALL_TO_UPPER);
  std::sort(signatures_.begin(), signatures_.end(), [](const auto& a, const auto& b) { return a.key < b.key; });
  for (std::size_t i = 0; i < signatures_.size(); ++i) {
    if (i && signatures_[i - 1].key == signatures_[i].key) throw std::logic_error("duplicate catalog signature");
    signatures_[i].id = static_cast<std::uint32_t>(i);
  }
}

const PrimitiveCatalog& PrimitiveCatalog::standard() {
  static const PrimitiveCatalog catalog;
  return catalog;
}

const PrimitiveSignature& PrimitiveCatalog::at(std::uint32_t id) const {
  if (id >= signatures_.size()) throw std::out_of_range("unknown primitive numeric ID");
  return signatures_[id];
}

const PrimitiveSignature& PrimitiveCatalog::resolve(std::string_view operation,
    const std::vector<RType>& arguments, RType result) const {
  const auto key = key_for(operation, arguments, result);
  return resolve(key);
}

const PrimitiveSignature& PrimitiveCatalog::resolve(std::string_view key) const {
  const auto found = std::lower_bound(signatures_.begin(), signatures_.end(), key,
      [](const auto& signature, const auto& wanted) { return signature.key < wanted; });
  if (found == signatures_.end() || found->key != key)
    throw std::invalid_argument("unknown exact primitive overload: " + std::string(key));
  return *found;
}

void PrimitiveCatalog::require_executable(std::uint32_t id) const {
  const auto& signature = at(id);
  if (!signature.executable())
    throw std::invalid_argument("primitive is declared but execution is not implemented: " + signature.key);
}

const ControlSignature& PrimitiveCatalog::resolve_control(std::string_view key) const {
  const auto found = std::lower_bound(controls_.begin(), controls_.end(), key,
      [](const auto& signature, const auto& wanted) { return signature.key < wanted; });
  if (found == controls_.end() || found->key != key)
    throw std::invalid_argument("unknown exact control signature: " + std::string(key));
  return *found;
}

}  // namespace gagp::evo::grammar
