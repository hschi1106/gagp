#include <algorithm>
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/evolution/repro/pack.hpp"
#include "gagp/evolution/repro/prep.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/payload/payload.hpp"
#include "splice_metadata.hpp"

namespace {

using namespace gagp;
using namespace gagp::evo;
using gagp::evo::repro::SpliceOccurrence;
using gagp::evo::repro::PackedChildSplice;
using gagp::evo::repro::PackedHostData;
using gagp::evo::repro::SpliceSourceKind;
using gagp::evo::repro::reconstruct_compiled_child_metadata;
using gagp::evo::repro::reconstruct_splice_metadata;

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

std::vector<AstNode> splice_nodes(
    const AstProgram& base, const std::vector<SpliceOccurrence>& occurrences,
    const std::vector<AstNode>& inserted) {
  std::vector<AstNode> nodes;
  std::size_t cursor = 0;
  for (const auto& occurrence : occurrences) {
    nodes.insert(nodes.end(), base.nodes.begin() + static_cast<std::ptrdiff_t>(cursor),
                 base.nodes.begin() + static_cast<std::ptrdiff_t>(occurrence.begin));
    nodes.insert(nodes.end(), inserted.begin(), inserted.end());
    cursor = occurrence.end;
  }
  nodes.insert(nodes.end(), base.nodes.begin() + static_cast<std::ptrdiff_t>(cursor),
               base.nodes.end());
  return nodes;
}

bool same_structure_except_region_ids(const std::vector<AstNode>& before,
                                      const std::vector<AstNode>& after) {
  if (before.size() != after.size()) return false;
  for (std::size_t i = 0; i < before.size(); ++i) {
    if (before[i].kind != after[i].kind || before[i].i1 != after[i].i1)
      return false;
    if (before[i].kind != NodeKind::REGION_VAR && before[i].i0 != after[i].i0)
      return false;
  }
  return true;
}

ExecResult execute(const AstProgram& ast) {
  const AstVerifyResult verified = verify_ast(ast, {});
  require(verified.ok, verified.diagnostic.message.c_str());
  ProgramGenome genome;
  genome.ast = ast;
  genome.meta = build_genome_meta(ast);
  return execute_bytecode_cpu(compile_for_eval(genome, verified.verified), {}, 1000);
}

ExecResult execute_named(const AstProgram& ast, const std::string& name,
                         const Value& value) {
  const AstVerifyResult verified = verify_ast(ast, {});
  require(verified.ok, verified.diagnostic.message.c_str());
  ProgramGenome genome;
  genome.ast = ast;
  genome.meta = build_genome_meta(ast);
  const BytecodeProgram bytecode = compile_for_eval(genome, verified.verified);
  return execute_bytecode_cpu(
      bytecode, {{bytecode.var2idx.at(name), value}}, 1000);
}

void require_valid(const AstProgram& ast, const char* message) {
  require(verify_ast(ast, {}).ok, message);
}

void test_repeated_lexical_capture_and_freshening() {
  AstProgram base;
  base.consts = {Value::from_int(10), Value::from_int(20), Value::from_int(0)};
  base.nodes = {
      {NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
      {NodeKind::ADD},
      {NodeKind::LET_REGION}, {NodeKind::CONST, 0}, {NodeKind::CONST, 2},
      {NodeKind::LET_REGION}, {NodeKind::CONST, 1}, {NodeKind::CONST, 2},
      {NodeKind::BLOCK_NIL},
  };
  base.lexical_regions = {
      {4, 1, {{1, RType::Int}}},
      {7, 1, {{7, RType::Int}}},
  };

  AstProgram donor;
  donor.consts = {Value::from_int(0)};
  donor.nodes = {
      {NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
      {NodeKind::LET_REGION}, {NodeKind::CONST, 0},
      {NodeKind::ADD}, {NodeKind::REGION_VAR, 40},
      {NodeKind::LET_REGION}, {NodeKind::CONST, 0},
      {NodeKind::REGION_VAR, 7}, {NodeKind::BLOCK_NIL},
  };
  donor.lexical_regions = {
      {3, 1, {{40, RType::Int}}},
      {7, 1, {{7, RType::Int}}},
  };
  require_valid(base, "repeated lexical base fixture is invalid");
  require_valid(donor, "repeated lexical donor fixture is invalid");

  const std::vector<SpliceOccurrence> occurrences = {
      {6, 7, {1}}, {9, 10, {7}}};
  std::vector<AstNode> inserted(donor.nodes.begin() + 5,
                                donor.nodes.begin() + 10);
  inserted[3].i0 = 2;
  AstProgram child = base;
  child.nodes = splice_nodes(base, occurrences, inserted);
  const auto before = child.nodes;

  reconstruct_splice_metadata(child, base, donor, occurrences, 5, 10, {40});
  require(same_structure_except_region_ids(before, child.nodes),
          "metadata reconstruction changed the pre-spliced structure");
  require(child.lexical_regions.size() == 4 &&
              child.lexical_regions[0].node_index == 4 &&
              child.lexical_regions[1].node_index == 8 &&
              child.lexical_regions[2].node_index == 11 &&
              child.lexical_regions[3].node_index == 15,
          "repeated splice did not relocate lexical owners");
  const int first_introduced = child.lexical_regions[1].bindings[0].id;
  const int second_introduced = child.lexical_regions[3].bindings[0].id;
  require(first_introduced != second_introduced && first_introduced != 1 &&
              first_introduced != 7 && second_introduced != 1 &&
              second_introduced != 7,
          "introduced binders were not freshened independently per copy");
  require(child.nodes[7].i0 == 1 && child.nodes[10].i0 == first_introduced &&
              child.nodes[14].i0 == 7 && child.nodes[17].i0 == second_introduced,
          "repeated free and introduced REGION_VAR references were not remapped");
  const ExecResult result = execute(child);
  require(!result.is_error && result.value.tag == ValueTag::Int &&
              result.value.i == 30,
          "reconstructed repeated lexical splice returned the wrong value");
}

RegionPlan counter_plan() {
  RegionPlan plan;
  plan.state_types = {ValueTag::Int};
  plan.result_type = ValueTag::Int;
  plan.parameter_types = {ValueTag::Int};
  RegionStateTransition edge;
  edge.kind = RegionTransitionKind::CoordinateOffset;
  edge.offset = -1;
  plan.requests = {{{edge}}};
  plan.limits = {8, 8, 1};
  plan.memoized = true;
  plan.coordinate_slots = {0};
  plan.coordinate_rank = {{0, 1}};
  plan.coordinate_domains = {
      {{RegionBoundKind::Literal, 0, 0},
       {RegionBoundKind::Literal, 5, 0}}};
  return plan;
}

void test_bounded_bindings_and_lexical_capture() {
  AstProgram base;
  base.consts = {Value::from_int(5), Value::from_int(0),
                 Value::from_int(1), Value::from_int(3)};
  base.nodes = {
      {NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
      {NodeKind::LET_REGION}, {NodeKind::CONST, 0}, {NodeKind::CONST, 1},
      {NodeKind::BLOCK_NIL},
  };
  base.lexical_regions = {{3, 1, {{9, RType::Int}}}};

  AstProgram donor;
  donor.consts = {Value::from_int(5), Value::from_int(3),
                  Value::from_int(0), Value::from_int(1)};
  donor.nodes = {
      {NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
      {NodeKind::LET_REGION}, {NodeKind::CONST, 0},
      {NodeKind::BOUNDED_REGION, 5}, {NodeKind::CONST, 1},
      {NodeKind::LE}, {NodeKind::REGION_VAR, 0}, {NodeKind::CONST, 2},
      {NodeKind::REGION_VAR, 2},
      {NodeKind::ADD}, {NodeKind::REGION_VAR, 1}, {NodeKind::CONST, 3},
      {NodeKind::CONST, 2}, {NodeKind::BLOCK_NIL},
  };
  BoundedRegionSpec spec;
  spec.node_index = 5;
  spec.plan = counter_plan();
  spec.parameters = {{RegionCaptureKind::Lexical, 40}};
  spec.phases = {
      {1, {{{RegionSlotBank::State, 0}, 0}}},
      {2, {{{RegionSlotBank::Parameter, 0}, 2}}},
      {3, {{{RegionSlotBank::Result, 0}, 1}}},
      {4, {}},
  };
  donor.bounded_region_specs = {spec};
  donor.lexical_regions = {{3, 1, {{40, RType::Int}}}};
  require_valid(base, "bounded capture base fixture is invalid");
  require_valid(donor, "bounded capture donor fixture is invalid");

  const std::vector<SpliceOccurrence> occurrences = {{5, 6, {9}}};
  std::vector<AstNode> inserted(donor.nodes.begin() + 5,
                                donor.nodes.begin() + 15);
  inserted[1].i0 = 3;
  inserted[4].i0 = 1;
  inserted[8].i0 = 2;
  inserted[9].i0 = 1;
  AstProgram child = base;
  child.nodes = splice_nodes(base, occurrences, inserted);
  const auto before = child.nodes;

  reconstruct_splice_metadata(child, base, donor, occurrences, 5, 15, {40});
  require(same_structure_except_region_ids(before, child.nodes),
          "bounded metadata reconstruction changed the node stream");
  require(child.bounded_region_specs.size() == 1 &&
              child.bounded_region_specs[0].node_index == 5 &&
              child.bounded_region_specs[0].parameters.size() == 1 &&
              child.bounded_region_specs[0].parameters[0].kind ==
                  RegionCaptureKind::Lexical &&
              child.bounded_region_specs[0].parameters[0].index == 9,
          "bounded plan or lexical capture was not reconstructed");
  const auto& phases = child.bounded_region_specs[0].phases;
  require(phases.size() == 4 && phases[0].bindings[0].binder_id != 9 &&
              phases[1].bindings[0].binder_id != 9 &&
              phases[2].bindings[0].binder_id != 9 &&
              phases[0].bindings[0].binder_id != phases[1].bindings[0].binder_id &&
              phases[0].bindings[0].binder_id != phases[2].bindings[0].binder_id &&
              phases[1].bindings[0].binder_id != phases[2].bindings[0].binder_id,
          "bounded phase binders were not freshened distinctly");
  require(child.nodes[8].i0 == phases[0].bindings[0].binder_id &&
              child.nodes[10].i0 == phases[1].bindings[0].binder_id &&
              child.nodes[12].i0 == phases[2].bindings[0].binder_id,
          "bounded phase REGION_VAR references did not follow fresh binders");
  const ExecResult result = execute(child);
  require(!result.is_error && result.value.tag == ValueTag::Int &&
              result.value.i == 8,
          "reconstructed bounded lexical capture returned the wrong value");
}

void test_bounded_name_capture_remaps_reordered_table() {
  AstProgram base;
  base.names = {"destination_only", "cap"};
  base.consts = {Value::from_int(99), Value::from_int(0),
                 Value::from_int(1), Value::from_int(3)};
  base.nodes = {
      {NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
      {NodeKind::CONST, 0}, {NodeKind::BLOCK_NIL},
  };

  AstProgram donor;
  donor.names = {"unused", "cap"};
  donor.consts = {Value::from_int(0), Value::from_int(1),
                  Value::from_int(3)};
  donor.nodes = {
      {NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
      {NodeKind::BOUNDED_REGION, 5}, {NodeKind::CONST, 2},
      {NodeKind::LE}, {NodeKind::REGION_VAR, 0}, {NodeKind::CONST, 0},
      {NodeKind::REGION_VAR, 2},
      {NodeKind::ADD}, {NodeKind::REGION_VAR, 1}, {NodeKind::CONST, 1},
      {NodeKind::CONST, 0}, {NodeKind::BLOCK_NIL},
  };
  BoundedRegionSpec spec;
  spec.node_index = 3;
  spec.plan = counter_plan();
  spec.parameters = {{RegionCaptureKind::Name, 1}};
  spec.phases = {
      {1, {{{RegionSlotBank::State, 0}, 0}}},
      {2, {{{RegionSlotBank::Parameter, 0}, 2}}},
      {3, {{{RegionSlotBank::Result, 0}, 1}}},
      {4, {}},
  };
  donor.bounded_region_specs = {spec};
  require_valid(base, "name capture base fixture is invalid");
  require_valid(donor, "name capture donor fixture is invalid");

  const std::vector<SpliceOccurrence> occurrences = {{3, 4, {}}};
  std::vector<AstNode> inserted(donor.nodes.begin() + 3,
                                donor.nodes.begin() + 13);
  inserted[1].i0 = 3;
  inserted[4].i0 = 1;
  inserted[8].i0 = 2;
  inserted[9].i0 = 1;
  AstProgram child = base;
  child.nodes = splice_nodes(base, occurrences, inserted);
  child.names = {"cap", "destination_only"};
  const auto names_before = child.names;

  reconstruct_splice_metadata(child, base, donor, occurrences, 3, 13, {});
  require(child.names == names_before &&
              child.bounded_region_specs.size() == 1 &&
              child.bounded_region_specs[0].parameters.size() == 1 &&
              child.bounded_region_specs[0].parameters[0].kind ==
                  RegionCaptureKind::Name &&
              child.bounded_region_specs[0].parameters[0].index == 0,
          "bounded name capture did not follow the reordered child table");
  const ExecResult result =
      execute_named(child, "cap", Value::from_int(7));
  require(!result.is_error && result.value.tag == ValueTag::Int &&
              result.value.i == 10,
          "remapped bounded name capture returned the wrong value");
}

struct TraversalFixture {
  AstProgram base;
  AstProgram donor;
  AstProgram child;
  std::vector<SpliceOccurrence> occurrences;
};

TraversalFixture traversal_fixture() {
  TraversalFixture fixture;
  fixture.base.consts = {
      Value::from_int(0), Value::from_int(4),
      payload::make_int_list_value(
          {Value::from_int(1), Value::from_int(2), Value::from_int(3)}),
  };
  fixture.base.nodes = {
      {NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
      {NodeKind::ADD}, {NodeKind::CONST, 0},
      {NodeKind::LET_REGION}, {NodeKind::CONST, 1},
      {NodeKind::REGION_VAR, 20}, {NodeKind::BLOCK_NIL},
  };
  fixture.base.lexical_regions = {{5, 1, {{20, RType::Int}}}};
  fixture.base.fuel_specs = {{5, {{FuelEvent::Bind, 7}}}};

  fixture.donor.consts = {
      payload::make_int_list_value(
          {Value::from_int(1), Value::from_int(2), Value::from_int(3)}),
      Value::from_int(0),
  };
  fixture.donor.nodes = {
      {NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
      {NodeKind::TRAVERSE}, {NodeKind::CONST, 0}, {NodeKind::CONST, 1},
      {NodeKind::CONST, 1}, {NodeKind::ADD},
      {NodeKind::REGION_VAR, 30}, {NodeKind::REGION_VAR, 32},
      {NodeKind::BLOCK_NIL},
  };
  fixture.donor.lexical_regions = {{3, 3, {
      {30, RType::Int}, {31, RType::Int}, {32, RType::Int}}}};
  fixture.donor.traversal_specs = {{3, TraversalDirection::Reverse}};
  fixture.donor.fuel_specs = {
      {3, {{FuelEvent::Repeat, 2}}},
      {7, {{FuelEvent::Operation, 3}}},
  };
  require_valid(fixture.base, "traversal base fixture is invalid");
  require_valid(fixture.donor, "traversal donor fixture is invalid");

  fixture.occurrences = {{4, 5, {}}};
  std::vector<AstNode> inserted(fixture.donor.nodes.begin() + 3,
                                fixture.donor.nodes.begin() + 10);
  inserted[1].i0 = 2;
  inserted[2].i0 = 0;
  inserted[3].i0 = 0;
  fixture.child = fixture.base;
  fixture.child.nodes = splice_nodes(fixture.base, fixture.occurrences, inserted);
  return fixture;
}

void test_traversal_and_fuel_relocation() {
  auto fixture = traversal_fixture();
  const auto before = fixture.child.nodes;
  reconstruct_splice_metadata(fixture.child, fixture.base, fixture.donor,
                              fixture.occurrences, 3, 10, {});
  require(same_structure_except_region_ids(before, fixture.child.nodes),
          "traversal metadata reconstruction changed the node stream");
  require(fixture.child.lexical_regions.size() == 2 &&
              fixture.child.lexical_regions[0].node_index == 4 &&
              fixture.child.lexical_regions[1].node_index == 11,
          "traversal splice did not insert and shift lexical owners");
  require(fixture.child.traversal_specs.size() == 1 &&
              fixture.child.traversal_specs[0].node_index == 4 &&
              fixture.child.traversal_specs[0].direction ==
                  TraversalDirection::Reverse,
          "traversal direction metadata was not relocated");
  require(fixture.child.fuel_specs.size() == 3 &&
              fixture.child.fuel_specs[0].node_index == 4 &&
              fixture.child.fuel_specs[1].node_index == 8 &&
              fixture.child.fuel_specs[2].node_index == 11 &&
              fixture.child.fuel_specs[0].charges[0].event == FuelEvent::Repeat &&
              fixture.child.fuel_specs[1].charges[0].event == FuelEvent::Operation &&
              fixture.child.fuel_specs[2].charges[0].event == FuelEvent::Bind,
          "inserted and retained fuel metadata was not relocated");
  const ExecResult result = execute(fixture.child);
  require(!result.is_error && result.value.tag == ValueTag::Int &&
              result.value.i == 10,
          "reconstructed traversal splice returned the wrong value");
}

void test_rejection_is_transactional() {
  auto fixture = traversal_fixture();
  fixture.child.nodes[4].kind = NodeKind::CONST;
  const std::string before = ast_cache_key(fixture.child);
  bool rejected = false;
  try {
    reconstruct_splice_metadata(fixture.child, fixture.base, fixture.donor,
                                fixture.occurrences, 3, 10, {});
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "mismatched donor provenance was accepted");
  require(ast_cache_key(fixture.child) == before,
          "rejected metadata reconstruction changed the child");
}

std::shared_ptr<const grammar::CompiledGrammar> scoped_repeated_hole_grammar() {
  return std::make_shared<const grammar::CompiledGrammar>(
      grammar::compile_grammar(grammar::parse_definition(R"({
    "format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","type":"Int"},
    "search_limits":{"max_nodes":20,"max_depth":10},
    "execution_limits":{"fuel":100},
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
       "alternatives":[{"id":"bound","weight":1,
         "expression":{"bound":"x"}}]}
    ]
  })")));
}

repro::GpuReproConfig compiled_config(
    const grammar::CompiledGrammar& grammar, std::size_t population_size) {
  repro::GpuReproConfig config;
  config.donor_pool_size_per_site = 2;
  config.population_size = static_cast<int>(population_size);
  config.pair_count = static_cast<int>((population_size + 1) / 2);
  config.candidates_per_program = 16;
  config.max_nodes = static_cast<int>(grammar.search_limits().max_nodes);
  config.max_donor_nodes = 1;
  config.max_names = 1;
  config.max_consts = 1;
  config.max_expr_depth = static_cast<int>(grammar.search_limits().max_depth);
  config.seed = UINT64_C(0x123456789abcdef0);
  return config;
}

struct CompiledSpliceFixture {
  PackedHostData packed;
  int destination_candidate = -1;
  int parent_source_candidate = -1;
  int donor_source = -1;
};

CompiledSpliceFixture compiled_splice_fixture() {
  const auto grammar = scoped_repeated_hole_grammar();
  std::vector<ProgramGenome> population = {
      repro::compact_genome_tables(grammar::generate_derivation(*grammar, 3).genome),
      repro::compact_genome_tables(grammar::generate_derivation(*grammar, 4).genome),
  };
  grammar::VariationContext context(grammar);
  const auto config = compiled_config(*grammar, population.size());
  const auto prep = repro::preprocess_population(population, config, context);

  const auto repeated_index = [&](std::size_t parent) {
    const auto found = std::find_if(
        prep.candidates.at(parent).begin(), prep.candidates.at(parent).end(),
        [](const repro::CandidateRange& candidate) {
          return candidate.occurrence_count == 2;
        });
    require(found != prep.candidates.at(parent).end(),
            "compiled fixture lost its repeated scoped candidate");
    return static_cast<int>(found - prep.candidates.at(parent).begin());
  };

  CompiledSpliceFixture fixture;
  fixture.destination_candidate = repeated_index(0);
  fixture.parent_source_candidate = repeated_index(1);
  const auto& destination =
      prep.candidates[0][static_cast<std::size_t>(fixture.destination_candidate)];
  const auto& parent_source =
      prep.candidates[1][static_cast<std::size_t>(fixture.parent_source_candidate)];
  require(destination.donor_count > 0,
          "compiled fixture lost its contextual donor");
  fixture.donor_source = destination.donor_offset;
  require(parent_source.compatibility_id == destination.compatibility_id &&
              parent_source.materialized_nodes == 1 &&
              prep.donor_contracts.at(
                  static_cast<std::size_t>(fixture.donor_source))
                      .materialized_nodes == 1 &&
              prep.donor_pool.at(static_cast<std::size_t>(fixture.donor_source))
                      .ast.nodes.front().kind == NodeKind::REGION_VAR,
          "compiled fixture stopped producing compatible one-node captures");
  fixture.packed = repro::pack_population(population, prep, config);
  require(fixture.packed.compiled_grammar == grammar &&
              fixture.packed.compiled_sources &&
              fixture.packed.compatibility_keys.size() >
                  destination.compatibility_id,
          "compiled fixture did not retain its grammar owner and source tables");
  return fixture;
}

AstProgram device_style_child(const CompiledSpliceFixture& fixture,
                              const PackedChildSplice& splice) {
  const auto& base = fixture.packed.compiled_sources->parents.at(
      static_cast<std::size_t>(splice.base_parent));
  const AstProgram* source = nullptr;
  if (splice.source_kind == SpliceSourceKind::Parent) {
    source = &fixture.packed.compiled_sources->parents.at(
        static_cast<std::size_t>(splice.source_index));
  } else {
    source = &fixture.packed.compiled_sources->donors.at(
        static_cast<std::size_t>(splice.source_index));
  }
  std::vector<AstNode> inserted(
      source->nodes.begin() + splice.source_begin,
      source->nodes.begin() + splice.source_end);

  const auto& destination = fixture.packed.candidates.at(
      static_cast<std::size_t>(splice.base_parent) *
          static_cast<std::size_t>(fixture.packed.config.candidates_per_program) +
      static_cast<std::size_t>(splice.destination_candidate));
  std::vector<SpliceOccurrence> occurrences;
  for (int i = 0; i < destination.occurrence_count; ++i) {
    const auto& occurrence = fixture.packed.occurrences.at(
        static_cast<std::size_t>(destination.occurrence_offset + i));
    occurrences.push_back({static_cast<std::size_t>(occurrence.start),
                           static_cast<std::size_t>(occurrence.stop), {}});
  }
  AstProgram child = base;
  child.nodes = splice_nodes(base, occurrences, inserted);
  return child;
}

PackedChildSplice parent_splice(const CompiledSpliceFixture& fixture) {
  const auto& source = fixture.packed.candidates.at(
      static_cast<std::size_t>(fixture.packed.config.candidates_per_program) +
      static_cast<std::size_t>(fixture.parent_source_candidate));
  PackedChildSplice splice;
  splice.applied = 1;
  splice.base_parent = 0;
  splice.destination_candidate = fixture.destination_candidate;
  splice.source_kind = SpliceSourceKind::Parent;
  splice.source_index = 1;
  splice.source_candidate = fixture.parent_source_candidate;
  splice.source_begin = source.start;
  splice.source_end = source.stop;
  splice.occurrence_count = 2;
  return splice;
}

PackedChildSplice donor_splice(const CompiledSpliceFixture& fixture) {
  const auto& source = fixture.packed.compiled_sources->donors.at(
      static_cast<std::size_t>(fixture.donor_source));
  PackedChildSplice splice;
  splice.applied = 1;
  splice.base_parent = 0;
  splice.destination_candidate = fixture.destination_candidate;
  splice.source_kind = SpliceSourceKind::CompiledDonor;
  splice.source_index = fixture.donor_source;
  splice.source_candidate = -1;
  splice.source_begin = 0;
  splice.source_end = static_cast<int>(source.nodes.size());
  splice.occurrence_count = 2;
  return splice;
}

void require_compiled_capture_mapping(
    const CompiledSpliceFixture& fixture, const AstProgram& child) {
  const auto& destination = fixture.packed.candidates.at(
      static_cast<std::size_t>(fixture.destination_candidate));
  int mapped[2] = {-1, -1};
  for (int i = 0; i < 2; ++i) {
    const auto& occurrence = fixture.packed.occurrences.at(
        static_cast<std::size_t>(destination.occurrence_offset + i));
    require(occurrence.binder_count == 1 && occurrence.binder_offset >= 0,
            "compiled occurrence lost its binder slice");
    mapped[i] = fixture.packed.occurrence_binder_ids.at(
        static_cast<std::size_t>(occurrence.binder_offset));
    require(child.nodes.at(static_cast<std::size_t>(occurrence.start)).kind ==
                    NodeKind::REGION_VAR &&
                child.nodes.at(static_cast<std::size_t>(occurrence.start)).i0 ==
                    mapped[i],
            "compiled provenance did not map a repeated capture");
  }
  require(mapped[0] != mapped[1],
          "compiled repeated occurrences did not use distinct physical binders");
}

void test_compiled_parent_and_donor_provenance() {
  const auto fixture = compiled_splice_fixture();
  for (const auto splice : {parent_splice(fixture), donor_splice(fixture)}) {
    AstProgram child = device_style_child(fixture, splice);
    reconstruct_compiled_child_metadata(child, fixture.packed, splice);
    require_compiled_capture_mapping(fixture, child);
    require_valid(child, "compiled provenance produced an invalid child");
    const ExecResult result = execute(child);
    require(!result.is_error && result.value.tag == ValueTag::Int &&
                result.value.i == 3,
            "compiled provenance reconstructed the wrong repeated-capture behavior");
  }
}

void require_compiled_rejection(
    AstProgram child, const PackedHostData& packed,
    const PackedChildSplice& splice, const char* message) {
  const std::string before = ast_cache_key(child);
  bool rejected = false;
  try {
    reconstruct_compiled_child_metadata(child, packed, splice);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, message);
  require(ast_cache_key(child) == before,
          "rejected compiled provenance changed the child");
}

void test_compiled_provenance_rejections_are_transactional() {
  const auto fixture = compiled_splice_fixture();
  const auto valid = donor_splice(fixture);
  const auto child = device_style_child(fixture, valid);

  auto wrong_contract = fixture.packed;
  wrong_contract.donor_contracts.at(
      static_cast<std::size_t>(fixture.donor_source)).compatibility_id =
      repro::kNoCompatibilityId;
  require_compiled_rejection(child, wrong_contract, valid,
                             "out-of-contract donor provenance was accepted");

  auto wrong_interval = valid;
  ++wrong_interval.source_end;
  require_compiled_rejection(child, fixture.packed, wrong_interval,
                             "wrong source interval was accepted");

  const auto valid_parent = parent_splice(fixture);
  const auto parent_child = device_style_child(fixture, valid_parent);
  auto stale_parent_interval = fixture.packed;
  const auto& parent_contract = stale_parent_interval.candidates.at(
      static_cast<std::size_t>(stale_parent_interval.config.candidates_per_program) +
      static_cast<std::size_t>(fixture.parent_source_candidate));
  ++stale_parent_interval.occurrences.at(
         static_cast<std::size_t>(parent_contract.occurrence_offset)).start;
  require_compiled_rejection(parent_child, stale_parent_interval, valid_parent,
                             "stale parent occurrence interval was accepted");

  auto wrong_source = valid;
  wrong_source.source_index = static_cast<int>(
      fixture.packed.compiled_sources->donors.size());
  require_compiled_rejection(child, fixture.packed, wrong_source,
                             "out-of-range source index was accepted");

  auto wrong_binders = fixture.packed;
  const auto& destination = wrong_binders.candidates.at(
      static_cast<std::size_t>(fixture.destination_candidate));
  wrong_binders.occurrences.at(
      static_cast<std::size_t>(destination.occurrence_offset)).binder_offset =
      static_cast<int>(wrong_binders.occurrence_binder_ids.size()) + 1;
  require_compiled_rejection(child, wrong_binders, valid,
                             "out-of-range occurrence binder slice was accepted");

  auto wrong_donor_binders = fixture.packed;
  wrong_donor_binders.donor_contracts.at(
      static_cast<std::size_t>(fixture.donor_source)).binder_offset =
      static_cast<int>(wrong_donor_binders.donor_binder_ids.size()) + 1;
  require_compiled_rejection(child, wrong_donor_binders, valid,
                             "out-of-range donor binder slice was accepted");
}

}  // namespace

int main() {
  try {
    payload::clear();
    test_repeated_lexical_capture_and_freshening();
    test_bounded_bindings_and_lexical_capture();
    test_bounded_name_capture_remaps_reordered_table();
    test_traversal_and_fuel_relocation();
    test_rejection_is_transactional();
    test_compiled_parent_and_donor_provenance();
    test_compiled_provenance_rejections_are_transactional();
    std::cout << "gagp_test_splice_metadata: OK\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
