#include "constant_prep.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "gagp/evolution/grammar/values.hpp"
#include "gagp/evolution/repro/types.hpp"

namespace gagp::evo::repro {
namespace {

constexpr std::size_t kMaxPreparedItems = 1'000'000;
constexpr std::size_t kMaxPreparedBytes = 256ULL * 1024 * 1024;

static_assert(std::is_trivially_copyable<ConstantMutationDomain>::value,
              "constant mutation domain rows must be trivially copyable");
static_assert(std::is_trivially_copyable<ConstantMutationGroup>::value,
              "constant mutation group rows must be trivially copyable");
static_assert(std::is_trivially_copyable<ConstantMutationStream>::value,
              "constant mutation stream rows must be trivially copyable");

int as_int(std::size_t value, const char* field) {
  if (value > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    throw std::invalid_argument(std::string(field) +
                                " exceeds signed integer capacity");
  return static_cast<int>(value);
}

void require_count(std::size_t value, const char* field) {
  if (value > kMaxPreparedItems)
    throw std::invalid_argument(std::string(field) +
                                " exceeds constant preparation item limit");
}

void add_bytes(std::size_t count, std::size_t width, std::size_t* total) {
  if (count > kMaxPreparedBytes / width ||
      *total > kMaxPreparedBytes - count * width)
    throw std::invalid_argument(
        "constant preparation tables exceed 256 MiB");
  *total += count * width;
}

std::size_t checked_table_bytes(const ConstantMutationTable& table) {
  const std::size_t domain_count = table.grammar_domains
      ? table.grammar_domains->domains.size() : 0;
  const std::size_t value_count = table.grammar_domains
      ? table.grammar_domains->values.size() : 0;
  const std::size_t expression_count = table.grammar_domains
      ? table.grammar_domains->expression_domains.size() : 0;
  require_count(domain_count, "constant domain count");
  require_count(value_count, "constant value count");
  require_count(expression_count, "constant expression count");
  require_count(table.groups.size(), "constant group count");
  require_count(table.group_nodes.size(), "constant group node count");
  require_count(table.node_group_origins.size(), "constant node origin count");
  require_count(table.metadata_roots.size(), "constant metadata root count");
  require_count(table.streams.size(), "constant stream count");

  std::size_t bytes = 0;
  add_bytes(domain_count, sizeof(ConstantMutationDomain), &bytes);
  add_bytes(value_count, sizeof(Value), &bytes);
  add_bytes(expression_count, sizeof(int), &bytes);
  add_bytes(table.groups.size(), sizeof(ConstantMutationGroup), &bytes);
  add_bytes(table.group_nodes.size(), sizeof(int), &bytes);
  add_bytes(table.node_group_origins.size(), sizeof(int), &bytes);
  add_bytes(table.metadata_roots.size(), sizeof(int), &bytes);
  add_bytes(table.streams.size(), sizeof(ConstantMutationStream), &bytes);
  return bytes;
}

void require_append_bounds(const ConstantMutationTable& table,
                           std::size_t groups, std::size_t group_nodes,
                           std::size_t node_origins,
                           std::size_t metadata_roots) {
  if (groups > kMaxPreparedItems - table.groups.size())
    throw std::invalid_argument(
        "constant group count exceeds constant preparation item limit");
  if (group_nodes > kMaxPreparedItems - table.group_nodes.size())
    throw std::invalid_argument(
        "constant group node count exceeds constant preparation item limit");
  if (node_origins > kMaxPreparedItems - table.node_group_origins.size())
    throw std::invalid_argument(
        "constant node origin count exceeds constant preparation item limit");
  if (metadata_roots > kMaxPreparedItems - table.metadata_roots.size())
    throw std::invalid_argument(
        "constant metadata root count exceeds constant preparation item limit");
  if (table.streams.size() == kMaxPreparedItems)
    throw std::invalid_argument(
        "constant stream count exceeds constant preparation item limit");

  std::size_t bytes = 0;
  const auto* domains = table.grammar_domains.get();
  add_bytes(domains ? domains->domains.size() : 0,
            sizeof(ConstantMutationDomain), &bytes);
  add_bytes(domains ? domains->values.size() : 0, sizeof(Value), &bytes);
  add_bytes(domains ? domains->expression_domains.size() : 0,
            sizeof(int), &bytes);
  add_bytes(table.groups.size() + groups, sizeof(ConstantMutationGroup), &bytes);
  add_bytes(table.group_nodes.size() + group_nodes, sizeof(int), &bytes);
  add_bytes(table.node_group_origins.size() + node_origins, sizeof(int), &bytes);
  add_bytes(table.metadata_roots.size() + metadata_roots, sizeof(int), &bytes);
  add_bytes(table.streams.size() + 1, sizeof(ConstantMutationStream), &bytes);
}

template <class T>
void reserve_growth(std::vector<T>& values, std::size_t required) {
  if (required <= values.capacity()) return;
  // Exact-size reserve on every stream would copy all earlier streams again.
  const auto grown = std::min(kMaxPreparedItems,
      std::max(required, values.capacity() + values.capacity() / 2 + 16));
  values.reserve(grown);
}

struct PendingGroup {
  int domain = -1;
  std::vector<int> nodes;
};

}  // namespace

std::shared_ptr<const ConstantMutationDomains> prepare_constant_mutation_domains(
    const std::shared_ptr<const grammar::CompiledGrammar>& grammar_owner) {
  if (!grammar_owner)
    throw std::invalid_argument(
        "constant mutation domains require a compiled grammar owner");
  const auto& compiled = *grammar_owner;
  auto out = std::make_shared<ConstantMutationDomains>();
  out->grammar_owner = grammar_owner;
  require_count(compiled.constants().size(), "constant domain count");
  require_count(compiled.expressions().size(), "constant expression count");
  out->domains.reserve(compiled.constants().size());
  out->expression_domains.assign(compiled.expressions().size(), -1);

  for (const auto& source : compiled.constants()) {
    if (source.integer_range) {
      if (source.type != RType::Int || source.minimum > source.maximum)
        throw std::invalid_argument("compiled constant range is invalid");
    } else if (source.values.empty()) {
      throw std::invalid_argument("compiled finite constant domain is empty");
    }
    if (source.values.size() > kMaxPreparedItems - out->values.size())
      throw std::invalid_argument(
          "constant value count exceeds constant preparation item limit");

    ConstantMutationDomain domain;
    domain.type = static_cast<int>(source.type);
    domain.value_offset = as_int(out->values.size(), "constant value offset");
    domain.value_count = as_int(source.values.size(), "constant value count");
    domain.integer_range = source.integer_range ? 1 : 0;
    domain.minimum = source.minimum;
    domain.maximum = source.maximum;
    for (const auto& value : source.values)
      out->values.push_back(grammar::materialize_constant(source.type, value));
    out->domains.push_back(domain);
  }

  for (std::size_t i = 0; i < compiled.expressions().size(); ++i) {
    const auto& expression = compiled.expressions()[i];
    if (expression.kind != grammar::ExpressionKind::Constant) continue;
    if (expression.target >= out->domains.size())
      throw std::invalid_argument(
          "compiled constant expression has invalid domain ID");
    out->expression_domains[i] = as_int(expression.target, "constant domain ID");
  }
  ConstantMutationTable table;
  table.grammar_domains = out;
  (void)checked_table_bytes(table);
  return out;
}

std::size_t constant_mutation_table_bytes(
    const ConstantMutationTable& table) {
  return checked_table_bytes(table);
}

void append_constant_mutation_stream(ConstantMutationTable& table,
                                     const AstProgram& ast,
                                     const grammar::DerivationMetadata& witness) {
  const std::size_t node_count = ast.nodes.size();
  (void)checked_table_bytes(table);
  require_count(node_count, "constant stream node count");
  if (witness.nodes.size() != node_count)
    throw std::invalid_argument(
        "constant mutation stream requires an exact witness node length");

  std::vector<int> metadata_roots;
  metadata_roots.reserve(static_cast<std::size_t>(kGpuReproMaxConsts));
  const auto add_metadata_root = [&](int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= ast.consts.size())
      throw std::invalid_argument(
          "constant mutation stream has an invalid metadata constant index");
    if (std::find(metadata_roots.begin(), metadata_roots.end(), index) !=
        metadata_roots.end())
      return;
    if (metadata_roots.size() ==
        static_cast<std::size_t>(kGpuReproMaxConsts))
      throw std::invalid_argument(
          "constant mutation stream exceeds metadata constant root limit");
    metadata_roots.push_back(index);
  };
  for (const auto& spec : ast.asgp_dp1d_specs)
    add_metadata_root(spec.boundary_const);
  for (const auto& spec : ast.asgp_dp2d_specs)
    add_metadata_root(spec.boundary_const);

