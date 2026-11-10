// `gagp run` and `gagp eval`: search a task and score a program.
#include "gagp/cli/run.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>

#include "gagp/cli/grammar_population_artifact.hpp"
#include "gagp/cli/json.hpp"
#include "gagp/cli/program_json.hpp"
#include "gagp/cli/search_config.hpp"
#include "gagp/cli/task.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/runtime/cpu/execution_session.hpp"
#include "gagp/runtime/payload/payload.hpp"
#include "../runtime/payload/staging.hpp"

namespace gagp::cli_detail {
namespace {

namespace gg = evo::grammar;
using Json = JsonValue;

// The research configuration (docs/design/research-mainline.md): resident
// phase population, steady state with the whole population as batch.
constexpr const char* kDefaultSearch = R"({
  "format": "gagp-search-config-v1",
  "representation": "phase_population",
  "population_size": 1024,
  "generations": 64,
  "selection": {"method": "tournament", "size": 2},
  "elitism": 1,
  "crossover": {"probability": 0.7, "attempts": 8},
  "mutation": {"probability": 0.3},
  "sites": {"scope": "uniform_phase", "draw": "counted"},
  "limits": {"site_depth": 7},
  "ranking": {"tie_break": "ast_nodes"},
  "schedule": {"mode": "steady_state", "replacement_size": 2}
})";
// The same search for the native AST representation, which has no phase
// sites, site-depth limit or phase schedule (generational).
constexpr const char* kDefaultNativeSearch = R"({
  "format": "gagp-search-config-v1",
  "representation": "native_ast",
  "population_size": 1024,
  "generations": 64,
  "selection": {"method": "tournament", "size": 2},
  "elitism": 1,
  "mutation": {"probability": 0.3}
})";
// Both engines reduce case scores in the same lane order; this keeps the GPU
// and CPU reference results bit-identical.
constexpr int kReductionLanes = 512;

Json str(std::string v) { Json j; j.kind = Json::Kind::String; j.string_v = std::move(v); return j; }
Json num(double v) { Json j; j.kind = Json::Kind::Number; j.number_v = v; return j; }
Json boolean(bool v) { Json j; j.kind = Json::Kind::Bool; j.bool_v = v; return j; }
Json arr(std::vector<Json> v) { Json j; j.kind = Json::Kind::Array; j.array_v = std::move(v); return j; }
Json obj(std::initializer_list<std::pair<const std::string, Json>> v) {
  Json j; j.kind = Json::Kind::Object; j.object_v = v; return j;
}

std::string read_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot read " + path);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}
void write_json(const std::filesystem::path& path, const Json& value) {
  std::ofstream out(path);
  out << gg::canonical_json(value) << '\n';
  if (!out) throw std::runtime_error("cannot write " + path.string());
}
Json program_json(const evo::AstProgram& ast) {
  return JsonParser(encode_ast_json(ast), {true, 512}).parse();
}

[[noreturn]] void usage_error(const std::string& message) {
  throw std::invalid_argument(message + "\n" + run_usage());
}

struct RunArgs {
  std::string task_path;
  std::string search_path;
  std::string init_path;
  std::string out_dir;
  std::string engine = "gpu";
  std::optional<std::uint64_t> seed;
  std::optional<int> population, generations;
  bool save_all = false;
  bool quiet = false;
};

RunArgs parse_run_args(const std::vector<std::string>& args) {
  RunArgs out;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const auto& arg = args[i];
    const auto value = [&]() -> const std::string& {
      if (i + 1 >= args.size()) usage_error(arg + " requires a value");
      return args[++i];
    };
    const auto integer = [&](int minimum) {
      const auto& text = value();
      std::size_t used = 0;
      long long parsed = 0;
      try { parsed = std::stoll(text, &used); } catch (const std::exception&) { used = 0; }
      if (used != text.size() || parsed < minimum || parsed > (1LL << 30)) usage_error(arg + " needs an integer >= " + std::to_string(minimum));
      return static_cast<int>(parsed);
    };
    if (arg == "--search") out.search_path = value();
    else if (arg == "--init") out.init_path = value();
    else if (arg == "--out") out.out_dir = value();
    else if (arg == "--engine") out.engine = value();
    else if (arg == "--seed") {
      const auto& text = value();
      if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) usage_error("--seed needs a nonnegative integer");
      out.seed = std::stoull(text);
    }
    else if (arg == "--population") out.population = integer(2);
    else if (arg == "--generations") out.generations = integer(1);
    else if (arg == "--save-all") out.save_all = true;
    else if (arg == "--quiet") out.quiet = true;
    else if (!arg.empty() && arg[0] == '-') usage_error("unknown option " + arg);
    else if (out.task_path.empty()) out.task_path = arg;
    else usage_error("unexpected argument " + arg);
  }
  if (out.task_path.empty()) usage_error("missing TASK");
  if (out.engine != "gpu" && out.engine != "cpu") usage_error("--engine must be gpu or cpu");
  return out;
}

