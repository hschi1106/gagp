#include "gagp/migration/legacy_ast_v1.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

#include "gagp/evolution/bounded_region.hpp"
#include "gagp/evolution/node_descriptor.hpp"

namespace gagp::migration::legacy_v1 {
namespace {

using TypeEnv = std::map<int, evo::RType>;

[[noreturn]] void fail(std::size_t node, const std::string& message) {
  throw std::invalid_argument("legacy ast-prefix-v1 node " +
                              std::to_string(node) + ": " + message);
}

evo::RType value_type(ValueTag tag) {
  switch (tag) {
    case ValueTag::Int: return evo::RType::Int;
    case ValueTag::Float: return evo::RType::Float;
    case ValueTag::Bool: return evo::RType::Bool;
    case ValueTag::Char: return evo::RType::Char;
    case ValueTag::String: return evo::RType::String;
    case ValueTag::IntList: return evo::RType::IntList;
    case ValueTag::FloatList: return evo::RType::FloatList;
    case ValueTag::StringList: return evo::RType::StringList;
    case ValueTag::Invalid: return evo::RType::Invalid;
  }
  return evo::RType::Invalid;
}

bool value_type(evo::RType type) {
  return type >= evo::RType::Int && type <= evo::RType::StringList;
}

bool numeric(evo::RType type) {
  return type == evo::RType::Int || type == evo::RType::Float;
}

bool sequence(evo::RType type) {
  return type == evo::RType::String || type == evo::RType::IntList ||
         type == evo::RType::FloatList || type == evo::RType::StringList;
}

evo::RType element(evo::RType type) {
  switch (type) {
    case evo::RType::String: return evo::RType::Char;
    case evo::RType::IntList: return evo::RType::Int;
    case evo::RType::FloatList: return evo::RType::Float;
    case evo::RType::StringList: return evo::RType::String;
    default: return evo::RType::Invalid;
  }
}

evo::RType list_from_tag(int tag) {
  switch (static_cast<ListTypeTag>(tag)) {
    case ListTypeTag::Int: return evo::RType::IntList;
    case ListTypeTag::Float: return evo::RType::FloatList;
    case ListTypeTag::String: return evo::RType::StringList;
  }
  return evo::RType::Invalid;
}

template <class Row>
const Row& row_for(const std::vector<Row>& rows, std::size_t owner,
                   const char* label) {
  const Row* found = nullptr;
  for (const auto& row : rows) {
    if (row.node_index != owner) continue;
    if (found != nullptr) fail(owner, std::string("duplicate ") + label);
    found = &row;
  }
  if (found == nullptr) fail(owner, std::string("missing ") + label);
  return *found;
}

void distinct(std::size_t node, std::initializer_list<int> ids,
              const char* label) {
  std::set<int> unique;
  for (const int id : ids)
    if (id < 0 || !unique.insert(id).second)
      fail(node, std::string(label) + " binders must be distinct non-negative IDs");
}

class Verifier {
 public:
  Verifier(const AstProgram& program, const std::vector<evo::InputSpec>& inputs)
      : program_(program) {
    out_.subtree_end.assign(program.nodes.size(), 0);
    out_.expression_types.assign(program.nodes.size(), evo::RType::Invalid);
    if (inputs.size() > program.names.size())
      throw std::invalid_argument("legacy ast-prefix-v1 input schema exceeds name table");
    for (std::size_t i = 0; i < inputs.size(); ++i) {
      if (program.names[i] != inputs[i].name || !value_type(inputs[i].type))
        throw std::invalid_argument("legacy ast-prefix-v1 input schema does not match name table");
      inputs_[static_cast<int>(i)] = inputs[i].type;
    }
  }

  VerifiedAst run() {
    if (program_.version != "ast-prefix")
      throw std::invalid_argument(
          "legacy AST migration requires version ast-prefix");
    if (program_.nodes.empty() || program_.nodes.front().kind != NodeKind::PROGRAM)
      throw std::invalid_argument("legacy ast-prefix-v1 must start with Program");
    validate_special_metadata();
    const auto end = structural(0);
    if (end != program_.nodes.size())
      fail(end, "trailing prefix nodes");
    TypeEnv locals = inputs_;
    verify_program(&locals);
    return std::move(out_);
  }

 private:
  void valid_name(std::size_t node, int id, const char* label) const {
    if (id < 0 || static_cast<std::size_t>(id) >= program_.names.size())
      fail(node, std::string(label) + " binder is outside the name table");
  }

  template <class Row>
  void validate_owners(const std::vector<Row>& rows, NodeKind expected,
                       std::set<std::size_t>* seen, const char* label) const {
    for (const auto& row : rows) {
      if (row.node_index >= program_.nodes.size() ||
          program_.nodes[row.node_index].kind != expected)
        fail(row.node_index, std::string(label) + " metadata owner is invalid");
      if (!seen->insert(row.node_index).second)
        fail(row.node_index, std::string("duplicate ") + label + " metadata");
    }
  }

