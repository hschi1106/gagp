#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "gagp/evolution/grammar/frame.hpp"
#include "gagp/evolution/grammar/variation_contract.hpp"

namespace gagp::evo::grammar {

class VariationContext;

struct ContextualDonor {
  ProgramGenome genome;
  // Ordinary inputs/locals; lexical captures require project_frame with frame.
  std::vector<InputSpec> inputs;
  GenerationFrame frame;
  VariationSpan payload;
  std::uint32_t nodes = 0;
  std::uint32_t depth = 0;
  std::uint32_t template_nesting = 0;
  // Canonically reconstructed in the destination frame, excluding envelope.
  ProjectedResources projected_resources;
};

ContextualDonor generate_donor(const CompiledGrammar& grammar, std::uint64_t seed,
    const VariationSite& site);
ContextualDonor generate_donor(VariationContext& context, std::uint64_t seed,
    const VariationSite& site);
// With an offspring budget, use local summaries only for certified replacement
// closure and invariant root charges; otherwise test the complete destination.
ContextualDonor generate_donor(VariationContext& context, std::uint64_t seed,
    const VariationSite& site, const ProgramGenome& destination,
    std::size_t maximum_attempts = 64);

// Same bounded per-seed generation, with admission reuse confined to this pool.
// A runtime generation failure is represented by an empty slot, without resampling.
std::vector<std::optional<ContextualDonor>> generate_donor_pool(VariationContext& context,
    const std::vector<std::uint64_t>& seeds, const VariationSite& site,
    const ProgramGenome& destination, std::size_t maximum_attempts = 64);

struct DonorPoolJob {
  const ProgramGenome* destination = nullptr;
  VariationSite site;
  std::vector<std::uint64_t> seeds;
};
using DonorPool = std::vector<std::optional<ContextualDonor>>;
// Speculative pools retain job/slot order and atomically commit group payloads.
// Equal fixed interfaces/budgets reuse a pool for at most eight destinations,
// then refill with the next job's seeds. Every call refreshes all pools.
// nullopt leaves payloads unchanged: callers must replay their original complete
// interleaved preparation, not just postpone all sequential pools until assembly.
std::optional<std::vector<DonorPool>> try_generate_donor_pools(VariationContext& context,
    const std::vector<DonorPoolJob>& jobs, std::size_t maximum_attempts = 64);

}  // namespace gagp::evo::grammar
