#include "gagp/evolution/ast_program.hpp"

#include <charconv>
#include <type_traits>
#include <utility>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace gagp::evo {

namespace {

// Decimal formatting without stream sentry/locale overhead on every AST field.
class AstTextWriter {
 public:
  AstTextWriter& operator<<(const std::string& value) { text_ += value; return *this; }
  AstTextWriter& operator<<(const char* value) { text_ += value; return *this; }
  template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
  AstTextWriter& operator<<(T value) {
    char buffer[32];
    const auto end = std::to_chars(buffer, buffer + sizeof(buffer), value).ptr;
    text_.append(buffer, end);
    return *this;
  }
  std::string str() { return std::move(text_); }
 private:
  std::string text_;
};

void append_region_bound(AstTextWriter& oss, const RegionBound& bound) {
  oss << static_cast<int>(bound.kind) << ":" << bound.literal << ":"
      << bound.operand;
}

void append_window_endpoint(AstTextWriter& oss,
                            const WindowEndpoint& endpoint) {
  oss << static_cast<int>(endpoint.kind) << ":" << endpoint.cut;
}

void append_region_plan(AstTextWriter& oss, const RegionPlan& plan) {
  oss << "v" << plan.version << ";s" << plan.state_types.size();
  for (ValueTag type : plan.state_types) oss << "/" << static_cast<int>(type);
  oss << ";r" << static_cast<int>(plan.result_type);
  oss << ";p" << plan.parameter_types.size();
  for (ValueTag type : plan.parameter_types) oss << "/" << static_cast<int>(type);
  oss << ";prep" << plan.preparations.size();
  for (const RegionPreparation& preparation : plan.preparations) {
    oss << "/" << static_cast<int>(preparation.type) << ":"
        << static_cast<int>(preparation.kind);
  }
  oss << ";expr" << plan.request_expression_types.size();
  for (ValueTag type : plan.request_expression_types)
    oss << "/" << static_cast<int>(type);
  oss << ";bounds" << plan.bound_operand_count;
  oss << ";req" << plan.requests.size();
  for (const RegionRequest& request : plan.requests) {
    oss << "/" << request.states.size();
    for (const RegionStateTransition& transition : request.states) {
      oss << "[" << static_cast<int>(transition.kind) << ":"
          << transition.source_state << ":" << transition.offset << ":";
      append_window_endpoint(oss, transition.window.begin);
      oss << ":";
      append_window_endpoint(oss, transition.window.end);
      oss << ":" << transition.expression << "]";
    }
  }
  oss << ";lim" << plan.limits.frames << ":" << plan.limits.cells << ":"
      << plan.limits.entry_fuel;
  oss << ";memo" << (plan.memoized ? 1 : 0);
  oss << ";dup" << static_cast<int>(plan.duplicate_policy);
  oss << ";progress" << static_cast<int>(plan.progress);
  oss << ";slots" << plan.coordinate_slots.size();
  for (std::uint32_t slot : plan.coordinate_slots) oss << "/" << slot;
  oss << ";rank" << plan.coordinate_rank.size();
  for (const RankAxis& axis : plan.coordinate_rank)
    oss << "/" << axis.coordinate << ":" << axis.direction;
  oss << ";domains" << plan.coordinate_domains.size();
  for (const RegionCoordinateDomain& domain : plan.coordinate_domains) {
    oss << "/[";
    append_region_bound(oss, domain.lower);
    oss << ":";
    append_region_bound(oss, domain.upper);
    oss << "]";
  }
  oss << ";endpoint" << static_cast<int>(plan.coordinate_endpoint)
      << ";sequence" << plan.sequence_state;
}

void append_bounded_region_specs(AstTextWriter& oss,
                                 const std::vector<BoundedRegionSpec>& specs) {
  oss << specs.size();
  for (const BoundedRegionSpec& spec : specs) {
    oss << "|owner" << spec.node_index << "{";
    append_region_plan(oss, spec.plan);
    oss << ";captures" << spec.parameters.size();
    for (const RegionCapture& capture : spec.parameters) {
      oss << "/" << static_cast<int>(capture.kind) << ":" << capture.index;
    }
    oss << ";phases" << spec.phases.size();
    for (const RegionAstPhase& phase : spec.phases) {
      oss << "/arg" << phase.argument << "[" << phase.bindings.size();
      for (const RegionAstBinding& binding : phase.bindings) {
        oss << "/" << static_cast<int>(binding.source.bank) << ":"
            << binding.source.slot << ":" << binding.binder_id;
      }
      oss << "]";
    }
    oss << "}";
  }
}

std::string canonical_prefix_serialize(const AstProgram& program) {
  AstTextWriter oss;
  oss << "AstPrefix(";
  for (std::size_t i = 0; i < program.nodes.size(); ++i) {
    if (i > 0) oss << ",";
    const AstNode& node = program.nodes[i];
    oss << static_cast<int>(node.kind) << ":" << node.i0 << ":" << node.i1;
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
  if (!program.bounded_region_specs.empty()) {
    oss << ";BoundedRegions=";
    append_bounded_region_specs(oss, program.bounded_region_specs);
  }
  oss << ")";
  return oss.str();
}

std::string encode_value_for_cache_key(const Value& value) {
  std::string encoded = std::to_string(static_cast<int>(value.tag)) + ":";
  if (value.tag == ValueTag::Int || value.tag == ValueTag::Char || value.tag == ValueTag::String ||
      value.tag == ValueTag::IntList || value.tag == ValueTag::FloatList || value.tag == ValueTag::StringList ||
      value.tag == ValueTag::FallbackToken) {
    encoded += std::to_string(value.i);
  } else if (value.tag == ValueTag::Float) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value.f, sizeof(bits));
    char buffer[16];
    const auto end = std::to_chars(buffer, buffer + sizeof(buffer), bits, 16).ptr;
    encoded.append(16 - static_cast<std::size_t>(end - buffer), '0');
    encoded.append(buffer, end);
  } else if (value.tag == ValueTag::Bool) {
    encoded += value.b ? '1' : '0';
  } else {
    encoded += "invalid";
  }
  return encoded;
}

std::string canonical_cache_key_serialize(const AstProgram& program, bool include_constants = true) {
  AstTextWriter oss;
  oss << "AstCache(";
  oss << "version:" << program.version.size() << ":" << program.version;
  oss << ";names:" << program.names.size();
  for (const std::string& name : program.names) {
    oss << "|" << name.size() << ":" << name;
  }
  oss << ";consts:" << (include_constants ? program.consts.size() : 0);
  if (include_constants) {
    for (const Value& value : program.consts) {
      const std::string encoded = encode_value_for_cache_key(value);
      oss << "|" << encoded.size() << ":" << encoded;
    }
  }
  oss << ";nodes:" << program.nodes.size();
  for (const AstNode& node : program.nodes) {
    oss << "|" << static_cast<int>(node.kind) << ":" << node.i0 << ":" << node.i1;
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
  if (!program.bounded_region_specs.empty()) {
    oss << ";bounded_regions:";
    append_bounded_region_specs(oss, program.bounded_region_specs);
  }
  oss << ")";
  return oss.str();
}

}  // namespace

std::string ast_to_string(const AstProgram& program) { return canonical_prefix_serialize(program); }

std::string ast_cache_key(const AstProgram& program) { return canonical_cache_key_serialize(program); }

std::string ast_structure_cache_key(const AstProgram& program) {
  return canonical_cache_key_serialize(program, false);
}

}  // namespace gagp::evo
