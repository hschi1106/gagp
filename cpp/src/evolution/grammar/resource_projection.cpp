#include "gagp/evolution/grammar/resource_projection.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "gagp/cli/json.hpp"

namespace gagp::evo::grammar {
ResourceCharge parse_resource_charge(const cli_detail::JsonValue& value) {
  using Kind = cli_detail::JsonValue::Kind;
  if (value.kind != Kind::Object || value.object_v.size() != 3 ||
      !value.object_v.count("nodes") || !value.object_v.count("depth") || !value.object_v.count("resets_depth"))
    throw std::invalid_argument("resource_charge requires exactly nodes, depth and resets_depth");
  const auto number = [&](const char* name, double maximum) {
    const auto& field = value.object_v.at(name);
    if (field.kind != Kind::Number || !std::isfinite(field.number_v) || field.number_v < 0 ||
        field.number_v > maximum || std::floor(field.number_v) != field.number_v)
      throw std::invalid_argument(std::string("resource_charge ") + name + " must be a bounded nonnegative integer");
    return static_cast<std::uint64_t>(field.number_v);
  };
  const auto& reset = value.object_v.at("resets_depth");
  if (reset.kind != Kind::Bool) throw std::invalid_argument("resource_charge resets_depth must be Boolean");
  return {number("nodes",65536),number("depth",256),reset.bool_v};
}

namespace {
std::uint64_t add(std::uint64_t left, std::uint64_t right) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left)
    throw std::overflow_error("projected resource overflow");
  return left + right;
}
}  // namespace

std::uint64_t ProjectedResources::peak(std::uint64_t incoming) const {
  return std::max(add(incoming, carried_depth), reset_depth);
}

bool ProjectedAllowance::accepts(const ProjectedResources& donor) const {
  return surrounding_fits && donor.nodes <= max_nodes &&
      donor.carried_depth <= max_carried_depth && donor.reset_depth <= max_reset_depth;
}

ResourceProjection::ResourceProjection(std::vector<std::size_t> subtree_ends,
    const std::vector<ResourceCharge>& charges) : ends_(std::move(subtree_ends)) {
  const auto size = ends_.size();
  if (!size || ends_.front() != size || (!charges.empty() && charges.size() != size))
    throw std::invalid_argument("resource projection requires one complete tree and aligned charges");
  const auto charge = [&](std::size_t i) { return charges.empty() ? ResourceCharge{} : charges[i]; };
  resources_.resize(size);
  incoming_.resize(size);
  std::vector<std::uint64_t> depths(size);
  std::vector<std::size_t> parents(size, size), ancestors;
  for (std::size_t i = 0; i < size; ++i) {
    while (!ancestors.empty() && ends_[ancestors.back()] <= i) ancestors.pop_back();
    if (ends_[i] <= i || ends_[i] > size || (i && ancestors.empty()) ||
        (!ancestors.empty() && ends_[i] > ends_[ancestors.back()]))
      throw std::invalid_argument("resource projection has invalid prefix subtree spans");
    if (!ancestors.empty()) {
      parents[i] = ancestors.back();
      incoming_[i] = depths[parents[i]];
    }
    const auto own = charge(i);
    depths[i] = add(own.resets_depth ? 0 : incoming_[i], own.depth);
    resources_[i] = {own.nodes, own.resets_depth ? 0 : own.depth,
        own.resets_depth ? own.depth : 0};
    ancestors.push_back(i);
  }
  for (std::size_t i = size; i-- > 1;) {
    const auto parent = parents[i];
    const auto own = charge(parent);
    auto& target = resources_[parent];
    const auto& child = resources_[i];
    target.nodes = add(target.nodes, child.nodes);
    if (own.resets_depth) target.reset_depth = std::max(target.reset_depth, child.peak(own.depth));
    else {
      target.carried_depth = std::max(target.carried_depth, add(own.depth, child.carried_depth));
      target.reset_depth = std::max(target.reset_depth, child.reset_depth);
    }
  }
  while (leaf_base_ < size) {
    if (leaf_base_ > std::numeric_limits<std::size_t>::max() / 4)
      throw std::overflow_error("resource projection index capacity exceeded");
    leaf_base_ *= 2;
  }
  maxima_.assign(leaf_base_ * 2, 0);
  std::copy(depths.begin(), depths.end(), maxima_.begin() + leaf_base_);
  for (std::size_t i = leaf_base_; --i > 0;)
    maxima_[i] = std::max(maxima_[i * 2], maxima_[i * 2 + 1]);
}

const ProjectedResources& ResourceProjection::subtree(std::size_t root) const {
  return resources_.at(root);
}

std::uint64_t ResourceProjection::incoming_depth(std::size_t root) const {
  return incoming_.at(root);
}

std::uint64_t ResourceProjection::maximum_depth(std::size_t begin, std::size_t end) const {
  std::uint64_t result = 0;
  for (begin += leaf_base_, end += leaf_base_; begin < end; begin /= 2, end /= 2) {
    if (begin & 1) result = std::max(result, maxima_[begin++]);
    if (end & 1) result = std::max(result, maxima_[--end]);
  }
  return result;
}

ProjectedAllowance ResourceProjection::replacement(const std::vector<std::size_t>& roots,
    std::uint64_t node_limit, std::uint64_t depth_limit) const {
  if (roots.empty()) throw std::invalid_argument("resource replacement requires at least one occurrence");
  std::size_t previous_end = 0;
  std::uint64_t removed = 0, outside_depth = 0, incoming = 0;
  for (const auto root : roots) {
    if (root >= ends_.size() || root < previous_end)
      throw std::invalid_argument("resource replacement occurrences overlap or are not sorted");
    outside_depth = std::max(outside_depth, maximum_depth(previous_end, root));
    removed = add(removed, resources_[root].nodes);
    incoming = std::max(incoming, incoming_[root]);
    previous_end = ends_[root];
  }
  outside_depth = std::max(outside_depth, maximum_depth(previous_end, ends_.size()));
  return projected_replacement_allowance(resources_.front().nodes, removed,
      roots.size(), incoming, outside_depth, node_limit, depth_limit);
}

}  // namespace gagp::evo::grammar
