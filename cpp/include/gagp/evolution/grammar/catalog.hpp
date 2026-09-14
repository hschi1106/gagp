#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gagp/evolution/ast_program.hpp"
#include "gagp/evolution/node_descriptor.hpp"

namespace gagp::evo::grammar {

inline constexpr std::string_view kCatalogVersion = "gagp-primitives-v2";

const std::array<RType, 8>& value_types();
std::string_view type_name(RType type);
RType parse_type(std::string_view name);

struct RegionBinding {
  std::string name;
  RType type;
};

struct RegionSlot {
  std::uint32_t argument;
  std::vector<RegionBinding> bindings;
};

struct PrimitiveSignature {
  std::uint32_t id = 0;
  std::string key;
  std::string operation;
  std::vector<RType> arguments;
  RType result = RType::Invalid;
  std::optional<NodeKind> lowering_node;
  std::vector<RegionSlot> regions;
  std::optional<TraversalDirection> traversal_direction;

  bool executable() const noexcept { return lowering_node.has_value(); }
};

struct ControlSlot {
  NodeCategory category = NodeCategory::Expression;
  // Invalid means a structural child without a runtime value, never a wildcard.
  RType value_type = RType::Invalid;
};

struct ControlSignature {
  std::uint32_t id = 0;
  std::string key;
  NodeKind lowering_node = NodeKind::PROGRAM;
  NodeCategory result = NodeCategory::Program;
  std::vector<ControlSlot> arguments;
  bool requires_name = false;
};

// Finite exact overloads, instantiated once during grammar construction.
// Numeric IDs are stable for this catalog version and sorted canonical keys.
class PrimitiveCatalog {
 public:
  static const PrimitiveCatalog& standard();
  const std::vector<PrimitiveSignature>& signatures() const noexcept { return signatures_; }
  const PrimitiveSignature& at(std::uint32_t id) const;
  const PrimitiveSignature& resolve(std::string_view key) const;
  const PrimitiveSignature& resolve(std::string_view operation,
      const std::vector<RType>& arguments, RType result) const;
  void require_executable(std::uint32_t id) const;
  const std::vector<ControlSignature>& control_signatures() const noexcept { return controls_; }
  const ControlSignature& resolve_control(std::string_view key) const;

 private:
  PrimitiveCatalog();
  std::vector<PrimitiveSignature> signatures_;
  std::vector<ControlSignature> controls_;
};

}  // namespace gagp::evo::grammar
