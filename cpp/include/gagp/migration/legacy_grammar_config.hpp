#pragma once

#include "gagp/evolution/grammar/compiled.hpp"
#include "gagp/evolution/input_spec.hpp"

namespace gagp::migration {

struct LegacyGrammarConfig {
  bool statement_assign = true, statement_if_stmt = true,
       statement_for_range = true, statement_return = true;
  bool expression_const = true, expression_var = true,
       expression_if_expr = true;
  bool unary_neg = true, unary_not = true;
  bool binary_add = true, binary_sub = true, binary_mul = true,
       binary_div = true, binary_mod = true, binary_lt = true,
       binary_le = true, binary_gt = true, binary_ge = true,
       binary_eq = true, binary_ne = true, binary_and = true,
       binary_or = true;
  bool builtin_abs = true, builtin_min = true, builtin_max = true,
       builtin_clip = true, builtin_idiv0 = true, builtin_imod0 = true,
       builtin_len = true, builtin_concat = true, builtin_slice = true,
       builtin_index = true, builtin_append = true, builtin_prepend = true,
       builtin_reverse = true, builtin_find = true, builtin_contains = true,
       builtin_singleton = true, builtin_char_to_string = true,
       builtin_string_to_char = true, builtin_ord = true, builtin_chr = true,
       builtin_is_letter = true, builtin_is_digit = true,
       builtin_is_space = true, builtin_is_vowel = true,
       builtin_to_lower = true, builtin_to_upper = true,
       builtin_to_string = true;
  bool value_int = true, value_float = true, value_bool = true,
       value_char = true, value_string = true, value_int_list = true,
       value_float_list = true, value_string_list = true;

  void validate() const;
  bool allows_type(evo::RType type) const;
  bool allows_node_kind(evo::NodeKind kind) const;
};

struct LegacyGrammarConfigConversion {
  evo::RType return_type = evo::RType::Invalid;
  std::vector<evo::InputSpec> inputs;
  std::vector<evo::grammar::RegionBinding> locals;
  std::vector<evo::grammar::ConstantDomain> constants;
  evo::grammar::GrammarLimits search_limits;
  evo::grammar::ExecutionLimits execution_limits;
  std::uint32_t max_statements_per_block = 6;
  std::int64_t max_for_k = 16;
};

evo::grammar::ResolvedDefinition convert_legacy_grammar_config(
    const LegacyGrammarConfig& config,
    const LegacyGrammarConfigConversion& options);

}  // namespace gagp::migration
