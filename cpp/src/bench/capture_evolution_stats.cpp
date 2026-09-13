// Include immutable evolve.cpp with observer-only call-site interception.
// Diagnostic executions are separate from performance measurements.
#include <fstream>
#include <iostream>
#include <iomanip>
#include <map>
#include <mutex>
#include <set>
#include <sstream>

#include "migration_snapshot.hpp"
#include "gagp/cli/commands.hpp"
#include "gagp/cli/options.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/lifecycle.hpp"
#include "gagp/evolution/selection.hpp"

namespace gagp::cli_detail {
std::vector<evo::EvalCase> decode_fitness_cases_json(const JsonValue& raw);
}

namespace gagp::migration {
// Called only by separately generated diagnostic copies of operator sources.
// These counts describe branches taken, including accepted unchanged children.
std::mutex operator_decision_mutex;
std::map<std::string, std::size_t> operator_decisions;
void record_operator_decision(const char* event) {
  std::lock_guard<std::mutex> lock(operator_decision_mutex);
  ++operator_decisions[event];
}
}  // namespace gagp::migration

namespace {
std::vector<std::string> observations;
std::vector<gagp::evo::InputSpec> verification_inputs;
std::size_t case_count = 0;

void observe(const std::vector<gagp::evo::ProgramGenome>& population, const char* phase) {
  std::map<int, int> nodes, depths;
  std::map<std::string, int> verification;
  std::set<std::string> programs;
  for (const auto& genome : population) {
    ++nodes[genome.meta.node_count];
    ++depths[genome.meta.max_depth];
    programs.insert(genome.meta.program_key);
    const auto checked = gagp::evo::verify_ast(genome.ast, verification_inputs);
    ++verification[gagp::evo::verify_code_name(checked.diagnostic.code)];
  }
  std::ostringstream out;
  out << "{\"phase\":\"" << phase << "\",\"population_size\":" << population.size()
      << ",\"unique_programs\":" << programs.size();
  if (std::string(phase) == "evaluation") {
    out << ",\"fitness_evaluations_requested\":" << population.size()
        << ",\"case_score_requests\":" << population.size() * case_count;
  }
  const auto histogram = [&](const char* name, const std::map<int, int>& values) {
    out << ",\"" << name << "\":[";
    bool first = true;
    for (const auto& entry : values) {
      if (!first) out << ',';
      first = false;
      out << '[' << entry.first << ',' << entry.second << ']';
    }
    out << ']';
  };
  histogram("node_counts", nodes);
  histogram("depths", depths);
  out << ",\"post_backend_verifier_counts\":{";
  bool first = true;
  for (const auto& entry : verification) {
    if (!first) out << ',';
    first = false;
    out << '"' << entry.first << "\":" << entry.second;
  }
  out << '}';
  if (std::string(phase) == "reproduction") {
    std::lock_guard<std::mutex> lock(gagp::migration::operator_decision_mutex);
    for (bool gpu : {false, true}) {
      out << (gpu ? ",\"gpu_kernel_decisions\":{" : ",\"host_operator_decisions\":{");
      bool first_event = true;
      for (const auto& event : gagp::migration::operator_decisions) {
        if ((event.first.rfind("gpu_kernel.", 0) == 0) != gpu) continue;
        if (!first_event) out << ',';
        first_event = false;
        out << '"' << event.first << "\":" << event.second;
      }
      out << '}';
    }
    gagp::migration::operator_decisions.clear();
  }
  out << '}';
  observations.push_back(out.str());
}
}  // namespace

