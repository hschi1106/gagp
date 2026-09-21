#include "gagp/evolution/ast_verify.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>

#include "gagp/evolution/bounded_region.hpp"
#include "gagp/evolution/fuel_events.hpp"
#include "gagp/evolution/node_descriptor.hpp"

namespace gagp::evo {

const char* verify_code_name(VerifyCode code) noexcept {
  switch (code) {
    case VerifyCode::Ok: return "ok";
    case VerifyCode::UnsupportedVersion: return "unsupported_version";
    case VerifyCode::EmptyProgram: return "empty_program";
    case VerifyCode::UnknownNodeKind: return "unknown_node_kind";
    case VerifyCode::InvalidRoot: return "invalid_root";
    case VerifyCode::UnexpectedNodeCategory: return "unexpected_node_category";
    case VerifyCode::TruncatedPrefix: return "truncated_prefix";
    case VerifyCode::TrailingNodes: return "trailing_nodes";
    case VerifyCode::NameIndexOutOfRange: return "name_index_out_of_range";
    case VerifyCode::ConstantIndexOutOfRange: return "constant_index_out_of_range";
    case VerifyCode::InvalidConstantTag: return "invalid_constant_tag";
    case VerifyCode::InvalidIndexField: return "invalid_index_field";
    case VerifyCode::MissingMetadata: return "missing_metadata";
    case VerifyCode::DuplicateMetadata: return "duplicate_metadata";
    case VerifyCode::MetadataNodeMismatch: return "metadata_node_mismatch";
    case VerifyCode::DependencyArityMismatch: return "dependency_arity_mismatch";
    case VerifyCode::InvalidBounds: return "invalid_bounds";
    case VerifyCode::DuplicateName: return "duplicate_name";
    case VerifyCode::DuplicateInput: return "duplicate_input";
    case VerifyCode::InvalidInputType: return "invalid_input_type";
    case VerifyCode::UndefinedLocal: return "undefined_local";
    case VerifyCode::UndefinedBinder: return "undefined_binder";
    case VerifyCode::DuplicateBinder: return "duplicate_binder";
    case VerifyCode::TypeMismatch: return "type_mismatch";
    case VerifyCode::InconsistentReturnType: return "inconsistent_return_type";
    case VerifyCode::MissingReturn: return "missing_return";
    case VerifyCode::NestedIsolatedRegion: return "nested_isolated_region";
    case VerifyCode::ResourceLimit: return "resource_limit";
  }
  return "unknown_verify_code";
}

namespace {

std::string node_path(std::size_t index) {
  return "$.nodes[" + std::to_string(index) + "]";
}

bool is_public_value_tag(ValueTag tag) {
  switch (tag) {
    case ValueTag::Int:
    case ValueTag::Float:
    case ValueTag::Bool:
    case ValueTag::Char:
    case ValueTag::String:
    case ValueTag::IntList:
    case ValueTag::FloatList:
    case ValueTag::StringList:
      return true;
    case ValueTag::FallbackToken:
    case ValueTag::Invalid:
      return false;
  }
  return false;
}

class StructuralVerifier {
 public:
  StructuralVerifier(const AstProgram& ast, const VerifyOptions& options)
      : ast_(ast), options_(options) {
    result_.verified.subtree_end.assign(ast.nodes.size(), 0);
    result_.verified.expression_types.assign(ast.nodes.size(), RType::Invalid);
    result_.verified.expression_scope_signatures.assign(ast.nodes.size(), 0);
    result_.verified.expression_binder_signatures.assign(ast.nodes.size(), 0);
    if (options.capture_exact_scopes) {
      result_.verified.expression_scope_ids.assign(
          ast.nodes.size(), std::numeric_limits<std::uint32_t>::max());
    }
  }

