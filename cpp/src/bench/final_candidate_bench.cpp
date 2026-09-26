#include <chrono>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "final_candidate_snapshot.hpp"
#include "gagp/cli/commands.hpp"
#include "gagp/cli/grammar_population_artifact.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/grammar/compiled.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/grammar/request.hpp"
#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/evolution/repro/backend.hpp"
#include "gagp/evolution/repro/gpu.hpp"
#include "gagp/evolution/selection.hpp"
#include "gagp/runtime/cpu/fitness_cpu.hpp"
#ifdef GAGP_HAS_CUDA
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#endif

namespace {
using namespace gagp;
using namespace gagp::evo;
using Clock = std::chrono::steady_clock;

struct Options {
  std::string action;
  std::string snapshot;
  std::string cases;
  std::string grammar;
  std::vector<std::string> population_roots;
  std::string output;
  std::string engine = "cpu";
  std::string repro_backend = "cpu";
  std::string cpu_repro_ablation = "none";
  bool repro_overlap = false;
  bool skip_final_eval = false;
  bool retain_final_population = false;
  int blocksize = 1024;
  int population_size = 64;
  int generations = 1;
  int selection_pressure = 2;
  int fuel = 20000;
  int max_nodes = 80;
  int max_depth = 7;
  int source_max_nodes = 0;
  int source_max_depth = 0;
  int minimum_dc_frames = 0;
  bool normalize_storage = false;
  int max_for = 16;
  int warmups = 3;
  int trials = 15;
  std::uint64_t seed = 0;
  double mutation_rate = 0.5;
  double mutation_subtree_prob = 0.8;
  double penalty = 1.0;
};

std::string read_text(const std::string& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot read " + path);
  std::ostringstream result;
  result << input.rdbuf();
  return result.str();
}

cli_detail::JsonValue read_json(const std::string& path) {
  return cli_detail::JsonParser(read_text(path), {true, 512}).parse();
}

void write_text(const std::string& path, const std::string& value) {
  std::ofstream output(path);
  if (!output || !(output << value << '\n'))
    throw std::runtime_error("cannot write " + path);
}

void write_result(const Options& options, std::string result) {
  if (options.normalize_storage) {
    if (result.empty() || result.back() != '}')
      throw std::logic_error("benchmark result must be a JSON object");
    result.pop_back();
    result += ",\"normalize_typed_storage\":true}";
  }
  if (options.minimum_dc_frames) {
    if (result.empty() || result.back() != '}')
      throw std::logic_error("benchmark result must be a JSON object");
    result.pop_back();
    result += ",\"minimum_dc_frames\":" + std::to_string(options.minimum_dc_frames) + "}";
  }
  if (options.source_max_nodes) {
    if (result.empty() || result.back() != '}')
      throw std::logic_error("benchmark result must be a JSON object");
    result.pop_back();
    result += ",\"source_resource_budget\":{\"max_nodes\":" +
        std::to_string(options.source_max_nodes) + ",\"offspring_max_depth\":" +
        std::to_string(options.source_max_depth) +
        ",\"initial_depth_unbounded\":true,\"physical_max_nodes\":" +
        std::to_string(options.max_nodes) + ",\"physical_max_depth\":" +
        std::to_string(options.max_depth) + "}}";
  }
  write_text(options.output, result);
}

bool on_off(const std::string& value, const char* option) {
  if (value == "on") return true;
  if (value == "off") return false;
  throw std::runtime_error(std::string(option) + " requires on or off");
}

Options parse(int argc, char** argv) {
  Options out;
  for (int i = 1; i < argc; ++i) {
    const std::string option = argv[i];
    if (++i >= argc) throw std::runtime_error("missing value for " + option);
    const std::string value = argv[i];
    if (option == "--action") out.action = value;
    else if (option == "--snapshot") out.snapshot = value;
    else if (option == "--cases") out.cases = value;
    else if (option == "--grammar-definition") out.grammar = value;
    else if (option == "--population-roots") {
      if (value.empty() || value.front() == ',' || value.back() == ',')
        throw std::runtime_error("--population-roots requires nonempty comma-separated root IDs");
      std::istringstream names(value);
      std::string name;
      while (std::getline(names, name, ',')) {
        if (name.empty()) throw std::runtime_error("--population-roots has an empty root ID");
        out.population_roots.push_back(name);
      }
    }
    else if (option == "--out-json") out.output = value;
    else if (option == "--engine") out.engine = value;
    else if (option == "--repro-backend") out.repro_backend = value;
    else if (option == "--cpu-repro-ablation") out.cpu_repro_ablation = value;
    else if (option == "--repro-overlap") out.repro_overlap = on_off(value, option.c_str());
    else if (option == "--skip-final-eval") out.skip_final_eval = on_off(value, option.c_str());
    else if (option == "--retain-final-population") out.retain_final_population = on_off(value, option.c_str());
    else if (option == "--blocksize") out.blocksize = std::stoi(value);
    else if (option == "--population-size") out.population_size = std::stoi(value);
    else if (option == "--generations") out.generations = std::stoi(value);
    else if (option == "--normalize-typed-storage") {
      if (value != "on" && value != "off")
        throw std::runtime_error("--normalize-typed-storage requires on or off");
      out.normalize_storage = value == "on";
    }
    else if (option == "--minimum-dc-frames") out.minimum_dc_frames = std::stoi(value);
    else if (option == "--selection-pressure") out.selection_pressure = std::stoi(value);
    else if (option == "--fuel") out.fuel = std::stoi(value);
    else if (option == "--max-total-nodes") out.max_nodes = std::stoi(value);
    else if (option == "--max-expr-depth") out.max_depth = std::stoi(value);
    else if (option == "--source-max-total-nodes") out.source_max_nodes = std::stoi(value);
    else if (option == "--source-max-expr-depth") out.source_max_depth = std::stoi(value);
    else if (option == "--warmups") out.warmups = std::stoi(value);
    else if (option == "--trials") out.trials = std::stoi(value);
    else if (option == "--seed") out.seed = std::stoull(value);
    else if (option == "--mutation-rate") out.mutation_rate = std::stod(value);
    else if (option == "--mutation-subtree-prob") out.mutation_subtree_prob = std::stod(value);
    else if (option == "--penalty") out.penalty = std::stod(value);
    // These release-1 generator limits do not alter restored candidate execution.
    else if (option == "--max-for-k") out.max_for = std::stoi(value);
    else if (option == "--max-stmts-per-block" || option == "--max-call-args") {}
    else throw std::runtime_error("unknown final-candidate adapter option: " + option);
  }
  if (out.action != "run" && out.action != "steady" && out.action != "repro-steady")
    throw std::runtime_error("--action must be run, steady, or repro-steady");
  if (out.snapshot.empty() || out.cases.empty() || out.output.empty())
    throw std::runtime_error("--snapshot, --cases, and --out-json are required");
  if (!out.population_roots.empty() && out.grammar.empty())
    throw std::runtime_error("--population-roots requires --grammar-definition");
  if (out.source_max_nodes < 0 || out.source_max_depth < 0 ||
      (out.source_max_nodes == 0) != (out.source_max_depth == 0))
    throw std::runtime_error("source resource limits must both be positive or both omitted");
  if (out.source_max_nodes && out.grammar.empty())
    throw std::runtime_error("source resource limits require --grammar-definition");
  if (out.minimum_dc_frames < 0 || out.minimum_dc_frames > 65535 ||
      (out.minimum_dc_frames && out.grammar.empty()))
    throw std::runtime_error("minimum DC frames requires a grammar and a value in 0..65535");
  if (out.engine != "cpu" && out.engine != "gpu")
    throw std::runtime_error("--engine must be cpu or gpu");
  if (out.warmups < 1 || out.trials < 1)
    throw std::runtime_error("warmup and trial counts must be positive");
  return out;
}

double elapsed(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

EvolutionConfig config(const Options& options) {
  EvolutionConfig cfg;
  cfg.population_size = options.population_size;
  cfg.generations = options.generations;
  cfg.mutation_rate = options.mutation_rate;
  cfg.mutation_subtree_prob = options.mutation_subtree_prob;
  cfg.penalty = options.penalty;
  cfg.eval_engine = options.engine == "gpu" ? EvalEngine::GPU : EvalEngine::CPU;
  cfg.reproduction_backend = repro::parse_reproduction_backend_name(options.repro_backend);
  cfg.cpu_repro_ablation = repro::parse_cpu_repro_ablation_name(options.cpu_repro_ablation);
  cfg.repro_overlap = options.repro_overlap;
  cfg.gpu_blocksize = options.blocksize;
  cfg.selection_pressure = options.selection_pressure;
  cfg.seed = options.seed;
  cfg.fuel = options.fuel;
  cfg.skip_final_eval = options.skip_final_eval;
  cfg.retain_final_population = options.retain_final_population;
  if (options.source_max_nodes) {
    cfg.initial_resource_budget = grammar::ProjectedBudget{
        static_cast<std::uint64_t>(options.source_max_nodes),
        std::numeric_limits<std::uint64_t>::max()};
    cfg.offspring_resource_budget = grammar::ProjectedBudget{
        static_cast<std::uint64_t>(options.source_max_nodes),
        static_cast<std::uint64_t>(options.source_max_depth)};
  }
  return cfg;
}

void evaluation_timing(std::ostream& out, const EvaluationTiming& value) {
  out << "{\"cpu_compile_ms\":" << value.cpu_compile_ms
      << ",\"gpu_compile_ms\":" << value.gpu_compile_ms
      << ",\"gpu_eval_call_ms\":" << value.gpu_eval_call_ms
      << ",\"gpu_eval_pack_ms\":" << value.gpu_eval_pack_ms
      << ",\"gpu_eval_launch_prep_ms\":" << value.gpu_eval_launch_prep_ms
      << ",\"gpu_eval_upload_ms\":" << value.gpu_eval_upload_ms
      << ",\"gpu_eval_kernel_ms\":" << value.gpu_eval_kernel_ms
      << ",\"gpu_eval_copyback_ms\":" << value.gpu_eval_copyback_ms
      << ",\"gpu_eval_teardown_ms\":" << value.gpu_eval_teardown_ms << '}';
}

void reproduction_timing(std::ostream& out, const ReproductionTiming& value) {
  out << "{\"selection_ms\":" << value.selection_ms
      << ",\"crossover_ms\":" << value.crossover_ms
      << ",\"mutation_ms\":" << value.mutation_ms
      << ",\"prepare_inputs_ms\":" << value.prepare_inputs_ms
      << ",\"setup_ms\":" << value.setup_ms
      << ",\"preprocess_ms\":" << value.preprocess_ms
      << ",\"pack_ms\":" << value.pack_ms
      << ",\"upload_ms\":" << value.upload_ms
      << ",\"kernel_ms\":" << value.kernel_ms
      << ",\"copyback_ms\":" << value.copyback_ms
      << ",\"decode_ms\":" << value.decode_ms
      << ",\"teardown_ms\":" << value.teardown_ms
      << ",\"selection_kernel_ms\":" << value.selection_kernel_ms
      << ",\"variation_kernel_ms\":" << value.variation_kernel_ms
      << ",\"crossover_attempts\":" << value.variation.crossover_attempts
      << ",\"mutation_attempts\":" << value.variation.mutation_attempts
      << ",\"contract_rejections\":" << value.variation.contract_rejections
      << ",\"budget_rejections\":" << value.variation.budget_rejections
      << ",\"generation_rejections\":" << value.variation.generation_rejections
      << ",\"acceptance_rejections\":" << value.variation.acceptance_rejections
      << ",\"fallback_children\":" << value.variation.fallback_children
      << ",\"unchanged_children\":" << value.variation.unchanged_children
      << ",\"changed_children\":" << value.variation.changed_children << '}';
}

void steady_programs(std::ostream& out, std::vector<BytecodeProgram> programs,
                     double compile_ms, const CaseSet& cases,
                     const EvolutionConfig& cfg, int warmups, int trials) {
  if (cfg.reproduction_backend != repro::ReproductionBackend::Cpu || cfg.repro_overlap ||
      cfg.cpu_repro_ablation != repro::CpuReproAblation::None)
    throw std::runtime_error("steady evaluation requires CPU reproduction, overlap off, and no ablation");
  double init_ms = 0.0;
#ifdef GAGP_HAS_CUDA
  FitnessSessionGpu session;
  if (cfg.eval_engine == EvalEngine::GPU) {
    const auto initialized = session.init(cases.bindings, cases.expected_values,
                                          cfg.fuel, cfg.gpu_blocksize, cfg.penalty);
    if (!initialized.ok) throw std::runtime_error("steady GPU init failed: " + initialized.err.message);
    init_ms = initialized.timing.total_ms;
  }
#else
  if (cfg.eval_engine == EvalEngine::GPU)
    throw std::runtime_error("steady GPU evaluation requires a CUDA build");
#endif
  out << "{\"format_version\":\"migration-steady-eval-v1\",\"engine\":\""
      << (cfg.eval_engine == EvalEngine::GPU ? "gpu" : "cpu")
      << "\",\"compile_ms\":" << compile_ms << ",\"session_init_ms\":" << init_ms
      << ",\"warmups\":" << warmups << ",\"measured_trials\":" << trials
      << ",\"samples\":[";
  std::vector<double> reference;
  for (int i = 0; i < warmups + trials; ++i) {
    EvaluationTiming timing;
    std::vector<double> scores;
    const auto start = Clock::now();
    if (cfg.eval_engine == EvalEngine::CPU) {
      scores = eval_fitness_cpu(programs, cases.bindings, cases.expected_values,
                                cfg.fuel, cfg.penalty, cfg.gpu_blocksize);
    } else {
#ifdef GAGP_HAS_CUDA
      const auto evaluated = session.eval_programs(programs);
      if (!evaluated.ok) throw std::runtime_error("steady GPU evaluation failed: " + evaluated.err.message);
      scores = evaluated.fitness;
      timing.gpu_eval_call_ms = evaluated.timing.total_ms;
      timing.gpu_eval_pack_ms = evaluated.timing.pack_ms;
      timing.gpu_eval_launch_prep_ms = evaluated.timing.launch_prep_ms;
      timing.gpu_eval_upload_ms = evaluated.timing.upload_ms;
      timing.gpu_eval_kernel_ms = evaluated.timing.kernel_ms;
      timing.gpu_eval_copyback_ms = evaluated.timing.copyback_ms;
      timing.gpu_eval_teardown_ms = evaluated.timing.teardown_ms;
#endif
    }
    const double call_ms = elapsed(start);
    if (i == 0) reference = scores;
    else if (scores != reference) throw std::runtime_error("steady fitness changed between calls");
    if (i != 0) out << ',';
    out << "{\"index\":" << i << ",\"warmup\":" << (i < warmups ? "true" : "false")
        << ",\"call_ms\":" << call_ms << ",\"phases\":";
    evaluation_timing(out, timing);
    out << '}';
  }
  out << "],\"fitness\":[";
  for (std::size_t i = 0; i < reference.size(); ++i) {
    if (i != 0) out << ',';
    out << reference[i];
  }
  out << "]}";
}

void steady(std::ostream& out, const std::vector<ProgramGenome>& population,
            const CaseSet& cases, const EvolutionConfig& cfg,
            int warmups, int trials) {
  const auto compile_start = Clock::now();
  std::vector<BytecodeProgram> programs;
  programs.reserve(population.size());
  for (const auto& genome : population)
    programs.push_back(compile_for_eval(genome, cases.input_names));
  const double compile_ms = elapsed(compile_start);
  steady_programs(out, std::move(programs), compile_ms, cases, cfg, warmups, trials);
}

void require_membership(const EvolutionConfig& cfg,
                        const std::vector<ProgramGenome>& population) {
  grammar::VariationContext context(cfg.compiled_grammar, population_requests(cfg), 128,
      cfg.offspring_resource_budget);
  for (std::size_t i = 0; i < population.size(); ++i) {
    try {
      const auto analysis = context.analyze(population[i]);
      if (cfg.initial_resource_budget && !cfg.initial_resource_budget->accepts(
              analysis->witness.resources->subtree()))
        throw std::runtime_error("initial source resource budget exceeded");
    } catch (const std::exception& error) {
      throw std::runtime_error("frozen member " + std::to_string(i) +
          " is not in the supplied v2 grammar: " + error.what());
    }
  }
}

std::string population_identity(const std::vector<ProgramGenome>& population) {
  std::ostringstream out;
  out << "{\"format_version\":\"final-candidate-children-v2\",\"programs\":[";
  for (std::size_t i = 0; i < population.size(); ++i) {
    if (i != 0) out << ',';
    out << cli_detail::encode_ast_json(population[i].ast);
  }
  out << "]}";
  return out.str();
}

void validate_reproduction_capture(
    const migration::bench::FrozenReproduction& frozen, const Options& options) {
  const auto& cfg = frozen.config;
  if (cfg.population_size != options.population_size ||
      cfg.tournament_k != options.selection_pressure ||
      cfg.max_nodes != (options.source_max_nodes ? options.source_max_nodes : options.max_nodes) ||
      cfg.max_expr_depth != (options.source_max_depth ? options.source_max_depth : options.max_depth) ||
      cfg.max_for_k != options.max_for || cfg.mutation_ratio != options.mutation_rate ||
      cfg.mutation_subtree_ratio != options.mutation_subtree_prob ||
      cfg.seed != options.seed)
    throw std::runtime_error("reproduction settings differ from frozen capture");
}

void repro_steady(std::ostream& out, migration::bench::FrozenReproduction frozen,
                  const std::vector<InputSpec>& inputs, EvolutionConfig cfg,
                  const Options& options, double fitness_prepare_ms = 0.0) {
  if (!cfg.compiled_grammar || !cfg.generation_request)
    throw std::runtime_error(
        "repro-steady requires --grammar-definition for current v2 reproduction");
  if (cfg.repro_overlap || cfg.cpu_repro_ablation != repro::CpuReproAblation::None)
    throw std::runtime_error("repro-steady requires overlap off and no CPU ablation");
  validate_reproduction_capture(frozen, options);
  cfg.verification_inputs = inputs;
  grammar::VariationContext context(cfg.compiled_grammar, population_requests(cfg), 128,
      cfg.offspring_resource_budget);
  for (auto& parent : frozen.parents) {
    auto witness = context.analyze(parent)->witness;
    if (cfg.initial_resource_budget && !cfg.initial_resource_budget->accepts(
            witness.resources->subtree()))
      throw std::runtime_error("initial source resource budget exceeded");
    parent.meta = build_genome_meta(parent.ast);
    parent.derivation = std::make_shared<const grammar::DerivationMetadata>(
        std::move(witness));
  }
  std::vector<ScoredGenomeRef> scored;
  for (std::size_t i = 0; i < frozen.parents.size(); ++i)
    scored.push_back({&frozen.parents[i],
        canonicalize_fitness_for_ranking(frozen.fitness[i])});

  repro::GpuReproPreparedData prepared;
  repro::ReproductionStats preparation;
  double prepare_ms = 0.0;
  if (cfg.reproduction_backend == repro::ReproductionBackend::Gpu) {
    const auto start = Clock::now();
    std::mt19937_64 rng(cfg.seed);
    prepared = repro::prepare_gpu_repro_backend_inputs(
        frozen.parents, cfg, rng(), &preparation);
    prepare_ms = elapsed(start);
  }

  const char* format = cfg.reproduction_backend == repro::ReproductionBackend::Gpu
      ? "migration-steady-reproduction-v1"
      : "migration-steady-cpu-reproduction-v1";
  out << "{\"format_version\":\"" << format << "\",\"warmups\":"
      << options.warmups << ",\"measured_trials\":" << options.trials
      << ",\"fitness_prepare_ms\":" << fitness_prepare_ms;
  if (cfg.reproduction_backend == repro::ReproductionBackend::Gpu)
    out << ",\"prepare_ms\":" << prepare_ms
        << ",\"prepare_inputs_ms\":" << preparation.prepare_inputs_ms
        << ",\"preprocess_ms\":" << preparation.preprocess_ms
        << ",\"pack_ms\":" << preparation.pack_ms;
  out << ",\"samples\":[";
  std::string reference;
  for (int i = 0; i < options.warmups + options.trials; ++i) {
    const auto start = Clock::now();
    repro::ReproductionResult result;
    if (cfg.reproduction_backend == repro::ReproductionBackend::Gpu) {
      result = repro::run_gpu_repro_backend_prepared(scored, cfg, prepared);
    } else {
      std::mt19937_64 rng(cfg.seed);
      result = repro::run_reproduction_backend(scored, cfg, rng);
    }
    const double call_ms = elapsed(start);
    if (result.next_population.size() != frozen.parents.size())
      throw std::runtime_error("current reproduction changed the population size");
    for (const auto& child : result.next_population)
      if (!verify_ast(child.ast, inputs).ok)
        throw std::runtime_error("current reproduction emitted an invalid child");
    const auto identity = population_identity(result.next_population);
    if (i == 0) reference = identity;
    else if (identity != reference)
      throw std::runtime_error("current reproduction changed between steady calls");
    if (i != 0) out << ',';
    const auto& s = result.stats;
    out << "{\"index\":" << i << ",\"warmup\":"
        << (i < options.warmups ? "true" : "false")
        << ",\"call_ms\":" << call_ms << ",\"phases\":{"
        << "\"selection_ms\":" << s.selection_ms
        << ",\"crossover_ms\":" << s.crossover_ms
        << ",\"mutation_ms\":" << s.mutation_ms
        << ",\"setup_ms\":" << s.setup_ms
        << ",\"upload_ms\":" << s.upload_ms
        << ",\"kernel_ms\":" << s.kernel_ms
        << ",\"copyback_ms\":" << s.copyback_ms
        << ",\"decode_ms\":" << s.decode_ms
        << ",\"selection_kernel_ms\":" << s.selection_kernel_ms
        << ",\"variation_kernel_ms\":" << s.variation_kernel_ms
        << ",\"crossover_attempts\":" << s.variation.crossover_attempts
        << ",\"mutation_attempts\":" << s.variation.mutation_attempts
        << ",\"contract_rejections\":" << s.variation.contract_rejections
        << ",\"budget_rejections\":" << s.variation.budget_rejections
        << ",\"generation_rejections\":" << s.variation.generation_rejections
        << ",\"acceptance_rejections\":" << s.variation.acceptance_rejections
        << ",\"fallback_children\":" << s.variation.fallback_children
        << ",\"unchanged_children\":" << s.variation.unchanged_children
        << ",\"changed_children\":" << s.variation.changed_children << "}}";
  }
  out << "],\"children\":" << reference << '}';
}

migration::bench::FrozenReproduction prepare_current_reproduction(
    std::vector<ProgramGenome> population, const CaseSet& cases,
    const Options& options, double* prepare_ms) {
  const auto start = Clock::now();
  std::vector<BytecodeProgram> programs;
  programs.reserve(population.size());
  for (const auto& genome : population)
    programs.push_back(compile_for_eval(genome, cases.input_names));
  auto fitness = eval_fitness_cpu(programs, cases.bindings, cases.expected_values,
                                  options.fuel, options.penalty, options.blocksize);
  if (fitness.size() != population.size())
    throw std::runtime_error("current reproduction fitness size mismatch");
  migration::bench::FrozenReproduction result;
  result.parents = std::move(population);
  result.fitness = std::move(fitness);
  auto& captured = result.config;
  captured.population_size = options.population_size;
  captured.pair_count = (options.population_size + 1) / 2;
  captured.candidates_per_program = 1;
  captured.donor_pool_size_per_type = 1;
  captured.max_nodes = options.source_max_nodes ? options.source_max_nodes : options.max_nodes;
  captured.max_donor_nodes = options.max_nodes;
  captured.max_names = options.max_nodes;
  captured.max_consts = options.max_nodes;
  captured.max_linear_rec_binders = options.max_nodes;
  captured.max_asgp_dc_binders = 0;
  captured.max_asgp_dp1d_specs = 0;
  captured.max_asgp_dp2d_specs = 0;
  captured.tournament_k = options.selection_pressure;
  captured.max_expr_depth = options.source_max_depth ? options.source_max_depth : options.max_depth;
  captured.max_for_k = options.max_for;
  captured.mutation_ratio = options.mutation_rate;
  captured.mutation_subtree_ratio = options.mutation_subtree_prob;
  captured.seed = options.seed;
  *prepare_ms = elapsed(start);
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto process_start = Clock::now();
    const Options options = parse(argc, argv);
    const auto snapshot = read_json(options.snapshot);
    const std::string snapshot_version = cli_detail::require_string(
        cli_detail::require_object_field(snapshot, "format_version"),
        "format_version");
    const bool bytecode_snapshot =
        snapshot_version == "migration-bytecode-population-v1";
    const bool current_population_snapshot =
        snapshot_version == cli_detail::kGeneratedGrammarPopulationArtifactVersion;
    if (options.normalize_storage && (bytecode_snapshot || current_population_snapshot))
      throw std::runtime_error("typed storage normalization applies only to frozen source AST imports");
    if (options.minimum_dc_frames && (bytecode_snapshot || current_population_snapshot))
      throw std::runtime_error("minimum DC frames applies only to frozen source AST imports");
    const auto cases_json = read_json(options.cases);
    const auto cases = bytecode_snapshot
        ? std::vector<EvalCase>{}
        : cli_detail::decode_fitness_cases_json(cases_json);
    const CaseSet case_set = bytecode_snapshot
        ? migration::bench::decode_final_candidate_bytecode_cases(cases_json)
        : prepare_case_set(cases);
    EvolutionConfig cfg = config(options);
    if (!options.grammar.empty()) {
      cfg.compiled_grammar = std::make_shared<const grammar::CompiledGrammar>(
          grammar::compile_grammar(grammar::load_definition(options.grammar)));
      cfg.generation_request = grammar::entry_request(*cfg.compiled_grammar);
      if (!options.population_roots.empty()) {
        const auto requests = grammar::named_population_requests(
            *cfg.compiled_grammar, options.population_roots);
        cfg.generation_request = requests.front();
        cfg.additional_generation_requests.assign(requests.begin() + 1, requests.end());
      }
      if (cfg.compiled_grammar->execution_limits().fuel != static_cast<std::uint32_t>(cfg.fuel))
        throw std::runtime_error("--fuel differs from supplied v2 grammar execution limit");
      if (options.max_nodes != static_cast<int>(cfg.compiled_grammar->search_limits().max_nodes) ||
          options.max_depth != static_cast<int>(cfg.compiled_grammar->search_limits().max_depth))
        throw std::runtime_error("frozen search limits differ from supplied v2 grammar");
    }
    std::ostringstream output;
    output << std::setprecision(17);
    if (options.action == "repro-steady") {
      if (current_population_snapshot) {
        if (!cfg.compiled_grammar || !cfg.generation_request)
          throw std::runtime_error(
              "current population repro-steady requires --grammar-definition");
        auto population = cli_detail::replay_generated_population_artifact(
            read_text(options.snapshot), cfg.compiled_grammar.get());
        if (population.size() != static_cast<std::size_t>(options.population_size))
          throw std::runtime_error("snapshot size differs from --population-size");
        double fitness_prepare_ms = 0.0;
        auto prepared = prepare_current_reproduction(
            std::move(population), case_set, options, &fitness_prepare_ms);
        repro_steady(output, std::move(prepared), case_set.input_specs, cfg,
                     options, fitness_prepare_ms);
        write_result(options, output.str());
        return 0;
      }
      auto frozen = migration::bench::decode_frozen_reproduction(
          snapshot, case_set.input_specs, options.minimum_dc_frames, options.normalize_storage);
      repro_steady(output, std::move(frozen), case_set.input_specs, cfg, options);
      write_result(options, output.str());
      return 0;
    }
    if (bytecode_snapshot) {
      if (options.action != "steady")
        throw std::runtime_error(
            "source-less frozen bytecode supports steady runtime evaluation only");
      if (!options.grammar.empty())
        throw std::runtime_error(
            "source-less frozen bytecode cannot claim v2 grammar membership");
      auto programs = migration::bench::decode_final_candidate_bytecode_population(snapshot);
      if (programs.size() != static_cast<std::size_t>(options.population_size))
        throw std::runtime_error("snapshot size differs from --population-size");
      steady_programs(output, std::move(programs), 0.0, case_set, cfg,
                      options.warmups, options.trials);
      write_result(options, output.str());
      return 0;
    }
    auto population = current_population_snapshot
        ? cli_detail::replay_generated_population_artifact(
              read_text(options.snapshot), cfg.compiled_grammar.get())
        : migration::bench::decode_final_candidate_population(
              snapshot, case_set.input_specs, options.minimum_dc_frames, options.normalize_storage);
    if (population.size() != static_cast<std::size_t>(options.population_size))
      throw std::runtime_error("snapshot size differs from --population-size");
    std::array<std::size_t, static_cast<std::size_t>(RType::Invalid)> return_counts{};
    for (std::size_t i = 0; i < population.size(); ++i) {
      const auto verified = verify_ast(population[i].ast, case_set.input_specs);
      if (!verified)
        throw std::runtime_error("lowered snapshot member " + std::to_string(i) +
            " failed v2 verification: " + verified.diagnostic.message);
      ++return_counts[static_cast<std::size_t>(verified.verified.return_type)];
    }
    if (options.action == "run" && case_set.expected_return_type == RType::Invalid &&
        cfg.additional_generation_requests.empty()) {
      std::ostringstream message;
      message << "mixed-return frozen cases cannot map to one v2 grammar generation request; parent returns";
      for (std::size_t i = 0; i < return_counts.size(); ++i)
        if (return_counts[i]) message << ' ' << grammar::type_name(static_cast<RType>(i))
                                      << '=' << return_counts[i];
      throw std::runtime_error(message.str());
    }
    if (cfg.compiled_grammar)
      require_membership(cfg, population);
    if (options.action == "steady") {
      steady(output, population, case_set, cfg, options.warmups, options.trials);
    } else {
      if (!cfg.compiled_grammar || !cfg.generation_request)
        throw std::runtime_error(
            "run requires --grammar-definition so v2 derivation membership can be reconstructed honestly");
      const double load_ms = elapsed(process_start);
      const auto start = Clock::now();
      const auto result = evolve_population(cases, cfg, &population);
      const double call_ms = elapsed(start);
      output << "{\"format_version\":\"migration-run-v1\",\"load_ms\":" << load_ms
          << ",\"evolve_call_ms\":" << call_ms
          << ",\"init_population_ms\":" << result.timing.init_population_ms
          << ",\"gpu_eval_init_ms\":" << result.timing.gpu_eval_init_ms
          << ",\"final_eval_ms\":" << result.timing.final_eval_ms
          << ",\"total_ms\":" << result.timing.total_ms << ",\"generations\":[";
      for (std::size_t i = 0; i < result.timing.generations.size(); ++i) {
        if (i != 0) output << ',';
        const auto& timing = result.timing.generations[i];
        output << "{\"eval_ms\":" << timing.eval_ms << ",\"repro_ms\":" << timing.repro_ms
            << ",\"total_ms\":" << timing.total_ms << ",\"evaluation\":";
        evaluation_timing(output, timing.evaluation);
        output << ",\"reproduction\":";
        reproduction_timing(output, timing.reproduction);
        output << ",\"best_fitness\":" << result.history_best_fitness[i]
            << ",\"mean_fitness\":" << result.history_mean_fitness[i]
            << ",\"best_nodes\":" << result.history_best[i].genome.meta.node_count << '}';
      }
      output << ']';
      if (options.retain_final_population && !cfg.skip_final_eval) {
        // Keep evidence validation outside the measured evolution call. The
        // initial budget admits unchanged grandfathered parents as well as
        // offspring; offspring-specific budgets are enforced during acceptance.
        std::vector<ProgramGenome> final_population;
        final_population.reserve(result.final_population.size());
        for (const auto& member : result.final_population)
          final_population.push_back(member.genome);
        if (final_population.size() != population.size())
          throw std::runtime_error("evolution changed the retained population size");
        require_membership(cfg, final_population);
        output << ",\"final_population\":" << population_identity(final_population)
               << ",\"final_population_validation\":{\"members\":"
               << final_population.size()
               << ",\"native_membership_lowering\":true,\"budget\":\"initial-including-grandfathered-parents\"}";
      }
      output << '}';
    }
    write_result(options, output.str());
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "final candidate benchmark: " << error.what() << '\n';
    return 2;
  }
}
