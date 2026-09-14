#include <exception>
#include <iostream>

#include "gagp/cli/grammar_generate.hpp"

int main(int argc, char** argv) {
  try {
    return gagp::cli_detail::run_grammar_generate_command(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << "gagp_generate_cli error: " << error.what() << '\n';
    return 2;
  }
}
