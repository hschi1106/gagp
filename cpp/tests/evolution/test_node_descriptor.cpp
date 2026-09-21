#include <cassert>
#include <cstddef>
#include <set>
#include <stdexcept>
#include <string>

#include "gagp/core/builtin.hpp"
#include "gagp/evolution/node_descriptor.hpp"

int main() {
  using namespace gagp::evo;

  const auto& descriptors = all_node_descriptors();
  assert(descriptors.size() == k_node_kind_count);

  std::set<std::string> source_names;
  std::set<std::string> serialized_names;
  std::size_t builtin_count = 0;
  for (std::size_t i = 0; i < descriptors.size(); ++i) {
    const NodeDescriptor& descriptor = descriptors[i];
    assert(is_known_node_kind(static_cast<int>(descriptor.kind)));
    assert(&node_descriptor(descriptor.kind) == &descriptor);
    assert(!descriptor.source_name.empty());
    assert(!descriptor.serialized_name.empty());
    assert(source_names.insert(std::string(descriptor.source_name)).second);
    assert(serialized_names.insert(std::string(descriptor.serialized_name)).second);

    if (descriptor.is_builtin()) {
      ++builtin_count;
      assert(descriptor.category == NodeCategory::Expression);
      assert(descriptor.builtin_arity == descriptor.prefix_arity);
      gagp::BuiltinId id = gagp::BuiltinId::Abs;
      assert(gagp::builtin_id_from_int(descriptor.builtin_id, id));
      assert(std::string(gagp::builtin_name(id)).size() > 0U);
    } else {
      assert(descriptor.builtin_arity == -1);
    }
    const bool in_device_builtin_range =
        static_cast<int>(descriptor.kind) >= static_cast<int>(NodeKind::CALL_ABS) &&
        static_cast<int>(descriptor.kind) <= static_cast<int>(NodeKind::CALL_SINGLETON);
    assert(in_device_builtin_range == descriptor.is_builtin());
  }

  assert(builtin_count == 27U);
  assert(node_descriptor(NodeKind::PROGRAM).prefix_arity == 1);
  assert(node_descriptor(NodeKind::CALL_INDEX).builtin_id == static_cast<int>(gagp::BuiltinId::Index));
  assert(static_cast<int>(NodeKind::LET_REGION) == 71);
  assert(static_cast<int>(NodeKind::BOUNDED_REGION) == 77);
  for (int legacy_kind = 53; legacy_kind <= 70; ++legacy_kind) {
    assert(!is_known_node_kind(legacy_kind));
  }

  assert(!is_known_node_kind(-1));
  assert(!is_known_node_kind(static_cast<int>(NodeKind::COUNT)));
  bool rejected = false;
  try {
    (void)node_descriptor(NodeKind::COUNT);
  } catch (const std::out_of_range&) {
    rejected = true;
  }
  assert(rejected);

  return 0;
}
