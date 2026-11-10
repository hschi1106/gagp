#pragma once

#include <string>
#include <vector>

#include "gagp/cli/json.hpp"
#include "gagp/evolution/ast_program.hpp"
#include "gagp/evolution/case_set.hpp"

namespace gagp::cli_detail {

// Programs as ast-prefix-v2 JSON and fitness-cases documents (spec/fitness_cases.md).
evo::AstProgram decode_ast_json(const JsonValue& raw);
std::string encode_ast_json(const evo::AstProgram& ast);
// A program as a readable s-expression, e.g. `(add (mul x x) 1)`; for display only.
std::string program_text(const evo::AstProgram& ast);
std::vector<evo::EvalCase> decode_fitness_cases_json(const JsonValue& raw);

}  // namespace gagp::cli_detail