  void validate_special_metadata() const {
    std::set<std::size_t> linear, dc, dp1, dp2;
    validate_owners(program_.linear_rec_binders, NodeKind::LINEAR_REC,
                    &linear, "LinearRec");
    validate_owners(program_.asgp_dc_binders, NodeKind::ASGP_DC,
                    &dc, "ASGP-DC");
    validate_owners(program_.asgp_dp1d_specs, NodeKind::ASGP_DP1D,
                    &dp1, "ASGP-DP1D");
    validate_owners(program_.asgp_dp2d_specs, NodeKind::ASGP_DP2D,
                    &dp2, "ASGP-DP2D");
    for (std::size_t i = 0; i < program_.nodes.size(); ++i) {
      const auto kind = program_.nodes[i].kind;
      if ((kind == NodeKind::LINEAR_REC && !linear.count(i)) ||
          (kind == NodeKind::ASGP_DC && !dc.count(i)) ||
          (kind == NodeKind::ASGP_DP1D && !dp1.count(i)) ||
          (kind == NodeKind::ASGP_DP2D && !dp2.count(i)))
        fail(i, "specialized node is missing its metadata row");
    }
    for (const auto& row : program_.linear_rec_binders) {
      valid_name(row.node_index, row.elem_name, "element");
      valid_name(row.node_index, row.accum_name, "accumulator");
      valid_name(row.node_index, row.index_name, "index");
    }
    for (const auto& row : program_.asgp_dc_binders) {
      for (const int id : {row.solve_xs_name, row.solve_n_name,
                           row.solve_lo_name, row.divide_n_name,
                           row.combine_left_name, row.combine_right_name})
        valid_name(row.node_index, id, "ASGP-DC");
    }
    for (const auto& row : program_.asgp_dp1d_specs) {
      valid_name(row.node_index, row.solve_state_name, "DP1 solve-state");
      valid_name(row.node_index, row.transition_state_name,
                 "DP1 transition-state");
      for (int id : row.transition_dep_names)
        valid_name(row.node_index, id, "DP1 dependency");
      if (row.lo >= row.hi || row.base_state < row.lo ||
          row.base_state >= row.hi)
        fail(row.node_index, "DP1 bounds/base state are inconsistent");
      for (int offset : row.dep_offsets)
        if (offset <= 0) fail(row.node_index, "DP1 offsets must be positive");
    }
    for (const auto& row : program_.asgp_dp2d_specs) {
      for (const int id : {row.solve_i_name, row.solve_j_name,
                           row.transition_i_name, row.transition_j_name})
        valid_name(row.node_index, id, "DP2 state");
      for (int id : row.transition_dep_names)
        valid_name(row.node_index, id, "DP2 dependency");
      if (row.i_lo >= row.i_hi || row.j_lo >= row.j_hi ||
          row.base_i < row.i_lo || row.base_i >= row.i_hi ||
          row.base_j < row.j_lo || row.base_j >= row.j_hi)
        fail(row.node_index, "DP2 bounds/base cell are inconsistent");
    }
  }

  std::size_t structural(std::size_t index) {
    if (index >= program_.nodes.size()) fail(index, "truncated prefix tree");
    const int arity = prefix_arity(program_, index);
    if (arity < 0) fail(index, "invalid dynamic arity");
    auto next = index + 1;
    for (int i = 0; i < arity; ++i) next = structural(next);
    out_.subtree_end[index] = next;
    return next;
  }

  std::vector<std::size_t> children(std::size_t node) const {
    std::vector<std::size_t> result;
    auto child = node + 1;
    for (int i = 0; i < prefix_arity(program_, node); ++i) {
      result.push_back(child);
      child = out_.subtree_end[child];
    }
    return result;
  }

  void typed(std::size_t node, evo::RType type) {
    if (!value_type(type)) fail(node, "expression has no exact value type");
    out_.expression_types[node] = type;
  }

