#include "gagp/evolution/repro/pack.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

#include "gagp/evolution/node_descriptor.hpp"

namespace gagp::evo::repro {

ProgramGenome compact_genome_tables(ProgramGenome out) {
  std::vector<bool> used_names(out.ast.names.size(), false);
  std::vector<bool> used_constants(out.ast.consts.size(), false);
  const auto mark = [](int index, std::vector<bool>* used, const char* table) {
    if (index < 0 || static_cast<std::size_t>(index) >= used->size())
      throw std::invalid_argument(std::string("cannot compact genome with invalid ") +
                                  table + " index");
    (*used)[static_cast<std::size_t>(index)] = true;
  };
  for (const auto& node : out.ast.nodes) {
    const auto& descriptor = node_descriptor(node.kind);
    if (descriptor.i0_role == NodeIndexRole::Name)
      mark(node.i0, &used_names, "name");
    else if (descriptor.i0_role == NodeIndexRole::Constant)
      mark(node.i0, &used_constants, "constant");
    if (descriptor.i1_role == NodeIndexRole::Name)
      mark(node.i1, &used_names, "name");
    else if (descriptor.i1_role == NodeIndexRole::Constant)
      mark(node.i1, &used_constants, "constant");
  }
  for (const auto& region : out.ast.bounded_region_specs)
    for (const auto& capture : region.parameters)
      if (capture.kind == RegionCaptureKind::Name)
        mark(capture.index, &used_names, "bounded-region capture name");

  // All indices were checked above, including bounded-region captures. When
  // every entry is live, stable compaction is the identity mapping.
  if (std::all_of(used_names.begin(), used_names.end(), [](bool used) { return used; }) &&
      std::all_of(used_constants.begin(), used_constants.end(), [](bool used) { return used; })) {
    out.meta = build_genome_meta(out.ast);
    return out;
  }

  const auto compact = [](const auto& source, const std::vector<bool>& used,
                          std::vector<int>* remap) {
    using Vector = std::decay_t<decltype(source)>;
    Vector result;
    result.reserve(source.size());
    remap->assign(source.size(), -1);
    for (std::size_t i = 0; i < source.size(); ++i) {
      if (!used[i]) continue;
      (*remap)[i] = static_cast<int>(result.size());
      result.push_back(source[i]);
    }
    return result;
  };
  std::vector<int> name_remap;
  std::vector<int> constant_remap;
  auto names = compact(out.ast.names, used_names, &name_remap);
  auto constants = compact(out.ast.consts, used_constants, &constant_remap);
  for (auto& node : out.ast.nodes) {
    const auto& descriptor = node_descriptor(node.kind);
    if (descriptor.i0_role == NodeIndexRole::Name) node.i0 = name_remap[node.i0];
    else if (descriptor.i0_role == NodeIndexRole::Constant)
      node.i0 = constant_remap[node.i0];
    if (descriptor.i1_role == NodeIndexRole::Name) node.i1 = name_remap[node.i1];
    else if (descriptor.i1_role == NodeIndexRole::Constant)
      node.i1 = constant_remap[node.i1];
  }
  for (auto& region : out.ast.bounded_region_specs)
    for (auto& capture : region.parameters)
      if (capture.kind == RegionCaptureKind::Name)
        capture.index = name_remap[capture.index];
  out.ast.names = std::move(names);
  out.ast.consts = std::move(constants);
  out.meta = build_genome_meta(out.ast);
  return out;
}

}  // namespace gagp::evo::repro
