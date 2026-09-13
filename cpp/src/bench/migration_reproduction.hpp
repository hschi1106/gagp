#pragma once

#include <iomanip>
#include <cmath>
#include <ostream>
#include <vector>

#include "migration_snapshot.hpp"
#include "gagp/cli/commands.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/repro/pack.hpp"
#include "gagp/evolution/repro/prep.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"

namespace gagp::migration {

struct ReproductionSnapshot {
  std::vector<evo::ProgramGenome> parents;
  std::vector<double> fitness;
  evo::repro::GpuReproConfig config;
  evo::repro::PreprocessOutput prep;
};

inline ReproductionSnapshot decode_reproduction(const cli_detail::JsonValue& raw) {
  using namespace cli_detail;
  using namespace evo;
  const auto array = [](const JsonValue& value) -> const std::vector<JsonValue>& {
    if (value.kind != JsonValue::Kind::Array) throw std::runtime_error("expected reproduction array");
    return value.array_v;
  };
  const auto number = [](const JsonValue& value) {
    if (value.kind != JsonValue::Kind::Number || !std::isfinite(value.number_v)) {
      throw std::runtime_error("expected finite reproduction number");
    }
    return value.number_v;
  };
  const auto u64 = [](const JsonValue& value) {
    const auto text = require_string(value, "unsigned decimal");
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) {
      throw std::runtime_error("invalid unsigned reproduction integer");
    }
    return std::stoull(text);
  };
  if (require_string(require_object_field(raw, "format_version"), "format_version") != "migration-reproduction-v1") {
    throw std::runtime_error("unsupported reproduction snapshot version");
  }
  ReproductionSnapshot out;
  out.parents = decode_population(require_object_field(raw, "parents"));
  for (const auto& value : array(require_object_field(raw, "fitness"))) out.fitness.push_back(number(value));
  const auto& config = require_object_field(raw, "config");
#define GAGP_RESTORE_REPRO_FIELD(name) out.config.name = require_int(require_object_field(config, #name), #name)
  GAGP_RESTORE_REPRO_FIELD(population_size);
  GAGP_RESTORE_REPRO_FIELD(pair_count);
  GAGP_RESTORE_REPRO_FIELD(candidates_per_program);
  GAGP_RESTORE_REPRO_FIELD(donor_pool_size_per_type);
  GAGP_RESTORE_REPRO_FIELD(max_nodes);
  GAGP_RESTORE_REPRO_FIELD(max_donor_nodes);
  GAGP_RESTORE_REPRO_FIELD(max_names);
  GAGP_RESTORE_REPRO_FIELD(max_consts);
  GAGP_RESTORE_REPRO_FIELD(max_linear_rec_binders);
  GAGP_RESTORE_REPRO_FIELD(max_asgp_dc_binders);
  GAGP_RESTORE_REPRO_FIELD(max_asgp_dp1d_specs);
  GAGP_RESTORE_REPRO_FIELD(max_asgp_dp2d_specs);
  GAGP_RESTORE_REPRO_FIELD(tournament_k);
  GAGP_RESTORE_REPRO_FIELD(max_expr_depth);
  GAGP_RESTORE_REPRO_FIELD(max_for_k);
#undef GAGP_RESTORE_REPRO_FIELD
  out.config.mutation_ratio = number(require_object_field(config, "mutation_ratio"));
  out.config.mutation_subtree_ratio = number(require_object_field(config, "mutation_subtree_ratio"));
  out.config.seed = u64(require_object_field(config, "seed"));
  for (const auto& row : array(require_object_field(raw, "subtree_ends"))) {
    std::vector<std::size_t> ends;
    for (const auto& value : array(row)) {
      const int end = require_int(value, "subtree end");
      if (end < 0) throw std::runtime_error("negative subtree end");
      ends.push_back(static_cast<std::size_t>(end));
    }
    out.prep.subtree_ends.push_back(std::move(ends));
  }
  for (const auto& row : array(require_object_field(raw, "candidates"))) {
    std::vector<repro::CandidateRange> candidates;
    for (const auto& value : array(row)) {
      const auto& fields = array(value);
      if (fields.size() != 10) throw std::runtime_error("invalid candidate field count");
      repro::CandidateRange c;
      c.start = require_int(fields[0], "start"); c.stop = require_int(fields[1], "stop");
      c.tag = require_int(fields[2], "tag"); c.aux = require_int(fields[3], "aux");
      c.scope_signature = u64(fields[4]); c.binder_signature = u64(fields[5]);
      c.scheme_kind = require_int(fields[6], "scheme"); c.phase_name = require_int(fields[7], "phase");
      c.visible_env_signature = u64(fields[8]); c.dp_dependency_arity = require_int(fields[9], "arity");
      candidates.push_back(c);
    }
    out.prep.candidates.push_back(std::move(candidates));
  }
  for (const auto& donor : array(require_object_field(raw, "donors"))) {
    repro::DonorProgram decoded;
    const int type = require_int(require_object_field(donor, "type"), "donor type");
    if (type < 0 || type >= static_cast<int>(RType::Invalid)) throw std::runtime_error("invalid donor type");
    decoded.type = static_cast<RType>(type);
    const auto& fragment = require_object_field(donor, "fragment");
    if (require_string(require_object_field(fragment, "format_version"), "format_version") != "migration-population-v1") {
      throw std::runtime_error("invalid donor fragment version");
    }
    const auto& programs = array(require_object_field(fragment, "programs"));
    if (programs.size() != 1) throw std::runtime_error("donor must contain one expression fragment");
    decoded.ast = decode_ast_json(require_object_field(programs[0], "structure"));
    if (!decoded.ast.consts.empty()) throw std::runtime_error("duplicate donor constants");
    for (const auto& constant : array(require_object_field(programs[0], "constants"))) {
      decoded.ast.consts.push_back(decode_value(constant));
    }
    // Validate a temporary executable wrapper; preserve the original fragment
    // and all of its side-table indices in the restored preparation data.
    auto wrapped = decoded.ast;
    wrapped.nodes.insert(wrapped.nodes.begin(), {{NodeKind::PROGRAM, 0, 0},
        {NodeKind::BLOCK_CONS, 0, 0}, {NodeKind::RETURN, 0, 0}});
    wrapped.nodes.push_back({NodeKind::BLOCK_NIL, 0, 0});
    for (auto& item : wrapped.linear_rec_binders) item.node_index += 3;
    for (auto& item : wrapped.asgp_dc_binders) item.node_index += 3;
    for (auto& item : wrapped.asgp_dp1d_specs) item.node_index += 3;
    for (auto& item : wrapped.asgp_dp2d_specs) item.node_index += 3;
    if (!verify_ast_structure(wrapped).ok) throw std::runtime_error("invalid donor expression structure");
    out.prep.donor_pool.push_back(std::move(decoded));
  }
  const auto n = out.parents.size();
  if (out.config.population_size != static_cast<int>(n) || out.fitness.size() != n ||
      out.prep.subtree_ends.size() != n || out.prep.candidates.size() != n ||
      out.config.donor_pool_size_per_type <= 0 || out.config.donor_pool_size_per_type > 64 ||
      out.prep.donor_pool.size() != static_cast<std::size_t>(out.config.donor_pool_size_per_type * repro::kGpuReproDonorTypeCount)) {
    throw std::runtime_error("reproduction snapshot dimensions disagree");
  }
  for (std::size_t p = 0; p < n; ++p) {
    const auto checked = verify_ast_structure(out.parents[p].ast);
    if (!checked.ok || out.prep.subtree_ends[p] != checked.verified.subtree_end) {
      throw std::runtime_error("reproduction subtree boundaries disagree with parents");
    }
    for (const auto& c : out.prep.candidates[p]) {
      if (c.start < 0 || c.stop < c.start || static_cast<std::size_t>(c.stop) > out.parents[p].ast.nodes.size()) {
        throw std::runtime_error("reproduction candidate outside parent");
      }
    }
  }
  return out;
}