  evo::RType expression(std::size_t node, const TypeEnv& locals,
                        const TypeEnv& binders, bool isolated = false) {
    const auto kind = program_.nodes[node].kind;
    const auto child = children(node);
    const auto one = [&] { return expression(child[0], locals, binders, isolated); };
    const auto two = [&] {
      return std::pair{expression(child[0], locals, binders, isolated),
                       expression(child[1], locals, binders, isolated)};
    };
    evo::RType result = evo::RType::Invalid;
    if (kind == NodeKind::CONST) {
      const int id = program_.nodes[node].i0;
      if (id < 0 || static_cast<std::size_t>(id) >= program_.consts.size())
        fail(node, "constant index is out of range");
      result = value_type(program_.consts[static_cast<std::size_t>(id)].tag);
    } else if (kind == NodeKind::VAR) {
      const auto it = locals.find(program_.nodes[node].i0);
      if (it == locals.end()) fail(node, "undefined variable");
      result = it->second;
    } else if (kind == NodeKind::BOUND_VAR) {
      const auto it = binders.find(program_.nodes[node].i0);
      if (it == binders.end()) fail(node, "undefined legacy binder");
      result = it->second;
    } else if (kind == NodeKind::REGION_VAR) {
      const auto it = binders.find(-program_.nodes[node].i0 - 1);
      if (it == binders.end()) fail(node, "undefined lexical binder");
      result = it->second;
    } else if (kind == NodeKind::NEG) {
      result = one(); if (!numeric(result)) fail(node, "Neg requires numeric input");
    } else if (kind == NodeKind::NOT) {
      result = one(); if (result != evo::RType::Bool) fail(node, "Not requires Bool");
    } else if (kind >= NodeKind::ADD && kind <= NodeKind::MOD) {
      const auto [a, b] = two();
      if (!numeric(a) || !numeric(b)) fail(node, "numeric binary operator requires numeric inputs");
      result = kind == NodeKind::DIV ? evo::RType::Float :
          (a == evo::RType::Float || b == evo::RType::Float ? evo::RType::Float : evo::RType::Int);
    } else if (kind >= NodeKind::LT && kind <= NodeKind::GE) {
      const auto [a, b] = two();
      if (!numeric(a) || !numeric(b)) fail(node, "ordered comparison requires numeric inputs");
      result = evo::RType::Bool;
    } else if (kind == NodeKind::EQ || kind == NodeKind::NE) {
      const auto [a, b] = two();
      if (a != b && !(numeric(a) && numeric(b))) fail(node, "equality inputs are incompatible");
      result = evo::RType::Bool;
    } else if (kind == NodeKind::AND || kind == NodeKind::OR) {
      const auto [a, b] = two();
      if (a != evo::RType::Bool || b != evo::RType::Bool) fail(node, "boolean operator requires Bool");
      result = evo::RType::Bool;
    } else if (kind == NodeKind::IF_EXPR) {
      const auto condition = expression(child[0], locals, binders, isolated);
      const auto yes = expression(child[1], locals, binders, isolated);
      const auto no = expression(child[2], locals, binders, isolated);
      if (condition != evo::RType::Bool || yes != no) fail(node, "IfExpr types do not agree");
      result = yes;
    } else if (kind >= NodeKind::CALL_ABS && kind <= NodeKind::CALL_SINGLETON) {
      result = builtin(node, child, locals, binders, isolated);
    } else if (kind == NodeKind::MAP_LIST || kind == NodeKind::FILTER_LIST) {
      const auto source = expression(child[0], locals, binders, isolated);
      const auto elem = element(source);
      if (elem == evo::RType::Invalid) fail(node, "map/filter requires a sequence");
      TypeEnv body = binders; body[program_.nodes[node].i0] = elem;
      const auto body_type = expression(child[1], locals, body, isolated);
      if (kind == NodeKind::FILTER_LIST) {
        if (body_type != evo::RType::Bool) fail(node, "filter predicate must return Bool");
        result = source;
      } else {
        result = list_from_tag(program_.nodes[node].i1);
        if (element(result) != body_type) fail(node, "map body/result types disagree");
      }
    } else if (kind == NodeKind::LINEAR_REC) {
      result = linear(node, child, locals, binders);
    } else if (kind == NodeKind::ASGP_DC) {
      result = dc(node, child, locals, binders);
    } else if (kind == NodeKind::ASGP_DP1D) {
      result = dp1(node, child, locals, binders);
    } else if (kind == NodeKind::ASGP_DP2D) {
      result = dp2(node, child, locals, binders);
    } else if (kind == NodeKind::LET_REGION || kind == NodeKind::TRAVERSE ||
               kind == NodeKind::TRAVERSE_RANGE) {
      result = lexical(node, child, locals, binders, isolated);
    } else if (kind == NodeKind::CHECK_INT) {
      (void)one(); result = evo::RType::Int;
    } else if (kind == NodeKind::CHECK_LIST) {
      result = one(); if (!sequence(result)) fail(node, "CheckList requires a sequence");
    } else if (kind == NodeKind::BOUNDED_REGION) {
      result = bounded(node, child, locals, binders, isolated);
    } else {
      fail(node, "node is not an expression");
    }
    typed(node, result);
    return result;
  }