  std::map<std::uint32_t, PendingGroup> pending;
  for (std::size_t i = 0; i < node_count; ++i) {
    const auto& node = ast.nodes[i];
    if (node.kind == NodeKind::CONST &&
        (node.i0 < 0 || static_cast<std::size_t>(node.i0) >= ast.consts.size()))
      throw std::invalid_argument(
          "constant mutation stream has an invalid AST constant index");

    const auto& origin = witness.nodes[i];
    if (origin.fixed || origin.expression == grammar::kNoGrammarId ||
        node.kind != NodeKind::CONST)
      continue;
    if (!table.grammar_domains ||
        origin.expression >= table.grammar_domains->expression_domains.size())
      throw std::invalid_argument(
          "constant mutation witness has an invalid expression ID");
    const int domain =
        table.grammar_domains->expression_domains[origin.expression];
    if (domain < 0) continue;
    if (static_cast<std::size_t>(domain) >=
        table.grammar_domains->domains.size())
      throw std::invalid_argument(
          "constant mutation witness has an invalid domain ID");

    auto& group = pending[origin.logical_instance];
    if (group.domain < 0) group.domain = domain;
    if (group.domain != domain)
      throw std::invalid_argument(
          "constant mutation logical group does not have a uniform domain");
    group.nodes.push_back(as_int(i, "constant mutation node index"));
  }

