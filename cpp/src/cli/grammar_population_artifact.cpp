#include "gagp/cli/grammar_population_artifact.hpp"
#include "gagp/cli/commands.hpp"

#include <cmath>
#include <stdexcept>

namespace gagp::cli_detail {
using evo::grammar::canonical_json;
namespace {
using Json = JsonValue;
constexpr std::size_t kMaxArtifactBytes = 256u * 1024u * 1024u;
constexpr std::size_t kMaxPopulation = 65536;
void check_count(std::size_t count) {
  if (count == 0 || count > kMaxPopulation)
    throw std::invalid_argument("grammar population must contain 1..65536 members");
}
Json string(const std::string& value) {
  Json out; out.kind = Json::Kind::String; out.string_v = value; return out;
}
}  // namespace

std::string encode_generated_population_artifact(const evo::grammar::CompiledGrammar& grammar,
    const std::vector<evo::ProgramGenome>& population) {
  check_count(population.size());
  Json root; root.kind = Json::Kind::Object;
  root.object_v["format_version"] = string(kGeneratedGrammarPopulationArtifactVersion);
  root.object_v["grammar_hash"] = string(grammar.content_hash());
  auto& count = root.object_v["count"];
  count.kind = Json::Kind::Number; count.number_v = population.size();
  auto& members = root.object_v["members"];
  members.kind = Json::Kind::Array;
  // Check incremental serialized size before retaining another parsed member.
  std::size_t bytes = canonical_json(root).size();
  for (const auto& genome : population) {
    if (!genome.derivation)
      throw std::invalid_argument("grammar population member lacks immutable derivation metadata");
    const auto member = encode_generated_artifact(grammar, {genome, *genome.derivation});
    const auto added_bytes = member.size() + (members.array_v.empty() ? 0 : 1);
    if (added_bytes > kMaxArtifactBytes - bytes)
      throw std::invalid_argument("grammar population artifact exceeds 256 MiB");
    bytes += added_bytes;
    // This also rejects edited ASTs that retained stale origin metadata.
    replay_generated_artifact(member, &grammar);
    members.array_v.push_back(JsonParser(member, {true, 512}).parse());
  }
  const auto artifact = canonical_json(root);
  if (artifact.size() > kMaxArtifactBytes)
    throw std::invalid_argument("grammar population artifact exceeds 256 MiB");
  return artifact;
}

std::vector<evo::ProgramGenome> replay_generated_population_artifact(const std::string& artifact,
    const evo::grammar::CompiledGrammar* required_grammar) {
  if (artifact.size() > kMaxArtifactBytes)
    throw std::invalid_argument("grammar population artifact exceeds 256 MiB");
  Json root;
  try { root = JsonParser(artifact, {true, 512}).parse(); }
  catch (const std::runtime_error& error) {
    throw std::invalid_argument(std::string("invalid grammar population JSON: ") + error.what());
  }
  if (require_string(require_object_field(root, "format_version"), "format_version") !=
      kGeneratedGrammarPopulationArtifactVersion)
    throw std::invalid_argument("unsupported grammar population artifact version");
  if (root.object_v.size() != 4)
    throw std::invalid_argument("grammar population artifact contains unknown root fields");
  const auto& hash = require_string(require_object_field(root, "grammar_hash"), "grammar_hash");
  if (required_grammar && required_grammar->content_hash() != hash)
    throw std::invalid_argument("grammar population required grammar identity mismatch; restore the recorded grammar and imports, or replay without a required grammar to use the embedded snapshot");
  const auto& members = require_object_field(root, "members");
  if (members.kind != Json::Kind::Array)
    throw std::invalid_argument("grammar population members must be an array");
  check_count(members.array_v.size());
  const auto& count = require_object_field(root, "count");
  if (count.kind != Json::Kind::Number || !std::isfinite(count.number_v) ||
      count.number_v != static_cast<double>(members.array_v.size()))
    throw std::invalid_argument("grammar population member count mismatch");
  std::vector<evo::ProgramGenome> population;
  population.reserve(members.array_v.size());
  for (const auto& member : members.array_v) {
    if (require_string(require_object_field(member, "grammar_hash"), "grammar_hash") != hash)
      throw std::invalid_argument("grammar population member grammar identity mismatch");
    population.push_back(replay_generated_artifact(canonical_json(member), required_grammar).genome);
  }
  return population;
}

}  // namespace gagp::cli_detail
