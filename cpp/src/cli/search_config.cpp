#include "gagp/cli/search_config.hpp"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace gagp::cli_detail {
namespace {

[[noreturn]] void fail(const std::string& message) {
  throw std::invalid_argument("search config: " + message);
}
const JsonValue& expect_object(const JsonValue& value, const std::string& where,
                               const std::set<std::string>& allowed) {
  if (value.kind != JsonValue::Kind::Object) fail(where + " must be an object");
  for (const auto& [key, unused] : value.object_v)
    if (!allowed.count(key)) fail("unknown key " + where + "." + key);
  return value;
}
const JsonValue* field(const JsonValue& object, const char* key) {
  const auto it = object.object_v.find(key);
  return it == object.object_v.end() ? nullptr : &it->second;
}
double number(const JsonValue& value, const std::string& where) {
  if (value.kind != JsonValue::Kind::Number || !std::isfinite(value.number_v)) fail(where + " must be a finite number");
  return value.number_v;
}
int integer(const JsonValue& value, const std::string& where, int minimum, int maximum) {
  const double raw = number(value, where);
  if (std::floor(raw) != raw || raw < minimum || raw > maximum)
    fail(where + " must be an integer in [" + std::to_string(minimum) + "," + std::to_string(maximum) + "]");
  return static_cast<int>(raw);
}
double probability(const JsonValue& value, const std::string& where) {
  const double raw = number(value, where);
  if (raw < 0 || raw > 1) fail(where + " must be in [0,1]");
  return raw;
}
std::string text(const JsonValue& value, const std::string& where) {
  if (value.kind != JsonValue::Kind::String) fail(where + " must be a string");
  return value.string_v;
}
// Seeds are 64-bit. Numbers are exact only up to 2^53; larger seeds use strings.
std::uint64_t seed_value(const JsonValue& value) {
  if (value.kind == JsonValue::Kind::String) {
    const auto& s = value.string_v;
    if (s.empty() || s.size() > 20 || s.find_first_not_of("0123456789") != std::string::npos) fail("seed string must be decimal digits");
    errno = 0;
    char* end = nullptr;
    const auto parsed = std::strtoull(s.c_str(), &end, 10);
    if (errno || *end) fail("seed out of range");
    return parsed;
  }
  const double raw = number(value, "seed");
  if (std::floor(raw) != raw || raw < 0 || raw > 9007199254740992.0) fail("numeric seed must be an integer in [0,2^53]; use a string for larger seeds");
  return static_cast<std::uint64_t>(raw);
}
JsonValue make_number(double value) { JsonValue v; v.kind = JsonValue::Kind::Number; v.number_v = value; return v; }
JsonValue make_string(std::string value) { JsonValue v; v.kind = JsonValue::Kind::String; v.string_v = std::move(value); return v; }
JsonValue make_object() { JsonValue v; v.kind = JsonValue::Kind::Object; return v; }

}  // namespace

bool SearchConfig::explicit_phase_variation() const {
  return crossover_probability || crossover_attempts || mutation_max_nodes || site_draw || site_scope;
}

