#include "gagp/evolution/grammar/donor.hpp"

#include <algorithm>
#include <thread>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <unordered_map>

#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/cache.hpp"
#include "gagp/evolution/grammar/derivation_resources.hpp"
#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/evolution/node_descriptor.hpp"
#include "variation_internal.hpp"
#include "../../runtime/payload/staging.hpp"

namespace gagp::evo::grammar {
namespace {

std::uint32_t subtree_depth(const AstProgram& ast, std::uint32_t* index,
    std::uint32_t end) {
  if (*index >= end)
    throw std::runtime_error("generated contextual donor has an incomplete payload subtree");
  const auto& node = ast.nodes[*index];
  const int arity = node_prefix_arity(node);
  ++*index;
  std::uint32_t child_depth = 0;
  for (int child = 0; child < arity; ++child)
    child_depth = std::max(child_depth, subtree_depth(ast, index, end));
  return child_depth + 1;
}

std::uint32_t payload_depth(const AstProgram& ast, const VariationSpan& payload) {
  auto index = payload.begin;
  const auto depth = subtree_depth(ast, &index, payload.end);
  if (index != payload.end)
    throw std::runtime_error("generated contextual donor payload contains trailing nodes");
  return depth;
}

void validate_site(const CompiledGrammar& grammar, const VariationSite& site) {
  if (site.nonterminal >= grammar.nonterminals().size())
    throw std::runtime_error("contextual donor site has an unknown nonterminal ID");
  const auto& target = grammar.nonterminals()[site.nonterminal];
  if (site.type != target.type)
    throw std::runtime_error("contextual donor site type differs from nonterminal " +
        target.stable_id);
  if (site.category != target.category)
    throw std::runtime_error("contextual donor site category differs from nonterminal " +
        target.stable_id);
  if (site.context != target.context)
    throw std::runtime_error("contextual donor site context differs from nonterminal " +
        target.stable_id);
  if (site.category != NodeCategory::Expression && site.category != NodeCategory::Program)
    throw std::runtime_error(
        "contextual donor site requires an Expression or Program nonterminal");
}

GenerationFrame donor_frame(const CompiledGrammar& grammar, const VariationSite& site) {
  GenerationFrame frame;
  if (!site.occurrence_binder_ids.empty() &&
      std::all_of(site.occurrence_binder_ids.front().begin(), site.occurrence_binder_ids.front().end(),
          [](int id) { return id >= 0; }))
    frame.binder_ids = site.occurrence_binder_ids.front();
  std::set<std::string> available_names;
  for (const auto& binding : site.available_locals) {
    if (!available_names.insert(binding.name).second)
      throw std::runtime_error("contextual donor site has duplicate available binding: " +
          binding.name);
    const auto input = std::find_if(grammar.inputs().begin(), grammar.inputs().end(),
        [&](const auto& candidate) { return candidate.name == binding.name; });
    if (input != grammar.inputs().end()) {
      if (input->type != binding.type)
        throw std::runtime_error(
            "contextual donor site input has the wrong exact type: " + binding.name);
      continue;
    }
    frame.locals.push_back(binding);
  }
  return frame;
}

ContextualDonor generate_donor_impl(const CompiledGrammar& grammar, VariationContext* context,
    std::uint64_t seed, const VariationSite& site) {
  validate_site(grammar, site);

  try {
    const auto request = donor_request(site);
    const auto frame = donor_frame(grammar, site);
    auto generated = context ? generate_derivation_in_frame(*context, seed, request, frame) :
        generate_derivation_in_frame(grammar, seed, request, frame);

    if (generated.derivation.seed_replayable)
      throw std::runtime_error("contextual donor retained seed-replayable provenance");
    if (generated.derivation.nodes.size() != generated.genome.ast.nodes.size())
      throw std::runtime_error("contextual donor witness does not cover its native AST");

    ContextualDonor donor;
    donor.frame = frame;
    donor.inputs = frame_inputs(grammar, request, frame);
    donor.genome = std::move(generated.genome);
    const auto size = static_cast<std::uint32_t>(donor.genome.ast.nodes.size());
    if (site.category == NodeCategory::Expression) {
      if (size < 5)
        throw std::runtime_error("generated expression donor lacks its native envelope");
      donor.payload = {3, size - 1};
    } else {
      donor.payload = {0, size};
    }
    donor.nodes = donor.payload.end - donor.payload.begin;
    donor.projected_resources = generated.derivation.resources->subtree(donor.payload.begin);
    donor.depth = payload_depth(donor.genome.ast, donor.payload);
    for (auto index = donor.payload.begin; index < donor.payload.end; ++index)
      donor.template_nesting = std::max(donor.template_nesting,
          generated.derivation.nodes[index].template_depth);

    if (!donor_fits(site, donor.nodes, donor.depth, donor.template_nesting))
      throw std::runtime_error("generated contextual donor does not fit the variation site");
    return donor;
  } catch (const std::runtime_error&) {
    throw;
  } catch (const std::invalid_argument& error) {
    throw std::runtime_error("contextual donor generation failed: " +
        std::string(error.what()));
  }
}

}  // namespace

ContextualDonor generate_donor(const CompiledGrammar& grammar, std::uint64_t seed,
    const VariationSite& site) {
  return generate_donor_impl(grammar, nullptr, seed, site);
}

ContextualDonor generate_donor(VariationContext& context, std::uint64_t seed,
    const VariationSite& site) {
  return generate_donor_impl(context.grammar(), &context, seed, site);
}

namespace {
struct PoolDestination {
  std::shared_ptr<const VariationAnalysis> analysis;
  const VariationSite* site = nullptr;
  std::optional<ProjectedAllowance> invariant_allowance;
};

void prepare_destination(VariationContext& context, const VariationSite& site,
    const ProgramGenome& destination, PoolDestination& prepared) {
  // Reject a malformed destination before retries, rather than disguising it as
  // an impossible donor. Site coordinates are supplied by its certified analysis.
  if (!prepared.site) {
    if (!prepared.analysis) prepared.analysis = context.analyze(destination);
    const auto& sites = prepared.analysis->sites;
    const auto certified = std::find_if(sites.begin(), sites.end(),
        [&](const VariationSite& candidate) {
          return candidate.nonterminal == site.nonterminal && candidate.compatibility_key == site.compatibility_key &&
              candidate.occurrences.size() == site.occurrences.size() &&
              std::equal(candidate.occurrences.begin(), candidate.occurrences.end(), site.occurrences.begin(),
                  [](const VariationSpan& a, const VariationSpan& b) { return a.begin == b.begin && a.end == b.end; });
        });
    if (certified == sites.end())
      throw std::invalid_argument("projected donor site is not a certified destination site");
    prepared.site = &*certified;
    if (!certified->has_projected_allowance && context.offspring_budget() &&
        certified->category == NodeCategory::Expression) {
      std::vector<std::uint32_t> roots;
      for (const auto& request : context.requests()) roots.push_back(request.nonterminal);
      const auto invariant = context.grammar().resource_invariant_roots(roots);
      const auto root = prepared.analysis->witness.request.nonterminal;
      const auto found = std::find(roots.begin(), roots.end(), root);
      if (found != roots.end() && invariant[static_cast<std::size_t>(found - roots.begin())]) {
        std::vector<std::size_t> occurrences;
        for (const auto& span : certified->occurrences) occurrences.push_back(span.begin);
        prepared.invariant_allowance = prepared.analysis->witness.resources->replacement(
            occurrences, context.offspring_budget()->max_nodes, context.offspring_budget()->max_depth);
      }
    }
  }
}

ContextualDonor generate_budgeted_donor(VariationContext& context, std::uint64_t seed,
    const VariationSite& site, const ProgramGenome& destination, std::size_t maximum_attempts,
    std::unordered_map<std::string, bool>& admissions, PoolDestination& prepared,
    bool retain_analysis = true) {
  if (!context.offspring_budget()) return generate_donor(context, seed, site);
  if (!maximum_attempts)
    throw std::invalid_argument("projected donor generation requires a positive attempt limit");
  prepare_destination(context, site, destination, prepared);
  // This pool owns the analysis even if accepted children evict its cache entry.
  const auto* certified = prepared.site;
  const auto& destination_analysis = prepared.analysis;
  GrammarRandom attempts(seed);
  auto attempt_seed = seed;
  std::vector<std::string> input_names;
  for (const auto& input : context.grammar().inputs()) input_names.push_back(input.name);
  // The destination, site and frame are fixed throughout these bounded retries.
  // Identical decoded donor ASTs therefore produce identical admission results.
  // The caller confines this bounded cache to one destination/site/frame.
  for (std::size_t attempt = 0; attempt < maximum_attempts; ++attempt) {
    auto donor = generate_donor(context, attempt_seed, *certified);
    // Generation just rebuilt this native key from the complete verified AST.
    // For scalar-only pools it already identifies every value exactly; the
    // destination/frame/grammar/fuel are fixed for this private admission cache.
    // Bound key storage independently of donor size. Longer keys and payload
    // values still use the decoded identity and its snapshot reads.
    const bool scalar = donor.genome.meta.program_key.size() <= 256 &&
        std::all_of(donor.genome.ast.consts.begin(), donor.genome.ast.consts.end(),
        [](const Value& value) {
          return value.tag == ValueTag::Int || value.tag == ValueTag::Float ||
              value.tag == ValueTag::Bool || value.tag == ValueTag::Char;
        });
    const auto identity = scalar ? "native-scalar:" + donor.genome.meta.program_key :
        "decoded:" + runtime_cache_identity(donor.genome, input_names,
            context.grammar().execution_limits().fuel);
    const auto previous = admissions.find(identity);
    if (previous != admissions.end()) {
      if (previous->second) return donor;
      attempt_seed = attempts.next();
      continue;
    }
    // This allowance comes from the destination's reconstructed analysis, not
    // caller-supplied site metadata. Its grammar certificate makes resource
    // charges invariant under transplantation. It can reject, never admit:
    // surviving candidates still undergo membership and execution validation.
    if (certified->has_projected_allowance &&
        !certified->projected_allowance.accepts(donor.projected_resources)) {
      if (admissions.size() < 256) admissions.emplace(identity, false);
      attempt_seed = attempts.next();
      continue;
    }
    if (prepared.invariant_allowance) {
      bool reject = false;
      try {
        // A mutation-entry derivation is not a destination membership proof.
        // Reconstruct the small donor in the destination frame before using its
        // charges. If it does not fit that grammar, the full child may select a
        // different enclosing production, so retain the original admission path.
        const auto resources = project_derivation_resources_in_frame(context.grammar(),
            donor.genome, donor_request(*certified), donor.frame);
        reject = !prepared.invariant_allowance->accepts(resources.subtree(donor.payload.begin));
      } catch (const std::invalid_argument&) {}
      if (reject) {
        if (admissions.size() < 256) admissions.emplace(identity, false);
        attempt_seed = attempts.next();
        continue;
      }
    }
    ProgramGenome child;
    child.ast = variation_detail::splice(destination.ast, *certified, donor.genome.ast,
        donor.payload, certified->occurrence_binder_ids.front());
    try {
      // Rejected attempts need canonical membership and resource reconstruction,
      // but not every future crossover site and compatibility key of the child.
      // Keep full analysis for accepted donors and retain the same retry seeds.
      const bool accepted = retain_analysis ?
          context.offspring_budget()->accepts(project_derivation_resources(context.grammar(), child,
              destination_analysis->witness.request).subtree()) :
          validate_budgeted_derivation(context.grammar(), child,
              destination_analysis->witness.request, *context.offspring_budget());
      if (accepted) {
        // Short-lived pool workers need execution admission, not future sites.
        if (retain_analysis) (void)context.analyze(child);
        if (admissions.size() < 256) admissions.emplace(identity, true);
        return donor;
      }
    } catch (const std::invalid_argument&) {
      // Membership is a separate constraint on the complete contextual child.
    }
    if (admissions.size() < 256) admissions.emplace(identity, false);
    attempt_seed = attempts.next();
  }
  throw std::runtime_error("projected donor generation exhausted its attempt limit; feasibility is unresolved");
}

}  // namespace

ContextualDonor generate_donor(VariationContext& context, std::uint64_t seed,
    const VariationSite& site, const ProgramGenome& destination, std::size_t maximum_attempts) {
  std::unordered_map<std::string, bool> admissions;
  PoolDestination prepared;
  return generate_budgeted_donor(context, seed, site, destination, maximum_attempts, admissions, prepared);
}

std::vector<std::optional<ContextualDonor>> generate_donor_pool(VariationContext& context,
    const std::vector<std::uint64_t>& seeds, const VariationSite& site,
    const ProgramGenome& destination, std::size_t maximum_attempts) {
  // Crossover and unselected mutation sites have no donor work.
  if (seeds.empty()) return {};
  // The cache cannot escape this one destination/site/frame. Exact decoded donor
  // identities therefore imply the same spliced child and admission result.
  std::unordered_map<std::string, bool> admissions;
  PoolDestination prepared;
  std::vector<std::optional<ContextualDonor>> result;
  result.reserve(seeds.size());
  for (const auto seed : seeds) {
    try {
      result.emplace_back(generate_budgeted_donor(context, seed, site, destination,
          maximum_attempts, admissions, prepared));
    } catch (const std::runtime_error&) {
      result.emplace_back(std::nullopt);
    }
  }
  return result;
}

}  // namespace gagp::evo::grammar
