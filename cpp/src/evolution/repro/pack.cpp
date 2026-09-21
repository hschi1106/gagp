#include "gagp/evolution/repro/pack.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "constant_prep.hpp"
#include "gagp/evolution/grammar/cache.hpp"
#include "gagp/evolution/node_descriptor.hpp"

namespace gagp::evo::repro {
namespace {

std::uint64_t hash_name(const std::string& value) {
  std::uint64_t hash = UINT64_C(1469598103934665603);
  for (unsigned char c : value) { hash ^= c; hash *= UINT64_C(1099511628211); }
  return hash;
}

void register_name(PackedHostData* packed, const std::string& name) {
  const auto id = hash_name(name);
  const auto [found, inserted] = packed->name_lookup.emplace(id, name);
  if (!inserted && found->second != name)
    throw std::invalid_argument("GPU reproduction name hash collision");
}

bool same_value(const Value& a, const Value& b) {
  if (a.tag != b.tag) return false;
  if (a.tag == ValueTag::Float) return std::memcmp(&a.f, &b.f, sizeof(double)) == 0;
  if (a.tag == ValueTag::Bool) return a.b == b.b;
  return a.tag == ValueTag::Invalid || a.i == b.i;
}

std::uint64_t splice_source_bytes(const AstProgram& ast) {
  constexpr std::uint64_t limit = UINT64_C(256) * 1024 * 1024;
  std::uint64_t bytes = sizeof(AstProgram);
  const auto add = [&](std::size_t count, std::size_t width) {
    if (width && (count > limit / width || bytes > limit - count * width))
      throw std::invalid_argument("compiled source metadata exceeds 256 MiB");
    bytes += count * width;
  };
  const auto vector = [&](const auto& values) {
    add(values.size(), sizeof(typename std::decay_t<decltype(values)>::value_type));
  };
  add(ast.version.size() + 1, 1);
  vector(ast.nodes); vector(ast.names); vector(ast.consts);
  for (const auto& name : ast.names) add(name.size() + 1, 1);
  vector(ast.lexical_regions);
  for (const auto& region : ast.lexical_regions) vector(region.bindings);
  vector(ast.traversal_specs); vector(ast.fuel_specs);
  for (const auto& fuel : ast.fuel_specs) vector(fuel.charges);
  vector(ast.bounded_region_specs);
  for (const auto& region : ast.bounded_region_specs) {
    vector(region.parameters); vector(region.phases);
    for (const auto& phase : region.phases) vector(phase.bindings);
    const auto& plan = region.plan;
    vector(plan.state_types); vector(plan.parameter_types); vector(plan.preparations);
    vector(plan.request_expression_types); vector(plan.requests);
    for (const auto& request : plan.requests) vector(request.states);
    vector(plan.coordinate_slots); vector(plan.coordinate_rank); vector(plan.coordinate_domains);
  }
  return bytes;
}

GpuReproConfig compiled_pack_config(const std::vector<ProgramGenome>& population,
                                    const PreprocessOutput& prep,
                                    GpuReproConfig config) {
  if (!prep.compiled_grammar || !prep.constant_mutation ||
      !prep.constant_mutation->grammar_domains ||
      prep.parent_constant_streams.size() != population.size() ||
      prep.donor_constant_streams.size() != prep.donor_pool.size() ||
      prep.population_identities.size() != population.size() ||
      prep.candidates.size() != population.size() ||
      prep.subtree_ends.size() != population.size() ||
      prep.donor_contracts.size() != prep.donor_pool.size() ||
      prep.donor_identities.size() != prep.donor_pool.size())
    throw std::invalid_argument("compiled pack requires complete preparation metadata");
  if (config.max_nodes != prep.prepared_max_nodes ||
      config.max_expr_depth != prep.prepared_max_depth)
    throw std::invalid_argument("compiled pack search limits differ from preparation");
  if (config.population_size != static_cast<int>(population.size()) ||
      config.pair_count != (config.population_size + 1) / 2 ||
      config.candidates_per_program <= 0 || config.candidates_per_program > 65536 ||
      config.max_nodes <= 0 || config.max_nodes > kGpuReproKernelMaxNodes ||
      config.max_donor_nodes <= 0 || config.max_donor_nodes > kGpuReproKernelMaxNodes ||
      config.max_names <= 0 || config.max_names > kGpuReproMaxNames ||
      config.max_consts <= 0 || config.max_consts > kGpuReproMaxConsts)
    throw std::invalid_argument("compiled pack population shape mismatch");

  const auto& mutation = *prep.constant_mutation;
  const auto& domains = *mutation.grammar_domains;
  if (!domains.grammar_owner ||
      domains.grammar_owner.owner_before(prep.compiled_grammar) ||
      prep.compiled_grammar.owner_before(domains.grammar_owner))
    throw std::invalid_argument("compiled pack constant domains do not match its grammar owner");
  const auto check_constant_stream = [&](int index, const AstProgram& ast) {
    if (index < 0 || static_cast<std::size_t>(index) >= mutation.streams.size())
      throw std::invalid_argument("compiled pack has an invalid constant stream index");
    const auto& stream = mutation.streams[index];
    if (stream.node_count < 0 || static_cast<std::size_t>(stream.node_count) != ast.nodes.size() ||
        stream.node_origin_offset < 0 || stream.group_offset < 0 || stream.group_count < 0 ||
        stream.metadata_root_offset < 0 || stream.metadata_root_count < 0 ||
        static_cast<std::size_t>(stream.metadata_root_offset) + stream.metadata_root_count > mutation.metadata_roots.size() ||
        static_cast<std::size_t>(stream.node_origin_offset) + stream.node_count > mutation.node_group_origins.size() ||
        static_cast<std::size_t>(stream.group_offset) + stream.group_count > mutation.groups.size())
      throw std::invalid_argument("compiled pack has an invalid constant stream slice");
  };
  for (std::size_t i = 0; i < population.size(); ++i)
    check_constant_stream(prep.parent_constant_streams[i], population[i].ast);
  for (std::size_t i = 0; i < prep.donor_pool.size(); ++i)
    check_constant_stream(prep.donor_constant_streams[i], prep.donor_pool[i].ast);

  int max_candidates = config.candidates_per_program;
  int max_donor_nodes = 1;
  int max_names = 1;
  int max_consts = 1;
  std::vector<std::string> inputs;
  for (const auto& input : prep.compiled_grammar->inputs()) inputs.push_back(input.name);
  const auto fuel = prep.compiled_grammar->execution_limits().fuel;
  for (std::size_t i = 0; i < population.size(); ++i) {
    const auto& ast = population[i].ast;
    if (ast.nodes.empty() || ast.nodes.size() > static_cast<std::size_t>(config.max_nodes))
      throw std::invalid_argument("compiled parent exceeds its request budget");
    ProgramGenome genome; genome.ast = ast;
    if (grammar::runtime_cache_identity(genome, inputs, fuel) != prep.population_identities[i])
      throw std::invalid_argument("compiled pack preparation does not match parent identity");
    if (prep.candidates[i].size() > static_cast<std::size_t>(max_candidates))
      throw std::invalid_argument("compiled parent candidate count exceeds configured capacity");
    if (prep.subtree_ends[i].size() != ast.nodes.size())
      throw std::invalid_argument("compiled pack has invalid subtree metadata");
    for (const auto& candidate : prep.candidates[i]) {
      if (candidate.compatibility_id >= prep.compatibility_keys.size() ||
          candidate.occurrence_offset < 0 || candidate.occurrence_count <= 0 ||
          static_cast<std::size_t>(candidate.occurrence_offset) + candidate.occurrence_count > prep.occurrences.size() ||
          candidate.donor_offset < 0 || candidate.donor_count < 0 ||
          static_cast<std::size_t>(candidate.donor_offset) + candidate.donor_count > prep.donor_pool.size() ||
          candidate.materialized_nodes <= 0 || candidate.materialized_nodes > config.max_nodes ||
          candidate.materialized_depth <= 0 || candidate.materialized_depth > candidate.materialized_nodes ||
          candidate.replacement_max_nodes <= 0 || candidate.replacement_max_depth <= 0 ||
          candidate.template_nesting < 0 || candidate.remaining_template_nesting < 0)
        throw std::invalid_argument("compiled pack has an invalid candidate contract");
      const int arity = prep.occurrences[candidate.occurrence_offset].binder_count;
      int previous_stop = 0;
      for (int j = 0; j < candidate.occurrence_count; ++j) {
        const auto& occurrence = prep.occurrences[candidate.occurrence_offset + j];
        if (occurrence.start < previous_stop || occurrence.stop <= occurrence.start ||
            occurrence.stop - occurrence.start != candidate.materialized_nodes ||
            static_cast<std::size_t>(occurrence.stop) > ast.nodes.size() ||
            prep.subtree_ends[i][occurrence.start] != static_cast<std::size_t>(occurrence.stop) ||
            (j == 0 && (occurrence.start != candidate.start || occurrence.stop != candidate.stop)) ||
            occurrence.binder_offset < 0 || occurrence.binder_count != arity ||
            static_cast<std::size_t>(occurrence.binder_offset) + occurrence.binder_count > prep.occurrence_binder_ids.size())
          throw std::invalid_argument("compiled pack has invalid occurrence binder slices");
        previous_stop = occurrence.stop;
      }
      for (int j = 0; j < candidate.donor_count; ++j) {
        const auto id = static_cast<std::size_t>(candidate.donor_offset + j);
        const auto& donor = prep.donor_contracts[id];
        if (donor.binder_offset < 0 || donor.binder_count != arity ||
            static_cast<std::size_t>(donor.binder_offset) + donor.binder_count > prep.donor_binder_ids.size() ||
            donor.compatibility_id != candidate.compatibility_id ||
            donor.materialized_nodes <= 0 || donor.materialized_nodes > candidate.replacement_max_nodes ||
            donor.materialized_depth <= 0 || donor.materialized_depth > candidate.replacement_max_depth ||
            donor.template_nesting < 0 || donor.template_nesting > candidate.remaining_template_nesting)
          throw std::invalid_argument("compiled pack has invalid donor binder slices");
      }
    }
    max_names = std::max(max_names, static_cast<int>(ast.names.size()));
    max_consts = std::max(max_consts, static_cast<int>(ast.consts.size()));
  }
  for (std::size_t i = 0; i < prep.donor_pool.size(); ++i) {
    const auto& ast = prep.donor_pool[i].ast;
    ProgramGenome genome; genome.ast = ast;
    if (grammar::runtime_cache_identity(genome, inputs, fuel) != prep.donor_identities[i])
      throw std::invalid_argument("compiled pack preparation does not match donor identity");
    if (prep.donor_contracts[i].materialized_nodes != static_cast<int>(ast.nodes.size()))
      throw std::invalid_argument("compiled pack donor measurement differs from payload");
    max_donor_nodes = std::max(max_donor_nodes, static_cast<int>(ast.nodes.size()));
    max_names = std::max(max_names, static_cast<int>(ast.names.size()));
    max_consts = std::max(max_consts, static_cast<int>(ast.consts.size()));
  }
  config.candidates_per_program = max_candidates;
  config.max_donor_nodes = max_donor_nodes;
  config.max_names = max_names;
  config.max_consts = max_consts;
  config.compiled_donor_count = static_cast<int>(prep.donor_pool.size());
  config.compiled_occurrence_count = static_cast<int>(prep.occurrences.size());
  config.constant_domain_count = static_cast<int>(domains.domains.size());
  config.constant_value_count = static_cast<int>(domains.values.size());
  config.constant_group_count = static_cast<int>(mutation.groups.size());
  config.constant_origin_count = static_cast<int>(mutation.node_group_origins.size());
  config.constant_stream_count = static_cast<int>(mutation.streams.size());
  config.constant_root_count = static_cast<int>(mutation.metadata_roots.size());
  std::uint64_t bytes = 0;
  for (const auto& parent : population) bytes += splice_source_bytes(parent.ast);
  for (const auto& donor : prep.donor_pool) bytes += splice_source_bytes(donor.ast);
  if (bytes > UINT64_C(256) * 1024 * 1024)
    throw std::invalid_argument("compiled packed buffers and sources exceed 256 MiB");
  return config;
}

}  // namespace

ProgramGenome compact_genome_tables(const ProgramGenome& genome) {
  ProgramGenome out = genome;
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

std::vector<ProgramGenome> compact_population_tables(const std::vector<ProgramGenome>& population) {
  std::vector<ProgramGenome> out;
  out.reserve(population.size());
  for (const auto& genome : population) out.push_back(compact_genome_tables(genome));
  return out;
}

PackedHostData pack_population(const std::vector<ProgramGenome>& population,
                               const PreprocessOutput& prep,
                               const GpuReproConfig& input_config) {
  const auto config = compiled_pack_config(population, prep, input_config);
  PackedHostData out;
  out.config = config;
  out.compiled_grammar = prep.compiled_grammar;
  auto sources = std::make_shared<CompiledSpliceSources>();
  sources->parents.reserve(population.size());
  sources->donors.reserve(prep.donor_pool.size());
  for (const auto& parent : population) sources->parents.push_back(parent.ast);
  for (const auto& donor : prep.donor_pool) sources->donors.push_back(donor.ast);
  out.compiled_sources = std::move(sources);
  out.constant_mutation = prep.constant_mutation;
  out.parent_constant_streams = prep.parent_constant_streams;
  out.donor_constant_streams = prep.donor_constant_streams;
  out.compatibility_keys = prep.compatibility_keys;
  out.occurrences = prep.occurrences;
  out.occurrence_binder_ids = prep.occurrence_binder_ids;
  out.donor_contracts = prep.donor_contracts;
  out.donor_binder_ids = prep.donor_binder_ids;

  const std::size_t parents = population.size();
  const std::size_t donors = prep.donor_pool.size();
  out.program_nodes.resize(parents * config.max_nodes);
  out.metas.resize(parents);
  out.candidates.resize(parents * config.candidates_per_program);
  out.program_name_ids.resize(parents * config.max_names);
  out.program_consts.resize(parents * config.max_consts, Value::invalid());
  out.donor_nodes.resize(donors * config.max_donor_nodes);
  out.donor_lens.resize(donors);
  out.donor_name_ids.resize(donors * config.max_names);
  out.donor_name_counts.resize(donors);
  out.donor_consts.resize(donors * config.max_consts, Value::invalid());
  out.donor_const_counts.resize(donors);

  const auto pack_ast = [&](const AstProgram& ast, PlainNode* nodes,
                            std::uint64_t* names, Value* constants) {
    for (std::size_t i = 0; i < ast.nodes.size(); ++i)
      nodes[i] = {static_cast<int>(ast.nodes[i].kind), ast.nodes[i].i0, ast.nodes[i].i1};
    for (std::size_t i = 0; i < ast.names.size(); ++i) {
      register_name(&out, ast.names[i]); names[i] = hash_name(ast.names[i]);
    }
    std::copy(ast.consts.begin(), ast.consts.end(), constants);
  };
  for (std::size_t p = 0; p < parents; ++p) {
    const auto& ast = population[p].ast;
    out.metas[p] = {static_cast<int>(ast.nodes.size()), static_cast<int>(ast.names.size()),
                    static_cast<int>(ast.consts.size()), static_cast<int>(prep.candidates[p].size())};
    pack_ast(ast, out.program_nodes.data() + p * config.max_nodes,
             out.program_name_ids.data() + p * config.max_names,
             out.program_consts.data() + p * config.max_consts);
    std::copy(prep.candidates[p].begin(), prep.candidates[p].end(),
              out.candidates.begin() + p * config.candidates_per_program);
  }
  for (std::size_t d = 0; d < donors; ++d) {
    const auto& ast = prep.donor_pool[d].ast;
    out.donor_lens[d] = static_cast<int>(ast.nodes.size());
    out.donor_name_counts[d] = static_cast<int>(ast.names.size());
    out.donor_const_counts[d] = static_cast<int>(ast.consts.size());
    pack_ast(ast, out.donor_nodes.data() + d * config.max_donor_nodes,
             out.donor_name_ids.data() + d * config.max_names,
             out.donor_consts.data() + d * config.max_consts);
  }
  return out;
}

}  // namespace gagp::evo::repro
