#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/cli/options.hpp"

namespace {

gagp::cli_detail::CliOptions parse(std::vector<std::string> args) {
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (std::string& arg : args) argv.push_back(arg.data());
  return gagp::cli_detail::parse_cli_options(static_cast<int>(argv.size()), argv.data());
}

void expect_error(const std::vector<std::string>& args, const std::string& message) {
  try {
    (void)parse(args);
  } catch (const std::runtime_error& error) {
    assert(error.what() == message);
    return;
  }
  assert(false && "expected parse failure");
}

}  // namespace

int main() {
  {
    const auto opts = parse({"gagp_evolve_cli", "--cases", "cases.json",
                             "--grammar-definition", "grammar.json"});
    assert(opts.cases_path == "cases.json");
    assert(opts.grammar_definition_path == "grammar.json");
    assert(opts.engine == "cpu");
    assert(opts.repro_backend == "cpu");
    assert(opts.cpu_repro_ablation == "none");
    assert(!opts.repro_overlap);
    assert(!opts.skip_final_eval);
    assert(!opts.retain_final_population);
    assert(opts.blocksize == 1024);
    assert(opts.population_size == 64);
    assert(opts.generations == 40);
    assert(opts.mutation_rate == 0.5);
    assert(opts.mutation_subtree_prob == 0.8);
    assert(opts.penalty == 1.0);
    assert(opts.selection_pressure == 2);
    assert(opts.seed == 0);
    assert(opts.fuel == 20000);
    assert(opts.max_expr_depth == 7);
    assert(opts.max_total_nodes == 80);
    assert(opts.show_program == "none");
    assert(opts.timing == "summary");
  }
  {
    const auto opts = parse({
        "gagp_evolve_cli", "--cases", "cases.json",
        "--population-json", "population.json", "--grammar-definition", "grammar.json",
        "--eval-ast-json", "ast.json", "--engine", "gpu", "--repro-backend", "cpu",
        "--cpu-repro-ablation", "gpu_candidates", "--repro-overlap", "on",
        "--skip-final-eval", "on", "--retain-final-population", "on",
        "--blocksize", "256", "--population-size", "32", "--generations", "9",
        "--mutation-rate", "0.25", "--mutation-subtree-prob", "0.4",
        "--penalty", "2.5", "--selection-pressure", "4", "--seed", "42",
        "--fuel", "500", "--max-expr-depth", "8", "--max-total-nodes", "100",
        "--show-program", "both", "--timing", "all", "--out-json", "run.json"});
    assert(opts.population_json == "population.json");
    assert(opts.grammar_definition_path == "grammar.json");
    assert(opts.eval_ast_json == "ast.json");
    assert(opts.engine == "gpu");
    assert(opts.cpu_repro_ablation == "gpu_candidates");
    assert(opts.repro_overlap && opts.skip_final_eval && opts.retain_final_population);
    assert(opts.blocksize == 256 && opts.population_size == 32 && opts.generations == 9);
    assert(opts.mutation_rate == 0.25 && opts.mutation_subtree_prob == 0.4);
    assert(opts.penalty == 2.5 && opts.selection_pressure == 4 && opts.seed == 42);
    assert(opts.fuel == 500 && opts.max_expr_depth == 8 && opts.max_total_nodes == 100);
    assert(opts.fuel_explicit && opts.max_expr_depth_explicit &&
           opts.max_total_nodes_explicit);
    assert(opts.show_program == "both" && opts.timing == "all" && opts.out_json == "run.json");
  }

  expect_error({"cli"}, "--cases is required");
  expect_error({"cli", "--cases"}, "missing value for --cases");
  expect_error({"cli", "--cases", "x"}, "--grammar-definition is required for evolution");
  expect_error({"cli", "--cases", "x", "--unknown"}, "unknown argument: --unknown");
  expect_error({"cli", "--cases", "x", "--grammar-config", "old.json"},
               "unknown argument: --grammar-config");
  expect_error({"cli", "--cases", "x", "--grammar-definition", "g", "--max-stmts-per-block", "7"},
               "unknown argument: --max-stmts-per-block");
  expect_error({"cli", "--cases", "x", "--grammar-definition", "g", "--max-for-k", "12"},
               "unknown argument: --max-for-k");
  expect_error({"cli", "--cases", "x", "--grammar-definition", "g", "--max-call-args", "5"},
               "unknown argument: --max-call-args");
  expect_error({"cli", "--cases", "x", "--grammar-definition", "g", "--engine", "other"}, "--engine must be cpu or gpu");
  expect_error({"cli", "--cases", "x", "--grammar-definition", "g", "--repro-backend", "other"},
               "--repro-backend must be cpu or gpu");
  expect_error({"cli", "--cases", "x", "--grammar-definition", "g", "--cpu-repro-ablation", "other"},
               "--cpu-repro-ablation must be one of: none|gpu_selection|gpu_candidates|gpu_coupled_donor");
  expect_error({"cli", "--cases", "x", "--grammar-definition", "g", "--repro-backend", "gpu", "--cpu-repro-ablation", "gpu_selection"},
               "--cpu-repro-ablation requires --repro-backend cpu");
  expect_error({"cli", "--cases", "x", "--grammar-definition", "g", "--repro-overlap", "maybe"},
               "--repro-overlap must be on or off");
  expect_error({"cli", "--cases", "x", "--grammar-definition", "g", "--blocksize", "0"}, "--blocksize must be > 0");
  expect_error({"cli", "--cases", "x", "--grammar-definition", "g", "--selection-pressure", "0"},
               "--selection-pressure must be > 0");
  expect_error({"cli", "--cases", "x", "--grammar-definition", "g", "--mutation-subtree-prob", "1.1"},
               "--mutation-subtree-prob must be in [0, 1]");
  expect_error({"cli", "--cases", "x", "--grammar-definition", "g", "--penalty", "-1"}, "--penalty must be >= 0");
  expect_error({"cli", "--cases", "x", "--grammar-definition", "g", "--show-program", "best"},
               "--show-program must be one of: none|ast|bytecode|both");
  expect_error({"cli", "--cases", "x", "--grammar-definition", "g", "--timing", "verbose"},
               "--timing must be one of: none|summary|per_gen|all");
}
