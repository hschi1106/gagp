#include "gagp/evolution/ast_program.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace gagp::evo {

namespace {

void append_int_vector(std::ostringstream& oss, const std::vector<int>& values) {
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i > 0) oss << "/";
    oss << values[i];
  }
}

std::string canonical_prefix_serialize(const AstProgram& program) {
  std::ostringstream oss;
  oss << "AstPrefix(";
  for (std::size_t i = 0; i < program.nodes.size(); ++i) {
    if (i > 0) oss << ",";
    const AstNode& node = program.nodes[i];
    oss << static_cast<int>(node.kind) << ":" << node.i0 << ":" << node.i1;
  }
  oss << ";LinearRec=";
  for (std::size_t i = 0; i < program.linear_rec_binders.size(); ++i) {
    if (i > 0) oss << ",";
    const LinearRecBinders& binders = program.linear_rec_binders[i];
    oss << binders.node_index << ":" << binders.elem_name << ":" << binders.accum_name << ":" << binders.index_name;
  }
  oss << ";AsgpDC=";
  for (std::size_t i = 0; i < program.asgp_dc_binders.size(); ++i) {
    if (i > 0) oss << ",";
    const AsgpDcBinders& binders = program.asgp_dc_binders[i];
    oss << binders.node_index << ":" << binders.solve_xs_name << ":" << binders.solve_n_name << ":"
        << binders.solve_lo_name << ":" << binders.divide_n_name << ":" << binders.combine_left_name
        << ":" << binders.combine_right_name;
  }
  oss << ";AsgpDP1D=";
  for (std::size_t i = 0; i < program.asgp_dp1d_specs.size(); ++i) {
    if (i > 0) oss << ",";
    const AsgpDp1dSpec& spec = program.asgp_dp1d_specs[i];
    oss << spec.node_index << ":" << spec.lo << ":" << spec.hi << ":" << spec.base_state << ":"
        << spec.boundary_const << ":" << static_cast<int>(spec.dep_kind) << ":";
    append_int_vector(oss, spec.dep_offsets);
    oss << ":" << spec.solve_state_name << ":" << spec.transition_state_name << ":";
    append_int_vector(oss, spec.transition_dep_names);
  }
  oss << ";AsgpDP2D=";
  for (std::size_t i = 0; i < program.asgp_dp2d_specs.size(); ++i) {
    if (i > 0) oss << ",";
    const AsgpDp2dSpec& spec = program.asgp_dp2d_specs[i];
    oss << spec.node_index << ":" << spec.i_lo << ":" << spec.i_hi << ":" << spec.j_lo << ":"
        << spec.j_hi << ":" << spec.base_i << ":" << spec.base_j << ":" << spec.boundary_const << ":"
        << static_cast<int>(spec.dep_kind) << ":" << spec.solve_i_name << ":" << spec.solve_j_name << ":"
        << spec.transition_i_name << ":" << spec.transition_j_name << ":";
    append_int_vector(oss, spec.transition_dep_names);
  }
  if (!program.lexical_regions.empty()) {
    oss << ";LexicalRegions=";
    for (std::size_t i = 0; i < program.lexical_regions.size(); ++i) {
      if (i > 0) oss << ",";
      const LexicalRegion& region = program.lexical_regions[i];
      oss << region.node_index << ":" << region.body_argument << ":";
      for (std::size_t j = 0; j < region.bindings.size(); ++j) {
        if (j > 0) oss << "/";
        const LexicalBinding& binding = region.bindings[j];
        oss << binding.id << ":" << static_cast<int>(binding.type);
      }
    }
  }
  if (!program.traversal_specs.empty()) {
    oss << ";TraversalSpecs=";
    for (std::size_t i = 0; i < program.traversal_specs.size(); ++i) {
      if (i > 0) oss << ",";
      const TraversalSpec& spec = program.traversal_specs[i];
      oss << spec.node_index << ":" << static_cast<int>(spec.direction);
    }
  }
  if (!program.fuel_specs.empty()) {
    oss << ";FuelSpecs=";
    for (std::size_t i = 0; i < program.fuel_specs.size(); ++i) {
      if (i > 0) oss << ",";
      const NodeFuelSpec& spec = program.fuel_specs[i];
      oss << spec.node_index << ":";
      for (std::size_t j = 0; j < spec.charges.size(); ++j) {
        if (j > 0) oss << "/";
        oss << static_cast<int>(spec.charges[j].event) << ":" << spec.charges[j].cost;
      }
    }
  }
  oss << ")";
  return oss.str();
}

