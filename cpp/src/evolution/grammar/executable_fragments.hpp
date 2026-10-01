#pragma once

#include <memory>
#include <vector>
#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/core/bytecode.hpp"
#include "gagp/core/region_executable.hpp"

namespace gagp::evo::grammar {

// Experimental independent-region-phase representation. The owner certifies a
// fixed root skeleton and grammar holes at import. This is not a general AST
// replacement: unsupported shapes throw, and the caller can use native evolution.
class ExecutableFragments {
  struct Fragment;
  struct Impl;
 public:
  class Genome {
   public:
    Genome() = default;
   private:
    std::vector<std::shared_ptr<const Fragment>> phases_;
    friend class ExecutableFragments;
  };
  struct Change { Genome genome; bool changed = false; bool rejected = false; };
  ExecutableFragments(std::shared_ptr<const CompiledGrammar> grammar,
      const ProgramGenome& exemplar, std::vector<std::string> inputs);
  ~ExecutableFragments();
  ExecutableFragments(const ExecutableFragments&) = delete;
  ExecutableFragments& operator=(const ExecutableFragments&) = delete;
  Genome import(const ProgramGenome& external) const;
  ProgramGenome export_ast(const Genome& genome) const;
  BytecodeProgram executable(const Genome& genome) const;
  RegionExecutable owned_executable(const Genome& genome) const;
  // Exactly one variation operator. Mutation generates a fresh grammar donor;
  // crossover chooses compatible sites inside a phase, not only whole phases.
  Change vary(const Genome& parent, const Genome& donor, std::uint64_t seed,
      bool mutation) const;
  std::size_t nodes(const Genome& genome) const;
  std::vector<std::uint64_t> identity(const Genome& genome) const;
  std::size_t live_fragments() const;
 private:
  std::unique_ptr<Impl> impl_;
};
}  // namespace gagp::evo::grammar