SearchConfig parse_search_config(const JsonValue& document) {
  expect_object(document, "document", {"format", "representation", "population_size", "generations", "seed",
      "selection", "elitism", "crossover", "mutation", "sites", "limits", "ranking", "fitness", "execution",
      "schedule"});
  const auto* format = field(document, "format");
  if (!format || text(*format, "format") != kSearchConfigFormat)
    fail(std::string("format must be \"") + kSearchConfigFormat + "\"");
  SearchConfig config;
  if (const auto* v = field(document, "representation")) {
    const auto name = text(*v, "representation");
    if (name == "resident") config.phase_population = true;
    else if (name == "native_ast") config.phase_population = false;
    else fail("representation must be resident or native_ast");
  }
  if (const auto* v = field(document, "population_size")) config.population_size = integer(*v, "population_size", 1, 1 << 20);
  if (const auto* v = field(document, "generations")) config.generations = integer(*v, "generations", 1, 1 << 20);
  if (const auto* v = field(document, "seed")) config.seed = seed_value(*v);
  if (const auto* v = field(document, "selection")) {
    expect_object(*v, "selection", {"method", "size"});
    if (const auto* m = field(*v, "method"); m && text(*m, "selection.method") != "tournament")
      fail("selection.method must be tournament");
    if (const auto* size = field(*v, "size")) config.tournament_size = integer(*size, "selection.size", 1, 1 << 20);
  }
  if (const auto* v = field(document, "elitism")) config.elite_count = integer(*v, "elitism", 0, 1 << 20);
  if (const auto* v = field(document, "crossover")) {
    expect_object(*v, "crossover", {"probability", "attempts"});
    if (const auto* p = field(*v, "probability")) config.crossover_probability = probability(*p, "crossover.probability");
    if (const auto* a = field(*v, "attempts")) config.crossover_attempts = integer(*a, "crossover.attempts", 1, 64);
  }
  if (const auto* v = field(document, "mutation")) {
    expect_object(*v, "mutation", {"probability", "max_subtree_nodes"});
    if (const auto* p = field(*v, "probability")) config.mutation_probability = probability(*p, "mutation.probability");
    if (const auto* n = field(*v, "max_subtree_nodes")) config.mutation_max_nodes = integer(*n, "mutation.max_subtree_nodes", 0, 1024);
  }
  if (const auto* v = field(document, "sites")) {
    expect_object(*v, "sites", {"scope", "draw"});
    if (const auto* s = field(*v, "scope")) {
      const auto name = text(*s, "sites.scope");
      if (name == "genome") config.site_scope = evo::PhaseSiteScope::Genome;
      else if (name == "uniform_phase") config.site_scope = evo::PhaseSiteScope::UniformPhase;
      else fail("sites.scope must be genome or uniform_phase");
    }
    if (const auto* d = field(*v, "draw")) {
      const auto name = text(*d, "sites.draw");
      if (name == "reservoir") config.site_draw = evo::PhaseSiteDraw::Reservoir;
      else if (name == "counted") config.site_draw = evo::PhaseSiteDraw::Counted;
      else fail("sites.draw must be reservoir or counted");
    }
  }
  if (const auto* v = field(document, "limits")) {
    expect_object(*v, "limits", {"site_depth"});
    if (const auto* d = field(*v, "site_depth")) config.site_depth = integer(*d, "limits.site_depth", 0, 15);
  }
  if (const auto* v = field(document, "ranking")) {
    expect_object(*v, "ranking", {"tie_break"});
    if (const auto* t = field(*v, "tie_break")) {
      const auto name = text(*t, "ranking.tie_break");
      if (name == "ast_nodes") config.grammar_site_ranking = false;
      else if (name == "grammar_sites") config.grammar_site_ranking = true;
      else fail("ranking.tie_break must be ast_nodes or grammar_sites");
    }
  }
  if (const auto* v = field(document, "execution")) {
    expect_object(*v, "execution", {"fuel"});
    if (const auto* f = field(*v, "fuel")) config.fuel = integer(*f, "execution.fuel", 1, 2147483647);
  }
  if (const auto* v = field(document, "fitness")) {
    expect_object(*v, "fitness", {"penalty"});
    if (const auto* p = field(*v, "penalty")) {
      const double penalty = number(*p, "fitness.penalty");
      if (penalty < 0) fail("fitness.penalty must be nonnegative");
      config.penalty = penalty;
    }
  }
  if (const auto* schedule = field(document, "schedule")) {
    evo::PhaseSchedulePolicy policy;
    expect_object(*schedule, "schedule", {"mode", "replacement_size", "batch"});
    if (const auto* m = field(*schedule, "mode")) {
      const auto name = text(*m, "schedule.mode");
      if (name == "generational") policy.mode = evo::PhaseScheduleMode::Generational;
      else if (name == "steady_state") policy.mode = evo::PhaseScheduleMode::SteadyState;
      else fail("schedule.mode must be generational or steady_state");
    }
    if (const auto* r = field(*schedule, "replacement_size")) policy.replacement_size = integer(*r, "schedule.replacement_size", 1, 64);
    if (const auto* b = field(*schedule, "batch")) policy.batch = integer(*b, "schedule.batch", 1, 1 << 20);
    try { evo::validate_phase_schedule_policy(policy); }
    catch (const std::invalid_argument& error) { fail(error.what()); }
    config.schedule = policy;
  }
  if (config.explicit_phase_variation()) {
    evo::PhaseVariationPolicy policy;
    if (config.crossover_probability) policy.crossover_probability = *config.crossover_probability;
    if (config.crossover_attempts) policy.crossover_attempts = *config.crossover_attempts;
    if (config.mutation_max_nodes) policy.mutation_max_nodes = *config.mutation_max_nodes;
    evo::validate_phase_variation_policy(policy);
  }
  return config;
}

SearchConfig load_search_config(const std::string& path) {
  std::ifstream in(path);
  if (!in) fail("cannot read " + path);
  std::ostringstream text;
  text << in.rdbuf();
  return parse_search_config(JsonParser(text.str(), {true, 64}).parse());
}

