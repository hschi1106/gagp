#include "executable_fragments.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <numeric>
#include <stdexcept>
#include <set>
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/grammar/cache.hpp"
#include "gagp/evolution/grammar/donor.hpp"
#include "gagp/evolution/grammar/derivation_resources.hpp"
#include "gagp/evolution/repro/pack.hpp"
#include "variation_internal.hpp"
#include "gagp/evolution/node_descriptor.hpp"
#include "gagp/evolution/grammar/values.hpp"

namespace gagp::evo {
RegionPhase compile_owned_phase(const AstProgram& fragment, const RegionAstPhase& bindings);
}
namespace gagp::evo::grammar {
namespace {
void require(bool value, const char* reason) {
  if (!value) throw std::invalid_argument(std::string("executable fragments: ") + reason);
}
struct Owner {
  std::shared_ptr<const CompiledGrammar> grammar;
  std::atomic<std::uint64_t> next{1};
  std::atomic<std::size_t> live{0};
};
std::vector<RegionPhase*> phase_rows(BytecodeProgram& code) {
  require(code.bounded_region_segments.size() == 1, "requires exactly one region");
  auto& s = code.bounded_region_segments[0];
  std::vector<RegionPhase*> result{&s.base_predicate, &s.base_body};
  for (auto& p : s.preparations) result.push_back(&p);
  for (auto& p : s.request_expressions) result.push_back(&p);
  result.push_back(&s.combine);
  if (s.boundary) result.push_back(&*s.boundary);
  return result;
}
struct Layout {
  VariationAnalysis analysis;
  std::vector<VariationSite> holes;
  std::vector<unsigned> ordinals, depths;
};
Layout analyze(const CompiledGrammar& grammar, const ProgramGenome& genome) {
  require(genome.ast.bounded_region_specs.size() == 1, "requires one AST region");
  for (const auto& value : genome.ast.consts)
    require(value.tag == ValueTag::Int || value.tag == ValueTag::Bool || value.tag == ValueTag::Float ||
        value.tag == ValueTag::Char, "registry-dependent imported constants require generic evolution");
  Layout layout;
  // Full admission at import/new-fragment construction. Public provenance is
  // never authority for the immutable fragment owner.
  ProgramGenome untrusted = genome;
  untrusted.derivation.reset();
  layout.analysis = analyze_variation(grammar, untrusted, entry_request(grammar));
  const auto& a = layout.analysis;
  const auto& region = genome.ast.bounded_region_specs[0];
  auto cursor = region.node_index + 1;
  for (unsigned j = 0; j < region.plan.state_types.size() + region.plan.bound_operand_count; ++j)
    cursor = a.verified.subtree_end.at(cursor);
  std::vector<unsigned> depths(genome.ast.nodes.size());
  std::vector<std::size_t> ends;
  for (std::size_t j = 0; j < depths.size(); ++j) {
    while (!ends.empty() && j >= ends.back()) ends.pop_back();
    depths[j] = ends.size() + 1; ends.push_back(a.verified.subtree_end[j]);
  }
  for (unsigned ordinal = 0; ordinal < region.phases.size(); ++ordinal) {
    const auto end = a.verified.subtree_end.at(cursor);
    for (const auto& site : a.sites) {
      if (site.occurrences.size() != 1 || site.occurrences[0].begin != cursor ||
          site.occurrences[0].end != end || site.slot == kNoGrammarId ||
          site.template_id == kNoGrammarId) continue;
      for (const auto& old : layout.holes)
        require(old.slot != site.slot || old.template_id != site.template_id,
            "cross-phase coupled holes require generic evolution");
      layout.holes.push_back(site); layout.ordinals.push_back(ordinal);
      layout.depths.push_back(depths[cursor]); break;
    }
    cursor = end;
  }
  require(!layout.holes.empty(), "no independently composable grammar phase holes");
  return layout;
}
// Canonical content only at import/audit, never to re-prove a live handle.
// Constant-pool sharing/order is not part of the root expression contract.
void canonical_constants(AstProgram& ast) {
  std::map<std::string, Value> values;
  for (const auto& v : ast.consts) values.emplace(canonical_constant_encoding(v), v);
  std::map<std::string, int> indices; std::vector<Value> constants;
  for (const auto& item : values) { indices[item.first] = constants.size(); constants.push_back(item.second); }
  std::vector<int> remap;
  for (const auto& v : ast.consts) remap.push_back(indices.at(canonical_constant_encoding(v)));
  for (auto& node : ast.nodes) {
    const auto& descriptor = node_descriptor(node.kind);
    if (descriptor.i0_role == NodeIndexRole::Constant) node.i0 = remap.at(node.i0);
    if (descriptor.i1_role == NodeIndexRole::Constant) node.i1 = remap.at(node.i1);
  }
  ast.consts = std::move(constants);
}
AstProgram extract(const AstProgram& source, VariationSpan span) {
  AstProgram out;
  out.names = source.names; out.consts = source.consts;
  out.nodes.assign(source.nodes.begin() + span.begin, source.nodes.begin() + span.end);
  for (const auto& r : source.lexical_regions)
    require(r.node_index < span.begin || r.node_index >= span.end,
        "nested lexical phase structures require generic evolution");
  for (const auto& r : source.traversal_specs)
    require(r.node_index < span.begin || r.node_index >= span.end,
        "nested traversal phase structures require generic evolution");
  for (const auto& r : source.bounded_region_specs)
    require(r.node_index < span.begin || r.node_index >= span.end,
        "nested regions require generic evolution");
  for (const auto& f : source.fuel_specs) if (f.node_index >= span.begin && f.node_index < span.end) {
    out.fuel_specs.push_back(f); out.fuel_specs.back().node_index -= span.begin;
  }
  ProgramGenome compact; compact.ast = std::move(out);
  auto result = repro::compact_genome_tables(std::move(compact)).ast;
  canonical_constants(result);
  return result;
}
}  // namespace

struct ExecutableFragments::Fragment {
  std::shared_ptr<Owner> owner;
  std::uint64_t id;
  unsigned slot;
  AstProgram source;  // only this phase, never its full source program/history
  RegionPhase code;
  RegionExecutableLayout::Phase gpu_phase;
  const RegionPhase& bytecode() const { return gpu_phase ? gpu_phase->code() : code; }
  VariationSite contract;
  std::vector<VariationSite> sites; // certified relative logical-hole groups
  Fragment(std::shared_ptr<Owner> o, unsigned s) : owner(std::move(o)),
      id(owner->next.fetch_add(1)), slot(s) { ++owner->live; }
  ~Fragment() { --owner->live; }
};
struct ExecutableFragments::Impl {
  std::shared_ptr<Owner> owner = std::make_shared<Owner>();
  ProgramGenome base;
  BytecodeProgram code;
  std::shared_ptr<const RegionExecutableLayout> gpu_layout;
  std::vector<RegionExecutableLayout::Phase> gpu_fixed_phases;
  Layout layout;
  std::vector<std::string> inputs;
  std::string skeleton;
  std::size_t fixed_nodes;

