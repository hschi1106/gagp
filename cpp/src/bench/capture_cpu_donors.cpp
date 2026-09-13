// Build against an immutable source tree with GAGP_MUTATION_SOURCE naming its
// mutation.cpp. Only the donor call is intercepted; the production mutation
// implementation and every other operator remain unchanged.
#include <fstream>
#include <chrono>
#include <iostream>
#include <sstream>

#include "migration_reproduction.hpp"
#include "gagp/cli/options.hpp"
#include "gagp/evolution/repro/backend.hpp"

// The adapter codec archive exports the existing parser; the immutable
// reference header predates this benchmark-only declaration.
namespace gagp::cli_detail {
std::vector<evo::EvalCase> decode_fitness_cases_json(const JsonValue& raw);
}

namespace {
std::vector<std::string> donor_calls;
struct FrozenDonorCall {
  int type = 0;
  int depth = 0;
  bool allow_asgp = false;
  std::string rng_before;
  std::mt19937_64 rng_after;
  gagp::evo::AstProgram input;
  gagp::evo::AstProgram donor;
};
std::vector<FrozenDonorCall> frozen_calls;
std::size_t next_call = 0;
bool replay_donors = false;
bool validate_replay_state = true;

gagp::evo::AstProgram read_fragment(const gagp::cli_detail::JsonValue& raw) {
  using namespace gagp::cli_detail;
  if (require_string(require_object_field(raw, "format_version"), "format_version") != "migration-population-v1") {
    throw std::runtime_error("invalid donor fragment version");
  }
  const auto& programs = require_object_field(raw, "programs");
  if (programs.kind != JsonValue::Kind::Array || programs.array_v.size() != 1) {
    throw std::runtime_error("expected exactly one donor fragment");
  }
  const auto& program = programs.array_v[0];
  auto ast = decode_ast_json(require_object_field(program, "structure"));
  if (!ast.consts.empty()) throw std::runtime_error("duplicate donor constants");
  const auto& constants = require_object_field(program, "constants");
  if (constants.kind != JsonValue::Kind::Array) throw std::runtime_error("expected donor constants array");
  for (const auto& value : constants.array_v) ast.consts.push_back(gagp::migration::decode_value(value));
  return ast;
}

void load_donor_tape(const gagp::cli_detail::JsonValue& tape) {
  using namespace gagp::cli_detail;
  if (require_string(require_object_field(tape, "format_version"), "format_version") != "migration-cpu-donor-capture-v1") {
    throw std::runtime_error("unsupported CPU donor tape version");
  }
  const auto& calls = require_object_field(tape, "calls");
  if (calls.kind != JsonValue::Kind::Array) throw std::runtime_error("expected donor calls array");
  for (const auto& call : calls.array_v) {
    FrozenDonorCall frozen;
    frozen.type = require_int(require_object_field(call, "type"), "type");
    frozen.depth = require_int(require_object_field(call, "depth"), "depth");
    const auto& allowed = require_object_field(call, "allow_asgp");
    if (allowed.kind != JsonValue::Kind::Bool) throw std::runtime_error("invalid donor allow_asgp");
    frozen.allow_asgp = allowed.bool_v;
    frozen.rng_before = require_string(require_object_field(call, "rng_before"), "rng_before");
    std::istringstream after(require_string(require_object_field(call, "rng_after"), "rng_after"));
    if (!(after >> frozen.rng_after)) throw std::runtime_error("invalid donor RNG state");
    after >> std::ws;
    if (!after.eof()) throw std::runtime_error("trailing donor RNG state");
    frozen.input = read_fragment(require_object_field(call, "input"));
    frozen.donor = read_fragment(require_object_field(call, "donor"));
    frozen_calls.push_back(std::move(frozen));
  }
  replay_donors = true;
}
}