  evo::RType builtin(std::size_t node, const std::vector<std::size_t>& child,
                     const TypeEnv& locals, const TypeEnv& binders, bool isolated) {
    std::vector<evo::RType> args;
    for (auto index : child) args.push_back(expression(index, locals, binders, isolated));
    const auto kind = program_.nodes[node].kind;
    if (kind == NodeKind::CALL_ABS) {
      if (!numeric(args[0])) fail(node, "abs requires numeric input"); return args[0];
    }
    if (kind == NodeKind::CALL_MIN || kind == NodeKind::CALL_MAX) {
      if (!numeric(args[0]) || !numeric(args[1])) fail(node, "min/max requires numeric inputs");
      return args[0] == evo::RType::Float || args[1] == evo::RType::Float ? evo::RType::Float : evo::RType::Int;
    }
    if (kind == NodeKind::CALL_CLIP) {
      if (!numeric(args[0]) || !numeric(args[1]) || !numeric(args[2])) fail(node, "clip requires numeric inputs");
      return args[0] == evo::RType::Float || args[1] == evo::RType::Float || args[2] == evo::RType::Float ? evo::RType::Float : evo::RType::Int;
    }
    if (kind == NodeKind::CALL_IDIV0 || kind == NodeKind::CALL_IMOD0) {
      if (args[0] != evo::RType::Int || args[1] != evo::RType::Int) fail(node, "integer builtin requires Int"); return evo::RType::Int;
    }
    if (kind == NodeKind::CALL_LEN) {
      if (!sequence(args[0])) fail(node, "len requires a sequence"); return evo::RType::Int;
    }
    if (kind == NodeKind::CALL_CONCAT) {
      if (args[0] != args[1] || !sequence(args[0])) fail(node, "concat requires matching sequences"); return args[0];
    }
    if (kind == NodeKind::CALL_SLICE) {
      if (!sequence(args[0]) || args[1] != evo::RType::Int || args[2] != evo::RType::Int) fail(node, "slice types are invalid"); return args[0];
    }
    if (kind == NodeKind::CALL_INDEX) {
      if (!sequence(args[0]) || args[1] != evo::RType::Int) fail(node, "index types are invalid"); return element(args[0]);
    }
    if (kind == NodeKind::CALL_APPEND || kind == NodeKind::CALL_PREPEND) {
      if (!sequence(args[0]) || element(args[0]) != args[1]) fail(node, "append/prepend types are invalid"); return args[0];
    }
    if (kind == NodeKind::CALL_REVERSE) {
      if (!sequence(args[0])) fail(node, "reverse requires a sequence"); return args[0];
    }
    if (kind == NodeKind::CALL_FIND || kind == NodeKind::CALL_CONTAINS) {
      const auto searched = args[0] == evo::RType::String
          ? evo::RType::String : element(args[0]);
      if (!sequence(args[0]) || searched != args[1])
        fail(node, "find/contains types are invalid");
      return kind == NodeKind::CALL_FIND ? evo::RType::Int : evo::RType::Bool;
    }
    if (kind == NodeKind::CALL_CHAR_TO_STRING) { if (args[0] != evo::RType::Char) fail(node, "char_to_string requires Char"); return evo::RType::String; }
    if (kind == NodeKind::CALL_STRING_TO_CHAR) { if (args[0] != evo::RType::String) fail(node, "string_to_char requires String"); return evo::RType::Char; }
    if (kind == NodeKind::CALL_ORD) { if (args[0] != evo::RType::Char) fail(node, "ord requires Char"); return evo::RType::Int; }
    if (kind == NodeKind::CALL_CHR) { if (args[0] != evo::RType::Int) fail(node, "chr requires Int"); return evo::RType::Char; }
    if (kind >= NodeKind::CALL_IS_LETTER && kind <= NodeKind::CALL_IS_VOWEL) { if (args[0] != evo::RType::Char) fail(node, "character predicate requires Char"); return evo::RType::Bool; }
    if (kind == NodeKind::CALL_TO_LOWER || kind == NodeKind::CALL_TO_UPPER) { if (args[0] != evo::RType::Char) fail(node, "case conversion requires Char"); return evo::RType::Char; }
    if (kind == NodeKind::CALL_TO_STRING) { if (!numeric(args[0])) fail(node, "to_string requires numeric input"); return evo::RType::String; }
    if (kind == NodeKind::CALL_SINGLETON) {
      if (args[0] == evo::RType::Char) return evo::RType::String;
      if (args[0] == evo::RType::Int) return evo::RType::IntList;
      if (args[0] == evo::RType::Float) return evo::RType::FloatList;
      if (args[0] == evo::RType::String) return evo::RType::StringList;
    }
    fail(node, "unsupported builtin signature");
  }

  evo::RType linear(std::size_t node, const std::vector<std::size_t>& child,
                    const TypeEnv& locals, const TypeEnv& binders) {
    const auto& row = row_for(program_.linear_rec_binders, node, "LinearRec metadata");
    distinct(node, {row.elem_name, row.accum_name, row.index_name}, "LinearRec");
    const auto source = expression(child[0], locals, binders);
    const auto start = expression(child[1], locals, binders);
    const auto empty = expression(child[2], locals, binders);
    if (source == evo::RType::String || !sequence(source) ||
        start != evo::RType::Int || !value_type(empty))
      fail(node, "invalid LinearRec operands");
    TypeEnv step = binders; step[row.elem_name] = element(source); step[row.accum_name] = empty; step[row.index_name] = evo::RType::Int;
    TypeEnv last = binders; last[row.elem_name] = element(source); last[row.index_name] = evo::RType::Int;
    if (expression(child[3], locals, step) != empty || expression(child[4], locals, last) != empty)
      fail(node, "LinearRec phase result types disagree");
    return empty;
  }

