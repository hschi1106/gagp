#include "gagp/evolution/grammar/cache.hpp"

#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/identity.hpp"
#include "gagp/evolution/grammar/values.hpp"
#include "gagp/evolution/node_descriptor.hpp"

#include <limits>
#include <stdexcept>
#include <string_view>

namespace gagp::evo::grammar {
namespace {
constexpr std::size_t kMaxIdentityBytes = 256 * 1024 * 1024;

void append_field(std::string& output, std::string_view value) {
  const auto prefix = std::to_string(value.size()) + ":";
  if (prefix.size() > kMaxIdentityBytes - output.size() ||
      value.size() > kMaxIdentityBytes - output.size() - prefix.size())
    throw std::invalid_argument("runtime cache identity exceeds byte limit");
  output += prefix;
  output.append(value.data(), value.size());
}
}  // namespace

std::string runtime_cache_identity(const ProgramGenome& genome,
                                   const std::vector<std::string>& input_names,
                                   std::uint32_t fuel) {
  if (fuel == 0 || fuel > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
    throw std::invalid_argument("runtime cache identity requires positive signed-int fuel");
  const auto& ast = genome.ast;
  if (!ast.linear_rec_binders.empty() || !ast.asgp_dc_binders.empty() ||
      !ast.asgp_dp1d_specs.empty() || !ast.asgp_dp2d_specs.empty())
    throw std::invalid_argument("runtime cache identity rejects specialized AST metadata");
  for (const auto& node : ast.nodes) {
    if (!is_known_node_kind(static_cast<int>(node.kind)))
      throw std::invalid_argument("runtime cache identity rejects unknown AST nodes");
    const auto& descriptor = node_descriptor(node.kind);
    if (descriptor.metadata != NodeMetadataKind::None || descriptor.category == NodeCategory::DependencyMarker)
      throw std::invalid_argument("runtime cache identity rejects specialized AST segments");
  }

  // The legacy AST serializer encodes registry tokens for sequence constants.
  // Serialize structure separately, then encode the entire decoded constant pool.
  AstProgram structure;
  structure.nodes = ast.nodes;
  structure.names = ast.names;
  structure.version = ast.version;
  std::string material;
  append_field(material, "generated-runtime-cache-v1");
  append_field(material, kGrammarSemanticVersion);
  append_field(material, ast_cache_key(structure));
  append_field(material, std::to_string(ast.consts.size()));
  for (const auto& value : ast.consts)
    append_field(material, canonical_json(encode_constant(value)));
  append_field(material, std::to_string(input_names.size()));
  for (const auto& name : input_names) append_field(material, name);
  append_field(material, std::to_string(fuel));
  return content_sha256(material);
}

}  // namespace gagp::evo::grammar