#ifdef GAGP_MUTATION_SOURCE
#include "subtree_utils.hpp"
namespace gagp::evo::subtree {
std::vector<AstNode> capture_random_expr(std::mt19937_64& rng, AstProgram& target,
                                       RType type, int depth, const GrammarConfig& grammar,
                                       bool allow_asgp) {
  std::ostringstream before, after;
  if (validate_replay_state) before << rng;
  ProgramGenome input;
  if (validate_replay_state) input.ast = target;
  const auto encoded_input = validate_replay_state ? migration::encode_population({input}) : std::string{};
  if (replay_donors) {
    if (next_call == frozen_calls.size()) throw std::runtime_error("CPU donor tape exhausted");
    const auto& frozen = frozen_calls[next_call++];
    ProgramGenome expected_input;
    if (validate_replay_state) expected_input.ast = frozen.input;
    if (frozen.type != static_cast<int>(type) || frozen.depth != depth || frozen.allow_asgp != allow_asgp ||
        (validate_replay_state && (frozen.rng_before != before.str() ||
                                  migration::encode_population({expected_input}) != encoded_input))) {
      throw std::runtime_error("CPU donor call differs from frozen tape");
    }
    auto original_nodes = target.nodes;
    target = frozen.donor;
    auto nodes = std::move(target.nodes);
    target.nodes = std::move(original_nodes);
    rng = frozen.rng_after;
    return nodes;
  }
  auto nodes = make_random_expr_nodes_for_type(rng, target, type, depth, grammar, allow_asgp);
  after << rng;
  ProgramGenome output;
  output.ast = target;
  output.ast.nodes = nodes;
  std::ostringstream encoded;
  encoded << "{\"type\":" << static_cast<int>(type) << ",\"depth\":" << depth
          << ",\"allow_asgp\":" << (allow_asgp ? "true" : "false")
          << ",\"rng_before\":\"" << before.str() << "\",\"rng_after\":\"" << after.str()
          << "\",\"input\":" << encoded_input << ",\"donor\":"
          << migration::encode_population({output}) << '}';
  donor_calls.push_back(encoded.str());
  return nodes;
}
}  // namespace gagp::evo::subtree
#define make_random_expr_nodes_for_type capture_random_expr
#include GAGP_MUTATION_SOURCE
#undef make_random_expr_nodes_for_type
#endif