  evo::RType dc(std::size_t node, const std::vector<std::size_t>& child,
                const TypeEnv& locals, const TypeEnv& binders) {
    const auto& row = row_for(program_.asgp_dc_binders, node, "ASGP-DC metadata");
    distinct(node, {row.solve_xs_name, row.solve_n_name, row.solve_lo_name}, "ASGP-DC solve");
    distinct(node, {row.combine_left_name, row.combine_right_name}, "ASGP-DC combine");
    const auto source = expression(child[0], locals, binders);
    if (!sequence(source)) fail(node, "ASGP-DC source is not a sequence");
    const auto solve = expression(child[1], {}, {{row.solve_xs_name, source}, {row.solve_n_name, evo::RType::Int}, {row.solve_lo_name, evo::RType::Int}}, true);
    const auto divide = expression(child[2], {}, {{row.divide_n_name, evo::RType::Int}}, true);
    const auto combine = expression(child[3], {}, {{row.combine_left_name, solve}, {row.combine_right_name, solve}}, true);
    if (!value_type(solve) || divide != evo::RType::Int || combine != solve) fail(node, "ASGP-DC phase result types disagree");
    return solve;
  }

  evo::RType dp1(std::size_t node, const std::vector<std::size_t>& child,
                 const TypeEnv& locals, const TypeEnv& binders) {
    const auto& row = row_for(program_.asgp_dp1d_specs, node, "ASGP-DP1D metadata");
    distinct(node, {row.solve_state_name}, "ASGP-DP1D solve");
    std::set<int> transition_ids{row.transition_state_name};
    for (int id : row.transition_dep_names)
      if (!transition_ids.insert(id).second)
        fail(node, "ASGP-DP1D transition binders must be distinct");
    const auto state = expression(child[0], locals, binders);
    const auto solve = expression(child[1], {}, {{row.solve_state_name, evo::RType::Int}}, true);
    TypeEnv transition{{row.transition_state_name, evo::RType::Int}};
    for (int id : row.transition_dep_names) transition[id] = solve;
    const auto next = expression(child[2], {}, transition, true);
    if (row.boundary_const < 0 || static_cast<std::size_t>(row.boundary_const) >= program_.consts.size()) fail(node, "DP1 boundary constant is out of range");
    if (state != evo::RType::Int || next != solve || value_type(program_.consts[row.boundary_const].tag) != solve) fail(node, "ASGP-DP1D types disagree");
    const bool dp1_kind = row.dep_kind >= NodeKind::DP1_BACKWARD1 && row.dep_kind <= NodeKind::DP1_FORWARD3;
    const int arity = static_cast<int>(row.dep_kind) <= static_cast<int>(NodeKind::DP1_BACKWARD3) ? static_cast<int>(row.dep_kind) - static_cast<int>(NodeKind::DP1_BACKWARD1) + 1 : static_cast<int>(row.dep_kind) - static_cast<int>(NodeKind::DP1_FORWARD1) + 1;
    if (!dp1_kind || arity <= 0 || row.dep_offsets.size() != static_cast<std::size_t>(arity) || row.transition_dep_names.size() != row.dep_offsets.size()) fail(node, "ASGP-DP1D dependency metadata is inconsistent");
    return solve;
  }

  evo::RType dp2(std::size_t node, const std::vector<std::size_t>& child,
                 const TypeEnv& locals, const TypeEnv& binders) {
    const auto& row = row_for(program_.asgp_dp2d_specs, node, "ASGP-DP2D metadata");
    distinct(node, {row.solve_i_name, row.solve_j_name}, "ASGP-DP2D solve");
    std::set<int> transition_ids{row.transition_i_name, row.transition_j_name};
    if (transition_ids.size() != 2)
      fail(node, "ASGP-DP2D transition state binders must be distinct");
    for (int id : row.transition_dep_names)
      if (!transition_ids.insert(id).second)
        fail(node, "ASGP-DP2D transition binders must be distinct");
    const auto i = expression(child[0], locals, binders);
    const auto j = expression(child[1], locals, binders);
    const auto solve = expression(child[2], {}, {{row.solve_i_name, evo::RType::Int}, {row.solve_j_name, evo::RType::Int}}, true);
    TypeEnv transition{{row.transition_i_name, evo::RType::Int}, {row.transition_j_name, evo::RType::Int}};
    for (int id : row.transition_dep_names) transition[id] = solve;
    const auto next = expression(child[3], {}, transition, true);
    int arity = 0;
    switch (row.dep_kind) {
      case NodeKind::DP2_DIAGONAL_BACKWARD: case NodeKind::DP2_DIAGONAL_FORWARD: arity = 1; break;
      case NodeKind::DP2_CROSS_BACKWARD: case NodeKind::DP2_CROSS_FORWARD: arity = 2; break;
      case NodeKind::DP2_NEIGHBORHOOD_BACKWARD3: case NodeKind::DP2_NEIGHBORHOOD_FORWARD3: arity = 3; break;
      default: fail(node, "ASGP-DP2D dependency kind is invalid");
    }
    if (row.boundary_const < 0 || static_cast<std::size_t>(row.boundary_const) >= program_.consts.size()) fail(node, "DP2 boundary constant is out of range");
    if (i != evo::RType::Int || j != evo::RType::Int || next != solve || value_type(program_.consts[row.boundary_const].tag) != solve || row.transition_dep_names.size() != static_cast<std::size_t>(arity)) fail(node, "ASGP-DP2D types disagree");
    return solve;
  }

