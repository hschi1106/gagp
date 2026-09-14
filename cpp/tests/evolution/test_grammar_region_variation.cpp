#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/core/value.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/crossover.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/donor.hpp"
#include "gagp/evolution/grammar/frame.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/evolution/mutation.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;

namespace {

void check(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

template <class Action>
void rejects(Action action, const std::string& message) {
  try {
    action();
  } catch (const std::invalid_argument&) {
    return;
  } catch (const std::runtime_error&) {
    return;
  }
  throw std::runtime_error(message);
}

std::shared_ptr<const CompiledGrammar> compile_shared(const std::string& text) {
  return std::make_shared<const CompiledGrammar>(
      compile_grammar(parse_definition(text)));
}

std::uint32_t nonterminal(const CompiledGrammar& grammar,
                          const std::string& stable_id) {
  const auto found = std::find_if(grammar.nonterminals().begin(),
      grammar.nonterminals().end(), [&](const auto& value) {
        return value.stable_id == stable_id;
      });
  if (found == grammar.nonterminals().end())
    throw std::runtime_error("missing fixture nonterminal " + stable_id);
  return found->id;
}

const VariationSite& site_for(const VariationAnalysis& analysis,
                              std::uint32_t nt) {
  const auto found = std::find_if(analysis.sites.begin(), analysis.sites.end(),
      [&](const auto& site) { return site.nonterminal == nt; });
  if (found == analysis.sites.end())
    throw std::runtime_error("missing scoped variation site");
  return *found;
}

std::int64_t execute_int(const ProgramGenome& genome) {
  const auto result = execute_bytecode_cpu(compile_for_eval(genome), {}, 10000);
  check(!result.is_error && result.value.tag == ValueTag::Int,
        "scoped variation fixture failed native execution");
  return result.value.i;
}

void require_closed(const CompiledGrammar& grammar, const ProgramGenome& genome) {
  check(static_cast<bool>(verify_ast(genome.ast, {})),
        "variation returned a non-closed native AST");
  require_membership(grammar, genome);
}

std::vector<int> binder_ids(const AstProgram& ast) {
  std::vector<int> result;
  std::set<int> unique;
  for (const auto& region : ast.lexical_regions) {
    for (const auto& binding : region.bindings) {
      check(unique.insert(binding.id).second,
            "variation produced duplicate lexical declaration IDs");
      result.push_back(binding.id);
    }
  }
  return result;
}

int binder_at_node(const AstProgram& ast, std::size_t node_index) {
  const auto found = std::find_if(ast.lexical_regions.begin(),
      ast.lexical_regions.end(), [&](const auto& region) {
        return region.node_index == node_index;
      });
  if (found == ast.lexical_regions.end() || found->bindings.size() != 1)
    throw std::runtime_error("missing single-binder lexical region");
  return found->bindings.front().id;
}

ProgramGenome rename_binders(ProgramGenome genome,
                             const std::map<int, int>& renames) {
  for (auto& region : genome.ast.lexical_regions) {
    for (auto& binding : region.bindings) {
      const auto found = renames.find(binding.id);
      if (found != renames.end()) binding.id = found->second;
    }
  }
  for (auto& node : genome.ast.nodes) {
    if (node.kind != NodeKind::REGION_VAR) continue;
    const auto found = renames.find(node.i0);
    if (found != renames.end()) node.i0 = found->second;
  }
  genome.derivation.reset();
  return genome;
}

const char* scoped_grammar = R"({
  "format_version":"grammar-definition-v1",
  "entry":{"nonterminal":"Main","type":"Int"},
  "search_limits":{"max_nodes":30,"max_depth":12},
  "execution_limits":{"fuel":10000},
  "templates":[{"id":"Sibling","type":"Int","scope":[],
    "holes":[{"id":"value","type":"Int","scope":[
      {"name":"x","type":"Int"}]}],
    "body":{"signature":"add(Int,Int)->Int","args":[
      {"signature":"let(Int,Int)->Int","args":[
        {"constant":{"type":"Int","values":["1"]}},{"hole":"value"}],
       "bind":{"1":["x"]}},
      {"signature":"let(Int,Int)->Int","args":[
        {"constant":{"type":"Int","values":["2"]}},{"hole":"value"}],
       "bind":{"1":["x"]}}]}}],
  "nonterminals":[
    {"id":"Main","type":"Int","scope":[],"alternatives":[
      {"id":"main","weight":1,"expression":{"template":"Sibling","holes":{
        "value":{"ref":"Scoped"}}}}]},
    {"id":"Scoped","type":"Int","scope":[{"name":"x","type":"Int"}],
      "alternatives":[
        {"id":"bound","weight":1,"expression":{"bound":"x"}},
        {"id":"plus_ten","weight":1,
         "expression":{"signature":"add(Int,Int)->Int","args":[
           {"bound":"x"},{"constant":{"type":"Int","values":["10"]}}]}},
        {"id":"own_let","weight":1,
         "expression":{"signature":"let(Int,Int)->Int","args":[
           {"constant":{"type":"Int","values":["5"]}},
           {"signature":"add(Int,Int)->Int","args":[
             {"bound":"x"},{"bound":"y"}]}],"bind":{"1":["y"]}}}
      ]}
  ]
})";