  AstVerifyResult run() {
    if (ast_.version != k_ast_prefix_version_current) {
      return fail(VerifyCode::UnsupportedVersion, 0, "$.version",
                  "expected AST version ast-prefix-v2");
    }
    if (ast_.nodes.empty()) {
      return fail(VerifyCode::EmptyProgram, 0, "$.nodes", "program has no prefix nodes");
    }
    if (options_.max_nodes != 0 && ast_.nodes.size() > options_.max_nodes) {
      return fail(VerifyCode::ResourceLimit, 0, "$.nodes", "node count exceeds configured limit");
    }
    if (!validate_metadata_limit()) return result_;
    if (!validate_nodes_and_tables()) return result_;
    if (ast_.nodes.front().kind != NodeKind::PROGRAM) {
      return fail(VerifyCode::InvalidRoot, 0, node_path(0), "root node must be PROGRAM");
    }

    std::size_t end = 0;
    if (!parse_program(0, &end)) return result_;
    if (end != ast_.nodes.size()) {
      return fail(VerifyCode::TrailingNodes, end, node_path(end),
                  "prefix program has trailing nodes");
    }
    if (!validate_side_tables()) return result_;

    result_.ok = true;
    result_.diagnostic = VerifyDiagnostic{};
    return result_;
  }

 private:
  AstVerifyResult fail(VerifyCode code, std::size_t node_index, std::string path,
                       std::string message) {
    result_.ok = false;
    result_.diagnostic = VerifyDiagnostic{code, node_index, std::move(path), std::move(message)};
    return result_;
  }

  bool fail_bool(VerifyCode code, std::size_t node_index, std::string path,
                 std::string message) {
    (void)fail(code, node_index, std::move(path), std::move(message));
    return false;
  }

  bool valid_name(int value, std::size_t node_index, const std::string& path) {
    if (value < 0 || static_cast<std::size_t>(value) >= ast_.names.size()) {
      return fail_bool(VerifyCode::NameIndexOutOfRange, node_index, path,
                       "name index is outside the names table");
    }
    return true;
  }

  bool valid_const(int value, std::size_t node_index, const std::string& path) {
    if (value < 0 || static_cast<std::size_t>(value) >= ast_.consts.size()) {
      return fail_bool(VerifyCode::ConstantIndexOutOfRange, node_index, path,
                       "constant index is outside the consts table");
    }
    return true;
  }

  bool validate_index_role(NodeIndexRole role, int value, std::size_t node_index,
                           const std::string& path) {
    switch (role) {
      case NodeIndexRole::Unused:
        if (value != 0) {
          return fail_bool(VerifyCode::InvalidIndexField, node_index, path,
                           "unused node index field must be zero");
        }
        return true;
      case NodeIndexRole::Name:
        return valid_name(value, node_index, path);
      case NodeIndexRole::Constant:
        return valid_const(value, node_index, path);
      case NodeIndexRole::BinderId:
        if (value < 0 || value == std::numeric_limits<int>::max()) {
          return fail_bool(VerifyCode::InvalidIndexField, node_index, path,
                           "binder id must be in the public non-negative id range");
        }
        return true;
      case NodeIndexRole::DynamicArity:
        if (value < 0 ||
            static_cast<std::size_t>(value) > kBoundedRegionArityCapacity) {
          return fail_bool(VerifyCode::InvalidIndexField, node_index, path,
                           "dynamic prefix arity is outside the supported range");
        }
        return true;
    }
    return false;
  }

