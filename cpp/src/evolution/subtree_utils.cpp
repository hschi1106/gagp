#include "subtree_utils.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gagp/evolution/node_descriptor.hpp"

namespace gagp::evo::subtree {

namespace {

bool value_equal(const Value& a, const Value& b) {
  if (a.tag != b.tag) return false;
  if (a.tag == ValueTag::Invalid) return true;
  if (a.tag == ValueTag::Bool) return a.b == b.b;
  if (a.tag == ValueTag::Int || a.tag == ValueTag::Char || a.tag == ValueTag::FallbackToken) return a.i == b.i;
  if (a.tag == ValueTag::Float) return a.f == b.f;
  if (a.tag == ValueTag::String || a.tag == ValueTag::IntList || a.tag == ValueTag::FloatList ||
      a.tag == ValueTag::StringList) return a.i == b.i;
  return false;
}

std::size_t fill_subtree_end_at(const AstProgram& program, std::size_t idx, std::vector<std::size_t>& out) {
  if (idx >= program.nodes.size()) throw std::runtime_error("prefix traversal out of range");
  std::size_t cur = idx + 1;
  for (int i = 0; i < node_prefix_arity(program.nodes[idx]); ++i) {
    cur = fill_subtree_end_at(program, cur, out);
  }
  out[idx] = cur;
  return cur;
}

int find_or_add_name(AstProgram& target, const std::string& name, std::unordered_map<std::string, int>& name2idx) {
  auto it = name2idx.find(name);
  if (it != name2idx.end()) return it->second;
  const int idx = static_cast<int>(target.names.size());
  target.names.push_back(name);
  name2idx[name] = idx;
  return idx;
}

int find_or_add_const(AstProgram& target, const Value& value) {
  for (std::size_t i = 0; i < target.consts.size(); ++i) {
    if (value_equal(target.consts[i], value)) return static_cast<int>(i);
  }
  const int idx = static_cast<int>(target.consts.size());
  target.consts.push_back(value);
  return idx;
}

std::vector<AstNode> map_subtree_nodes_into(AstProgram& target,
                                            const AstProgram& donor,
                                            std::size_t start,
                                            std::size_t stop) {
  std::unordered_map<std::string, int> name2idx;
  for (std::size_t i = 0; i < target.names.size(); ++i) {
    name2idx[target.names[i]] = static_cast<int>(i);
  }
  std::unordered_map<int, int> name_map;
  std::unordered_map<int, int> const_map;
  std::vector<AstNode> out;
  out.reserve(stop - start);
  for (std::size_t i = start; i < stop; ++i) {
    AstNode node = donor.nodes[i];
    if (node.kind == NodeKind::CONST) {
      auto it = const_map.find(node.i0);
      if (it == const_map.end()) {
        const int mapped = find_or_add_const(target, donor.consts.at(static_cast<std::size_t>(node.i0)));
        const_map[node.i0] = mapped;
        node.i0 = mapped;
      } else {
        node.i0 = it->second;
      }
    } else if (node.kind == NodeKind::VAR || node.kind == NodeKind::BOUND_VAR ||
               node.kind == NodeKind::ASSIGN || node.kind == NodeKind::FOR_RANGE) {
      auto it = name_map.find(node.i0);
      if (it == name_map.end()) {
        const int mapped = find_or_add_name(target, donor.names.at(static_cast<std::size_t>(node.i0)), name2idx);
        name_map[node.i0] = mapped;
        node.i0 = mapped;
      } else {
        node.i0 = it->second;
      }
    }
    out.push_back(node);
  }
  return out;
}

int remap_name_from_donor(AstProgram& target, const AstProgram& donor, int donor_name_id) {
  std::unordered_map<std::string, int> name2idx;
  for (std::size_t i = 0; i < target.names.size(); ++i) {
    name2idx[target.names[i]] = static_cast<int>(i);
  }
  return find_or_add_name(target, donor.names.at(static_cast<std::size_t>(donor_name_id)), name2idx);
}

int remap_const_from_donor(AstProgram& target, const AstProgram& donor, int donor_const_id) {
  return find_or_add_const(target, donor.consts.at(static_cast<std::size_t>(donor_const_id)));
}

std::vector<int> remap_names_from_donor(AstProgram& target,
                                        const AstProgram& donor,
                                        const std::vector<int>& donor_name_ids) {
  std::vector<int> out;
  out.reserve(donor_name_ids.size());
  for (int donor_name_id : donor_name_ids) {
    out.push_back(remap_name_from_donor(target, donor, donor_name_id));
  }
  return out;
}

bool node_in_interval(std::size_t node_index, std::size_t start, std::size_t stop) {
  return node_index >= start && node_index < stop;
}

std::unordered_map<int, int> region_binder_renames(
    const AstProgram& base, const AstProgram& donor,
    std::size_t donor_start, std::size_t donor_stop) {
  std::set<int> introduced_ids;
  std::vector<int> introduced_order;
  const auto introduce = [&](int id) {
    if (introduced_ids.insert(id).second) introduced_order.push_back(id);
  };
  for (const auto& region : donor.lexical_regions) {
    if (!node_in_interval(region.node_index, donor_start, donor_stop)) continue;
    for (const auto& binding : region.bindings) introduce(binding.id);
  }
  for (const auto& region : donor.bounded_region_specs) {
    if (!node_in_interval(region.node_index, donor_start, donor_stop)) continue;
    for (const auto& phase : region.phases)
      for (const auto& binding : phase.bindings) introduce(binding.binder_id);
  }
  std::set<int> blocked_ids;
  for (const auto& region : base.lexical_regions)
    for (const auto& binding : region.bindings) blocked_ids.insert(binding.id);
  for (const auto& region : base.bounded_region_specs)
    for (const auto& phase : region.phases)
      for (const auto& binding : phase.bindings) blocked_ids.insert(binding.binder_id);
  for (std::size_t i = donor_start; i < donor_stop; ++i) {
    const auto& node = donor.nodes[i];
    if (node.kind == NodeKind::REGION_VAR && !introduced_ids.count(node.i0))
      blocked_ids.insert(node.i0);
  }
  for (const auto& region : donor.bounded_region_specs) {
    if (!node_in_interval(region.node_index, donor_start, donor_stop)) continue;
    for (const auto& capture : region.parameters)
      if (capture.kind == RegionCaptureKind::Lexical && !introduced_ids.count(capture.index))
        blocked_ids.insert(capture.index);
  }
  std::set<int> reserved_ids = blocked_ids;
  for (int id : introduced_ids)
    if (!blocked_ids.count(id)) reserved_ids.insert(id);
  std::unordered_map<int, int> renames;
  int next_candidate = 0;
  for (const int id : introduced_order) {
    int mapped = id;
    if (blocked_ids.count(mapped)) {
      while (next_candidate < std::numeric_limits<int>::max() && reserved_ids.count(next_candidate))
        ++next_candidate;
      if (next_candidate == std::numeric_limits<int>::max())
        throw std::overflow_error("no public lexical binder id remains for subtree splice");
      mapped = next_candidate;
    }
    renames.emplace(id, mapped);
    reserved_ids.insert(mapped);
  }
  return renames;
}


}  // namespace

int node_arity(NodeKind kind) {
  return node_descriptor(kind).prefix_arity;
}

std::vector<std::size_t> build_subtree_end(const AstProgram& program) {
  std::vector<std::size_t> out(program.nodes.size(), 0);
  if (!program.nodes.empty()) {
    const std::size_t end = fill_subtree_end_at(program, 0, out);
    if (end != program.nodes.size()) throw std::runtime_error("prefix trailing tokens");
  }
  return out;
}

AstProgram replace_subtree(const AstProgram& base,
                           std::size_t target_start,
                           std::size_t target_stop,
                           const AstProgram& donor,
                           std::size_t donor_start,
                           std::size_t donor_stop) {
  AstProgram out;
  out.version = k_ast_prefix_version_current;
  out.names = base.names;
  out.consts = base.consts;
  std::vector<AstNode> donor_nodes = map_subtree_nodes_into(out, donor, donor_start, donor_stop);
  const std::unordered_map<int, int> binder_renames =
      region_binder_renames(base, donor, donor_start, donor_stop);
  for (AstNode& node : donor_nodes) {
    if (node.kind != NodeKind::REGION_VAR) continue;
    const auto rename = binder_renames.find(node.i0);
    if (rename != binder_renames.end()) node.i0 = rename->second;
  }
  const std::size_t removed = target_stop - target_start;
  const std::size_t inserted = donor_nodes.size();
  for (const LexicalRegion& region : base.lexical_regions) {
    if (node_in_interval(region.node_index, target_start, target_stop)) continue;
    LexicalRegion shifted = region;
    if (shifted.node_index >= target_stop) {
      shifted.node_index = shifted.node_index - removed + inserted;
    }
    out.lexical_regions.push_back(std::move(shifted));
  }
  for (const LexicalRegion& region : donor.lexical_regions) {
    if (!node_in_interval(region.node_index, donor_start, donor_stop)) continue;
    LexicalRegion copied = region;
    copied.node_index = target_start + (region.node_index - donor_start);
    for (LexicalBinding& binding : copied.bindings) {
      binding.id = binder_renames.at(binding.id);
    }
    out.lexical_regions.push_back(std::move(copied));
  }
  for (const auto& spec : base.bounded_region_specs) {
    if (node_in_interval(spec.node_index, target_start, target_stop)) continue;
    auto shifted = spec;
    if (shifted.node_index >= target_stop) shifted.node_index = shifted.node_index - removed + inserted;
    out.bounded_region_specs.push_back(std::move(shifted));
  }
  for (const auto& spec : donor.bounded_region_specs) {
    if (!node_in_interval(spec.node_index, donor_start, donor_stop)) continue;
    auto copied = spec;
    copied.node_index = target_start + (spec.node_index - donor_start);
    for (auto& phase : copied.phases)
      for (auto& binding : phase.bindings) binding.binder_id = binder_renames.at(binding.binder_id);
    for (auto& capture : copied.parameters) {
      if (capture.kind == RegionCaptureKind::Name)
        capture.index = remap_name_from_donor(out, donor, capture.index);
      else if (const auto found = binder_renames.find(capture.index); found != binder_renames.end())
        capture.index = found->second;
    }
    out.bounded_region_specs.push_back(std::move(copied));
  }
  for (const TraversalSpec& spec : base.traversal_specs) {
    if (node_in_interval(spec.node_index, target_start, target_stop)) continue;
    TraversalSpec shifted = spec;
    if (shifted.node_index >= target_stop) {
      shifted.node_index = shifted.node_index - removed + inserted;
    }
    out.traversal_specs.push_back(shifted);
  }
  for (const TraversalSpec& spec : donor.traversal_specs) {
    if (!node_in_interval(spec.node_index, donor_start, donor_stop)) continue;
    TraversalSpec copied = spec;
    copied.node_index = target_start + (spec.node_index - donor_start);
    out.traversal_specs.push_back(copied);
  }
  for (const NodeFuelSpec& spec : base.fuel_specs) {
    if (node_in_interval(spec.node_index, target_start, target_stop)) continue;
    NodeFuelSpec shifted = spec;
    if (shifted.node_index >= target_stop)
      shifted.node_index = shifted.node_index - removed + inserted;
    out.fuel_specs.push_back(std::move(shifted));
  }
  for (const NodeFuelSpec& spec : donor.fuel_specs) {
    if (!node_in_interval(spec.node_index, donor_start, donor_stop)) continue;
    NodeFuelSpec copied = spec;
    copied.node_index = target_start + (spec.node_index - donor_start);
    out.fuel_specs.push_back(std::move(copied));
  }
  out.nodes.reserve(base.nodes.size() - (target_stop - target_start) + donor_nodes.size());
  out.nodes.insert(out.nodes.end(), base.nodes.begin(), base.nodes.begin() + static_cast<std::ptrdiff_t>(target_start));
  out.nodes.insert(out.nodes.end(), donor_nodes.begin(), donor_nodes.end());
  out.nodes.insert(out.nodes.end(), base.nodes.begin() + static_cast<std::ptrdiff_t>(target_stop), base.nodes.end());
  return out;
}

}  // namespace gagp::evo::subtree