void apply_search_config(const SearchConfig& config, evo::EvolutionConfig& cfg) {
  if (config.phase_population) cfg.phase_population = config.phase_population;
  if (config.population_size) cfg.population_size = *config.population_size;
  if (config.generations) cfg.generations = *config.generations;
  if (config.seed) cfg.seed = *config.seed;
  if (config.tournament_size) cfg.selection_pressure = *config.tournament_size;
  if (config.elite_count) cfg.elite_count = *config.elite_count;
  if (config.mutation_probability) cfg.mutation_rate = *config.mutation_probability;
  if (config.penalty) cfg.penalty = *config.penalty;
  if (config.fuel) cfg.fuel = *config.fuel;
  if (config.site_depth) cfg.gpu_phase_site_depth = *config.site_depth;
  if (config.grammar_site_ranking) cfg.gpu_phase_site_parsimony = *config.grammar_site_ranking;
  if (config.schedule) cfg.phase_schedule = config.schedule;
  if (config.explicit_phase_variation()) {
    evo::PhaseVariationPolicy policy;
    if (config.site_draw) policy.site_draw = *config.site_draw;
    if (config.site_scope) policy.site_scope = *config.site_scope;
    if (config.crossover_probability) policy.crossover_probability = *config.crossover_probability;
    if (config.crossover_attempts) policy.crossover_attempts = *config.crossover_attempts;
    if (config.mutation_max_nodes) policy.mutation_max_nodes = *config.mutation_max_nodes;
    cfg.phase_variation = policy;
  }
}

JsonValue effective_search_config(const evo::EvolutionConfig& cfg) {
  JsonValue out = make_object();
  out.object_v["format"] = make_string(kSearchConfigFormat);
  const bool phase = cfg.phase_population.value_or(false);
  out.object_v["representation"] = make_string(phase ? "resident" : "native_ast");
  out.object_v["population_size"] = make_number(cfg.population_size);
  out.object_v["generations"] = make_number(cfg.generations);
  out.object_v["seed"] = make_string(std::to_string(cfg.seed));
  auto selection = make_object();
  selection.object_v["method"] = make_string("tournament");
  selection.object_v["size"] = make_number(cfg.selection_pressure);
  out.object_v["selection"] = selection;
  out.object_v["elitism"] = make_number(cfg.elite_count);
  auto mutation = make_object();
  mutation.object_v["probability"] = make_number(cfg.mutation_rate);
  out.object_v["mutation"] = mutation;
  auto fitness = make_object();
  fitness.object_v["penalty"] = make_number(cfg.penalty);
  out.object_v["fitness"] = fitness;
  auto execution = make_object();
  execution.object_v["fuel"] = make_number(cfg.fuel);
  out.object_v["execution"] = execution;
  if (phase) {
    const auto policy = cfg.phase_variation.value_or(evo::PhaseVariationPolicy{});
    out.object_v["mutation"].object_v["max_subtree_nodes"] = make_number(policy.mutation_max_nodes);
    auto crossover = make_object();
    crossover.object_v["probability"] = make_number(policy.crossover_probability);
    crossover.object_v["attempts"] = make_number(policy.crossover_attempts);
    out.object_v["crossover"] = crossover;
    auto sites = make_object();
    sites.object_v["scope"] = make_string(policy.site_scope == evo::PhaseSiteScope::UniformPhase ? "uniform_phase" : "genome");
    sites.object_v["draw"] = make_string(policy.site_draw == evo::PhaseSiteDraw::Counted ? "counted" : "reservoir");
    out.object_v["sites"] = sites;
    auto limits = make_object();
    limits.object_v["site_depth"] = make_number(cfg.gpu_phase_site_depth);
    out.object_v["limits"] = limits;
    auto ranking = make_object();
    ranking.object_v["tie_break"] = make_string(cfg.gpu_phase_site_parsimony ? "grammar_sites" : "ast_nodes");
    out.object_v["ranking"] = ranking;
    // Only a non-default schedule is recorded, so generational records are unchanged.
    if (cfg.phase_schedule && !cfg.phase_schedule->is_default()) {
      const auto& policy = *cfg.phase_schedule;
      auto schedule = make_object();
      schedule.object_v["mode"] = make_string("steady_state");
      schedule.object_v["batch"] = make_number(evo::resolve_phase_schedule(policy, cfg.population_size).batch);
      schedule.object_v["replacement_size"] = make_number(policy.replacement_size);
      out.object_v["schedule"] = schedule;
    }
  }
  return out;
}

}  // namespace gagp::cli_detail