  evo::RType lexical(std::size_t node, const std::vector<std::size_t>& child,
                     const TypeEnv& locals, const TypeEnv& binders, bool isolated) {
    const auto& region = row_for(program_.lexical_regions, node, "lexical-region metadata");
    const auto kind = program_.nodes[node].kind;
    const std::size_t body_slot = kind == NodeKind::LET_REGION ? 1 : (kind == NodeKind::TRAVERSE_RANGE ? 5 : 3);
    std::vector<evo::RType> args;
    for (std::size_t i = 0; i < body_slot; ++i) args.push_back(expression(child[i], locals, binders, isolated));
    std::vector<evo::RType> expected;
    if (kind == NodeKind::LET_REGION) expected = {args[0]};
    else {
      if (!sequence(args[0]) || args[1] != evo::RType::Int || (body_slot == 5 && (args[2] != evo::RType::Int || args[3] != evo::RType::Int))) fail(node, "traversal operands are invalid");
      expected = {element(args[0]), evo::RType::Int, args.back()};
    }
    if (region.body_argument != body_slot || region.bindings.size() != expected.size()) fail(node, "lexical-region shape is invalid");
    TypeEnv body = binders;
    for (std::size_t i = 0; i < expected.size(); ++i) {
      if (region.bindings[i].type != expected[i]) fail(node, "lexical binding type mismatch");
      body[-region.bindings[i].id - 1] = expected[i];
    }
    const auto result = expression(child[body_slot], locals, body, isolated);
    if (kind != NodeKind::LET_REGION && result != args.back()) fail(node, "traversal accumulator type mismatch");
    return result;
  }

  evo::RType bounded(std::size_t node, const std::vector<std::size_t>& child,
                     const TypeEnv& locals, const TypeEnv& binders, bool isolated) {
    const auto& spec = row_for(program_.bounded_region_specs, node, "bounded-region metadata");
    const auto states = spec.plan.state_types.size();
    for (std::size_t i = 0; i < states; ++i)
      if (expression(child[i], locals, binders, isolated) != value_type(spec.plan.state_types[i])) fail(node, "bounded-region state type mismatch");
    for (std::size_t i = 0; i < spec.plan.bound_operand_count; ++i)
      if (expression(child[states + i], locals, binders, isolated) != evo::RType::Int) fail(node, "bounded-region bound is not Int");
    for (std::size_t ordinal = 0; ordinal < spec.phases.size(); ++ordinal) {
      TypeEnv phase;
      const auto phase_kind = evo::bounded_region_phase_kind(spec.plan, ordinal);
      const auto prep = evo::bounded_region_preparation_ordinal(spec.plan, ordinal);
      for (const auto& binding : spec.phases[ordinal].bindings)
        phase[-binding.binder_id - 1] = value_type(region_slot_type(
            spec.plan, phase_kind, binding.source, prep));
      const auto result = expression(child[spec.phases[ordinal].argument], {}, phase, true);
      if (result != value_type(evo::bounded_region_phase_type(spec.plan, ordinal))) fail(node, "bounded-region phase type mismatch");
    }
    return value_type(spec.plan.result_type);
  }

  void verify_program(TypeEnv* locals) {
    const auto top = children(0);
    if (top.size() != 1) fail(0, "Program arity is invalid");
    verify_block(top[0], locals);
    if (out_.return_type == evo::RType::Invalid) fail(0, "program has no Return");
  }

  void verify_block(std::size_t node, TypeEnv* locals) {
    auto current = node;
    while (program_.nodes[current].kind == NodeKind::BLOCK_CONS) {
      const auto parts = children(current);
      statement(parts[0], locals);
      current = parts[1];
    }
    if (program_.nodes[current].kind != NodeKind::BLOCK_NIL) fail(current, "block does not end in BlockNil");
  }

