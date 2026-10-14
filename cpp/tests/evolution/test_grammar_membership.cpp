#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <tuple>
#include <optional>
#include <vector>

#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "../../src/runtime/payload/staging.hpp"
#include "../../src/evolution/grammar/evaluation_identity.hpp"
#include "../../src/evolution/grammar/variation_internal.hpp"
#include "gagp/evolution/grammar/cache.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/repro/pack.hpp"

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;

namespace {
void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void differential_admission(const CompiledGrammar& grammar, ProgramGenome genome) {
  genome.derivation.reset();  // Never compare a cached certificate with itself.
  const char* setting = std::getenv("GAGP_CPU_FUSED_ADMISSION");
  const std::optional<std::string> previous = setting ? std::optional<std::string>(setting) : std::nullopt;
  DerivationMetadata witnesses[2];
  std::vector<std::vector<int>> environments[2];
  bool accepted[2] = {};
  for (int mode = 0; mode < 2; ++mode) {
    if (mode) setenv("GAGP_CPU_FUSED_ADMISSION", "1", 1);
    else unsetenv("GAGP_CPU_FUSED_ADMISSION");
    try {
      witnesses[mode] = reconstruct_derivation(grammar, genome, entry_request(grammar),
          nullptr, &environments[mode]);
      accepted[mode] = true;
    } catch (const std::invalid_argument&) {}
  }
  if (previous) setenv("GAGP_CPU_FUSED_ADMISSION", previous->c_str(), 1);
  else unsetenv("GAGP_CPU_FUSED_ADMISSION");
  check(accepted[0] == accepted[1], "fused admission changed membership");
  if (!accepted[0]) return;
  const auto& a = witnesses[0]; const auto& b = witnesses[1];
  check(a.logical_steps == b.logical_steps && a.derived_nodes == b.derived_nodes &&
      a.lowered_instructions == b.lowered_instructions && a.nodes.size() == b.nodes.size() &&
      a.choices.size() == b.choices.size() && a.templates.size() == b.templates.size() &&
      a.holes.size() == b.holes.size() && environments[0] == environments[1],
      "fused admission changed witness dimensions or exact scope");
  for (std::size_t i = 0; i < a.nodes.size(); ++i) {
    const auto& x = a.nodes[i]; const auto& y = b.nodes[i];
    check(std::tie(x.expression, x.production, x.nonterminal, x.logical_instance,
        x.template_instance, x.slot, x.fixed, x.template_depth) ==
        std::tie(y.expression, y.production, y.nonterminal, y.logical_instance,
        y.template_instance, y.slot, y.fixed, y.template_depth), "fused node origin differs");
    const auto& r = a.resources->subtree(i); const auto& q = b.resources->subtree(i);
    check(std::tie(r.nodes, r.carried_depth, r.reset_depth) ==
        std::tie(q.nodes, q.carried_depth, q.reset_depth), "fused resource projection differs");
  }
  for (std::size_t i = 0; i < a.choices.size(); ++i) {
    const auto& x = a.choices[i]; const auto& y = b.choices[i];
    check(std::tie(x.nonterminal, x.production, x.parent, x.ast_begin, x.ast_end,
        x.template_instance, x.slot, x.enclosing_template_depth) ==
        std::tie(y.nonterminal, y.production, y.parent, y.ast_begin, y.ast_end,
        y.template_instance, y.slot, y.enclosing_template_depth), "fused canonical choice differs");
  }
  for (std::size_t i = 0; i < a.templates.size(); ++i)
    check(std::tie(a.templates[i].template_id, a.templates[i].parent) ==
        std::tie(b.templates[i].template_id, b.templates[i].parent), "fused template owner differs");
  for (std::size_t i = 0; i < a.holes.size(); ++i) {
    const auto& x = a.holes[i]; const auto& y = b.holes[i];
    check(std::tie(x.template_instance, x.slot, x.ast_begin, x.ast_end) ==
        std::tie(y.template_instance, y.slot, y.ast_begin, y.ast_end), "fused hole occurrence differs");
  }
}
void rejects(const CompiledGrammar& grammar, const ProgramGenome& genome) {
  differential_admission(grammar, genome);
  check(static_cast<bool>(verify_ast(genome.ast, {})), "rejection fixture must remain native-valid");
  try { require_membership(grammar, genome); }
  catch (const std::invalid_argument&) { return; }
  throw std::runtime_error("nonmember native AST was accepted");
}
cli_detail::JsonValue json(const std::string& text) {
  return cli_detail::JsonParser(text, {true, 256}).parse();
}
CompiledGrammar compile(const cli_detail::JsonValue& doc) {
  return compile_grammar(parse_definition(canonical_json(doc)));
}
std::string constant_grammar(const std::string& type, const std::string& values) {
  return R"({"format_version":"grammar-definition-v2","entry":{"nonterminal":"Value","type":")" + type +
    R"("},"search_limits":{"max_nodes":5,"max_depth":4},"execution_limits":{"fuel":100},
    "nonterminals":[{"id":"Value","type":")" + type + R"(","scope":[],"alternatives":[
    {"id":"constant","weight":1,"expression":{"constant":{"type":")" + type + R"(","values":)" + values + "}}}]}]}";
}
}  // namespace

