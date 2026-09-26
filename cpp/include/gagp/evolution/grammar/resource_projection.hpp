#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace gagp::cli_detail { struct JsonValue; }

namespace gagp::evo::grammar {

// Charges describe construction resources, independently of runtime fuel and
// physical allocation. Zero charges can represent administrative expansion.
struct ResourceCharge {
  std::uint64_t nodes = 1;
  std::uint64_t depth = 1;
  bool resets_depth = false;
};

// Exact authored charge object; shared by source validation and compilation.
ResourceCharge parse_resource_charge(const cli_detail::JsonValue& value);

struct ProjectedResources {
  std::uint64_t nodes = 0;
  // A subtree entered at depth d peaks at max(d + carried_depth, reset_depth).
  // The incoming ancestor itself is included, even if the subtree resets depth.
  std::uint64_t carried_depth = 0;
  std::uint64_t reset_depth = 0;
  std::uint64_t peak(std::uint64_t incoming = 0) const;
};

struct ProjectedBudget {
  std::uint64_t max_nodes = 0;
  std::uint64_t max_depth = 0;
  bool accepts(const ProjectedResources& resources) const {
    return resources.nodes <= max_nodes && resources.carried_depth <= max_depth &&
        resources.reset_depth <= max_depth;
  }
};

struct ProjectedAllowance {
  bool surrounding_fits = false;
  std::uint64_t max_nodes = 0;
  std::uint64_t max_carried_depth = 0;
  std::uint64_t max_reset_depth = 0;
  bool accepts(const ProjectedResources& donor) const;
};

namespace resource_detail {
// Internal fast path. The caller has proved nonempty/disjoint occurrences and
// that the untouched nodes and incoming depths fit. No unsigned underflow in
// the final node allowance is possible under those preconditions.
inline ProjectedAllowance allowance_after_validation(std::uint64_t total_nodes,
    std::uint64_t removed_nodes, std::size_t occurrences,
    std::uint64_t maximum_incoming_depth, std::uint64_t node_limit, std::uint64_t depth_limit) {
  return {true, (node_limit - total_nodes + removed_nodes) / occurrences,
      depth_limit - maximum_incoming_depth, depth_limit};
}
}  // namespace resource_detail

// Shared arithmetic for an indexed projection or a verified unit-charge scan.
// outside_depth may be a proved upper bound for all untouched node depths.
inline ProjectedAllowance projected_replacement_allowance(
    std::uint64_t total_nodes, std::uint64_t removed_nodes,
    std::size_t occurrences, std::uint64_t maximum_incoming_depth,
    std::uint64_t outside_depth, std::uint64_t node_limit, std::uint64_t depth_limit) {
  if (!occurrences || removed_nodes > total_nodes)
    throw std::invalid_argument("invalid projected replacement resource totals");
  const auto outside_nodes = total_nodes - removed_nodes;
  if (outside_nodes > node_limit || outside_depth > depth_limit || maximum_incoming_depth > depth_limit)
    return {};
  return resource_detail::allowance_after_validation(total_nodes, removed_nodes,
      occurrences, maximum_incoming_depth, node_limit, depth_limit);
}

// An independently validated prefix-tree resource index. No grammar membership,
// source correspondence, replacement eligibility or physical capacity is implied
// by a caller-supplied charge vector. Those certificates remain separate.
class ResourceProjection {
 public:
  // Empty charges mean unit physical node/depth charges with no reset.
  ResourceProjection(std::vector<std::size_t> subtree_ends,
      const std::vector<ResourceCharge>& charges = {});
  const ProjectedResources& subtree(std::size_t root = 0) const;
  std::uint64_t incoming_depth(std::size_t root) const;
  // Replace sorted, disjoint complete subtrees with copies of the same donor.
  // Node allowance is per occurrence; all incoming depths and untouched nodes
  // participate. This also handles repair of an initially over-budget parent.
  ProjectedAllowance replacement(const std::vector<std::size_t>& roots,
      std::uint64_t node_limit, std::uint64_t depth_limit) const;

 private:
  std::uint64_t maximum_depth(std::size_t begin, std::size_t end) const;
  std::vector<std::size_t> ends_;
  std::vector<ProjectedResources> resources_;
  std::vector<std::uint64_t> incoming_;
  std::vector<std::uint64_t> maxima_;
  std::size_t leaf_base_ = 1;
};

}  // namespace gagp::evo::grammar
