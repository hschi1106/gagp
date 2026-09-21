#pragma once

#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/grammar/compiled.hpp"
#include "gagp/evolution/grammar/random.hpp"
#include "gagp/evolution/grammar/request.hpp"
#include "gagp/evolution/grammar/frame.hpp"

namespace gagp::evo::grammar {

inline constexpr const char* kGrammarSemanticVersion = "gagp-native-2.0.0";
inline constexpr std::uint32_t kGrammarMaxLoweredInstructions = 1048576;
inline constexpr const char* kGrammarGeneratorVersion = "typed-derivation-v2";

struct NodeOrigin {
  std::uint32_t expression = kNoGrammarId;
  std::uint32_t production = kNoGrammarId;
  std::uint32_t nonterminal = kNoGrammarId;
  std::uint32_t logical_instance = kNoGrammarId;
  std::uint32_t template_instance = kNoGrammarId;
  std::uint32_t slot = kNoGrammarId;
  bool fixed = false;
  // Physical template nesting in a reconstructed witness (copies may differ).
  std::uint32_t template_depth = 0;
};

struct DerivationChoice {
  std::uint32_t nonterminal = kNoGrammarId;
  std::uint32_t production = kNoGrammarId;
  std::uint32_t parent = kNoGrammarId;
  std::uint32_t ast_begin = 0;
  std::uint32_t ast_end = 0;
  // Incoming logical hole contract, populated by membership reconstruction.
  std::uint32_t template_instance = kNoGrammarId;
  std::uint32_t slot = kNoGrammarId;
  std::uint32_t enclosing_template_depth = 0;
};

struct TemplateInstance {
  std::uint32_t template_id = kNoGrammarId;
  std::uint32_t parent = kNoGrammarId;
};

struct HoleOccurrence {
  std::uint32_t template_instance = kNoGrammarId;
  std::uint32_t slot = kNoGrammarId;
  std::uint32_t ast_begin = 0;
  std::uint32_t ast_end = 0;
};

struct DerivationMetadata {
  // Membership reconstruction certifies structure, not an original RNG history.
  bool seed_replayable = true;
  GenerationRequest request;
  std::vector<std::uint32_t> request_scope_mapping;
  std::string grammar_hash;
  std::string semantic_version = kGrammarSemanticVersion;
  std::string generator_version = kGrammarGeneratorVersion;
  std::string rng_version = kGrammarRngVersion;
  GrammarLimits search_limits;
  ExecutionLimits execution_limits;
  std::uint64_t seed = 0;
  std::uint32_t logical_steps = 0;
  std::uint32_t derived_nodes = 0;
  std::uint32_t lowered_instructions = 0;
  std::vector<NodeOrigin> nodes;
  std::vector<DerivationChoice> choices;
  std::vector<TemplateInstance> templates;
  std::vector<HoleOccurrence> holes;
};

struct GeneratedDerivation {
  ProgramGenome genome;
  DerivationMetadata derivation;
};

// An expression entry receives a fixed PROGRAM/BLOCK_CONS/RETURN/BLOCK_NIL
// envelope. Its four nodes and three prefix levels count against search limits;
// a Program entry already includes its complete structural envelope.
GeneratedDerivation generate_derivation(const CompiledGrammar& grammar, std::uint64_t seed);
GeneratedDerivation generate_derivation(const CompiledGrammar& grammar, std::uint64_t seed,
    const GenerationRequest& request);

// Isolated donor validation may bind declared native locals as explicit inputs.
// The returned complete AST is frame-dependent and is not an original seed artifact.
GeneratedDerivation generate_derivation_in_frame(const CompiledGrammar& grammar, std::uint64_t seed,
    const GenerationRequest& request, const GenerationFrame& frame);

}  // namespace gagp::evo::grammar
