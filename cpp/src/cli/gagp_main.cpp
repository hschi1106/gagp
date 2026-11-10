// The `gagp` command: run a search, score a program, author grammars.
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "gagp/cli/grammar_author.hpp"
#include "gagp/cli/grammar_generate.hpp"
#include "gagp/cli/run.hpp"

namespace {
constexpr const char* kUsage =
    "gagp - grammar-guided genetic programming on the GPU\n"
    "\n"
    "  gagp run TASK.json [options]        search for a program (see `gagp run --help`)\n"
    "  gagp eval TASK.json PROGRAM.json    score a program (see `gagp eval --help`)\n"
    "  gagp grammar validate|inspect|resolve ...   check and explore a grammar definition\n"
    "  gagp grammar generate ...           generate programs from a grammar\n"
    "\n"
    "Documentation: README.md and docs/reference/cli.md.\n";
}  // namespace

int main(int argc, char** argv) {
  const std::vector<std::string> args(argv + 1, argv + argc);
  if (args.empty() || args[0] == "--help" || args[0] == "-h" || args[0] == "help") {
    std::cout << kUsage;
    return args.empty() ? 2 : 0;
  }
  try {
    const std::vector<std::string> rest(args.begin() + 1, args.end());
    if ((args[0] == "run" || args[0] == "eval") && !rest.empty() && (rest[0] == "--help" || rest[0] == "-h")) {
      std::cout << (args[0] == "run" ? gagp::cli_detail::run_usage() : gagp::cli_detail::eval_usage()) << '\n';
      return 0;
    }
    if (args[0] == "run") return gagp::cli_detail::run_task_command(rest);
    if (args[0] == "eval") return gagp::cli_detail::eval_program_command(rest);
    if (args[0] == "grammar" && !rest.empty() && rest[0] == "generate") {
      std::vector<char*> forwarded{argv[0]};
      for (int i = 3; i < argc; ++i) forwarded.push_back(argv[i]);
      return gagp::cli_detail::run_grammar_generate_command(static_cast<int>(forwarded.size()), forwarded.data());
    }
    if (args[0] == "grammar") {
      std::vector<char*> forwarded{argv[0]};
      for (int i = 2; i < argc; ++i) forwarded.push_back(argv[i]);
      return gagp::cli_detail::run_grammar_author_command(static_cast<int>(forwarded.size()), forwarded.data());
    }
    std::cerr << "gagp: unknown command " << args[0] << "\n\n" << kUsage;
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "gagp " << args[0] << ": " << error.what() << '\n';
    return 2;
  }
}