struct Parents {
  ProgramGenome bound;
  ProgramGenome own_let;
};

Parents find_parents(const CompiledGrammar& grammar) {
  Parents result;
  for (std::uint64_t seed = 0; seed < 4096 &&
       (result.bound.ast.nodes.empty() || result.own_let.ast.nodes.empty()); ++seed) {
    auto candidate = generate_derivation(grammar, seed).genome;
    const auto value = execute_int(candidate);
    if (value == 3 && result.bound.ast.nodes.empty()) result.bound = candidate;
    if (value == 13 && result.own_let.ast.nodes.empty()) result.own_let = candidate;
  }
  check(!result.bound.ast.nodes.empty() && !result.own_let.ast.nodes.empty(),
        "could not generate bound and nested-Let scoped parents");
  return result;
}

void test_analysis_is_alpha_invariant(const CompiledGrammar& grammar,
                                      const ProgramGenome& parent,
                                      std::uint32_t scoped) {
  const auto analysis = analyze_variation(grammar, parent);
  const auto& site = site_for(analysis, scoped);
  check(site.occurrences.size() == 2 &&
            site.occurrence_binder_ids.size() == 2 &&
            site.occurrence_binder_ids[0].size() == 1 &&
            site.occurrence_binder_ids[1].size() == 1 &&
            site.occurrence_binder_ids[0][0] !=
                site.occurrence_binder_ids[1][0],
        "repeated scoped site lost its two physical capture environments");

  std::map<int, int> renames;
  int next = 100;
  for (int id : binder_ids(parent.ast)) renames.emplace(id, next++);
  const auto alpha = rename_binders(parent, renames);
  require_closed(grammar, alpha);
  const auto alpha_analysis = analyze_variation(grammar, alpha);
  const auto& alpha_site = site_for(alpha_analysis, scoped);
  check(compatible_sites(site, alpha_site),
        "physical binder IDs leaked into compatibility equality");
  check(site.occurrence_binder_ids != alpha_site.occurrence_binder_ids,
        "alpha-renamed site retained stale physical binder IDs");
}

void test_framed_bound_donor(const CompiledGrammar& grammar,
                             const ProgramGenome& parent,
                             std::uint32_t scoped) {
  const auto analysis = analyze_variation(grammar, parent);
  const auto& site = site_for(analysis, scoped);
  const auto request = donor_request(site);
  ContextualDonor donor;
  for (std::uint64_t seed = 0; seed < 256; ++seed) {
    auto candidate = generate_donor(grammar, seed, site);
    if (std::any_of(candidate.genome.ast.nodes.begin(),
        candidate.genome.ast.nodes.end(), [](const auto& node) {
          return node.kind == NodeKind::REGION_VAR;
        })) {
      donor = std::move(candidate);
      break;
    }
  }
  check(!donor.genome.ast.nodes.empty() && donor.frame.binder_ids.size() == 1,
        "contextual donor did not retain its native REGION_VAR capture");
  rejects([&] { require_membership(grammar, donor.genome, request); },
          "open contextual donor passed public closed membership");
  require_membership_in_frame(grammar, donor.genome, request, donor.frame);

  const auto projected = project_frame(grammar, request, donor.frame,
                                       donor.genome.ast);
  const auto verified = verify_ast(projected.ast, projected.inputs);
  check(static_cast<bool>(verified),
        "private frame projection did not verify as a closed AST");
  std::vector<std::string> names;
  for (const auto& input : projected.inputs) names.push_back(input.name);
  ProgramGenome projected_genome;
  projected_genome.ast = projected.ast;
  const auto bytecode = compile_for_eval(projected_genome, verified.verified, names);
  const auto synthetic = bytecode.var2idx.at(projected.inputs.back().name);
  const auto execution = execute_bytecode_cpu(
      bytecode, {{synthetic, Value::from_int(7)}}, 1000);
  check(!execution.is_error && execution.value.tag == ValueTag::Int &&
            (execution.value.i == 7 || execution.value.i == 12 ||
             execution.value.i == 17),
        "private frame projection did not supply the captured binder value");

  auto extra_request = request;
  extra_request.visible_environment.push_back({"extra", RType::Int});
  GenerationFrame duplicate = donor.frame;
  duplicate.binder_ids.push_back(duplicate.binder_ids.front());
  rejects([&] { (void)frame_inputs(grammar, extra_request, duplicate); },
          "frame accepted duplicate native binder IDs");
  GenerationFrame negative = donor.frame;
  negative.binder_ids[0] = -1;
  rejects([&] { (void)frame_inputs(grammar, request, negative); },
          "frame accepted a negative native binder ID");
  GenerationFrame wrong_count = donor.frame;
  wrong_count.binder_ids.push_back(91);
  rejects([&] { (void)frame_inputs(grammar, request, wrong_count); },
          "frame accepted binder IDs not aligned with visible scope");

  ContextualDonor introduced;
  for (std::uint64_t seed = 0; seed < 4096; ++seed) {
    auto candidate = generate_donor(grammar, seed, site);
    if (!candidate.genome.ast.lexical_regions.empty()) {
      introduced = std::move(candidate);
      break;
    }
  }
  check(!introduced.genome.ast.nodes.empty(),
        "could not generate a contextual donor with an introduced Let binder");
  auto collision = introduced.frame;
  collision.binder_ids[0] =
      introduced.genome.ast.lexical_regions.front().bindings.front().id;
  rejects([&] {
    (void)project_frame(grammar, request, collision, introduced.genome.ast);
  }, "frame projection accepted an external/introduced binder-ID collision");
}

