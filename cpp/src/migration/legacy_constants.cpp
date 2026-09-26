#include "gagp/migration/legacy_constants.hpp"

#include <stdexcept>

#include "gagp/evolution/transition/bounded_regions.hpp"
#include "gagp/migration/legacy_budget.hpp"

namespace gagp::migration {

LegacyConstantProjection lower_with_constant_origins(const legacy_v1::AstProgram& source,
    const std::vector<evo::InputSpec>& inputs) {
  const auto layout = predict_legacy_expansion_layout(source,inputs);
  LegacyConstantProjection result;
  result.genome = evo::transition::lower_bounded_regions(source,inputs);
  if (result.genome.ast.nodes.size() != layout.metrics.nodes)
    throw std::logic_error("constant origin mapping: lowering extent differs from predicted layout");
  result.source_by_target.assign(result.genome.ast.nodes.size(),kNoLegacyConstant);
  for (std::size_t i = 0; i < source.nodes.size(); ++i) {
    if (source.nodes[i].kind != legacy_v1::NodeKind::CONST) continue;
    const auto& span = layout.source_nodes.at(i);
    const auto& target = result.genome.ast.nodes.at(span.begin);
    if (span.end != span.begin + 1 || target.kind != evo::NodeKind::CONST ||
        target.i0 != source.nodes[i].i0 || result.source_by_target.at(span.begin) != kNoLegacyConstant)
      throw std::logic_error("constant origin mapping: source constant lost its unique lowered leaf");
    result.sites.push_back({i,span.begin});
    result.source_by_target[span.begin] = i;
  }
  return result;
}

}  // namespace gagp::migration
