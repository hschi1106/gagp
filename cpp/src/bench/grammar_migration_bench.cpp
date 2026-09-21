#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "migration_snapshot.hpp"
#include "migration_reproduction.hpp"
#include "migration_reproduction_bench.hpp"
#include "gagp/cli/commands.hpp"
#include "gagp/cli/grammar_population_artifact.hpp"
#include "gagp/cli/options.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/population_init.hpp"
#include "gagp/core/bytecode_verify.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#ifdef GAGP_HAS_CUDA
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#endif

namespace {
using namespace gagp;
using namespace gagp::evo;
using namespace gagp::cli_detail;
using Clock = std::chrono::steady_clock;

JsonValue read_json(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot read " + path);
  std::ostringstream text;
  text << in.rdbuf();
  return JsonParser(text.str()).parse();
}

void write_file(const std::string& path, const std::string& text) {
  std::ofstream out(path);
  if (!out || !(out << text << '\n')) throw std::runtime_error("cannot write " + path);
}

double elapsed(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

int positive_count(const std::string& text) {
  std::size_t consumed = 0;
  const int value = std::stoi(text, &consumed);
  if (consumed != text.size() || value <= 0) throw std::runtime_error("trial count must be a positive integer");
  return value;
}

bool same_request(const grammar::GenerationRequest& left,
                  const grammar::GenerationRequest& right) {
  if (left.nonterminal != right.nonterminal || left.type != right.type ||
      left.budget.max_nodes != right.budget.max_nodes ||
      left.budget.max_depth != right.budget.max_depth ||
      left.visible_environment.size() != right.visible_environment.size()) {
    return false;
  }
  for (std::size_t i = 0; i < left.visible_environment.size(); ++i) {
    if (left.visible_environment[i].name != right.visible_environment[i].name ||
        left.visible_environment[i].type != right.visible_environment[i].type) {
      return false;
    }
  }
  return true;
}

void evaluation_timing(std::ostream& out, const EvaluationTiming& t);

void steady_evaluation(std::ostream& out, const std::vector<ProgramGenome>& population,
                       const CaseSet& cases, const EvolutionConfig& cfg, int warmups, int trials,
                       const std::vector<BytecodeProgram>* frozen_bytecode = nullptr) {
  if (cfg.reproduction_backend != repro::ReproductionBackend::Cpu || cfg.repro_overlap ||
      cfg.cpu_repro_ablation != repro::CpuReproAblation::None) {
    throw std::runtime_error("steady evaluation does not execute reproduction; use CPU reproduction with overlap off");
  }
  const auto compile_start = Clock::now();
  std::vector<BytecodeProgram> programs;
  if (frozen_bytecode) programs = *frozen_bytecode;
  else for (const auto& genome : population) programs.push_back(compile_for_eval(genome, cases.input_names));
  const double compile_ms = frozen_bytecode ? 0.0 : elapsed(compile_start);
  double init_ms = 0;
#ifdef GAGP_HAS_CUDA
  FitnessSessionGpu session;
  if (cfg.eval_engine == EvalEngine::GPU) {
    const auto init = session.init(cases.bindings, cases.expected_values, cfg.fuel, cfg.gpu_blocksize, cfg.penalty);
    if (!init.ok) throw std::runtime_error("steady GPU init failed: " + init.err.message);
    init_ms = init.timing.total_ms;
  }
#else
  if (cfg.eval_engine == EvalEngine::GPU) throw std::runtime_error("steady GPU evaluation requires CUDA");
#endif
  out << "{\"format_version\":\"migration-steady-eval-v1\",\"engine\":\""
      << (cfg.eval_engine == EvalEngine::GPU ? "gpu" : "cpu") << "\",\"compile_ms\":" << compile_ms
      << ",\"session_init_ms\":" << init_ms << ",\"warmups\":" << warmups
      << ",\"measured_trials\":" << trials << ",\"samples\":[";
  std::vector<double> reference;
  for (int i = 0; i < warmups + trials; ++i) {
    std::vector<double> scores;
    EvaluationTiming timing;
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
    if (scores.size() != programs.size()) throw std::runtime_error("steady evaluation result count changed");
    if (i == 0) reference = scores;
    else if (scores != reference) throw std::runtime_error("steady evaluation changed frozen-work fitness");
    if (i) out << ',';
    out << "{\"index\":" << i << ",\"warmup\":" << (i < warmups ? "true" : "false")
        << ",\"call_ms\":" << call_ms << ",\"phases\":";
    evaluation_timing(out, timing);
    out << '}';
  }
  out << "],\"fitness\":[";
  for (std::size_t i = 0; i < reference.size(); ++i) {
    if (i) out << ',';
    out << reference[i];
  }
  out << "]}";
}

void evaluation_timing(std::ostream& out, const EvaluationTiming& t) {
  out << "{\"cpu_compile_ms\":" << t.cpu_compile_ms
      << ",\"gpu_compile_ms\":" << t.gpu_compile_ms
      << ",\"gpu_eval_call_ms\":" << t.gpu_eval_call_ms
      << ",\"gpu_eval_pack_ms\":" << t.gpu_eval_pack_ms
      << ",\"gpu_eval_launch_prep_ms\":" << t.gpu_eval_launch_prep_ms
      << ",\"gpu_eval_upload_ms\":" << t.gpu_eval_upload_ms
      << ",\"gpu_eval_kernel_ms\":" << t.gpu_eval_kernel_ms
      << ",\"gpu_eval_copyback_ms\":" << t.gpu_eval_copyback_ms
      << ",\"gpu_eval_teardown_ms\":" << t.gpu_eval_teardown_ms << '}';
}

void reproduction_timing(std::ostream& out, const ReproductionTiming& t) {
  out << "{\"selection_ms\":" << t.selection_ms << ",\"crossover_ms\":" << t.crossover_ms
      << ",\"mutation_ms\":" << t.mutation_ms << ",\"prepare_inputs_ms\":" << t.prepare_inputs_ms
      << ",\"setup_ms\":" << t.setup_ms << ",\"preprocess_ms\":" << t.preprocess_ms
      << ",\"pack_ms\":" << t.pack_ms << ",\"upload_ms\":" << t.upload_ms
      << ",\"kernel_ms\":" << t.kernel_ms << ",\"copyback_ms\":" << t.copyback_ms
      << ",\"decode_ms\":" << t.decode_ms << ",\"teardown_ms\":" << t.teardown_ms
      << ",\"selection_kernel_ms\":" << t.selection_kernel_ms
      << ",\"variation_kernel_ms\":" << t.variation_kernel_ms
      << ",\"crossover_attempts\":" << t.variation.crossover_attempts
      << ",\"mutation_attempts\":" << t.variation.mutation_attempts
      << ",\"contract_rejections\":" << t.variation.contract_rejections
      << ",\"budget_rejections\":" << t.variation.budget_rejections
      << ",\"generation_rejections\":" << t.variation.generation_rejections
      << ",\"acceptance_rejections\":" << t.variation.acceptance_rejections
      << ",\"fallback_children\":" << t.variation.fallback_children
      << ",\"unchanged_children\":" << t.variation.unchanged_children
      << ",\"changed_children\":" << t.variation.changed_children << '}';
}

void emit_result(std::ostream& out, const ExecResult& result) {
  out << "{\"error\":";
  if (result.is_error) out << '"' << err_code_name(result.err.code) << '"';
  else out << "null,\"value\":" << migration::encode_value(result.value);
  out << '}';
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const auto process_start = Clock::now();
    std::string action, snapshot, ast_path, grammar_definition_path;
    int warmups = 3, trials = 15;
    bool explicit_trial_counts = false;
    std::vector<char*> common{argv[0]};
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--trials" || arg == "--warmups") {
        explicit_trial_counts = true;
        if (++i >= argc) throw std::runtime_error("missing trial count");
        (arg == "--trials" ? trials : warmups) = positive_count(argv[i]);
      } else if (arg == "--action" || arg == "--snapshot" || arg == "--source-ast" ||
                 arg == "--grammar-definition") {
        if (++i >= argc) throw std::runtime_error("missing adapter option value");
        if (arg == "--action") action = argv[i];
        else if (arg == "--snapshot") snapshot = argv[i];
        else if (arg == "--source-ast") ast_path = argv[i];
        else {
          if (!grammar_definition_path.empty())
            throw std::runtime_error("duplicate --grammar-definition");
          grammar_definition_path = argv[i];
        }
      } else {
        common.push_back(argv[i]);
      }
    }
    if ((action != "freeze" && action != "freeze-repro" && action != "repro-check" && action != "repro-steady" && action != "run" && action != "oracle" && action != "steady") || snapshot.empty()) {
      throw std::runtime_error("require --action freeze|freeze-repro|repro-check|repro-steady|run|oracle|steady and --snapshot PATH; remaining options follow evolve CLI");
    }
    if (warmups < 3 || trials < 15 || warmups > 1000 || trials > 10000) {
      throw std::runtime_error("require 3..1000 warmups and 15..10000 measured trials");
    }
    if (explicit_trial_counts && action != "steady" && action != "repro-steady") {
      throw std::runtime_error("--warmups and --trials only apply to steady measurement actions");
    }
    const bool compiled_mode = !grammar_definition_path.empty();
    if (compiled_mode && action != "run" && action != "oracle" && action != "steady") {
      throw std::runtime_error(
          "--grammar-definition supports only run, oracle and steady actions; compiled freeze and reproduction snapshots are unsupported");
    }
    const CliOptions opts = parse_cli_options(static_cast<int>(common.size()), common.data());
    if (!opts.population_json.empty() || !opts.eval_ast_json.empty()) {
      throw std::runtime_error("adapter uses --snapshot and --source-ast, not seed replay or --eval-ast-json");
    }
    if (action == "repro-check") {
      if (opts.out_json.empty()) throw std::runtime_error("repro-check requires --out-json");
      const auto restored = migration::decode_reproduction(read_json(snapshot));
      std::ostringstream encoded;
      migration::encode_reproduction(encoded, restored.parents, restored.fitness, restored.config, restored.prep);
      write_file(opts.out_json, encoded.str());
      return 0;
    }
    EvolutionConfig cfg;
    cfg.population_size = opts.population_size;
    cfg.generations = opts.generations;
    cfg.seed = opts.seed;
    cfg.fuel = opts.fuel;
    cfg.penalty = opts.penalty;
    cfg.gpu_blocksize = opts.blocksize;
    cfg.eval_engine = opts.engine == "gpu" ? EvalEngine::GPU : EvalEngine::CPU;
    cfg.reproduction_backend = repro::parse_reproduction_backend_name(opts.repro_backend);
    cfg.cpu_repro_ablation = repro::parse_cpu_repro_ablation_name(opts.cpu_repro_ablation);
    cfg.repro_overlap = opts.repro_overlap;
    cfg.selection_pressure = opts.selection_pressure;
    cfg.mutation_rate = opts.mutation_rate;
    cfg.mutation_subtree_prob = opts.mutation_subtree_prob;
    cfg.skip_final_eval = opts.skip_final_eval;
    cfg.retain_final_population = opts.retain_final_population;
    cfg.limits = Limits{opts.max_expr_depth, opts.max_stmts_per_block, opts.max_total_nodes,
                        opts.max_for_k, opts.max_call_args};
    if (compiled_mode && !opts.grammar_config_path.empty())
      throw std::runtime_error("--grammar-definition cannot be combined with --grammar-config");
    if (compiled_mode) {
      cfg.compiled_grammar = std::make_shared<const grammar::CompiledGrammar>(
          grammar::compile_grammar(grammar::load_definition(grammar_definition_path)));
      cfg.generation_request = grammar::entry_request(*cfg.compiled_grammar);
      if (cfg.fuel <= 0 || static_cast<std::uint32_t>(cfg.fuel) !=
                               cfg.compiled_grammar->execution_limits().fuel) {
        throw std::runtime_error(
            "--fuel must match the compiled grammar execution limit");
      }
    }
    if (!opts.grammar_config_path.empty()) cfg.grammar = decode_grammar_config_json(read_json(opts.grammar_config_path));
    std::vector<EvalCase> cases;
    CaseSet case_set;
    const auto cases_json = read_json(opts.cases_path);
    const auto case_format = require_string(require_object_field(cases_json, "format_version"), "format_version");
    const bool frozen_cases = case_format == "migration-evaluation-cases-v1";
    if (frozen_cases) {
      if (compiled_mode)
        throw std::runtime_error("compiled grammar benchmarks require fitness-cases input");
      if (action != "steady") throw std::runtime_error("raw binding snapshots are only supported by steady evaluation");
      const auto& rows = require_object_field(cases_json, "cases");
      if (rows.kind != JsonValue::Kind::Array || rows.array_v.empty()) throw std::runtime_error("empty raw case snapshot");
      for (const auto& row : rows.array_v) {
        CaseBindings bindings;
        const auto& inputs = require_object_field(row, "inputs");
        if (inputs.kind != JsonValue::Kind::Array) throw std::runtime_error("raw bindings must be an array");
        for (const auto& input : inputs.array_v) {
          if (input.kind != JsonValue::Kind::Array || input.array_v.size() != 2) throw std::runtime_error("invalid raw binding");
          bindings.push_back({require_int(input.array_v[0], "input index"), migration::decode_value(input.array_v[1])});
        }
        case_set.bindings.push_back(std::move(bindings));
        case_set.expected_values.push_back(migration::decode_value(require_object_field(row, "expected")));
      }
    } else {
      cases = decode_fitness_cases_json(cases_json);
      case_set = prepare_case_set(cases, cfg.grammar);
    }
    std::vector<ProgramGenome> population;
    if (action == "repro-steady") {
      if (opts.out_json.empty()) throw std::runtime_error("repro-steady requires --out-json");
      cfg.verification_inputs = case_set.input_specs;
      const auto restored = migration::decode_reproduction(read_json(snapshot));
      std::ostringstream measured;
      migration::steady_reproduction(measured, restored, cfg, warmups, trials);
      write_file(opts.out_json, measured.str());
      return 0;
    }
    std::vector<BytecodeProgram> frozen_bytecode;
    if (action == "freeze") {
      if (ast_path.empty()) population = initialize_population(cfg, case_set).population;
      else {
        ProgramGenome genome;
        genome.ast = decode_ast_json(read_json(ast_path));
        const auto verified = verify_ast(genome.ast, case_set.input_specs);
        if (!verified.ok) throw std::runtime_error("source AST rejected before metadata analysis");
        genome.meta = build_genome_meta(genome.ast);
        population.assign(static_cast<std::size_t>(cfg.population_size), genome);
      }
    } else {
      if (!ast_path.empty()) throw std::runtime_error("--source-ast only applies to freeze");
      const auto raw = read_json(snapshot);
      const auto format = require_string(require_object_field(raw, "format_version"), "format_version");
      const bool compiled_snapshot = format == kGeneratedGrammarPopulationArtifactVersion;
      if (compiled_snapshot != compiled_mode) {
        throw std::runtime_error(compiled_snapshot
            ? "grammar-population-v1 requires --grammar-definition"
            : "--grammar-definition requires a grammar-population-v1 snapshot");
      }
      if (compiled_snapshot) {
        population = replay_generated_population_artifact(
            grammar::canonical_json(raw), cfg.compiled_grammar.get());
        if (!population.front().derivation)
          throw std::runtime_error("grammar population member lacks a recorded request");
        cfg.generation_request = population.front().derivation->request;
        for (const auto& genome : population) {
          if (!genome.derivation ||
              !same_request(genome.derivation->request, *cfg.generation_request)) {
            throw std::runtime_error(
                "grammar population members do not share one exact generation request");
          }
        }
        validate_grammar_case_set(*cfg.compiled_grammar, case_set,
                                  *cfg.generation_request);
        for (const auto& one : cases)
          if (one.inputs.size() != cfg.compiled_grammar->inputs().size())
            throw std::runtime_error(
                "every fitness case must supply every compiled grammar input");
      } else if (format == "migration-bytecode-population-v1") {
        if (action != "steady") throw std::runtime_error("bytecode snapshots only support steady evaluation");
        const auto& programs = require_object_field(raw, "programs");
        if (programs.kind != JsonValue::Kind::Array || programs.array_v.empty()) throw std::runtime_error("empty bytecode population");
        for (const auto& program : programs.array_v) {
          auto decoded = migration::decode_bytecode(program);
          const auto checked = verify_bytecode(decoded);
          if (!checked.ok) throw std::runtime_error("steady bytecode snapshot failed verification");
          frozen_bytecode.push_back(std::move(decoded));
        }
      } else {
        if (frozen_cases) throw std::runtime_error("raw case bindings require a bytecode population snapshot");
        population = migration::decode_population(raw);
      }
      if (population.size() + frozen_bytecode.size() != static_cast<std::size_t>(cfg.population_size)) {
        throw std::runtime_error("snapshot size differs from --population-size");
      }
    }
    for (const auto& genome : population) {
      const auto verified = verify_ast(genome.ast, case_set.input_specs);
      if (!verified.ok) throw std::runtime_error(std::string("snapshot AST rejected: ") +
                                                 verify_code_name(verified.diagnostic.code));
      if (compiled_mode)
        grammar::require_membership(*cfg.compiled_grammar, genome, *cfg.generation_request);
    }
    if (action == "freeze") {
      write_file(snapshot, migration::encode_population(population));
      std::cout << "frozen " << population.size() << " materialized programs\n";
      return 0;
    }
    if (opts.out_json.empty()) throw std::runtime_error("run/oracle/steady require --out-json");
    std::ostringstream out;
    out << std::setprecision(17);
    if (action == "freeze-repro") {
      migration::freeze_reproduction(out, population, case_set, cfg);
    } else if (action == "steady") {
      steady_evaluation(out, population, case_set, cfg, warmups, trials,
                         frozen_bytecode.empty() ? nullptr : &frozen_bytecode);
    } else if (action == "oracle") {
      std::vector<BytecodeProgram> programs;
      for (const auto& genome : population) programs.push_back(compile_for_eval(genome, case_set.input_names));
      const auto cpu_fitness = eval_fitness_cpu(programs, case_set.bindings, case_set.expected_values,
                                               cfg.fuel, cfg.penalty, cfg.gpu_blocksize);
      auto fitness = cpu_fitness;
      if (cfg.eval_engine == EvalEngine::GPU) {
#ifdef GAGP_HAS_CUDA
        FitnessSessionGpu session;
        const auto init = session.init(case_set.bindings, case_set.expected_values, cfg.fuel,
                                       cfg.gpu_blocksize, cfg.penalty);
        if (!init.ok) throw std::runtime_error("GPU oracle initialization failed: " + init.err.message);
        const auto evaluated = session.eval_programs(programs);
        if (!evaluated.ok) throw std::runtime_error("GPU oracle evaluation failed: " + evaluated.err.message);
        fitness = evaluated.fitness;
#else
        throw std::runtime_error("GPU oracle requested without CUDA");
#endif
      }
      // The legacy bytecode snapshot codec predates generic regions and semantic
      // fuel schedules. Compiled oracles retain the complete native AST instead
      // of publishing a bytecode object with those execution fields missing.
      if (compiled_mode)
        out << "{\"format_version\":\"grammar-oracle-v1\",\"grammar_hash\":\""
            << cfg.compiled_grammar->content_hash() << "\",\"programs\":[";
      else
        out << "{\"format_version\":\"migration-oracle-v1\",\"programs\":[";
      for (std::size_t p = 0; p < population.size(); ++p) {
        if (p) out << ',';
        const auto& bytecode = programs[p];
        if (compiled_mode)
          out << "{\"ast\":" << encode_ast_json(population[p].ast);
        else
          out << "{\"bytecode\":" << migration::encode_bytecode(bytecode);
        out << ",\"cpu_fitness\":" << cpu_fitness[p] << ",\"fitness\":" << fitness[p]
            << ",\"cases\":[";
        for (std::size_t c = 0; c < case_set.bindings.size(); ++c) {
          if (c) out << ',';
          std::vector<std::pair<int, Value>> inputs;
          for (const auto& binding : case_set.bindings[c]) inputs.emplace_back(binding.idx, binding.value);
          const auto result = execute_bytecode_cpu(bytecode, inputs, cfg.fuel);
          out << "{\"at_fuel_limit\":";
          emit_result(out, result);
          out << ",\"first_non_timeout_fuel\":";
          if (result.is_error && result.err.code == ErrCode::Timeout) out << "null";
          else {
            int lo = 0, hi = cfg.fuel;
            while (lo < hi) {
              const int mid = lo + (hi - lo) / 2;
              const auto probe = execute_bytecode_cpu(bytecode, inputs, mid);
              if (probe.is_error && probe.err.code == ErrCode::Timeout) lo = mid + 1;
              else hi = mid;
            }
            out << lo << ",\"at_boundary\":";
            emit_result(out, execute_bytecode_cpu(bytecode, inputs, lo));
            if (lo > 0) {
              out << ",\"below_boundary\":";
              emit_result(out, execute_bytecode_cpu(bytecode, inputs, lo - 1));
            }
          }
          out << '}';
        }
        out << "]}";
      }
      out << "]}";
    } else {
      const double load_ms = elapsed(process_start);
      const auto start = Clock::now();
      const auto result = evolve_population(cases, cfg, &population);
      const double call_ms = elapsed(start);
      const auto& timing = result.timing;
      out << "{\"format_version\":\"migration-run-v1\",\"load_ms\":" << load_ms
          << ",\"evolve_call_ms\":" << call_ms << ",\"init_population_ms\":" << timing.init_population_ms
          << ",\"gpu_eval_init_ms\":" << timing.gpu_eval_init_ms
          << ",\"final_eval_ms\":" << timing.final_eval_ms << ",\"total_ms\":" << timing.total_ms
          << ",\"generations\":[";
      for (std::size_t i = 0; i < timing.generations.size(); ++i) {
        if (i) out << ',';
        const auto& t = timing.generations[i];
        out << "{\"eval_ms\":" << t.eval_ms << ",\"repro_ms\":" << t.repro_ms
            << ",\"total_ms\":" << t.total_ms << ",\"evaluation\":";
        evaluation_timing(out, t.evaluation);
        out << ",\"reproduction\":";
        reproduction_timing(out, t.reproduction);
        out << ",\"best_fitness\":" << result.history_best_fitness[i]
            << ",\"mean_fitness\":" << result.history_mean_fitness[i]
            << ",\"best_nodes\":" << result.history_best[i].genome.meta.node_count << '}';
      }
      out << "]}";
    }
    write_file(opts.out_json, out.str());
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "migration benchmark: " << error.what() << '\n';
    return 1;
  }
}