std::string encode_value_for_cache_key(const Value& value) {
  std::ostringstream oss;
  oss << static_cast<int>(value.tag) << ":";
  if (value.tag == ValueTag::Int || value.tag == ValueTag::Char || value.tag == ValueTag::String ||
      value.tag == ValueTag::IntList || value.tag == ValueTag::FloatList || value.tag == ValueTag::StringList ||
      value.tag == ValueTag::FallbackToken) {
    oss << value.i;
    return oss.str();
  }
  if (value.tag == ValueTag::Float) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value.f, sizeof(bits));
    oss << std::hex << std::setfill('0') << std::setw(16) << bits;
    return oss.str();
  }
  if (value.tag == ValueTag::Bool) {
    oss << (value.b ? 1 : 0);
    return oss.str();
  }
  oss << "invalid";
  return oss.str();
}

std::string canonical_cache_key_serialize(const AstProgram& program) {
  std::ostringstream oss;
  oss << "AstCache(";
  oss << "version:" << program.version.size() << ":" << program.version;
  oss << ";names:" << program.names.size();
  for (const std::string& name : program.names) {
    oss << "|" << name.size() << ":" << name;
  }
  oss << ";consts:" << program.consts.size();
  for (const Value& value : program.consts) {
    const std::string encoded = encode_value_for_cache_key(value);
    oss << "|" << encoded.size() << ":" << encoded;
  }
  oss << ";nodes:" << program.nodes.size();
  for (const AstNode& node : program.nodes) {
    oss << "|" << static_cast<int>(node.kind) << ":" << node.i0 << ":" << node.i1;
  }
  oss << ";linear_rec:" << program.linear_rec_binders.size();
  for (const LinearRecBinders& binders : program.linear_rec_binders) {
    oss << "|" << binders.node_index << ":" << binders.elem_name << ":" << binders.accum_name << ":" << binders.index_name;
  }
  oss << ";asgp_dc:" << program.asgp_dc_binders.size();
  for (const AsgpDcBinders& binders : program.asgp_dc_binders) {
    oss << "|" << binders.node_index << ":" << binders.solve_xs_name << ":" << binders.solve_n_name
        << ":" << binders.solve_lo_name << ":" << binders.divide_n_name << ":" << binders.combine_left_name
        << ":" << binders.combine_right_name;
  }
  oss << ";asgp_dp1d:" << program.asgp_dp1d_specs.size();
  for (const AsgpDp1dSpec& spec : program.asgp_dp1d_specs) {
    oss << "|" << spec.node_index << ":" << spec.lo << ":" << spec.hi << ":" << spec.base_state << ":"
        << spec.boundary_const << ":" << static_cast<int>(spec.dep_kind) << ":";
    append_int_vector(oss, spec.dep_offsets);
    oss << ":" << spec.solve_state_name << ":" << spec.transition_state_name << ":";
    append_int_vector(oss, spec.transition_dep_names);
  }
  oss << ";asgp_dp2d:" << program.asgp_dp2d_specs.size();
  for (const AsgpDp2dSpec& spec : program.asgp_dp2d_specs) {
    oss << "|" << spec.node_index << ":" << spec.i_lo << ":" << spec.i_hi << ":" << spec.j_lo << ":"
        << spec.j_hi << ":" << spec.base_i << ":" << spec.base_j << ":" << spec.boundary_const << ":"
        << static_cast<int>(spec.dep_kind) << ":" << spec.solve_i_name << ":" << spec.solve_j_name << ":"
        << spec.transition_i_name << ":" << spec.transition_j_name << ":";
    append_int_vector(oss, spec.transition_dep_names);
  }
  if (!program.lexical_regions.empty()) {
    oss << ";lexical_regions:" << program.lexical_regions.size();
    for (const LexicalRegion& region : program.lexical_regions) {
      oss << "|" << region.node_index << ":" << region.body_argument << ":"
          << region.bindings.size();
      for (const LexicalBinding& binding : region.bindings) {
        oss << "/" << binding.id << ":" << static_cast<int>(binding.type);
      }
    }
  }
  if (!program.traversal_specs.empty()) {
    oss << ";traversal_specs:" << program.traversal_specs.size();
    for (const TraversalSpec& spec : program.traversal_specs) {
      oss << "|" << spec.node_index << ":" << static_cast<int>(spec.direction);
    }
  }
  if (!program.fuel_specs.empty()) {
    oss << ";fuel_specs:" << program.fuel_specs.size();
    for (const NodeFuelSpec& spec : program.fuel_specs) {
      oss << "|" << spec.node_index << ":" << spec.charges.size();
      for (const FuelCharge& charge : spec.charges)
        oss << "/" << static_cast<int>(charge.event) << ":" << charge.cost;
    }
  }
  oss << ")";
  return oss.str();
}

}  // namespace

std::string ast_to_string(const AstProgram& program) { return canonical_prefix_serialize(program); }

std::string ast_cache_key(const AstProgram& program) { return canonical_cache_key_serialize(program); }

}  // namespace gagp::evo
