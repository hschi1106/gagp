#pragma once
#include "gagp/evolution/grammar/variation.hpp"

namespace gagp::evo::repro {
struct PackedHostData;
struct GpuReproChildView;
struct PreparedParentCertificates;
struct PackedChildSplice;
std::vector<ProgramGenome> decode_compiled_pass(const PackedHostData&, const GpuReproChildView&,
    grammar::VariationContext&, const PreparedParentCertificates*);
}
namespace gagp::evo::grammar::variation_detail {
// Private native preparation owner. Construction copies and admits input before
// publication; no mutable AST, proof or registry payload aliases escape.
class OwnedScalarPopulation final {
 public:
  static std::shared_ptr<const OwnedScalarPopulation> create(
      const std::vector<ProgramGenome>& input, VariationContext& context);
  // Decodes and admits device output before taking ownership. No API accepts
  // a caller-populated vector plus a claimed certificate for this continuation.
  static std::shared_ptr<const OwnedScalarPopulation> from_gpu_pass(
      const repro::PackedHostData& packed, const repro::GpuReproChildView& view,
      VariationContext& context, const repro::PreparedParentCertificates& certificates);
  const std::vector<ProgramGenome>& genomes() const { return genomes_; }
  const std::shared_ptr<const CompiledGrammar>& grammar_owner() const { return grammar_; }
  bool matches(const VariationContext& context) const;
  const DerivationMetadata& witness(std::size_t index) const;
  const VerifiedAst& verified(std::size_t index) const;
  const std::vector<std::vector<int>>& environments(std::size_t index) const;
  const std::string& identity(std::size_t index) const;
 private:
  OwnedScalarPopulation() = default;
  friend std::vector<ProgramGenome> repro::decode_compiled_pass(const repro::PackedHostData&,
      const repro::GpuReproChildView&, gagp::evo::grammar::VariationContext&, const repro::PreparedParentCertificates*);
  std::optional<ProgramGenome> admit_crossover(AstProgram& candidate,
      const repro::PackedHostData& packed, const repro::PackedChildSplice& splice,
      VariationContext& context) const;
  bool transport_decisions_ = false;
  static std::shared_ptr<const OwnedScalarPopulation> adopt_decoded(
      std::vector<ProgramGenome> input, VariationContext& context);
  std::shared_ptr<const CompiledGrammar> grammar_;
  GenerationRequest request_;
  std::vector<ProgramGenome> genomes_;
  std::vector<std::shared_ptr<const DerivationCertificate>> proofs_;
};
}  // namespace gagp::evo::grammar::variation_detail
