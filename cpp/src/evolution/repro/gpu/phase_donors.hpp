#pragma once
#include <memory>
#include <optional>
#include <vector>
#include "gagp/evolution/grammar/donor.hpp"
namespace gagp::evo::repro {
// Private donor-construction output. These origins are construction metadata,
// not a canonical DerivationMetadata or an externally reusable certificate.
struct ConstructedPhaseDonor {
  AstProgram fragment;
  std::vector<grammar::NodeOrigin> origins;
  std::uint32_t depth=0;
};
struct GpuPhaseDonorMetrics {
  double setup_ms=0;
  std::size_t device_bytes=0;
};
class GpuPhaseDonorSession;
std::shared_ptr<GpuPhaseDonorSession> make_gpu_phase_donor_session(
    std::shared_ptr<const grammar::CompiledGrammar> grammar);
// One result per job/seed; unsupported jobs are nullopt and use native donors.
// Admitted jobs either construct every requested donor or report an explicit
// execution error; capacity failures are never silently published as success.
std::vector<std::optional<std::vector<ConstructedPhaseDonor>>> construct_gpu_phase_donors(
    GpuPhaseDonorSession& session, const grammar::CompiledGrammar& grammar,
    const std::vector<grammar::DonorPoolJob>& jobs, GpuPhaseDonorMetrics* metrics = nullptr);
} // namespace gagp::evo::repro
