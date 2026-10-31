#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include "gagp/cli/json.hpp"
#include "gagp/cli/search_config.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/search_policy.hpp"

namespace {
using namespace gagp;
using namespace gagp::cli_detail;

void require(bool value, const std::string& message) {
  if (!value) throw std::runtime_error(message);
}
JsonValue parse(const std::string& text) { return JsonParser(text, {true, 64}).parse(); }
SearchConfig config(const std::string& body) {
  return parse_search_config(parse(std::string("{\"format\":\"gagp-search-config-v1\"") + body + "}"));
}
void rejects(const std::string& body, const std::string& fragment) {
  try {
    (void)config(body);
  } catch (const std::invalid_argument& error) {
    require(std::string(error.what()).find(fragment) != std::string::npos,
            "unexpected rejection for " + body + ": " + error.what());
    return;
  }
  throw std::runtime_error("accepted invalid search config: " + body);
}
struct EnvGuard {
  const char* name; bool present; std::string value;
  explicit EnvGuard(const char* n) : name(n), present(std::getenv(n) != nullptr), value(present ? std::getenv(n) : "") {}
  ~EnvGuard() { if (present) setenv(name, value.c_str(), 1); else unsetenv(name); }
};

void test_parse_and_apply() {
  const auto empty = config("");
  require(!empty.generations && !empty.explicit_phase_variation(), "empty document set fields");
  const auto full = config(
      ",\"representation\":\"phase_population\",\"population_size\":1024,\"generations\":64,"
      "\"seed\":\"18446744073709551615\",\"selection\":{\"method\":\"tournament\",\"size\":3},"
      "\"elitism\":1,\"crossover\":{\"probability\":0.7,\"attempts\":4},\"mutation\":{\"probability\":0.3},"
      "\"sites\":{\"scope\":\"uniform_phase\",\"draw\":\"counted\"},\"limits\":{\"site_depth\":7},"
      "\"ranking\":{\"tie_break\":\"grammar_sites\"},\"fitness\":{\"penalty\":1000},"
      "\"execution\":{\"fuel\":200000}");
  evo::EvolutionConfig cfg;
  apply_search_config(full, cfg);
  require(cfg.phase_population && *cfg.phase_population, "representation");
  require(cfg.population_size == 1024 && cfg.generations == 64, "size/generations");
  require(cfg.seed == UINT64_C(18446744073709551615), "64-bit string seed");
  require(cfg.selection_pressure == 3 && cfg.elite_count == 1 && cfg.mutation_rate == 0.3, "selection/elitism/mutation");
  require(cfg.gpu_phase_site_depth == 7 && cfg.gpu_phase_site_parsimony && cfg.penalty == 1000, "limits/ranking/penalty");
  require(cfg.fuel == 200000, "execution fuel");
  require(cfg.phase_variation && cfg.phase_variation->crossover_probability == 0.7 &&
          cfg.phase_variation->crossover_attempts == 4 &&
          cfg.phase_variation->site_draw == evo::PhaseSiteDraw::Counted &&
          cfg.phase_variation->site_scope == evo::PhaseSiteScope::UniformPhase, "phase variation");
  // One explicit variation member makes the whole policy explicit with documented defaults.
  evo::EvolutionConfig partial;
  apply_search_config(config(",\"crossover\":{\"probability\":0.5}"), partial);
  require(partial.phase_variation && partial.phase_variation->site_draw == evo::PhaseSiteDraw::Reservoir &&
          partial.phase_variation->site_scope == evo::PhaseSiteScope::Genome &&
          partial.phase_variation->crossover_attempts == 8, "partial variation defaults");
  require(config(",\"seed\":35").seed == 35u, "numeric seed");
}

void test_rejections() {
  rejects(",\"bogus\":1", "unknown key document.bogus");
  rejects(",\"crossover\":{\"rate\":1}", "unknown key crossover.rate");
  rejects(",\"crossover\":{\"probability\":1.5}", "crossover.probability");
  rejects(",\"crossover\":{\"attempts\":0}", "crossover.attempts");
  rejects(",\"mutation\":{\"probability\":-0.1}", "mutation.probability");
  rejects(",\"mutation\":{\"max_subtree_nodes\":1025}", "mutation.max_subtree_nodes");
  rejects(",\"execution\":{\"fuel\":0}", "execution.fuel");
  rejects(",\"execution\":{\"steps\":1}", "unknown key execution.steps");
  rejects(",\"generations\":2.5", "generations");
  rejects(",\"limits\":{\"site_depth\":16}", "limits.site_depth");
  rejects(",\"sites\":{\"draw\":\"uniform\"}", "sites.draw");
  rejects(",\"sites\":{\"scope\":\"phase\"}", "sites.scope");
  rejects(",\"ranking\":{\"tie_break\":\"size\"}", "ranking.tie_break");
  rejects(",\"selection\":{\"method\":\"lexicase\"}", "selection.method");
  rejects(",\"fitness\":{\"penalty\":-1}", "fitness.penalty");
  rejects(",\"seed\":-1", "seed");
  rejects(",\"seed\":\"12x\"", "seed");
  rejects(",\"representation\":\"tree\"", "representation");
  bool format_rejected = false;
  try { (void)parse_search_config(parse("{\"format\":\"v0\"}")); }
  catch (const std::invalid_argument&) { format_rejected = true; }
  require(format_rejected, "wrong format accepted");
}

void test_effective_round_trip() {
  evo::EvolutionConfig cfg;
  cfg.phase_population = true;
  cfg.population_size = 256; cfg.generations = 12; cfg.seed = 99; cfg.selection_pressure = 4;
  cfg.elite_count = 2; cfg.mutation_rate = 0.25; cfg.penalty = 7; cfg.gpu_phase_site_depth = 5;
  cfg.fuel = 12345;
  cfg.phase_variation = evo::PhaseVariationPolicy{evo::PhaseSiteDraw::Counted, evo::PhaseSiteScope::UniformPhase, 0.6, 5, 24};
  const auto document = effective_search_config(cfg);
  evo::EvolutionConfig copy;
  apply_search_config(parse_search_config(parse(evo::grammar::canonical_json(document))), copy);
  require(copy.phase_population == cfg.phase_population && copy.population_size == cfg.population_size &&
          copy.generations == cfg.generations && copy.seed == cfg.seed &&
          copy.selection_pressure == cfg.selection_pressure && copy.elite_count == cfg.elite_count &&
          copy.mutation_rate == cfg.mutation_rate && copy.penalty == cfg.penalty &&
          copy.gpu_phase_site_depth == cfg.gpu_phase_site_depth && copy.phase_variation == cfg.phase_variation &&
          copy.fuel == cfg.fuel,
          "effective configuration does not round-trip");
}

void test_environment_mapping() {
  EnvGuard counted("GAGP_COUNTED_SITE_DRAW"), uniform("GAGP_GPU_UNIFORM_PHASE_SITES"), gate("GAGP_GPU_SOURCE_CROSSOVER_GATE");
  unsetenv("GAGP_COUNTED_SITE_DRAW"); unsetenv("GAGP_GPU_UNIFORM_PHASE_SITES"); unsetenv("GAGP_GPU_SOURCE_CROSSOVER_GATE");
  require(evo::phase_variation_policy_from_environment() == evo::PhaseVariationPolicy{}, "default environment policy");
  setenv("GAGP_COUNTED_SITE_DRAW", "1", 1); setenv("GAGP_GPU_UNIFORM_PHASE_SITES", "1", 1); setenv("GAGP_GPU_SOURCE_CROSSOVER_GATE", "1", 1);
  const auto legacy = evo::phase_variation_policy_from_environment();
  require(legacy.site_draw == evo::PhaseSiteDraw::Counted && legacy.site_scope == evo::PhaseSiteScope::UniformPhase &&
          legacy.crossover_probability == 0.7 && legacy.crossover_attempts == 8, "legacy environment policy");
  evo::require_environment_agrees(legacy);
  bool conflict = false;
  try { evo::require_environment_agrees(evo::PhaseVariationPolicy{}); }
  catch (const std::invalid_argument&) { conflict = true; }
  require(conflict, "conflicting legacy flag accepted");
  require(evo::phase_variation_profile_suffix(legacy) == "-source-cross07", "gate profile suffix");
  require(evo::phase_variation_profile_suffix(evo::PhaseVariationPolicy{}).empty(), "default profile suffix");
  bool invalid = false;
  require(evo::phase_variation_profile_suffix({evo::PhaseSiteDraw::Counted, evo::PhaseSiteScope::Genome, 1, 8, 32}) == "-mutcap32", "cap suffix");
  try { evo::validate_phase_variation_policy({evo::PhaseSiteDraw::Reservoir, evo::PhaseSiteScope::Genome, 1, 65}); }
  catch (const std::invalid_argument&) { invalid = true; }
  require(invalid, "invalid attempts accepted");
}
}  // namespace

int main() {
  try {
    test_parse_and_apply();
    test_rejections();
    test_effective_round_trip();
    test_environment_mapping();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "search config tests passed\n";
  return 0;
}