  bool validate_metadata_limit() {
    if (options_.max_metadata_entries == 0) return true;
    std::size_t metadata_count = 0;
    const auto add_entries = [&](std::size_t count) {
      if (count > options_.max_metadata_entries - metadata_count) return false;
      metadata_count += count;
      return true;
    };
    if (!add_entries(ast_.lexical_regions.size()) ||
        !add_entries(ast_.traversal_specs.size()) ||
        !add_entries(ast_.fuel_specs.size()) ||
        !add_entries(ast_.bounded_region_specs.size())) {
      return fail_bool(VerifyCode::ResourceLimit, 0, "$",
                       "metadata count exceeds configured limit");
    }
    for (const LexicalRegion& region : ast_.lexical_regions) {
      if (!add_entries(region.bindings.size())) {
        return fail_bool(VerifyCode::ResourceLimit, region.node_index, "$.lexical_regions",
                         "metadata count exceeds configured limit");
      }
    }
    for (const NodeFuelSpec& spec : ast_.fuel_specs) {
      if (!add_entries(spec.charges.size())) {
        return fail_bool(VerifyCode::ResourceLimit, spec.node_index, "$.fuel_specs",
                         "metadata count exceeds configured limit");
      }
    }
    for (const BoundedRegionSpec& spec : ast_.bounded_region_specs) {
      if (!add_entries(spec.parameters.size()) ||
          !add_entries(spec.phases.size()) ||
          !add_entries(spec.plan.state_types.size()) ||
          !add_entries(spec.plan.parameter_types.size()) ||
          !add_entries(spec.plan.preparations.size()) ||
          !add_entries(spec.plan.request_expression_types.size()) ||
          !add_entries(spec.plan.coordinate_slots.size()) ||
          !add_entries(spec.plan.coordinate_rank.size()) ||
          !add_entries(spec.plan.coordinate_domains.size()) ||
          !add_entries(spec.plan.requests.size())) {
        return fail_bool(VerifyCode::ResourceLimit, spec.node_index,
                         "$.bounded_region_specs",
                         "metadata count exceeds configured limit");
      }
      for (const auto& request : spec.plan.requests) {
        if (!add_entries(request.states.size()))
          return fail_bool(VerifyCode::ResourceLimit, spec.node_index,
                           "$.bounded_region_specs", "metadata count exceeds configured limit");
      }
      for (const RegionAstPhase& phase : spec.phases) {
        if (!add_entries(phase.bindings.size())) {
          return fail_bool(VerifyCode::ResourceLimit, spec.node_index,
                           "$.bounded_region_specs",
                           "metadata count exceeds configured limit");
        }
      }
    }
    return true;
  }

  bool validate_nodes_and_tables() {
    for (std::size_t i = 0; i < ast_.nodes.size(); ++i) {
      const AstNode& node = ast_.nodes[i];
      const int kind_value = static_cast<int>(node.kind);
      if (!is_known_node_kind(kind_value)) {
        return fail_bool(VerifyCode::UnknownNodeKind, i, node_path(i) + ".kind",
                         "node kind is outside the current descriptor table");
      }
      const NodeDescriptor& descriptor = node_descriptor(node.kind);
      if (!validate_index_role(descriptor.i0_role, node.i0, i, node_path(i) + ".i0")) return false;
      if (!validate_index_role(descriptor.i1_role, node.i1, i, node_path(i) + ".i1")) return false;
    }
    for (std::size_t i = 0; i < ast_.consts.size(); ++i) {
      if (!is_public_value_tag(ast_.consts[i].tag)) {
        return fail_bool(VerifyCode::InvalidConstantTag, 0,
                         "$.consts[" + std::to_string(i) + "]",
                         "constant uses a private or invalid value tag");
      }
    }
    return true;
  }

  bool node_at(std::size_t index, NodeCategory category, const AstNode** out) {
    if (index >= ast_.nodes.size()) {
      return fail_bool(VerifyCode::TruncatedPrefix, index, node_path(index),
                       "prefix program ended before the expected node");
    }
    const NodeDescriptor& descriptor = node_descriptor(ast_.nodes[index].kind);
    if (descriptor.category != category) {
      return fail_bool(VerifyCode::UnexpectedNodeCategory, index, node_path(index),
                       "node appears in an invalid prefix grammar position");
    }
    *out = &ast_.nodes[index];
    return true;
  }

  bool parse_program(std::size_t index, std::size_t* end) {
    const AstNode* node = nullptr;
    if (!node_at(index, NodeCategory::Program, &node)) return false;
    if (node->kind != NodeKind::PROGRAM) {
      return fail_bool(VerifyCode::InvalidRoot, index, node_path(index),
                       "program position requires PROGRAM");
    }
    std::size_t next = 0;
    if (!parse_block(index + 1, &next)) return false;
    result_.verified.subtree_end[index] = next;
    *end = next;
    return true;
  }