int main() {
  try {
    setenv("GAGP_REUSE_ADMISSION_COMPILE", "1", 1);
    const auto scalar_doc = json(R"({"format_version":"grammar-definition-v2",
      "entry":{"nonterminal":"Expr","type":"Int"},
      "search_limits":{"max_nodes":40,"max_depth":12},"execution_limits":{"fuel":1000},
      "nonterminals":[{"id":"Expr","type":"Int","scope":[],"alternatives":[
        {"id":"leaf","weight":1,"expression":{"constant":{"type":"Int","range":["-3","3"]}}},
        {"id":"sum","weight":4,"expression":{"signature":"add(Int,Int)->Int","args":[{"ref":"Expr"},{"ref":"Expr"}]}}
      ]}]})");
    const auto scalar = compile(scalar_doc);
    ProgramGenome sum;
    for (std::uint64_t seed = 0; seed < 256; ++seed) {
      auto generated = generate_derivation(scalar, seed).genome;
      require_membership(scalar, generated);
      generated.derivation.reset();
      differential_admission(scalar, generated);
      require_membership(scalar, generated);
      if (generated.ast.nodes[3].kind == NodeKind::ADD) sum = generated;
    }
    check(!sum.ast.nodes.empty(), "scalar seeds did not exercise recursive sum");
    // A genuine proof can be reused; public provenance and stale ASTs cannot
    // authorize changed programs or a different request.
    sum.derivation = std::make_shared<const DerivationMetadata>(reconstruct_derivation(scalar, sum));
    check(bool(sum.derivation->certificate), "membership must retain opaque proof");
    const auto reused = reconstruct_derivation(scalar, sum);
    check(reused.certificate == sum.derivation->certificate, "identical membership should reuse proof");
    const auto executable = admitted_bytecode_for_eval(sum, {}, scalar.execution_limits().fuel);
    check(bool(executable), "scalar admission must retain its existing lowering");
    const auto combined = gagp::evo::grammar::detail::evaluation_identity(sum, {}, scalar.execution_limits().fuel);
    check(combined.executable == executable &&
          combined.key == runtime_cache_identity(sum, {}, scalar.execution_limits().fuel),
          "combined executable lookup must compute the real cache identity");
    auto stale_combined = sum;
    stale_combined.ast.consts.front() = Value::from_int(999);
    const auto stale_lookup = gagp::evo::grammar::detail::evaluation_identity(
        stale_combined, {}, scalar.execution_limits().fuel);
    check(!stale_lookup.executable && stale_lookup.key != combined.key,
          "combined lookup reused a stale mutable AST certificate");
    check(!gagp::evo::grammar::detail::evaluation_identity(sum, {"wrong_input"}, scalar.execution_limits().fuel).executable &&
          !gagp::evo::grammar::detail::evaluation_identity(sum, {}, scalar.execution_limits().fuel + 1).executable,
          "combined lookup ignored the execution contract");
    check(admitted_bytecode_for_eval(sum, {}, scalar.execution_limits().fuel) == executable,
          "unchanged scalar executable lost its sealed identity");
    check(!admitted_bytecode_for_eval(sum, {"wrong_input"}, scalar.execution_limits().fuel),
          "wrong input layout reused executable");
    check(!admitted_bytecode_for_eval(sum, {}, scalar.execution_limits().fuel + 1),
          "wrong execution contract reused executable");
    auto changed_executable = sum; changed_executable.ast.nodes[3].kind = NodeKind::SUB;
    check(!admitted_bytecode_for_eval(changed_executable, {}, scalar.execution_limits().fuel),
          "changed_executable AST reused stale executable");
    changed_executable = sum; changed_executable.ast.consts.front() = Value::from_int(999);
    check(!admitted_bytecode_for_eval(changed_executable, {}, scalar.execution_limits().fuel),
          "changed_executable constant reused stale executable");
    changed_executable = sum; changed_executable.derivation.reset();
    check(!admitted_bytecode_for_eval(changed_executable, {}, scalar.execution_limits().fuel),
          "unsealed AST obtained cached executable");
    auto forged_metadata = std::make_shared<DerivationMetadata>(*sum.derivation);
    forged_metadata->lowered_instructions = 0;
    auto forged = sum; forged.derivation = forged_metadata;
    check(admitted_bytecode_for_eval(forged, {}, scalar.execution_limits().fuel) == executable,
          "public annotations should not affect the sealed executable");
    check(reconstruct_derivation(scalar, forged).lowered_instructions == reused.lowered_instructions,
          "cached witness must not trust public provenance fields");
    auto stale = sum; stale.ast.nodes[3].kind = NodeKind::SUB;
    bool stale_rejected = false;
    try { (void)reconstruct_derivation(scalar, stale); }
    catch (const std::invalid_argument&) { stale_rejected = true; }
    check(stale_rejected, "stale proof must not admit changed AST");
    stale = sum;
    for (auto& value : stale.ast.consts) value = Value::from_int(99);
    stale_rejected = false;
    try { (void)reconstruct_derivation(scalar, stale); }
    catch (const std::invalid_argument&) { stale_rejected = true; }
    check(stale_rejected, "proof identity includes decoded constants");
    auto tight_request = entry_request(scalar); tight_request.budget.max_nodes = 5;
    stale_rejected = false;
    try { (void)reconstruct_derivation(scalar, sum, tight_request); }
    catch (const std::invalid_argument&) { stale_rejected = true; }
    check(stale_rejected, "proof cannot bypass changed request budget");
    auto forbidden = sum;
    forbidden.ast.nodes[3].kind = NodeKind::SUB;
    rejects(scalar, forbidden);
    forbidden = sum;
    for (auto& constant : forbidden.ast.consts) constant = Value::from_int(4);
    rejects(scalar, forbidden);
    auto changed = sum;
    for (auto& constant : changed.ast.consts) constant = Value::from_int(-3);
    require_membership(scalar, changed);
    for (auto& constant : changed.ast.consts) constant = Value::from_int(3);
    require_membership(scalar, changed);
    changed.ast.names.push_back("unused_name");
    changed.ast.consts.push_back(Value::from_int(999));
    require_membership(scalar, changed);
    require_membership(scalar, repro::compact_genome_tables(changed));

    // Removing unused constants after full admission must preserve canonical
    // choices and executable values. A stale external proof cannot authorize it.
    VariationContext compact_context(std::make_shared<const CompiledGrammar>(scalar));
    for (bool remove_name : {false, true}) {
      auto padded = sum;
      padded.ast.consts.insert(padded.ast.consts.begin(), Value::from_bool(false));
      for (auto& node : padded.ast.nodes) if (node.kind == NodeKind::CONST) ++node.i0;
      padded.ast.consts.push_back(Value::from_int(999));
      if (remove_name) padded.ast.names.push_back("unused_compaction_name");
      auto compacted = variation_detail::admit_and_compact_execution(padded, compact_context);
      auto independent = compacted; independent.derivation.reset();
      VerifiedAst independently_verified;
      auto proof = reconstruct_derivation(scalar, independent, entry_request(scalar),
                                         &independently_verified, nullptr);
      check(ast_cache_key(compacted.ast) == ast_cache_key(repro::compact_genome_tables(padded).ast),
            "owned compaction changed referenced values");
      check(compacted.derivation->choices.size() == proof.choices.size() &&
            compacted.derivation->logical_steps == proof.logical_steps &&
            compacted.derivation->lowered_instructions == proof.lowered_instructions,
            "compaction transported a different canonical witness");
      for (std::size_t i = 0; i < proof.choices.size(); ++i) {
        const auto& x = compacted.derivation->choices[i]; const auto& y = proof.choices[i];
        check(x.nonterminal == y.nonterminal && x.production == y.production &&
              x.parent == y.parent && x.ast_begin == y.ast_begin && x.ast_end == y.ast_end &&
              x.template_instance == y.template_instance && x.slot == y.slot,
              "compaction changed canonical production choices");
      }
      const auto reused_code = admitted_bytecode_for_eval(compacted, {}, scalar.execution_limits().fuel);
      check(bool(reused_code), "compacted proof was not bound to its actual runtime identity");
      const auto independently_lowered = compile_for_eval(independent);
      for (int fuel = 0; fuel <= 1000; ++fuel) {
        const auto retained = execute_bytecode_cpu(*reused_code, {}, fuel);
        const auto rebuilt = execute_bytecode_cpu(independently_lowered, {}, fuel);
        check(retained.is_error == rebuilt.is_error &&
            (retained.is_error ? retained.err.code == rebuilt.err.code :
                retained.value.tag == rebuilt.value.tag && retained.value.i == rebuilt.value.i),
            "compacted executable changed output, errors or fuel exhaustion");
      }
      auto bad_constant = padded;
      bad_constant.derivation.reset();
      bad_constant.ast.consts.push_back(Value::invalid());
      bool bad_constant_rejected = false;
      try { (void)variation_detail::certify_execution(bad_constant, compact_context); }
      catch (const std::invalid_argument&) { bad_constant_rejected = true; }
      check(bad_constant_rejected, "compaction hid an unused invalid constant tag");
      compacted.ast.consts.front() = Value::from_int(999);
      check(!admitted_bytecode_for_eval(compacted, {}, scalar.execution_limits().fuel),
            "compacted certificate permitted subsequent mutable changes");
      bool invalid_rejected = false;
      try { (void)variation_detail::admit_and_compact_execution(compacted, compact_context); }
      catch (const std::invalid_argument&) { invalid_rejected = true; }
      check(invalid_rejected, "owned compaction trusted stale external admission");
      padded.ast.names = {"duplicate", "duplicate"};
      invalid_rejected = false;
      try { (void)variation_detail::admit_and_compact_execution(padded, compact_context); }
      catch (const std::invalid_argument&) { invalid_rejected = true; }
      check(invalid_rejected, "owned compaction hid malformed unused names");
    }

    auto tight = scalar_doc;
    tight.object_v.at("search_limits").object_v.at("max_nodes").number_v = 5;
    rejects(compile(tight), sum);
    tight = scalar_doc;
    tight.object_v.at("search_limits").object_v.at("max_depth").number_v = 4;
    rejects(compile(tight), sum);

    const std::vector<std::pair<std::string, std::string>> domains{
      {"Int", "[\"-9223372036854775808\",\"9223372036854775807\"]"},
      {"Float", "[-0.0]"}, {"Bool", "[true]"}, {"Char", "[\"\\ud83d\\ude00\"]"},
      {"String", "[\"a\\u0000b\"]"}, {"IntList", "[[\"-3\",\"7\"]]"},
      {"FloatList", "[[-0.0,2.25]]"}, {"StringList", "[[\"\",\"a\\u0000b\"]]"}};
    for (const auto& domain : domains) {
      const auto grammar = compile_grammar(parse_definition(constant_grammar(domain.first, domain.second)));
      for (std::uint64_t seed = 0; seed < 16; ++seed) {
        auto genome = generate_derivation(grammar, seed).genome;
        genome.derivation.reset();
        require_membership(grammar, genome);
        if (seed==0 && (domain.first=="String" || domain.first.find("List")!=std::string::npos)) {
          check(!reconstruct_derivation(grammar,genome).certificate,
                "mutable registry proofs require an enclosing read snapshot");
          payload::StagedPayloads reads;
          {
            payload::StagedPayloads::Scope scope(reads);
            genome.derivation=std::make_shared<const DerivationMetadata>(reconstruct_derivation(grammar,genome));
            check(bool(genome.derivation->certificate),"snapshot can retain a payload proof");
            check(!admitted_bytecode_for_eval(genome, {}, grammar.execution_limits().fuel),
                  "payload certificate must decline executable reuse");
            check(reconstruct_derivation(grammar,genome).certificate==genome.derivation->certificate,
                  "identical decoded payload proof should be reusable inside snapshot");
          }
          check(payload::StagedPayloads::commit_all({&reads}),"payload proof snapshot must validate");
          check(!reconstruct_derivation(grammar,genome).certificate,
                "outside snapshot must revalidate mutable payloads even when carrying proof");
        }
        if (domain.first == "Float") {
          genome.ast.consts[0] = Value::from_float(0.0);
          rejects(grammar, genome);
        }
      }
    }

    auto template_doc = scalar_doc;
    template_doc.object_v["templates"] = json(R"([
      {"id":"Double","type":"Int","scope":[],"holes":[{"id":"value","type":"Int","scope":[]}],
       "body":{"signature":"add(Int,Int)->Int","args":[{"hole":"value"},{"hole":"value"}]}},
      {"id":"Forward","type":"Int","scope":[],"holes":[{"id":"value","type":"Int","scope":[]}],
       "body":{"template":"Double","holes":{"value":{"hole":"value"}}}}
    ])");
    template_doc.object_v.at("nonterminals").array_v.push_back(json(R"({
      "id":"Main","type":"Int","scope":[],"alternatives":[{"id":"forward","weight":1,
      "expression":{"template":"Forward","holes":{"value":{"ref":"Expr"}}}}]})"));
    template_doc.object_v.at("entry").object_v.at("nonterminal").string_v = "Main";
    const auto templates = compile(template_doc);
    for (std::uint64_t seed = 0; seed < 64; ++seed) {
      auto genome = generate_derivation(templates, seed).genome;
      require_membership(templates, genome);
      genome.derivation.reset();
      require_membership(templates, genome);
    }
    // Force two one-node occurrences, then distinguish index equality from value equality.
    template_doc.object_v.at("search_limits").object_v.at("max_nodes").number_v = 7;
    const auto leaves = compile(template_doc);
    auto repeated = generate_derivation(leaves, 7).genome;
    repeated.derivation.reset();
    check(repeated.ast.nodes[4].kind == NodeKind::CONST && repeated.ast.nodes[5].kind == NodeKind::CONST,
          "repeated-hole fixture did not generate leaves");
    repeated.ast.consts = {Value::from_int(1), Value::from_int(1)};
    repeated.ast.nodes[4].i0 = 0;
    repeated.ast.nodes[5].i0 = 1;
    differential_admission(leaves, repeated);
    require_membership(leaves, repeated);
    require_membership(leaves, repro::compact_genome_tables(repeated));
    repeated.ast.consts[1] = Value::from_int(2);
    rejects(leaves, repeated);

    // B initially fails at the left child while A is active. After A selects
    // its leaf and the first root alternative fails on the right child, B at
    // the SAME AST coordinate must be retried under the new alias ancestry.
    const auto ancestry = compile(json(R"({
      "format_version":"grammar-definition-v2",
      "entry":{"nonterminal":"Root","type":"Int"},
      "search_limits":{"max_nodes":20,"max_depth":10},"execution_limits":{"fuel":100},
      "nonterminals":[
        {"id":"Root","type":"Int","scope":[],"alternatives":[
          {"id":"first","weight":1,"expression":{"signature":"add(Int,Int)->Int","args":[
            {"ref":"A"},{"constant":{"type":"Int","values":["0"]}}]}},
          {"id":"second","weight":1,"expression":{"signature":"add(Int,Int)->Int","args":[
            {"ref":"B"},{"constant":{"type":"Int","values":["1"]}}]}}]},
        {"id":"A","type":"Int","scope":[],"alternatives":[
          {"id":"alias","weight":1,"expression":{"ref":"B"}},
          {"id":"leaf","weight":1,"expression":{"constant":{"type":"Int","values":["1"]}}}]},
        {"id":"B","type":"Int","scope":[],"alternatives":[
          {"id":"alias","weight":1,"expression":{"ref":"A"}}]}]
    })"));
    auto ancestry_member = repeated;
    ancestry_member.ast.consts[1] = Value::from_int(1);
    differential_admission(ancestry, ancestry_member);
    require_membership(ancestry, ancestry_member);
    check(reconstruct_derivation(ancestry, ancestry_member).nodes.size() == ancestry_member.ast.nodes.size(),
          "alias-dependent failure was reused under a different ancestry");
    ancestry_member.ast.consts[0] = Value::from_int(2);
    rejects(ancestry, ancestry_member);

    const auto fuel_grammar = compile(json(R"({
      "format_version":"grammar-definition-v2",
      "entry":{"nonterminal":"Main","type":"Int"},
      "search_limits":{"max_nodes":12,"max_depth":8},
      "execution_limits":{"fuel":100},
      "nonterminals":[
        {"id":"Leaf","type":"Int","scope":[],"alternatives":[
          {"id":"one","weight":1,"expression":{"constant":{"type":"Int","values":["0"]},"fuel_events":{"operation":1}}},
          {"id":"two","weight":1,"expression":{"constant":{"type":"Int","values":["0"]},"fuel_events":{"operation":2}}}]},
        {"id":"Main","type":"Int","scope":[],"alternatives":[{"id":"double","weight":1,
          "expression":{"template":"Double","holes":{"value":{"ref":"Leaf"}}}}]}],
      "templates":[{"id":"Double","type":"Int","scope":[],
        "holes":[{"id":"value","type":"Int","scope":[]}],
        "body":{"signature":"add(Int,Int)->Int","args":[{"hole":"value"},{"hole":"value"}]}}]
    })"));
    auto fuel_repeated = generate_derivation(fuel_grammar, 3).genome;
    check(fuel_repeated.ast.fuel_specs.size() == 2,
          "generation did not copy a repeated-hole fuel profile");
    differential_admission(fuel_grammar, fuel_repeated);
    require_membership(fuel_grammar, fuel_repeated);
    fuel_repeated.derivation.reset();
    require_membership(fuel_grammar, fuel_repeated);
    (void)reconstruct_derivation(fuel_grammar, fuel_repeated);
    require_membership(fuel_grammar,
                       repro::compact_genome_tables(fuel_repeated));
    auto divergent_profile = fuel_repeated;
    divergent_profile.ast.fuel_specs[1].charges[0].cost =
        divergent_profile.ast.fuel_specs[0].charges[0].cost == 1 ? 2 : 1;
    rejects(fuel_grammar, divergent_profile);
    auto missing_profile = fuel_repeated;
    missing_profile.ast.fuel_specs.erase(missing_profile.ast.fuel_specs.begin());
    rejects(fuel_grammar, missing_profile);

    const auto branch_fuel = compile(json(R"({
      "format_version":"grammar-definition-v2",
      "entry":{"nonterminal":"Expr","type":"Int"},
      "search_limits":{"max_nodes":12,"max_depth":8},
      "execution_limits":{"fuel":100},
      "nonterminals":[{"id":"Expr","type":"Int","scope":[],"alternatives":[{
        "id":"branch","weight":1,"expression":{
          "signature":"if(Bool,Int,Int)->Int",
          "fuel_events":{"branch_test":0,"branch_merge":3},
          "args":[{"constant":{"type":"Bool","values":[true]}},
                  {"constant":{"type":"Int","values":["1"]}},
                  {"constant":{"type":"Int","values":["2"]}}]}}]}]
    })"));
    auto branch = generate_derivation(branch_fuel, 0).genome;
    check(branch.ast.fuel_specs.size() == 1 &&
              branch.ast.fuel_specs[0].node_index == 3 &&
              branch.ast.fuel_specs[0].charges.size() == 2,
          "generation did not attach the authored profile to its physical owner");
    require_membership(branch_fuel, branch);
    std::reverse(branch.ast.fuel_specs[0].charges.begin(),
                 branch.ast.fuel_specs[0].charges.end());
    require_membership(branch_fuel, branch);
    auto missing_event = branch;
    missing_event.ast.fuel_specs[0].charges.pop_back();
    rejects(branch_fuel, missing_event);
    auto duplicate_event = branch;
    duplicate_event.ast.fuel_specs[0].charges[1] = duplicate_event.ast.fuel_specs[0].charges[0];
    check(!verify_ast(duplicate_event.ast, {}), "native verification accepted duplicate fuel events");
    bool duplicate_rejected = false;
    try { require_membership(branch_fuel, duplicate_event); }
    catch (const std::invalid_argument&) { duplicate_rejected = true; }
    check(duplicate_rejected, "membership accepted duplicate fuel events");
    branch.ast.fuel_specs[0].charges[0].cost += 1;
    rejects(branch_fuel, branch);

    auto undeclared_profile = generate_derivation(scalar, 0).genome;
    undeclared_profile.ast.fuel_specs.push_back(
        {3, {{FuelEvent::Operation, 0}}});
    rejects(scalar, undeclared_profile);

    auto aliases = scalar_doc;
    aliases.object_v.at("nonterminals").array_v[0].object_v.at("alternatives").array_v.insert(
        aliases.object_v.at("nonterminals").array_v[0].object_v.at("alternatives").array_v.begin(),
        json(R"({"id":"alias","weight":1e300,"expression":{"ref":"Expr"}})"));
    const auto alias_grammar = compile(aliases);
    require_membership(alias_grammar, sum);
    check(reconstruct_derivation(alias_grammar, sum).nodes.size() == sum.ast.nodes.size(),
          "self-alias match did not retain a reconstructable successful decision");
    for (std::uint64_t seed = 0; seed < 32; ++seed)
      require_membership(alias_grammar, generate_derivation(alias_grammar, seed).genome);
    aliases.object_v.at("nonterminals").array_v.push_back(json(R"({
      "id":"Other","type":"Int","scope":[],"alternatives":[
        {"id":"back","weight":1,"expression":{"ref":"Expr"}},
        {"id":"exit","weight":1,"expression":{"constant":{"type":"Int","values":["0"]}}}]} )"));
    aliases.object_v.at("nonterminals").array_v[0].object_v.at("alternatives").array_v[0]
        .object_v.at("expression").object_v.at("ref").string_v = "Other";
    const auto mutual = compile(aliases);
    differential_admission(mutual, sum);
    require_membership(mutual, sum);
    check(reconstruct_derivation(mutual, sum).nodes.size() == sum.ast.nodes.size(),
          "mutual-alias match retained an active or failed production decision");
    for (std::uint64_t seed = 0; seed < 32; ++seed)
      require_membership(mutual, generate_derivation(mutual, seed).genome);
    std::cout << "grammar membership passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