void test_scoped_mutation_changes_child(
    const std::shared_ptr<const CompiledGrammar>& grammar,
    const ProgramGenome& parent) {
  const auto original_ids = binder_ids(parent.ast);
  bool changed = false;
  VariationContext context(grammar);
  for (std::uint64_t seed = 0; seed < 4096 && !changed; ++seed) {
    const auto child = mutate(parent, 10000 + seed, context, 1.0);
    require_closed(*grammar, child);
    const auto value = execute_int(child);
    if (value != 3 && binder_ids(child.ast) == original_ids) {
      check(value == 13 || value == 23,
            "scoped mutation split repeated captures between alternatives");
      changed = true;
    }
  }
  check(changed && context.counters().changed_children > 0,
        "scoped mutation produced only fallbacks or unchanged children");
}

void test_capture_collision_crossover(
    const std::shared_ptr<const CompiledGrammar>& grammar,
    ProgramGenome destination, const ProgramGenome& donor_parent,
    std::uint32_t scoped) {
  const auto donor_analysis = analyze_variation(*grammar, donor_parent);
  const auto& donor_site = site_for(donor_analysis, scoped);
  const auto payload = donor_site.occurrences.front();
  const auto introduced = std::find_if(donor_parent.ast.lexical_regions.begin(),
      donor_parent.ast.lexical_regions.end(), [&](const auto& region) {
        return region.node_index >= payload.begin && region.node_index < payload.end;
      });
  check(introduced != donor_parent.ast.lexical_regions.end(),
        "nested-Let crossover donor has no introduced declaration");
  const int collision_id = introduced->bindings.front().id;

  const auto destination_ids = binder_ids(destination.ast);
  check(destination_ids.size() == 2,
        "destination fixture does not have two sibling captures");
  destination = rename_binders(std::move(destination), {
      {destination_ids[0], collision_id}, {destination_ids[1], collision_id + 100}});
  require_closed(*grammar, destination);
  const auto renamed_ids = binder_ids(destination.ast);
  check(renamed_ids.front() == collision_id,
        "capture-collision fixture did not collide with donor declaration");
  const auto destination_analysis = analyze_variation(*grammar, destination);
  check(compatible_sites(site_for(destination_analysis, scoped), donor_site),
        "alpha-renamed destination became incompatible with its scoped donor");

  bool crossed = false;
  VariationContext context(grammar);
  for (std::uint64_t seed = 0; seed < 4096 && !crossed; ++seed) {
    const auto children = crossover(destination, donor_parent, 20000 + seed, context);
    require_closed(*grammar, children.first);
    require_closed(*grammar, children.second);
    if (execute_int(children.first) == 13 &&
        binder_at_node(children.first.ast, 4) == collision_id) {
      check(execute_int(children.second) == 3,
            "scoped crossover did not exchange repeated-hole choices atomically");
      const auto ids = binder_ids(children.first.ast);
      check(std::set<int>(ids.begin(), ids.end()).size() == ids.size(),
            "capture collision was not freshened before free-reference remapping");
      check(ids.size() == 4 &&
                std::count(ids.begin(), ids.end(), collision_id) == 1 &&
                std::count(ids.begin(), ids.end(), collision_id + 100) == 1,
            "crossover confused captured and donor-introduced binder IDs");
      crossed = true;
    }
  }
  check(crossed && context.counters().changed_children > 0,
        "scoped crossover never produced a changed capture-remapped child");
}

}  // namespace

int main() {
  try {
    const auto grammar = compile_shared(scoped_grammar);
    const auto scoped = nonterminal(*grammar, "Scoped");
    const auto parents = find_parents(*grammar);
    test_analysis_is_alpha_invariant(*grammar, parents.bound, scoped);
    test_framed_bound_donor(*grammar, parents.bound, scoped);
    test_scoped_mutation_changes_child(grammar, parents.bound);
    test_capture_collision_crossover(grammar, parents.bound,
                                     parents.own_let, scoped);
    std::cout << "gagp_test_grammar_region_variation: OK\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