  bool parse_block(std::size_t index, std::size_t* end) {
    const AstNode* node = nullptr;
    if (!node_at(index, NodeCategory::Block, &node)) return false;
    if (node->kind == NodeKind::BLOCK_NIL) {
      result_.verified.subtree_end[index] = index + 1;
      *end = index + 1;
      return true;
    }
    if (node->kind != NodeKind::BLOCK_CONS) {
      return fail_bool(VerifyCode::UnexpectedNodeCategory, index, node_path(index),
                       "block position requires BLOCK_NIL or BLOCK_CONS");
    }
    std::size_t next = 0;
    if (!parse_statement(index + 1, &next)) return false;
    if (!parse_block(next, &next)) return false;
    result_.verified.subtree_end[index] = next;
    *end = next;
    return true;
  }

  bool parse_statement(std::size_t index, std::size_t* end) {
    const AstNode* node = nullptr;
    if (!node_at(index, NodeCategory::Statement, &node)) return false;
    ++result_.verified.statement_count;
    if (options_.max_statements != 0 &&
        result_.verified.statement_count > options_.max_statements) {
      return fail_bool(VerifyCode::ResourceLimit, index, node_path(index),
                       "statement count exceeds configured limit");
    }

    std::size_t next = index + 1;
    switch (node->kind) {
      case NodeKind::ASSIGN:
      case NodeKind::RETURN:
        if (!parse_expression(next, 1, &next)) return false;
        break;
      case NodeKind::IF_STMT:
        if (!parse_expression(next, 1, &next) || !parse_block(next, &next) ||
            !parse_block(next, &next)) return false;
        break;
      case NodeKind::FOR_RANGE:
        if (!parse_expression(next, 1, &next) || !parse_block(next, &next)) return false;
        break;
      default:
        return fail_bool(VerifyCode::UnexpectedNodeCategory, index, node_path(index),
                         "unknown statement form");
    }
    result_.verified.subtree_end[index] = next;
    *end = next;
    return true;
  }

  bool parse_expression(std::size_t index, std::size_t depth, std::size_t* end) {
    const AstNode* node = nullptr;
    if (!node_at(index, NodeCategory::Expression, &node)) return false;
    result_.verified.max_expression_depth =
        std::max(result_.verified.max_expression_depth, depth);
    if (options_.max_expression_depth != 0 && depth > options_.max_expression_depth) {
      return fail_bool(VerifyCode::ResourceLimit, index, node_path(index),
                       "expression depth exceeds configured limit");
    }
    const int arity = node_prefix_arity(*node);
    std::size_t next = index + 1;
    for (int child = 0; child < arity; ++child) {
      if (!parse_expression(next, depth + 1, &next)) return false;
    }
    result_.verified.subtree_end[index] = next;
    *end = next;
    return true;
  }

  bool check_metadata_node(std::size_t node_index, NodeKind kind,
                           const std::string& path) {
    if (node_index >= ast_.nodes.size() || ast_.nodes[node_index].kind != kind) {
      return fail_bool(VerifyCode::MetadataNodeMismatch, node_index, path + ".node_index",
                       "metadata node_index does not identify the matching structured node");
    }
    return true;
  }

  bool check_unique(std::set<std::size_t>* seen, std::size_t node_index,
                    const std::string& path) {
    if (!seen->insert(node_index).second) {
      return fail_bool(VerifyCode::DuplicateMetadata, node_index, path + ".node_index",
                       "structured node has duplicate side-table metadata");
    }
    return true;
  }

