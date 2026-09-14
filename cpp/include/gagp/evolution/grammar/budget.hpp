#pragma once

#include <functional>
#include <optional>

#include "gagp/evolution/grammar/compiled.hpp"

namespace gagp::evo::grammar {

using HoleBudgetLookup = std::function<std::optional<std::uint32_t>(std::uint32_t, std::uint32_t)>;

// Exact minimum for fixed skeletons and shared holes, using the nonterminal
// fixed-point tables and optionally already-reserved enclosing hole budgets.
std::uint32_t minimum_expression_nodes(const CompiledGrammar& grammar,
    std::uint32_t expression, std::uint32_t depth, const HoleBudgetLookup& enclosing = {});

}  // namespace gagp::evo::grammar