  std::size_t added_group_nodes = 0;
  for (const auto& entry : pending) {
    if (entry.second.nodes.size() > kMaxPreparedItems - added_group_nodes)
      throw std::invalid_argument(
          "constant group node count exceeds constant preparation item limit");
    added_group_nodes += entry.second.nodes.size();
  }
  require_append_bounds(table, pending.size(), added_group_nodes, node_count,
                        metadata_roots.size());

  const int group_offset = as_int(table.groups.size(),
                                  "constant stream group offset");
  const int origin_offset = as_int(table.node_group_origins.size(),
                                   "constant stream node origin offset");
  const int metadata_root_offset = as_int(
      table.metadata_roots.size(), "constant stream metadata root offset");
  reserve_growth(table.groups, table.groups.size() + pending.size());
  reserve_growth(table.group_nodes, table.group_nodes.size() + added_group_nodes);
  reserve_growth(table.node_group_origins,
      table.node_group_origins.size() + node_count);
  reserve_growth(table.metadata_roots,
                 table.metadata_roots.size() + metadata_roots.size());
  reserve_growth(table.streams, table.streams.size() + 1);
  table.node_group_origins.insert(
      table.node_group_origins.end(), node_count,
      kNoConstantMutationGroup);
  table.metadata_roots.insert(table.metadata_roots.end(),
                              metadata_roots.begin(), metadata_roots.end());

  for (const auto& entry : pending) {
    const int group_id = as_int(table.groups.size(),
                                "constant mutation group ID");
    const int node_offset = as_int(table.group_nodes.size(),
                                   "constant group node offset");
    for (const int node : entry.second.nodes) {
      table.group_nodes.push_back(node);
      table.node_group_origins[
          static_cast<std::size_t>(origin_offset + node)] = group_id;
    }
    table.groups.push_back(ConstantMutationGroup{
        entry.first, entry.second.domain, node_offset,
        as_int(entry.second.nodes.size(), "constant group node count")});
  }
  table.streams.push_back(ConstantMutationStream{
      origin_offset, as_int(node_count, "constant stream node count"),
      group_offset, as_int(pending.size(), "constant stream group count"),
      metadata_root_offset,
      as_int(metadata_roots.size(), "constant stream metadata root count")});
}

void append_constant_mutation_stream(ConstantMutationTable& table,
                                     const AstProgram& ast,
                                     const VerifiedAst& verified,
                                     const grammar::DerivationMetadata& witness) {
  if (verified.subtree_end.size() != ast.nodes.size() ||
      verified.expression_types.size() != ast.nodes.size())
    throw std::invalid_argument(
        "constant mutation stream requires an exact verified AST node length");
  append_constant_mutation_stream(table, ast, witness);
}

}  // namespace gagp::evo::repro
