#include "gagp/evolution/grammar/donor.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/node_descriptor.hpp"

namespace gagp::evo::grammar {
namespace {

std::uint32_t subtree_depth(const AstProgram& ast, std::uint32_t* index,
    std::uint32_t end) {
  if (*index >= end)
    throw std::runtime_error("generated contextual donor has an incomplete payload subtree");
  const auto& descriptor = node_descriptor(ast.nodes[*index].kind);
  ++*index;
  std::uint32_t child_depth = 0;
  for (int child = 0; child < descriptor.prefix_arity; ++child)
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

}  // namespace

ContextualDonor generate_donor(const CompiledGrammar& grammar, std::uint64_t seed,
    const VariationSite& site) {
  validate_site(grammar, site);

  try {
    const auto request = donor_request(site);
    const auto frame = donor_frame(grammar, site);
    auto generated = generate_derivation_in_frame(grammar, seed, request, frame);

    if (generated.derivation.seed_replayable)
      throw std::runtime_error("contextual donor retained seed-replayable provenance");
    if (generated.derivation.nodes.size() != generated.genome.ast.nodes.size())
      throw std::runtime_error("contextual donor witness does not cover its native AST");

    ContextualDonor donor;
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

}  // namespace gagp::evo::grammar