// Reads the first `count` programs of a population: a gagp-population-v1
// document (`gagp run --save-all`) or a grammar-population-v2 document
// (`gagp grammar generate`).
std::vector<evo::ProgramGenome> load_population(const std::string& path, const gg::CompiledGrammar& grammar,
                                                std::size_t count) {
  const std::string text = read_file(path);
  const Json doc = JsonParser(text, {true, 512}).parse();
  std::vector<evo::ProgramGenome> population;
  if (const auto format = doc.object_v.find("format_version"); format != doc.object_v.end() &&
      format->second.string_v == kGeneratedGrammarPopulationArtifactVersion) {
    population = replay_generated_population_artifact(text, &grammar);
  } else {
    if (doc.kind != Json::Kind::Object || !doc.object_v.count("programs"))
      throw std::invalid_argument(path + ": expected a population document with a programs array");
    if (const auto it = doc.object_v.find("grammar_hash"); it != doc.object_v.end() &&
        it->second.string_v != grammar.content_hash())
      throw std::invalid_argument(path + ": population was made for a different grammar");
    const auto& programs = doc.object_v.at("programs").array_v;
    for (std::size_t i = 0; i < std::min(count, programs.size()); ++i) {
      evo::ProgramGenome g;
      g.ast = decode_ast_json(programs[i]);
      g.meta = evo::build_genome_meta(g.ast);
      g.derivation = std::make_shared<gg::DerivationMetadata>(gg::reconstruct_derivation(grammar, g));
      population.push_back(std::move(g));
    }
  }
  if (population.size() < count)
    throw std::invalid_argument(path + ": has " + std::to_string(population.size()) +
                                " programs, the run needs " + std::to_string(count));
  population.resize(count);
  return population;
}

// Per-case judgement of one program (the success criterion of a run).
struct CaseVerdict { bool correct = false, error = false; double loss = 0; };

CaseVerdict judge_case(const ExecResult& result, const Value& expected, const Task& task) {
  CaseVerdict v;
  v.loss = task.fitness.metric == FitnessMetric::Absolute ? task.case_cap : 1;
  if (result.is_error) { v.error = true; return v; }
  const Value& got = result.value;
  if (expected.tag == ValueTag::IntList) {
    std::vector<Value> want, have;
    payload::lookup_list(expected, &want);
    if (got.tag != ValueTag::IntList || !payload::lookup_list(got, &have)) { v.error = true; return v; }
    if (task.fitness.metric == FitnessMetric::TupleSummary) {
      // The answer is the first element; the summaries only guide the search.
      v.correct = have.size() == want.size() && !have.empty() && have[0].tag == ValueTag::Int && have[0].i == want[0].i;
    } else {
      v.correct = have.size() == want.size();
      for (std::size_t j = 0; v.correct && j < have.size(); ++j)
        v.correct = have[j].tag == ValueTag::Int && have[j].i == want[j].i;
    }
  } else if (expected.tag == ValueTag::Int) {
    if (got.tag != ValueTag::Int) { v.error = true; return v; }
    v.correct = got.i == expected.i;
    if (task.fitness.metric == FitnessMetric::Absolute)
      v.loss = std::min(task.case_cap, fitness_integer_gap(got.i, expected.i));
  } else {
    v.correct = fitness_case_score(got, expected, 1, FitnessPolicy{FitnessMetric::Exact}) == 0;
  }
  if (v.correct) v.loss = 0;
  return v;
}