#ifdef GAGP_EVOLVE_SOURCE
namespace gagp::evo {
std::vector<ScoredGenomeRef> capture_rank(const std::vector<ProgramGenome>& population,
                                        const std::vector<double>& fitness, bool sort_output) {
  observe(population, "evaluation");
  return rank_population_refs(population, fitness, sort_output);
}
namespace repro {
ReproductionResult capture_reproduction(const std::vector<ScoredGenomeRef>& scored,
                                        const EvolutionConfig& cfg, std::mt19937_64& rng) {
  auto result = run_reproduction_backend(scored, cfg, rng);
  observe(result.next_population, "reproduction");
  return result;
}
}  // namespace repro
repro::ReproductionResult capture_overlap(std::future<OverlapPrepared>* future,
    const std::vector<ProgramGenome>& population, const std::vector<double>& fitness,
    const EvolutionConfig& cfg) {
  auto result = finish_gpu_reproduction_overlap(future, population, fitness, cfg);
  observe(result.next_population, "reproduction");
  return result;
}
}  // namespace gagp::evo
#define rank_population_refs capture_rank
#define run_reproduction_backend capture_reproduction
#define finish_gpu_reproduction_overlap capture_overlap
#include GAGP_EVOLVE_SOURCE
#undef rank_population_refs
#undef run_reproduction_backend
#undef finish_gpu_reproduction_overlap
#endif

int main(int argc, char** argv) {
  using namespace gagp;
  using namespace gagp::evo;
  using namespace gagp::cli_detail;
  try {
    std::string snapshot;
    std::vector<char*> common{argv[0]};
    for (int i = 1; i < argc; ++i) {
      if (std::string(argv[i]) == "--snapshot") {
        if (++i >= argc) throw std::runtime_error("missing snapshot path");
        snapshot = argv[i];
      } else common.push_back(argv[i]);
    }
    const auto opts = parse_cli_options(static_cast<int>(common.size()), common.data());
    const auto read = [](const std::string& path) {
      std::ifstream in(path);
      if (!in) throw std::runtime_error("cannot read " + path);
      std::ostringstream text;
      text << in.rdbuf();
      return JsonParser(text.str()).parse();
    };
    EvolutionConfig cfg;
    cfg.population_size = opts.population_size; cfg.generations = opts.generations;
    cfg.seed = opts.seed; cfg.fuel = opts.fuel; cfg.penalty = opts.penalty;
    cfg.gpu_blocksize = opts.blocksize;
    cfg.eval_engine = opts.engine == "gpu" ? EvalEngine::GPU : EvalEngine::CPU;
    cfg.reproduction_backend = repro::parse_reproduction_backend_name(opts.repro_backend);
    cfg.cpu_repro_ablation = repro::parse_cpu_repro_ablation_name(opts.cpu_repro_ablation);
    cfg.repro_overlap = opts.repro_overlap;
    cfg.selection_pressure = opts.selection_pressure;
    cfg.mutation_rate = opts.mutation_rate; cfg.mutation_subtree_prob = opts.mutation_subtree_prob;
    cfg.skip_final_eval = opts.skip_final_eval; cfg.retain_final_population = opts.retain_final_population;
    cfg.limits = Limits{opts.max_expr_depth, opts.max_stmts_per_block, opts.max_total_nodes,
                        opts.max_for_k, opts.max_call_args};
    if (!opts.grammar_config_path.empty()) cfg.grammar = decode_grammar_config_json(read(opts.grammar_config_path));
    const auto cases = decode_fitness_cases_json(read(opts.cases_path));
    case_count = cases.size();
    verification_inputs = prepare_case_set(cases, cfg.grammar).input_specs;
    const auto population = migration::decode_population(read(snapshot));
    const auto result = evolve_population(cases, cfg, &population);
    std::vector<ProgramGenome> best;
    for (const auto& individual : result.history_best) best.push_back(individual.genome);
    std::ofstream out(opts.out_json);
    out << std::setprecision(17) << "{\"format_version\":\"migration-evolution-statistics-v1\",\"best_programs\":"
        << migration::encode_population(best) << ",\"fitness_history\":[";
    for (std::size_t i = 0; i < result.history_best_fitness.size(); ++i) {
      if (i) out << ',';
      out << '[' << result.history_best_fitness[i] << ',' << result.history_mean_fitness[i] << ']';
    }
    out << "],\"populations\":[";
    for (std::size_t i = 0; i < observations.size(); ++i) {
      if (i) out << ',';
      out << observations[i];
    }
    out << "]}\n";
    if (!out) throw std::runtime_error("failed to write evolution statistics");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