int main(int argc, char** argv) {
  using namespace gagp;
  using namespace gagp::evo;
  using namespace gagp::cli_detail;
  try {
    std::string snapshot, donor_tape;
    bool steady = false;
    int warmups = 3, trials = 15;
    std::vector<char*> common{argv[0]};
    for (int i = 1; i < argc; ++i) {
      const std::string option = argv[i];
      if (option == "--steady") {
        steady = true;
      } else if (option == "--warmups" || option == "--trials") {
        if (++i >= argc) throw std::runtime_error("missing trial count");
        const std::string count = argv[i];
        std::size_t used = 0;
        const int parsed = std::stoi(count, &used);
        if (used != count.size()) throw std::runtime_error("invalid trial count");
        (option == "--warmups" ? warmups : trials) = parsed;
      } else if (option == "--snapshot" || option == "--donor-tape") {
        if (++i >= argc) throw std::runtime_error("missing reproduction snapshot");
        (option == "--snapshot" ? snapshot : donor_tape) = argv[i];
      } else common.push_back(argv[i]);
    }
    const auto opts = parse_cli_options(static_cast<int>(common.size()), common.data());
    if (warmups < 3 || warmups > 1000 || trials < 15 || trials > 10000) {
      throw std::runtime_error("require 3..1000 warmups and 15..10000 measured trials");
    }
    if (steady && donor_tape.empty()) throw std::runtime_error("steady reproduction requires a frozen donor tape");
    const auto read = [](const std::string& path) {
      std::ifstream in(path);
      if (!in) throw std::runtime_error("cannot read " + path);
      std::ostringstream text;
      text << in.rdbuf();
      return JsonParser(text.str()).parse();
    };
    const auto frozen = migration::decode_reproduction(read(snapshot));
    if (!donor_tape.empty()) {
#ifndef GAGP_MUTATION_SOURCE
      throw std::runtime_error("donor replay requires the instrumented production mutation build");
#endif
      load_donor_tape(read(donor_tape));
    }
    EvolutionConfig cfg;
    cfg.population_size = opts.population_size;
    cfg.seed = opts.seed;
    cfg.mutation_rate = opts.mutation_rate;
    cfg.mutation_subtree_prob = opts.mutation_subtree_prob;
    cfg.selection_pressure = opts.selection_pressure;
    cfg.limits = Limits{opts.max_expr_depth, opts.max_stmts_per_block, opts.max_total_nodes,
                        opts.max_for_k, opts.max_call_args};
    if (!opts.grammar_config_path.empty()) cfg.grammar = decode_grammar_config_json(read(opts.grammar_config_path));
    cfg.verification_inputs = prepare_case_set(decode_fitness_cases_json(read(opts.cases_path)), cfg.grammar).input_specs;
    if (frozen.parents.size() != static_cast<std::size_t>(cfg.population_size)) {
      throw std::runtime_error("parent population size differs from capture");
    }
    std::vector<ScoredGenomeRef> scored;
    for (std::size_t i = 0; i < frozen.parents.size(); ++i) {
      scored.push_back({&frozen.parents[i], frozen.fitness[i]});
    }
    std::mt19937_64 rng(cfg.seed);
    const auto result = repro::run_reproduction_backend(scored, cfg, rng);
    if (replay_donors && next_call != frozen_calls.size()) throw std::runtime_error("unused CPU donor tape entries");
    const auto children = migration::encode_population(result.next_population);
    std::ostringstream samples;
    samples << std::setprecision(17);
    if (steady) {
      // The first call above checks exact RNG/input state outside measurement.
      // Each timed call still checks request shape, complete tape consumption,
      // and (after timing) exact output equality. No generation is timed.
      validate_replay_state = false;
      for (int i = 0; i < warmups + trials; ++i) {
        next_call = 0;
        std::mt19937_64 trial_rng(cfg.seed);
        const auto start = std::chrono::steady_clock::now();
        const auto trial = repro::run_reproduction_backend(scored, cfg, trial_rng);
        const double call_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (next_call != frozen_calls.size() || migration::encode_population(trial.next_population) != children) {
          throw std::runtime_error("steady CPU reproduction differs from validated replay");
        }
        if (i) samples << ',';
        samples << "{\"index\":" << i << ",\"warmup\":" << (i < warmups ? "true" : "false")
                << ",\"call_ms\":" << call_ms << ",\"phases\":{\"selection_ms\":" << trial.stats.selection_ms
                << ",\"crossover_ms\":" << trial.stats.crossover_ms
                << ",\"mutation_ms\":" << trial.stats.mutation_ms << "}}";
      }
    }
    std::ofstream out(opts.out_json);
    out << "{\"format_version\":\"" << (steady ? "migration-steady-cpu-reproduction-v1" : "migration-cpu-donor-capture-v1")
        << "\",\"children\":" << children << ",\"verification\":[";
    for (std::size_t i = 0; i < result.next_population.size(); ++i) {
      if (i) out << ',';
      const auto checked = verify_ast(result.next_population[i].ast, cfg.verification_inputs);
      out << "{\"ok\":" << (checked.ok ? "true" : "false")
          << ",\"code\":\"" << verify_code_name(checked.diagnostic.code)
          << "\",\"node\":" << checked.diagnostic.node_index << '}';
    }
    out << "],\"calls\":[";
    for (std::size_t i = 0; i < donor_calls.size(); ++i) {
      if (i) out << ',';
      out << donor_calls[i];
    }
    out << "],\"replayed_calls\":" << next_call << ",\"generated_calls\":" << donor_calls.size();
    if (steady) out << ",\"validation_calls\":1,\"warmups\":" << warmups << ",\"measured_trials\":" << trials
                    << ",\"samples\":[" << samples.str() << ']';
    out << "}\n";
    if (!out) throw std::runtime_error("failed to write CPU donor capture");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