Json judge(const evo::ProgramGenome& genome, const std::vector<evo::EvalCase>& cases, const Task& task, int fuel) {
  const auto set = evo::prepare_case_set(cases);
  CpuExecutionSession session(evo::compile_for_eval(genome, set.input_names));
  std::size_t correct = 0, errors = 0;
  double loss = 0;
  for (std::size_t i = 0; i < cases.size(); ++i) {
    CaseVerdict v;
    try {
      payload::StagedPayloads payloads(8u << 20);
      payload::StagedPayloads::Scope scope(payloads);
      std::size_t input_cells = 0;
      std::vector<std::pair<int, Value>> inputs;
      for (const auto& b : set.bindings[i]) {
        inputs.emplace_back(b.idx, b.value);
        std::vector<Value> cells;
        if (b.value.tag == ValueTag::IntList && payload::lookup_list(b.value, &cells)) input_cells += cells.size();
      }
      payload::ListAllocationScope budget(task.fitness.list_allocation_limit, input_cells);
      v = judge_case(session.execute(inputs, fuel), set.expected_values[i], task);
    } catch (const payload::PayloadBudgetExceeded&) {
      v.error = true;
      v.loss = task.fitness.metric == FitnessMetric::Absolute ? task.case_cap : 1;
    }
    correct += v.correct;
    errors += v.error;
    loss += v.loss;
  }
  const double n = static_cast<double>(cases.size());
  return obj({{"cases", num(n)}, {"correct", num(correct)}, {"wrong", num(n - correct)},
              {"runtime_errors", num(errors)}, {"mean_loss", num(loss / n)},
              {"solved", boolean(correct == cases.size())}});
}

std::shared_ptr<const gg::CompiledGrammar> load_grammar(const std::string& path) {
  return std::make_shared<const gg::CompiledGrammar>(gg::compile_grammar(gg::load_definition(path)));
}

}  // namespace

std::string run_usage() {
  return "usage: gagp run TASK.json [--seed N] [--population N] [--generations N]\n"
         "                [--engine gpu|cpu] [--search SEARCH.json] [--init POPULATION.json]\n"
         "                [--out DIR] [--save-all] [--quiet]\n"
         "       gagp eval TASK.json PROGRAM.json [--split train|test]";
}

int run_task_command(const std::vector<std::string>& argv) {
  const RunArgs args = parse_run_args(argv);
  const Task task = load_task(args.task_path);
  const auto grammar = load_grammar(task.grammar_path);
  const auto cases = load_fitness_cases(task.train_path);

  evo::EvolutionConfig cfg;
  cfg.eval_engine = args.engine == "gpu" ? evo::EvalEngine::GPU : evo::EvalEngine::CPU;
  cfg.reproduction_backend = args.engine == "gpu" ? evo::repro::ReproductionBackend::Gpu
                                                  : evo::repro::ReproductionBackend::Cpu;
  cfg.repro_overlap = args.engine == "gpu";
  cfg.gpu_blocksize = kReductionLanes;
  cfg.compiled_grammar = grammar;
  cfg.generation_request = gg::entry_request(*grammar);
  cfg.fuel = static_cast<int>(grammar->execution_limits().fuel);
  cfg.penalty = task.case_cap;
  cfg.fitness_policy = task.fitness;
  cfg.selection_pressure = 2;
  cfg.elite_count = 1;
  cfg.mutation_rate = .3;
  cfg.mutation_subtree_prob = 1;
  cfg.skip_final_eval = false;
  cfg.retain_final_population = true;
  const auto search = args.search_path.empty() ? SearchConfig{} : load_search_config(args.search_path);
  const bool native = search.phase_population == false;
  apply_search_config(parse_search_config(JsonParser(native ? kDefaultNativeSearch : kDefaultSearch).parse()), cfg);
  apply_search_config(search, cfg);
  if (args.population) cfg.population_size = *args.population;
  if (args.generations) cfg.generations = *args.generations;
  if (args.seed) cfg.seed = *args.seed;

  std::vector<evo::ProgramGenome> initial;
  if (!args.init_path.empty())
    initial = load_population(args.init_path, *grammar, static_cast<std::size_t>(cfg.population_size));

  if (!args.quiet)
    std::cerr << "gagp run " << task.name << ": engine=" << args.engine << " population=" << cfg.population_size
              << " generations=" << cfg.generations << " seed=" << cfg.seed << " cases=" << cases.size() << '\n';
  const auto t0 = std::chrono::steady_clock::now();
  const auto result = evo::evolve_population(cases, cfg, args.init_path.empty() ? nullptr : &initial);
  const double wall_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

  const auto& best = result.final_population.front();
  std::vector<Json> history;
  for (std::size_t i = 0; i < result.history_best.size(); ++i) {
    const auto& t = result.timing.generations[i];
    history.push_back(obj({{"generation", num(i)}, {"best_fitness", num(result.history_best_fitness[i])},
        {"mean_fitness", num(result.history_mean_fitness[i])}, {"best_wrong", num(result.history_best[i].wrong)},
        {"best_nodes", num(result.history_best[i].genome.ast.nodes.size())},
        {"elapsed_ms", num(t.elapsed_search_ms)}, {"eval_ms", num(t.eval_ms)}, {"repro_ms", num(t.repro_ms)}}));
  }
  Json summary = obj({{"format", str("gagp-run-v1")}, {"task", str(task.name)}, {"engine", str(args.engine)},
      {"population", num(cfg.population_size)}, {"generations", num(cfg.generations)},
      {"seed", str(std::to_string(cfg.seed))}, {"train_cases", num(cases.size())},
      {"search", effective_search_config(cfg)},
      {"best", obj({{"fitness", num(best.fitness)}, {"wrong", num(best.wrong)},
                    {"nodes", num(best.genome.ast.nodes.size())}, {"program", program_json(best.genome.ast)}})},
      {"history", arr(std::move(history))},
      {"time_ms", obj({{"total", num(wall_ms)}, {"search", num(result.timing.total_ms)},
                       {"initialization", num(result.timing.init_population_ms)}})}});
  if (!task.test_path.empty())
    summary.object_v["test"] = judge(best.genome, load_fitness_cases(task.test_path), task, cfg.fuel);

  if (!args.quiet) {
    std::fprintf(stderr, "best fitness %.6g, wrong %d of %zu train cases, %zu nodes; %.1f s\n", best.fitness, best.wrong,
                 cases.size(), best.genome.ast.nodes.size(), wall_ms / 1000);
    if (summary.object_v.count("test")) {
      const auto& t = summary.object_v.at("test").object_v;
      std::fprintf(stderr, "test: %g of %g correct%s\n", t.at("correct").number_v, t.at("cases").number_v,
                   t.at("solved").bool_v ? " (solved)" : "");
    }
  }
  if (!args.out_dir.empty()) {
    const std::filesystem::path out(args.out_dir);
    std::filesystem::create_directories(out);
    write_json(out / "result.json", summary);
    if (args.save_all) {
      std::vector<Json> programs, lineage;
      for (const auto& one : result.final_population) programs.push_back(program_json(one.genome.ast));
      for (std::size_t i = 0; i < result.history_best.size(); ++i)
        lineage.push_back(obj({{"generation", num(i)}, {"fitness", num(result.history_best[i].fitness)},
                               {"program", program_json(result.history_best[i].genome.ast)}}));
      write_json(out / "population.json", obj({{"format", str("gagp-population-v1")},
          {"grammar_hash", str(grammar->content_hash())}, {"programs", arr(std::move(programs))}}));
      write_json(out / "history.json", obj({{"history", arr(std::move(lineage))}}));
    }
  }
  return 0;
}