  void validate(const Genome& genome) const {
    require(genome.phases_.size() == layout.holes.size(), "empty/stale or wrong-layout genome");
    for (std::size_t p = 0; p < genome.phases_.size(); ++p)
      require(genome.phases_[p] && genome.phases_[p]->owner == owner && genome.phases_[p]->slot == p,
          "wrong owner/grammar/profile/slot");
  }
  std::string normalized(const ProgramGenome& genome, const Layout& source) const {
    require(source.ordinals == layout.ordinals && source.holes.size() == layout.holes.size(),
        "different phase layout");
    ProgramGenome normalized = genome;
    for (std::size_t p = source.holes.size(); p-- > 0;) {
      require(compatible_sites(source.holes[p], layout.holes[p]), "different grammar hole contract");
      normalized.ast = variation_detail::splice(normalized.ast, source.holes[p], base.ast,
          layout.holes[p].occurrences[0], layout.holes[p].occurrence_binder_ids[0]);
    }
    normalized = repro::compact_genome_tables(std::move(normalized));
    canonical_constants(normalized.ast);
    return runtime_cache_identity(normalized, inputs, owner->grammar->execution_limits().fuel);
  }
  std::shared_ptr<const Fragment> fragment(const ProgramGenome& genome, const Layout& source,
      BytecodeProgram& compiled, unsigned p) const {
    const auto& hole = source.holes.at(p); const auto span = hole.occurrences.at(0);
    require(std::uint64_t(source.analysis.witness.logical_steps) * (layout.holes.size() + 1) <= 1048576 &&
        std::uint64_t(source.analysis.witness.lowered_instructions) * (layout.holes.size() + 1) <= 1048576,
        "conservative combination construction limit exceeded");
    auto f = std::make_shared<Fragment>(owner, p);
    f->source = extract(genome.ast, span);
    f->code = *phase_rows(compiled).at(source.ordinals[p]);
    if (gpu_layout) f->gpu_phase = gpu_layout->admit_phase(source.ordinals[p], std::move(f->code));
    f->contract = hole;
    f->contract.occurrences = {{0, static_cast<unsigned>(f->source.nodes.size())}};
    for (const auto& site : source.analysis.sites) {
      // Atomic replacement includes every logical occurrence. Never split a
      // repeated hole or infer logical coupling from identical content.
      if (site.occurrences.empty() || !std::all_of(site.occurrences.begin(), site.occurrences.end(),
          [&](auto s) { return s.begin >= span.begin && s.end <= span.end; })) continue;
      auto local = site;
      for (auto& s : local.occurrences) { s.begin -= span.begin; s.end -= span.begin; }
      f->sites.push_back(std::move(local));
    }
    return f;
  }
  std::shared_ptr<const Fragment> admit_phase(AstProgram candidate, const Fragment& old, unsigned p) const {
    ProgramGenome envelope;
    envelope.ast = std::move(candidate);
    envelope.ast.nodes.insert(envelope.ast.nodes.begin(), {{NodeKind::PROGRAM,0,0},
        {NodeKind::BLOCK_CONS,0,0},{NodeKind::RETURN,0,0}});
    envelope.ast.nodes.push_back({NodeKind::BLOCK_NIL,0,0});
    for (auto& f : envelope.ast.fuel_specs) f.node_index += 3;
    GenerationFrame frame; frame.binder_ids = old.contract.occurrence_binder_ids[0];
    auto request = donor_request(layout.holes[p]);
    auto analysis = analyze_closed_phase(*owner->grammar, envelope, request, frame);
    require(std::uint64_t(analysis.witness.logical_steps) * (layout.holes.size() + 1) <= 1048576 &&
        std::uint64_t(analysis.witness.lowered_instructions) * (layout.holes.size() + 1) <= 1048576,
        "phase exceeds conservative construction capacity");
    const auto span = VariationSpan{3, static_cast<unsigned>(envelope.ast.nodes.size()-1)};
    auto f = std::make_shared<Fragment>(owner, p);
    f->source = extract(envelope.ast, span);
    f->code = compile_owned_phase(f->source, base.ast.bounded_region_specs[0].phases.at(layout.ordinals[p]));
    if (gpu_layout) f->gpu_phase = gpu_layout->admit_phase(layout.ordinals[p], std::move(f->code));
    bool found = false;
    for (auto site : analysis.sites) {
      if (site.occurrences.empty() || !std::all_of(site.occurrences.begin(),site.occurrences.end(),
          [&](auto s){return s.begin>=span.begin && s.end<=span.end;})) continue;
      const bool root = site.nonterminal == old.contract.nonterminal && site.occurrences.size() == 1 &&
          site.occurrences[0].begin == span.begin && site.occurrences[0].end == span.end;
      for (auto& s : site.occurrences) { s.begin-=3;s.end-=3; }
      const auto enclosing = 256u - old.contract.remaining_template_nesting;
      require(site.remaining_template_nesting >= enclosing, "phase exceeds enclosing template capacity");
      site.remaining_template_nesting -= enclosing;
      if (root) {
        // Retain the incoming hole boundary; isolated reconstruction has no
        // enclosing template instance. Its internal logical groups stay distinct.
        site.template_id=old.contract.template_id;site.slot=old.contract.slot;
        site.compatibility_key=old.contract.compatibility_key;
        site.remaining_template_nesting=old.contract.remaining_template_nesting;
        f->contract=site; found=true;
      }
      f->sites.push_back(std::move(site));
    }
    require(found,"closed phase lost incoming nonterminal contract");
    return f;
  }
  bool fits(const Genome& genome) const {
    std::size_t count = fixed_nodes;
    const auto request = entry_request(*owner->grammar);
    for (std::size_t p = 0; p < genome.phases_.size(); ++p) {
      const auto& f = *genome.phases_[p]; count += f.source.nodes.size();
      if (f.contract.materialized_depth + layout.depths[p] - 1 > request.budget.max_depth ||
          !donor_fits(layout.holes[p], f.contract)) return false;
    }
    return count <= request.budget.max_nodes;
  }
  ProgramGenome hydrate(const AstProgram& source, const VariationSite& contract, unsigned slot) const {
    ProgramGenome g;
    g.ast = variation_detail::splice(base.ast, layout.holes.at(slot), source,
        {0, static_cast<unsigned>(source.nodes.size())}, contract.occurrence_binder_ids.at(0));
    return repro::compact_genome_tables(std::move(g));
  }
};
ExecutableFragments::ExecutableFragments(std::shared_ptr<const CompiledGrammar> grammar,
    const ProgramGenome& exemplar, std::vector<std::string> inputs) : impl_(std::make_unique<Impl>()) {
  require(bool(grammar), "missing grammar");
  require(resource_charges_are_local(*grammar), "nonlocal resource charges require generic evolution");
  for (const auto& domain : grammar->constants())
    require(domain.type == RType::Int || domain.type == RType::Bool || domain.type == RType::Float ||
        domain.type == RType::Char, "registry-dependent constants require generic evolution");
  impl_->owner->grammar = std::move(grammar); impl_->inputs = std::move(inputs);
  impl_->base = exemplar; impl_->base.derivation.reset();
  impl_->layout = analyze(*impl_->owner->grammar, impl_->base);
  impl_->code = compile_for_eval(impl_->base, impl_->layout.analysis.verified, impl_->inputs);
  if (std::getenv("GAGP_OWNED_EXECUTABLE")) {
    impl_->gpu_layout = RegionExecutableLayout::admit(impl_->code);
    for (std::size_t p=0;p<impl_->gpu_layout->phase_count();++p)
      impl_->gpu_fixed_phases.push_back(impl_->gpu_layout->initial_phase(p));
  }
  impl_->skeleton = impl_->normalized(impl_->base, impl_->layout);
  impl_->fixed_nodes = impl_->base.ast.nodes.size();
  for (const auto& site : impl_->layout.holes) impl_->fixed_nodes -= site.materialized_nodes;
}
ExecutableFragments::~ExecutableFragments() = default;
ExecutableFragments::Genome ExecutableFragments::import(const ProgramGenome& external) const {
  auto layout = analyze(*impl_->owner->grammar, external);
  require(impl_->normalized(external, layout) == impl_->skeleton, "root skeleton differs");
  auto compiled = compile_for_eval(external, layout.analysis.verified, impl_->inputs);
  Genome result;
  for (unsigned p = 0; p < layout.holes.size(); ++p)
    result.phases_.push_back(impl_->fragment(external, layout, compiled, p));
  require(impl_->fits(result), "import exceeds conservative combination budget");
  return result;
}
ProgramGenome ExecutableFragments::export_ast(const Genome& genome) const {
  impl_->validate(genome); ProgramGenome out = impl_->base;
  for (std::size_t p = genome.phases_.size(); p-- > 0;) {
    const auto& f = *genome.phases_[p];
    out.ast = variation_detail::splice(out.ast, impl_->layout.holes[p], f.source,
        f.contract.occurrences[0], f.contract.occurrence_binder_ids[0]);
  }
  out.derivation.reset();
  out = repro::compact_genome_tables(std::move(out));
  canonical_constants(out.ast);
  return out;
}
BytecodeProgram ExecutableFragments::executable(const Genome& genome) const {
  impl_->validate(genome); auto code = impl_->code; auto rows = phase_rows(code);
  for (std::size_t p = 0; p < genome.phases_.size(); ++p)
    *rows.at(impl_->layout.ordinals[p]) = genome.phases_[p]->bytecode();
  return code;
}
RegionExecutable ExecutableFragments::owned_executable(const Genome& genome) const {
  impl_->validate(genome);
  require(bool(impl_->gpu_layout),"owned executable profile was not enabled at owner construction");
  auto phases=impl_->gpu_fixed_phases;
  for(std::size_t p=0;p<genome.phases_.size();++p)phases[impl_->layout.ordinals[p]]=genome.phases_[p]->gpu_phase;
  return RegionExecutable::compose(impl_->gpu_layout,std::move(phases));
}
std::size_t ExecutableFragments::nodes(const Genome& genome) const {
  impl_->validate(genome); std::size_t n = impl_->fixed_nodes;
  for (const auto& f : genome.phases_) n += f->source.nodes.size(); return n;
}
std::vector<std::uint64_t> ExecutableFragments::identity(const Genome& genome) const {
  impl_->validate(genome); std::vector<std::uint64_t> ids;
  for (const auto& f : genome.phases_) ids.push_back(f->id); return ids;
}
std::size_t ExecutableFragments::live_fragments() const { return impl_->owner->live; }
ExecutableFragments::Change ExecutableFragments::vary(const Genome& parent, const Genome& donor,
    std::uint64_t seed, bool mutation) const {
  impl_->validate(parent); impl_->validate(donor);
  GrammarRandom random(seed); Change out{parent};
  const auto p = random.bounded(parent.phases_.size());
  const auto& f = *parent.phases_[p];
  if (f.sites.empty()) return out;
  const auto& site = f.sites[random.bounded(f.sites.size())];
  AstProgram candidate;
  if (mutation) {
    // A fresh grammar derivation in the exact destination scope, not a finite
    // bank or a type-only replacement. Local admission failures leave the parent.
    VariationContext context(impl_->owner->grammar, entry_request(*impl_->owner->grammar), 16);
    try {
      const auto generated = generate_donor(context, random.next(), site);
      if (!donor_fits(site, generated.nodes, generated.depth, generated.template_nesting)) {
        out.rejected = true; return out;
      }
      candidate = variation_detail::splice(f.source, site, generated.genome.ast,
          generated.payload, generated.frame.binder_ids);
    } catch (const std::runtime_error&) { out.rejected = true; return out; }
  } else {
    const Fragment* source = nullptr; const VariationSite* selected = nullptr;
    std::uint64_t count = 0;
    for (const auto& df : donor.phases_) for (const auto& ds : df->sites)
      if (compatible_sites(site, ds) && donor_fits(site, ds) && random.bounded(++count) == 0) {
        source = df.get(); selected = &ds;
      }
    if (!selected) return out;
    candidate = variation_detail::splice(f.source, site, source->source,
        selected->occurrences[0], selected->occurrence_binder_ids[0], site.crossover_closed);
  }
  try {
    std::shared_ptr<const Fragment> replacement;
    if (std::getenv("GAGP_LOCAL_FRAGMENT_ADMISSION")) {
      replacement = impl_->admit_phase(std::move(candidate), f, p);
    } else {
    auto materialized = impl_->hydrate(candidate, f.contract, p);
    auto layout = analyze(*impl_->owner->grammar, materialized);
    require(layout.ordinals == impl_->layout.ordinals, "new fragment changed layout");
    for (std::size_t q = 0; q < layout.holes.size(); ++q)
      require(compatible_sites(layout.holes[q], impl_->layout.holes[q]), "new fragment changed membership contract");
    auto compiled = compile_for_eval(materialized, layout.analysis.verified, impl_->inputs);
    replacement = impl_->fragment(materialized, layout, compiled, p);
    }
    // Persist only the new phase and its certified sites. Full analysis,
    // temporary source AST and every unused compiled phase die here.
    if (ast_cache_key(replacement->source) == ast_cache_key(f.source)) return out;
    out.genome.phases_[p] = std::move(replacement);
    if (!impl_->fits(out.genome)) { out.genome = parent; out.rejected = true; return out; }
    out.changed = true; return out;
  } catch (const std::invalid_argument&) { out.rejected = true; return out; }
}
}  // namespace gagp::evo::grammar
