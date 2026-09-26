#include "gagp/migration/typed_storage.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>

#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/node_descriptor.hpp"

namespace gagp::migration {
namespace {
using namespace evo;
using Mask = std::uint16_t;
using State = std::vector<Mask>;
constexpr Mask kUndefined = 1u << 8;
Mask mask(RType type) {
  if (type < RType::Int || type > RType::StringList)
    throw std::invalid_argument("typed storage mapping requires exact source types");
  return Mask{1} << static_cast<unsigned>(type);
}
RType type_of(ValueTag tag) {
  switch (tag) {
    case ValueTag::Int: return RType::Int;
    case ValueTag::Float: return RType::Float;
    case ValueTag::Bool: return RType::Bool;
    case ValueTag::Char: return RType::Char;
    case ValueTag::String: return RType::String;
    case ValueTag::IntList: return RType::IntList;
    case ValueTag::FloatList: return RType::FloatList;
    case ValueTag::StringList: return RType::StringList;
    default: throw std::invalid_argument("typed storage mapping has an invalid capture type");
  }
}
State join(State left, const State& right) {
  for (std::size_t i = 0; i < left.size(); ++i) left[i] |= right[i];
  return left;
}
std::optional<State> join_paths(std::optional<State> left, std::optional<State> right) {
  if (!left) return right;
  if (!right) return left;
  return join(std::move(*left), *right);
}

class Normalizer {
 public:
  Normalizer(const AstProgram& ast, const std::vector<InputSpec>& inputs,
             const VerifiedAst& verified)
      : ast_(ast), verified_(verified), declared_(ast.names.size(), 0),
        initial_(ast.names.size(), kUndefined), input_types_(ast.names.size(), 0) {
    for (const auto& input : inputs) {
      const auto found = std::find(ast.names.begin(), ast.names.end(), input.name);
      if (found == ast.names.end()) continue;
      const auto id = static_cast<std::size_t>(found - ast.names.begin());
      initial_[id] = input_types_[id] = declared_[id] = mask(input.type);
    }
    for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
      const auto& node = ast.nodes[i];
      if (node.kind == NodeKind::ASSIGN) declared_[node.i0] |= mask(verified.expression_types[i + 1]);
      if (node.kind == NodeKind::FOR_RANGE) declared_[node.i0] |= mask(RType::Int);
      if (node.kind == NodeKind::VAR) declared_[node.i0] |= mask(verified.expression_types[i]);
    }
    for (const auto& region : ast.bounded_region_specs)
      for (std::size_t i = 0; i < region.parameters.size(); ++i)
        if (region.parameters[i].kind == RegionCaptureKind::Name)
          declared_[region.parameters[i].index] |= mask(type_of(region.plan.parameter_types[i]));
  }

  TypedStorageNormalization run(bool canonical_names) {
    (void)block(1, initial_, true);
    TypedStorageNormalization out{ast_, 0};
    std::set<std::string> used(ast_.names.begin(), ast_.names.end());
    std::map<std::pair<int, RType>, int> names;
    const auto rename = [&](int id, RType type) {
      if ((!canonical_names && !split(id)) || input_types_[id] == mask(type)) return id;
      const auto key = std::make_pair(id, type);
      const auto found = names.find(key);
      if (found != names.end()) return found->second;
      std::string name;
      if (canonical_names) {
        name = "_typed_storage_name_";
        constexpr char hex[] = "0123456789abcdef";
        for (unsigned char byte : ast_.names.at(id)) {
          name += hex[byte >> 4];
          name += hex[byte & 15];
        }
        name += "_" + std::to_string(static_cast<int>(type));
        if (!used.insert(name).second)
          throw std::invalid_argument("canonical typed storage name collides with source name: " + name);
      } else {
        name = "_typed_storage_" + std::to_string(id) + "_" +
               std::to_string(static_cast<int>(type));
        while (!used.insert(name).second) name += "_";
      }
      const int next = static_cast<int>(out.ast.names.size());
      out.ast.names.push_back(name);
      names.emplace(key, next);
      return next;
    };
    for (std::size_t i = 0; i < out.ast.nodes.size(); ++i) {
      auto& node = out.ast.nodes[i];
      int next = node.i0;
      if (node.kind == NodeKind::ASSIGN) next = rename(node.i0, verified_.expression_types[i + 1]);
      if (node.kind == NodeKind::FOR_RANGE) next = rename(node.i0, RType::Int);
      if (node.kind == NodeKind::VAR) next = rename(node.i0, verified_.expression_types[i]);
      out.renamed_uses += next != node.i0;
      node.i0 = next;
    }
    for (auto& region : out.ast.bounded_region_specs)
      for (std::size_t i = 0; i < region.parameters.size(); ++i) {
        auto& capture = region.parameters[i];
        if (capture.kind != RegionCaptureKind::Name) continue;
        const int next = rename(capture.index, type_of(region.plan.parameter_types[i]));
        out.renamed_uses += next != capture.index;
        capture.index = next;
      }
    return out;
  }

