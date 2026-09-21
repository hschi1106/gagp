#pragma once

#include <cstddef>
#include <vector>

#include "gagp/evolution/ast_program.hpp"

namespace gagp::evo::subtree {

int node_arity(NodeKind kind);
std::vector<std::size_t> build_subtree_end(const AstProgram& program);
AstProgram replace_subtree(const AstProgram& base,
                           std::size_t target_start,
                           std::size_t target_stop,
                           const AstProgram& donor,
                           std::size_t donor_start,
                           std::size_t donor_stop);

}  // namespace gagp::evo::subtree
