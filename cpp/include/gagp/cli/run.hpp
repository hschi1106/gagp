#pragma once

#include <string>
#include <vector>

namespace gagp::cli_detail {

std::string run_usage();
// `gagp run TASK.json [options]`; arguments exclude the subcommand.
int run_task_command(const std::vector<std::string>& args);
// `gagp eval TASK.json PROGRAM.json [--split train|test]`.
int eval_program_command(const std::vector<std::string>& args);

}  // namespace gagp::cli_detail