  void statement(std::size_t node, TypeEnv* locals) {
    const auto kind = program_.nodes[node].kind;
    const auto child = children(node);
    if (kind == NodeKind::RETURN) {
      const auto type = expression(child[0], *locals, {});
      if (out_.return_type != evo::RType::Invalid && out_.return_type != type) fail(node, "inconsistent return type");
      out_.return_type = type;
    } else if (kind == NodeKind::ASSIGN) {
      (*locals)[program_.nodes[node].i0] = expression(child[0], *locals, {});
    } else if (kind == NodeKind::IF_STMT) {
      if (expression(child[0], *locals, {}) != evo::RType::Bool) fail(node, "IfStmt condition is not Bool");
      TypeEnv yes = *locals, no = *locals; verify_block(child[1], &yes); verify_block(child[2], &no);
      for (auto it = locals->begin(); it != locals->end();) {
        if (!yes.count(it->first) || !no.count(it->first) || yes[it->first] != no[it->first]) it = locals->erase(it); else ++it;
      }
    } else if (kind == NodeKind::FOR_RANGE) {
      if (expression(child[0], *locals, {}) != evo::RType::Int) fail(node, "ForRange bound is not Int");
      TypeEnv body = *locals; body[program_.nodes[node].i0] = evo::RType::Int; verify_block(child[1], &body);
    } else fail(node, "node is not a statement");
  }

