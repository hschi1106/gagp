#pragma once

#include <memory>
#include <string>
#include "gagp/evolution/ast_program.hpp"

namespace gagp::evo {

namespace grammar { struct DerivationMetadata; }

struct GenomeMeta {
  int node_count = 0;
  int max_depth = 0;
  bool uses_builtins = false;
  std::string program_key;
};

struct ProgramGenome {
  AstProgram ast;
  GenomeMeta meta;
  // Immutable origin survives clones and table compaction; changed children
  // require new provenance and default to having none.
  std::shared_ptr<const grammar::DerivationMetadata> derivation;
};

GenomeMeta build_genome_meta(const AstProgram& ast);

}  // namespace gagp::evo
