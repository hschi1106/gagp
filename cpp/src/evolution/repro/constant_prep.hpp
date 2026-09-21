#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "gagp/core/value.hpp"
#include "gagp/evolution/repro/constant_types.hpp"
#include "gagp/evolution/ast_program.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/grammar/compiled.hpp"
#include "gagp/evolution/grammar/generate.hpp"

namespace gagp::evo::repro {

// Immutable rows shared by every population prepared from one compiled grammar.
// values contains exact materializations in declared order, including duplicates.
struct ConstantMutationDomains {
  std::vector<ConstantMutationDomain> domains;
  std::vector<Value> values;
  std::vector<int> expression_domains;
  std::shared_ptr<const grammar::CompiledGrammar> grammar_owner;
};

// Owns only the population-specific mutation streams and shares its grammar rows.
struct ConstantMutationTable {
  std::shared_ptr<const ConstantMutationDomains> grammar_domains;
  std::vector<ConstantMutationGroup> groups;
  std::vector<int> group_nodes;
  std::vector<int> node_group_origins;
  std::vector<int> metadata_roots;
  std::vector<ConstantMutationStream> streams;
};

std::shared_ptr<const ConstantMutationDomains> prepare_constant_mutation_domains(
    const std::shared_ptr<const grammar::CompiledGrammar>& grammar);

// Logical bytes owned by the bounded flat vectors (excluding allocator
// capacity and payload-registry backing stores).
std::size_t constant_mutation_table_bytes(
    const ConstantMutationTable& table);

// Appends one verified program or fragment's mutation stream. witness must
// describe exactly ast.nodes. Mutable constant groups follow the CPU mutation
// ordering: ascending logical_instance, independent of constant-pool aliasing.
void append_constant_mutation_stream(ConstantMutationTable& table,
                                     const AstProgram& ast,
                                     const grammar::DerivationMetadata& witness);

// Full-program convenience overload which also checks the native verifier
// sidecar length. Fragments verified in a larger frame use the core overload.
void append_constant_mutation_stream(ConstantMutationTable& table,
                                     const AstProgram& ast,
                                     const VerifiedAst& verified,
                                     const grammar::DerivationMetadata& witness);

}  // namespace gagp::evo::repro