inline void encode_reproduction(std::ostream& out,
    const std::vector<evo::ProgramGenome>& parents, const std::vector<double>& fitness,
    const evo::repro::GpuReproConfig& config, const evo::repro::PreprocessOutput& prep) {
  out << std::setprecision(17)
      << "{\"format_version\":\"migration-reproduction-v1\",\"parents\":"
      << encode_population(parents) << ",\"fitness\":[";
  for (std::size_t i = 0; i < fitness.size(); ++i) {
    if (i) out << ',';
    out << fitness[i];
  }
  out << "],\"config\":{";
#define GAGP_CAPTURE_REPRO_FIELD(name) out << "\"" #name "\":" << config.name << ','
  GAGP_CAPTURE_REPRO_FIELD(population_size);
  GAGP_CAPTURE_REPRO_FIELD(pair_count);
  GAGP_CAPTURE_REPRO_FIELD(candidates_per_program);
  GAGP_CAPTURE_REPRO_FIELD(donor_pool_size_per_type);
  GAGP_CAPTURE_REPRO_FIELD(max_nodes);
  GAGP_CAPTURE_REPRO_FIELD(max_donor_nodes);
  GAGP_CAPTURE_REPRO_FIELD(max_names);
  GAGP_CAPTURE_REPRO_FIELD(max_consts);
  GAGP_CAPTURE_REPRO_FIELD(max_linear_rec_binders);
  GAGP_CAPTURE_REPRO_FIELD(max_asgp_dc_binders);
  GAGP_CAPTURE_REPRO_FIELD(max_asgp_dp1d_specs);
  GAGP_CAPTURE_REPRO_FIELD(max_asgp_dp2d_specs);
  GAGP_CAPTURE_REPRO_FIELD(tournament_k);
  GAGP_CAPTURE_REPRO_FIELD(max_expr_depth);
  GAGP_CAPTURE_REPRO_FIELD(max_for_k);
  GAGP_CAPTURE_REPRO_FIELD(mutation_ratio);
  GAGP_CAPTURE_REPRO_FIELD(mutation_subtree_ratio);
#undef GAGP_CAPTURE_REPRO_FIELD
  out << "\"seed\":\"" << config.seed << "\"},\"subtree_ends\":[";
  for (std::size_t p = 0; p < prep.subtree_ends.size(); ++p) {
    if (p) out << ',';
    out << '[';
    for (std::size_t i = 0; i < prep.subtree_ends[p].size(); ++i) {
      if (i) out << ',';
      out << prep.subtree_ends[p][i];
    }
    out << ']';
  }
  out << "],\"candidates\":[";
  for (std::size_t p = 0; p < prep.candidates.size(); ++p) {
    if (p) out << ',';
    out << '[';
    for (std::size_t i = 0; i < prep.candidates[p].size(); ++i) {
      if (i) out << ',';
      const auto& c = prep.candidates[p][i];
      out << '[' << c.start << ',' << c.stop << ',' << c.tag << ',' << c.aux
          << ",\"" << c.scope_signature << "\",\"" << c.binder_signature << "\","
          << c.scheme_kind << ',' << c.phase_name << ",\"" << c.visible_env_signature
          << "\"," << c.dp_dependency_arity << ']';
    }
    out << ']';
  }
  out << "],\"donors\":[";
  for (std::size_t i = 0; i < prep.donor_pool.size(); ++i) {
    if (i) out << ',';
    const auto& donor = prep.donor_pool[i];
    evo::ProgramGenome fragment;
    fragment.ast = donor.ast;
    out << "{\"type\":" << static_cast<int>(donor.type)
        << ",\"fragment\":" << encode_population({fragment}) << '}';
  }
  out << "]}";
}