 private:
  bool split(int id) const { return (declared_[id] & (declared_[id] - 1)) != 0; }
  void read(std::size_t owner, int id, RType type, const State& state) const {
    if (split(id) && state[id] != mask(type))
      throw std::invalid_argument("typed storage mapping cannot prove read at node " +
          std::to_string(owner) + " of " + ast_.names[id] +
          ": reaching definitions include another type or an undefined value");
  }
  std::optional<bool> known_bool(std::size_t index) const {
    const auto& node = ast_.nodes[index];
    if (node.kind == NodeKind::CONST && ast_.consts[node.i0].tag == ValueTag::Bool)
      return ast_.consts[node.i0].b;
    if (node.kind == NodeKind::NOT) {
      const auto value = known_bool(index + 1);
      if (value) return !*value;
    }
    if (node.kind == NodeKind::AND || node.kind == NodeKind::OR) {
      const auto left = known_bool(index + 1);
      if (!left) return std::nullopt;
      if (node.kind == NodeKind::AND && !*left) return false;
      if (node.kind == NodeKind::OR && *left) return true;
      return known_bool(verified_.subtree_end[index + 1]);
    }
    if (node.kind == NodeKind::IF_EXPR) {
      const auto condition = known_bool(index + 1);
      if (!condition) return std::nullopt;
      const auto yes = verified_.subtree_end[index + 1];
      return known_bool(*condition ? yes : verified_.subtree_end[yes]);
    }
    return std::nullopt;
  }
  void expression(std::size_t start, const State& state, bool check) const {
    if (!check) return;
    const auto& node = ast_.nodes[start];
    if (node.kind == NodeKind::VAR)
      read(start, node.i0, verified_.expression_types[start], state);
    for (const auto& region : ast_.bounded_region_specs) {
      if (region.node_index != start) continue;
      for (std::size_t i = 0; i < region.parameters.size(); ++i)
        if (region.parameters[i].kind == RegionCaptureKind::Name)
          read(region.node_index, region.parameters[i].index,
               type_of(region.plan.parameter_types[i]), state);
    }
    if (node.kind == NodeKind::AND || node.kind == NodeKind::OR || node.kind == NodeKind::IF_EXPR) {
      expression(start + 1, state, true);
      const auto condition = known_bool(start + 1);
      const auto second = verified_.subtree_end[start + 1];
      if (node.kind == NodeKind::IF_EXPR) {
        if (!condition || *condition) expression(second, state, true);
        if (!condition || !*condition) expression(verified_.subtree_end[second], state, true);
      } else if (!condition || (node.kind == NodeKind::AND ? *condition : !*condition)) {
        expression(second, state, true);
      }
      return;
    }
    auto child = start + 1;
    for (int i = 0; i < node_prefix_arity(node); ++i) {
      expression(child, state, true);
      child = verified_.subtree_end[child];
    }
  }
  std::optional<State> block(std::size_t index, State state, bool check) const {
    while (ast_.nodes[index].kind == NodeKind::BLOCK_CONS) {
      const auto statement = index + 1;
      auto next = transfer(statement, std::move(state), check);
      if (!next) return std::nullopt;
      state = std::move(*next);
      index = verified_.subtree_end[statement];
    }
    return state;
  }
  std::optional<State> transfer(std::size_t index, State state, bool check) const {
    const auto& node = ast_.nodes[index];
    expression(index + 1, state, check);
    if (node.kind == NodeKind::ASSIGN) {
      state[node.i0] = mask(verified_.expression_types[index + 1]);
    } else if (node.kind == NodeKind::RETURN) {
      return std::nullopt;
    } else if (node.kind == NodeKind::IF_STMT) {
      const auto yes = verified_.subtree_end[index + 1];
      const auto no = verified_.subtree_end[yes];
      const auto condition = known_bool(index + 1);
      if (condition) return block(*condition ? yes : no, std::move(state), check);
      return join_paths(block(yes, state, check), block(no, state, check));
    } else if (node.kind == NodeKind::FOR_RANGE) {
      const auto body = verified_.subtree_end[index + 1];
      const auto incoming = state;
      // Finite monotone lattice: each name has eight source types plus undefined.
      // Include the zero-iteration path and all loop-carried assignments.
      for (;;) {
        auto body_input = state;
        body_input[node.i0] = mask(RType::Int);
        const auto exit = block(body, std::move(body_input), false);
        const auto next = exit ? join(incoming, *exit) : incoming;
        if (next == state) break;
        state = next;
      }
      auto body_input = state;
      body_input[node.i0] = mask(RType::Int);
      (void)block(body, std::move(body_input), check);
    }
    return state;
  }

  const AstProgram& ast_;
  const VerifiedAst& verified_;
  State declared_, initial_, input_types_;
};
}  // namespace

TypedStorageNormalization normalize_typed_storage(const evo::AstProgram& ast,
    const std::vector<evo::InputSpec>& inputs, bool canonical_names) {
  const auto checked = evo::verify_ast(ast, inputs);
  if (!checked) throw std::invalid_argument("typed storage mapping requires a verified AST: " +
                                            checked.diagnostic.message);
  auto out = Normalizer(ast, inputs, checked.verified).run(canonical_names);
  const auto normalized = evo::verify_ast(out.ast, inputs);
  if (!normalized || normalized.verified.return_type != checked.verified.return_type)
    throw std::invalid_argument("typed storage mapping failed final native verification");
  return out;
}
}  // namespace gagp::migration