int eval_program_command(const std::vector<std::string>& argv) {
  std::vector<std::string> positional;
  std::string split = "test";
  for (std::size_t i = 0; i < argv.size(); ++i) {
    if (argv[i] == "--split") {
      if (i + 1 >= argv.size()) usage_error("--split requires a value");
      split = argv[++i];
    } else if (!argv[i].empty() && argv[i][0] == '-') usage_error("unknown option " + argv[i]);
    else positional.push_back(argv[i]);
  }
  if (positional.size() != 2) usage_error("eval needs TASK.json and PROGRAM.json");
  if (split != "train" && split != "test") usage_error("--split must be train or test");
  const Task task = load_task(positional[0]);
  const auto& cases_path = split == "train" ? task.train_path : task.test_path;
  if (cases_path.empty()) throw std::invalid_argument("task has no " + split + " split");
  const auto grammar = load_grammar(task.grammar_path);
  // Accepts a gagp-run-v1 result (its best program), a population (its first
  // program) or a bare program.
  const Json doc = JsonParser(read_file(positional[1]), {true, 512}).parse();
  const Json* program = &doc;
  if (doc.object_v.count("best")) program = &doc.object_v.at("best").object_v.at("program");
  else if (doc.object_v.count("programs")) program = &doc.object_v.at("programs").array_v.at(0);
  const auto cases = load_fitness_cases(cases_path);
  evo::ProgramGenome genome;
  genome.ast = decode_ast_json(*program);
  if (const auto verified = evo::verify_ast(genome.ast, evo::canonical_input_specs(cases)); !verified)
    throw std::invalid_argument(std::string("invalid AST (") + evo::verify_code_name(verified.diagnostic.code) +
                                ") at " + verified.diagnostic.path + ": " + verified.diagnostic.message);
  genome.meta = evo::build_genome_meta(genome.ast);
  gg::require_membership(*grammar, genome);
  auto verdict = judge(genome, cases, task, static_cast<int>(grammar->execution_limits().fuel));
  verdict.object_v["task"] = str(task.name);
  verdict.object_v["split"] = str(split);
  std::cout << gg::canonical_json(verdict) << '\n';
  return 0;
}

}  // namespace gagp::cli_detail