  const AstProgram& program_;
  TypeEnv inputs_;
  VerifiedAst out_;
};

}  // namespace

int prefix_arity(const AstProgram& program, std::size_t node_index) {
  const auto kind = program.nodes.at(node_index).kind;
  switch (kind) {
    case NodeKind::PROGRAM: return 1;
    case NodeKind::BLOCK_NIL: return 0;
    case NodeKind::BLOCK_CONS: return 2;
    case NodeKind::ASSIGN: case NodeKind::RETURN: case NodeKind::NEG:
    case NodeKind::NOT: case NodeKind::CALL_ABS: case NodeKind::CALL_LEN:
    case NodeKind::CALL_REVERSE: case NodeKind::CALL_CHAR_TO_STRING:
    case NodeKind::CALL_STRING_TO_CHAR: case NodeKind::CALL_ORD:
    case NodeKind::CALL_CHR: case NodeKind::CALL_IS_LETTER:
    case NodeKind::CALL_IS_DIGIT: case NodeKind::CALL_IS_SPACE:
    case NodeKind::CALL_IS_VOWEL: case NodeKind::CALL_TO_LOWER:
    case NodeKind::CALL_TO_UPPER: case NodeKind::CALL_TO_STRING:
    case NodeKind::CALL_SINGLETON: case NodeKind::CHECK_INT:
    case NodeKind::CHECK_LIST: return 1;
    case NodeKind::IF_STMT: case NodeKind::IF_EXPR: case NodeKind::CALL_CLIP:
    case NodeKind::CALL_SLICE: case NodeKind::ASGP_DP1D: return 3;
    case NodeKind::FOR_RANGE: case NodeKind::LET_REGION: case NodeKind::ADD:
    case NodeKind::SUB: case NodeKind::MUL: case NodeKind::DIV:
    case NodeKind::MOD: case NodeKind::LT: case NodeKind::LE: case NodeKind::GT:
    case NodeKind::GE: case NodeKind::EQ: case NodeKind::NE: case NodeKind::AND:
    case NodeKind::OR: case NodeKind::CALL_MIN: case NodeKind::CALL_MAX:
    case NodeKind::CALL_IDIV0: case NodeKind::CALL_IMOD0:
    case NodeKind::CALL_CONCAT: case NodeKind::CALL_INDEX:
    case NodeKind::CALL_APPEND: case NodeKind::CALL_PREPEND:
    case NodeKind::CALL_FIND: case NodeKind::CALL_CONTAINS:
    case NodeKind::MAP_LIST: case NodeKind::FILTER_LIST: return 2;
    case NodeKind::CONST: case NodeKind::VAR: case NodeKind::BOUND_VAR:
    case NodeKind::REGION_VAR: case NodeKind::DP1_BACKWARD1:
    case NodeKind::DP1_BACKWARD2: case NodeKind::DP1_BACKWARD3:
    case NodeKind::DP1_FORWARD1: case NodeKind::DP1_FORWARD2:
    case NodeKind::DP1_FORWARD3: case NodeKind::DP2_CROSS_BACKWARD:
    case NodeKind::DP2_CROSS_FORWARD: case NodeKind::DP2_DIAGONAL_BACKWARD:
    case NodeKind::DP2_DIAGONAL_FORWARD:
    case NodeKind::DP2_NEIGHBORHOOD_BACKWARD3:
    case NodeKind::DP2_NEIGHBORHOOD_FORWARD3: return 0;
    case NodeKind::LINEAR_REC: return 5;
    case NodeKind::ASGP_DC: case NodeKind::ASGP_DP2D: return 4;
    case NodeKind::TRAVERSE: return 4;
    case NodeKind::TRAVERSE_RANGE: return 6;
    case NodeKind::BOUNDED_REGION: return program.nodes[node_index].i0;
    case NodeKind::COUNT: break;
  }
  return -1;
}

evo::NodeKind current_kind(NodeKind kind) {
#define GAGP_LEGACY_CURRENT(name) case NodeKind::name: return evo::NodeKind::name
  switch (kind) {
    GAGP_LEGACY_CURRENT(PROGRAM); GAGP_LEGACY_CURRENT(BLOCK_NIL);
    GAGP_LEGACY_CURRENT(BLOCK_CONS); GAGP_LEGACY_CURRENT(ASSIGN);
    GAGP_LEGACY_CURRENT(IF_STMT); GAGP_LEGACY_CURRENT(FOR_RANGE);
    GAGP_LEGACY_CURRENT(RETURN); GAGP_LEGACY_CURRENT(CONST);
    GAGP_LEGACY_CURRENT(VAR); GAGP_LEGACY_CURRENT(NEG); GAGP_LEGACY_CURRENT(NOT);
    GAGP_LEGACY_CURRENT(ADD); GAGP_LEGACY_CURRENT(SUB); GAGP_LEGACY_CURRENT(MUL);
    GAGP_LEGACY_CURRENT(DIV); GAGP_LEGACY_CURRENT(MOD); GAGP_LEGACY_CURRENT(LT);
    GAGP_LEGACY_CURRENT(LE); GAGP_LEGACY_CURRENT(GT); GAGP_LEGACY_CURRENT(GE);
    GAGP_LEGACY_CURRENT(EQ); GAGP_LEGACY_CURRENT(NE); GAGP_LEGACY_CURRENT(AND);
    GAGP_LEGACY_CURRENT(OR); GAGP_LEGACY_CURRENT(IF_EXPR);
    GAGP_LEGACY_CURRENT(CALL_ABS); GAGP_LEGACY_CURRENT(CALL_MIN);
    GAGP_LEGACY_CURRENT(CALL_MAX); GAGP_LEGACY_CURRENT(CALL_CLIP);
    GAGP_LEGACY_CURRENT(CALL_IDIV0); GAGP_LEGACY_CURRENT(CALL_IMOD0);
    GAGP_LEGACY_CURRENT(CALL_LEN); GAGP_LEGACY_CURRENT(CALL_CONCAT);
    GAGP_LEGACY_CURRENT(CALL_SLICE); GAGP_LEGACY_CURRENT(CALL_INDEX);
    GAGP_LEGACY_CURRENT(CALL_APPEND); GAGP_LEGACY_CURRENT(CALL_PREPEND);
    GAGP_LEGACY_CURRENT(CALL_REVERSE); GAGP_LEGACY_CURRENT(CALL_FIND);
    GAGP_LEGACY_CURRENT(CALL_CONTAINS); GAGP_LEGACY_CURRENT(CALL_CHAR_TO_STRING);
    GAGP_LEGACY_CURRENT(CALL_STRING_TO_CHAR); GAGP_LEGACY_CURRENT(CALL_ORD);
    GAGP_LEGACY_CURRENT(CALL_CHR); GAGP_LEGACY_CURRENT(CALL_IS_LETTER);
    GAGP_LEGACY_CURRENT(CALL_IS_DIGIT); GAGP_LEGACY_CURRENT(CALL_IS_SPACE);
    GAGP_LEGACY_CURRENT(CALL_IS_VOWEL); GAGP_LEGACY_CURRENT(CALL_TO_LOWER);
    GAGP_LEGACY_CURRENT(CALL_TO_UPPER); GAGP_LEGACY_CURRENT(CALL_TO_STRING);
    GAGP_LEGACY_CURRENT(CALL_SINGLETON); GAGP_LEGACY_CURRENT(BOUND_VAR);
    GAGP_LEGACY_CURRENT(LET_REGION); GAGP_LEGACY_CURRENT(TRAVERSE);
    GAGP_LEGACY_CURRENT(TRAVERSE_RANGE); GAGP_LEGACY_CURRENT(REGION_VAR);
    GAGP_LEGACY_CURRENT(CHECK_INT); GAGP_LEGACY_CURRENT(CHECK_LIST);
    GAGP_LEGACY_CURRENT(BOUNDED_REGION);
    default: throw std::invalid_argument("legacy specialized node has no current AST representation");
  }
#undef GAGP_LEGACY_CURRENT
}

VerifiedAst verify(const AstProgram& program,
                   const std::vector<evo::InputSpec>& inputs) {
  return Verifier(program, inputs).run();
}

}  // namespace gagp::migration::legacy_v1
