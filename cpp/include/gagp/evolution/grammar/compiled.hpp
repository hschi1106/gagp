#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include <unordered_set>

#include "gagp/evolution/grammar/catalog.hpp"
#include "gagp/evolution/grammar/constants.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/structured.hpp"

namespace gagp::evo::grammar {

inline constexpr std::uint32_t kNoGrammarId = std::numeric_limits<std::uint32_t>::max();

struct GrammarLimits {
  std::uint32_t max_nodes = 0;
  std::uint32_t max_depth = 0;
};

struct ExecutionLimits {
  std::uint32_t fuel = 0;
};

enum class ExpressionKind : std::uint8_t { Primitive, Reference, Constant, Input, Bound, Template, Hole, Structured, Control, Local };

struct TemplateHole {
  std::uint32_t id = 0;
  std::string stable_id;
  RType type = RType::Invalid;
  std::vector<RegionBinding> scope;
  NodeCategory category = NodeCategory::Expression;
};

struct CompiledTemplate {
  std::uint32_t id = 0;
  std::string stable_id;
  RType type = RType::Invalid;
  std::vector<RegionBinding> scope;
  std::vector<TemplateHole> holes;
  std::uint32_t body = kNoGrammarId;
  NodeCategory category = NodeCategory::Expression;
};

struct CompiledExpression {
  ExpressionKind kind = ExpressionKind::Primitive;
  RType type = RType::Invalid;
  // Catalog, nonterminal, domain, input, or lexical binding ID according to kind.
  std::uint32_t target = kNoGrammarId;
  std::vector<std::uint32_t> children;
  // Reference scope bindings mapped into the caller's lexical environment.
  std::vector<std::uint32_t> scope_mapping;
  std::vector<RegionSlot> regions;
  std::uint32_t template_id = kNoGrammarId;
  bool fixed = false;
  std::uint32_t context = kNoGrammarId;
  NodeCategory category = NodeCategory::Expression;
  std::uint32_t local = kNoGrammarId;
};

struct CompiledProduction {
  std::uint32_t id = 0;
  std::string stable_id;
  std::uint32_t nonterminal = 0;
  double weight = 0;
  std::uint32_t expression = 0;
  // Index d gives minimum materialized nodes at depth <= d; UINT_MAX is impossible.
  std::vector<std::uint32_t> minimum_nodes_by_depth;
};

struct CompiledNonterminal {
  std::uint32_t id = 0;
  std::string stable_id;
  RType type = RType::Invalid;
  std::vector<RegionBinding> scope;
  std::vector<std::uint32_t> productions;
  std::vector<std::uint32_t> minimum_nodes_by_depth;
  std::uint32_t minimum_depth = kNoGrammarId;
  std::uint32_t minimum_nodes = kNoGrammarId;
  std::uint32_t context = kNoGrammarId;
  NodeCategory category = NodeCategory::Expression;
};

struct ContextNonterminal {
  std::uint32_t nonterminal = 0;
  std::vector<std::uint32_t> scope_mapping;
};

struct CompiledContext {
  std::uint32_t id = 0;
  std::vector<RegionBinding> scope;
  std::vector<ContextNonterminal> nonterminals;
  std::array<std::vector<std::uint32_t>, 8> productions_by_type;
  std::array<std::vector<std::uint32_t>, 4> productions_by_category;
};

class GrammarCompiler;

// Construct once; consumers only receive const access to numeric tables.
class CompiledGrammar {
 public:
  const std::string& content_hash() const noexcept { return content_hash_; }
  const std::string& canonical_definition() const noexcept { return canonical_; }
  const GrammarLimits& search_limits() const noexcept { return search_limits_; }
  const ExecutionLimits& execution_limits() const noexcept { return execution_limits_; }
  std::uint32_t entry() const noexcept { return entry_; }
  const std::vector<CompiledNonterminal>& nonterminals() const noexcept { return nonterminals_; }
  const std::vector<CompiledProduction>& productions() const noexcept { return productions_; }
  const std::vector<CompiledExpression>& expressions() const noexcept { return expressions_; }
  const std::vector<ConstantDomain>& constants() const noexcept { return constants_; }
  // Finite-domain lookup is compiled once; encoding is the singleton domain JSON.
  bool constant_encoding_allowed(std::uint32_t domain, const std::string& encoding) const {
    return constant_encodings_.at(domain).count(encoding) != 0;
  }
  const std::vector<CompiledTemplate>& templates() const noexcept { return templates_; }
  const std::vector<CompiledContext>& contexts() const noexcept { return contexts_; }
  const std::vector<StructuredContract>& structured_contracts() const noexcept { return structured_; }
  const std::vector<RegionBinding>& inputs() const noexcept { return inputs_; }
  const std::vector<RegionBinding>& locals() const noexcept { return locals_; }
  const std::vector<std::uint32_t>& productions_for_category(NodeCategory category) const;
  const std::vector<std::uint32_t>& productions_for_type(RType type) const;
  void require_executable() const;
  void require_executable(std::uint32_t nonterminal) const;

 private:
  friend class GrammarCompiler;
  CompiledGrammar() = default;
  std::string content_hash_, canonical_;
  GrammarLimits search_limits_;
  ExecutionLimits execution_limits_;
  std::uint32_t entry_ = 0;
  std::vector<CompiledNonterminal> nonterminals_;
  std::vector<CompiledProduction> productions_;
  std::vector<CompiledExpression> expressions_;
  std::vector<ConstantDomain> constants_;
  std::vector<std::unordered_set<std::string>> constant_encodings_;
  std::vector<CompiledTemplate> templates_;
  std::vector<CompiledContext> contexts_;
  std::vector<StructuredContract> structured_;
  std::vector<RegionBinding> inputs_;
  std::vector<RegionBinding> locals_;
  std::array<std::vector<std::uint32_t>, 8> by_type_;
  std::array<std::vector<std::uint32_t>, 4> by_category_;
};

CompiledGrammar compile_grammar(const ResolvedDefinition& definition);

}  // namespace gagp::evo::grammar