// Capture the actual production preprocessor output, before packing. Donors
// are expression fragments, not independently executable population members.
inline void freeze_reproduction(std::ostream& out,
                                const std::vector<evo::ProgramGenome>& population,
                                const evo::CaseSet& cases, const evo::EvolutionConfig& cfg) {
  std::vector<BytecodeProgram> programs;
  for (const auto& genome : population) {
    programs.push_back(evo::compile_for_eval(genome, cases.input_names));
  }
  const auto fitness = eval_fitness_cpu(programs, cases.bindings, cases.expected_values,
                                        cfg.fuel, cfg.penalty, cfg.gpu_blocksize);
  const auto ranked = evo::rank_population_refs(population, fitness);
  std::vector<evo::ProgramGenome> parents;
  for (const auto& parent : ranked) parents.push_back(*parent.genome);
  parents = evo::repro::compact_population_tables(parents);
  const auto config = evo::repro::make_gpu_repro_config(parents, cfg);
  const auto prep = evo::repro::preprocess_population(parents, config, cfg.grammar);
  std::vector<double> ranked_fitness;
  for (const auto& parent : ranked) ranked_fitness.push_back(parent.fitness);
  encode_reproduction(out, parents, ranked_fitness, config, prep);
}

}  // namespace gagp::migration
