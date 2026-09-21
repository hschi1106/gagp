#pragma once

#include <cstdint>
#include <string>

namespace gagp::cli_detail {

struct CliOptions {
  std::string cases_path;
  std::string population_json;
  std::string grammar_definition_path;
  std::string eval_ast_json;
  std::string engine = "cpu";
  std::string repro_backend = "cpu";
  std::string cpu_repro_ablation = "none";
  bool repro_overlap = false;
  bool skip_final_eval = false;
  bool retain_final_population = false;
  int blocksize = 1024;
  int population_size = 64;
  int generations = 40;
  double mutation_rate = 0.5;
  double mutation_subtree_prob = 0.8;
  double penalty = 1.0;
  int selection_pressure = 2;
  std::uint64_t seed = 0;
  int fuel = 20000;
  // Parser bookkeeping: distinguishes a default from an explicit contract override.
  bool fuel_explicit = false;
  int max_expr_depth = 7;
  bool max_expr_depth_explicit = false;
  int max_stmts_per_block = 6;
  bool max_stmts_per_block_explicit = false;
  int max_total_nodes = 80;
  bool max_total_nodes_explicit = false;
  int max_for_k = 16;
  bool max_for_k_explicit = false;
  int max_call_args = 3;
  bool max_call_args_explicit = false;
  std::string show_program = "none";
  std::string timing = "summary";
  std::string out_json;
};

CliOptions parse_cli_options(int argc, char** argv);

}  // namespace gagp::cli_detail