  bool validate_side_tables() {
    std::set<std::size_t> expected_lexical;
    std::set<std::size_t> expected_traversal;
    std::set<std::size_t> expected_bounded;
    for (std::size_t i = 0; i < ast_.nodes.size(); ++i) {
      switch (node_descriptor(ast_.nodes[i].kind).metadata) {
        case NodeMetadataKind::LexicalRegion: expected_lexical.insert(i); break;
        case NodeMetadataKind::BoundedRegion: expected_bounded.insert(i); break;
        case NodeMetadataKind::None: break;
      }
      if (ast_.nodes[i].kind == NodeKind::TRAVERSE ||
          ast_.nodes[i].kind == NodeKind::TRAVERSE_RANGE) {
        expected_traversal.insert(i);
      }
    }

    std::set<std::size_t> seen_lexical;
    std::set<int> declared_binder_ids;
    for (std::size_t i = 0; i < ast_.lexical_regions.size(); ++i) {
      const LexicalRegion& row = ast_.lexical_regions[i];
      const std::string path = "$.lexical_regions[" + std::to_string(i) + "]";
      if (row.node_index >= ast_.nodes.size()) {
        return fail_bool(VerifyCode::MetadataNodeMismatch, row.node_index,
                         path + ".node_index",
                         "lexical metadata node_index does not identify a region node");
      }
      const NodeKind kind = ast_.nodes[row.node_index].kind;
      int expected_body_argument = 0;
      std::size_t expected_binding_count = 0;
      switch (kind) {
        case NodeKind::LET_REGION:
          expected_body_argument = 1;
          expected_binding_count = 1;
          break;
        case NodeKind::TRAVERSE:
          expected_body_argument = 3;
          expected_binding_count = 3;
          break;
        case NodeKind::TRAVERSE_RANGE:
          expected_body_argument = 5;
          expected_binding_count = 3;
          break;
        default:
          return fail_bool(VerifyCode::MetadataNodeMismatch, row.node_index,
                           path + ".node_index",
                           "lexical metadata may only identify a region node");
      }
      if (!check_unique(&seen_lexical, row.node_index, path)) return false;
      if (row.body_argument != expected_body_argument) {
        return fail_bool(VerifyCode::MetadataNodeMismatch, row.node_index,
                         path + ".body_argument",
                         "lexical metadata body_argument does not match the region form");
      }
      if (row.bindings.size() != expected_binding_count) {
        return fail_bool(VerifyCode::DependencyArityMismatch, row.node_index,
                         path + ".bindings",
                         "lexical metadata binding count does not match the region form");
      }
      for (std::size_t j = 0; j < row.bindings.size(); ++j) {
        const LexicalBinding& binding = row.bindings[j];
        const std::string binding_path =
            path + ".bindings[" + std::to_string(j) + "]";
        if (binding.id < 0 || binding.id == std::numeric_limits<int>::max()) {
          return fail_bool(VerifyCode::InvalidIndexField, row.node_index,
                           binding_path + ".id",
                           "lexical binding id must be in the public non-negative id range");
        }
        if (!is_public_rtype(binding.type)) {
          return fail_bool(VerifyCode::InvalidInputType, row.node_index,
                           binding_path + ".type",
                           "lexical binding must use one of the eight public value types");
        }
        if (!declared_binder_ids.insert(binding.id).second) {
          return fail_bool(VerifyCode::DuplicateBinder, row.node_index,
                           binding_path + ".id",
                           "lexical binding ids must be globally unique");
        }
      }
    }

    std::set<std::size_t> seen_bounded;
    for (std::size_t i = 0; i < ast_.bounded_region_specs.size(); ++i) {
      const BoundedRegionSpec& row = ast_.bounded_region_specs[i];
      const std::string path =
          "$.bounded_region_specs[" + std::to_string(i) + "]";
      if (!check_metadata_node(row.node_index, NodeKind::BOUNDED_REGION, path) ||
          !check_unique(&seen_bounded, row.node_index, path)) {
        return false;
      }
      try {
        validate_region_plan(row.plan);
      } catch (const std::invalid_argument& error) {
        return fail_bool(VerifyCode::InvalidBounds, row.node_index,
                         path + ".plan", error.what());
      }

      std::size_t arity = 0;
      try {
        arity = bounded_region_arity(row.plan);
      } catch (const std::invalid_argument& error) {
        return fail_bool(VerifyCode::DependencyArityMismatch, row.node_index,
                         path + ".plan", error.what());
      }
      if (ast_.nodes[row.node_index].i0 != static_cast<int>(arity)) {
        return fail_bool(VerifyCode::DependencyArityMismatch, row.node_index,
                         node_path(row.node_index) + ".i0",
                         "bounded region node arity does not match its plan");
      }
      if (row.parameters.size() != row.plan.parameter_types.size()) {
        return fail_bool(VerifyCode::DependencyArityMismatch, row.node_index,
                         path + ".parameters",
                         "bounded region captures must match parameter types");
      }
      std::set<std::pair<int, int>> captured;
      for (std::size_t j = 0; j < row.parameters.size(); ++j) {
        const RegionCapture& capture = row.parameters[j];
        if (!captured.emplace(static_cast<int>(capture.kind), capture.index).second)
          return fail_bool(VerifyCode::DuplicateMetadata, row.node_index, path + ".parameters",
                           "bounded region captures must be distinct");
        const std::string capture_path =
            path + ".parameters[" + std::to_string(j) + "]";
        switch (capture.kind) {
          case RegionCaptureKind::Lexical:
            if (capture.index < 0 ||
                capture.index == std::numeric_limits<int>::max()) {
              return fail_bool(
                  VerifyCode::InvalidIndexField, row.node_index,
                  capture_path + ".index",
                  "lexical capture id must be in the public non-negative id range");
            }
            break;
          case RegionCaptureKind::Name:
            if (!valid_name(capture.index, row.node_index,
                            capture_path + ".index")) {
              return false;
            }
            break;
          default:
            return fail_bool(VerifyCode::InvalidIndexField, row.node_index,
                             capture_path + ".kind",
                             "invalid bounded region capture kind");
        }
      }

      const std::size_t phase_count =
          arity - row.plan.state_types.size() - row.plan.bound_operand_count;
      if (row.phases.size() != phase_count) {
        return fail_bool(VerifyCode::DependencyArityMismatch, row.node_index,
                         path + ".phases",
                         "bounded region phase count does not match its plan");
      }
      for (std::size_t j = 0; j < row.phases.size(); ++j) {
        const RegionAstPhase& phase = row.phases[j];
        const std::string phase_path =
            path + ".phases[" + std::to_string(j) + "]";
        const std::size_t expected_argument = row.plan.state_types.size() +
                                              row.plan.bound_operand_count + j;
        if (phase.argument != expected_argument) {
          return fail_bool(VerifyCode::MetadataNodeMismatch, row.node_index,
                           phase_path + ".argument",
                           "bounded region phase argument is not canonical");
        }
        if (phase.bindings.size() > kBoundedRegionPhaseBindingCapacity) {
          return fail_bool(VerifyCode::ResourceLimit, row.node_index,
                           phase_path + ".bindings",
                           "bounded region phase binding count exceeds capacity");
        }

        const RegionPhaseKind kind =
            bounded_region_phase_kind(row.plan, j);
        const std::uint32_t preparation =
            bounded_region_preparation_ordinal(row.plan, j);
        std::set<std::pair<int, std::uint32_t>> sources;
        for (std::size_t k = 0; k < phase.bindings.size(); ++k) {
          const RegionAstBinding& binding = phase.bindings[k];
          const std::string binding_path =
              phase_path + ".bindings[" + std::to_string(k) + "]";
          const auto source_key = std::make_pair(
              static_cast<int>(binding.source.bank), binding.source.slot);
          if (!sources.insert(source_key).second) {
            return fail_bool(VerifyCode::DuplicateMetadata, row.node_index,
                             binding_path + ".source",
                             "bounded region phase source slots must be unique");
          }
          try {
            (void)region_slot_type(row.plan, kind, binding.source,
                                   preparation);
          } catch (const std::invalid_argument& error) {
            return fail_bool(VerifyCode::MetadataNodeMismatch, row.node_index,
                             binding_path + ".source", error.what());
          }
          if (binding.binder_id < 0 ||
              binding.binder_id == std::numeric_limits<int>::max()) {
            return fail_bool(
                VerifyCode::InvalidIndexField, row.node_index,
                binding_path + ".binder_id",
                "phase binder id must be in the public non-negative id range");
          }
          if (!declared_binder_ids.insert(binding.binder_id).second) {
            return fail_bool(VerifyCode::DuplicateBinder, row.node_index,
                             binding_path + ".binder_id",
                             "phase binder ids must be globally unique");
          }
        }
      }
    }

    std::set<std::size_t> seen_traversal;
    for (std::size_t i = 0; i < ast_.traversal_specs.size(); ++i) {
      const TraversalSpec& row = ast_.traversal_specs[i];
      const std::string path = "$.traversal_specs[" + std::to_string(i) + "]";
      if (row.node_index >= ast_.nodes.size() ||
          (ast_.nodes[row.node_index].kind != NodeKind::TRAVERSE &&
           ast_.nodes[row.node_index].kind != NodeKind::TRAVERSE_RANGE)) {
        return fail_bool(VerifyCode::MetadataNodeMismatch, row.node_index,
                         path + ".node_index",
                         "traversal metadata may only identify a traversal node");
      }
      if (!check_unique(&seen_traversal, row.node_index, path)) return false;
      if (row.direction != TraversalDirection::Forward &&
          row.direction != TraversalDirection::Reverse) {
        return fail_bool(VerifyCode::InvalidIndexField, row.node_index,
                         path + ".direction",
                         "traversal direction must be Forward or Reverse");
      }
    }

    std::set<std::size_t> seen_fuel;
    for (std::size_t i = 0; i < ast_.fuel_specs.size(); ++i) {
      const NodeFuelSpec& row = ast_.fuel_specs[i];
      const std::string path = "$.fuel_specs[" + std::to_string(i) + "]";
      if (row.node_index >= ast_.nodes.size()) {
        return fail_bool(VerifyCode::MetadataNodeMismatch, row.node_index,
                         path + ".node_index",
                         "fuel metadata node_index does not identify a node");
      }
      if (!check_unique(&seen_fuel, row.node_index, path)) return false;
      if (row.charges.empty()) {
        return fail_bool(VerifyCode::MissingMetadata, row.node_index,
                         path + ".charges",
                         "fuel metadata must contain at least one charge");
      }
      std::set<FuelEvent> seen_events;
      for (std::size_t j = 0; j < row.charges.size(); ++j) {
        const FuelCharge& charge = row.charges[j];
        const std::string charge_path =
            path + ".charges[" + std::to_string(j) + "]";
        if (!seen_events.insert(charge.event).second) {
          return fail_bool(VerifyCode::DuplicateMetadata, row.node_index,
                           charge_path + ".event",
                           "fuel events must be unique within a node profile");
        }
        if (!supports_fuel_event(ast_.nodes[row.node_index].kind, charge.event)) {
          return fail_bool(VerifyCode::MetadataNodeMismatch, row.node_index,
                           charge_path + ".event",
                           "fuel event is not supported by the owning node kind");
        }
        if (charge.cost > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
          return fail_bool(VerifyCode::InvalidIndexField, row.node_index,
                           charge_path + ".cost",
                           "fuel charge cost exceeds INT_MAX");
        }
      }
    }

    if (!check_missing(expected_lexical, seen_lexical, "$.lexical_regions") ||
        !check_missing(expected_traversal, seen_traversal, "$.traversal_specs") ||
        !check_missing(expected_bounded, seen_bounded,
                       "$.bounded_region_specs")) return false;
    return true;
  }

  bool is_public_rtype(RType type) const {
    switch (type) {
      case RType::Int:
      case RType::Float:
      case RType::Bool:
      case RType::Char:
      case RType::String:
      case RType::IntList:
      case RType::FloatList:
      case RType::StringList:
        return true;
      case RType::Any:
      case RType::Invalid:
        return false;
    }
    return false;
  }

  bool check_missing(const std::set<std::size_t>& expected,
                     const std::set<std::size_t>& seen,
                     const std::string& path) {
    for (std::size_t node_index : expected) {
      if (seen.count(node_index) == 0U) {
        return fail_bool(VerifyCode::MissingMetadata, node_index, path,
                         "structured node is missing required side-table metadata");
      }
    }
    return true;
  }

  const AstProgram& ast_;
  const VerifyOptions& options_;
  AstVerifyResult result_;
};

}  // namespace

AstVerifyResult verify_ast_structure(const AstProgram& ast, const VerifyOptions& options) {
  return StructuralVerifier(ast, options).run();
}

}  // namespace gagp::evo
