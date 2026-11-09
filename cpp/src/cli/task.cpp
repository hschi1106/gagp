#include "gagp/cli/task.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

#include "gagp/cli/commands.hpp"
#include "gagp/cli/json.hpp"

namespace gagp::cli_detail {
namespace {

[[noreturn]] void fail(const std::string& path, const std::string& message) {
  throw std::invalid_argument("task " + path + ": " + message);
}

std::string read_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot read " + path);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

void reject_unknown(const std::string& path, const JsonValue& object, const std::string& where,
                    const std::set<std::string>& allowed) {
  if (object.kind != JsonValue::Kind::Object) fail(path, where + " must be an object");
  for (const auto& [key, unused] : object.object_v)
    if (!allowed.count(key)) fail(path, "unknown key " + where + "." + key);
}

const JsonValue* field(const JsonValue& object, const char* key) {
  const auto it = object.object_v.find(key);
  return it == object.object_v.end() ? nullptr : &it->second;
}

std::string text(const std::string& path, const JsonValue& value, const std::string& where) {
  if (value.kind != JsonValue::Kind::String || value.string_v.empty())
    fail(path, where + " must be a nonempty string");
  return value.string_v;
}

double positive(const std::string& path, const JsonValue& value, const std::string& where) {
  if (value.kind != JsonValue::Kind::Number || !std::isfinite(value.number_v) || value.number_v <= 0)
    fail(path, where + " must be a positive number");
  return value.number_v;
}

FitnessMetric metric_named(const std::string& path, const std::string& name) {
  if (name == "absolute") return FitnessMetric::Absolute;
  if (name == "exact") return FitnessMetric::Exact;
  if (name == "levenshtein") return FitnessMetric::Levenshtein;
  if (name == "tuple_summary") return FitnessMetric::TupleSummary;
  fail(path, "fitness.metric must be absolute, exact, levenshtein or tuple_summary");
}

}  // namespace

Task load_task(const std::string& path) {
  const JsonValue doc = JsonParser(read_file(path), {true, 64}).parse();
  reject_unknown(path, doc, "task", {"format", "name", "grammar", "train", "test", "fitness"});
  const auto* format = field(doc, "format");
  if (!format || format->kind != JsonValue::Kind::String || format->string_v != kTaskFormat)
    fail(path, std::string("format must be ") + kTaskFormat);
  const auto base = std::filesystem::path(path).parent_path();
  const auto resolve = [&](const char* key, bool required) -> std::string {
    const auto* value = field(doc, key);
    if (!value) {
      if (required) fail(path, std::string("missing ") + key);
      return {};
    }
    return (base / text(path, *value, key)).string();
  };
  Task task;
  const auto* name = field(doc, "name");
  task.name = name ? text(path, *name, "name") : std::filesystem::path(path).parent_path().filename().string();
  task.grammar_path = resolve("grammar", true);
  task.train_path = resolve("train", true);
  task.test_path = resolve("test", false);

  const auto* fitness = field(doc, "fitness");
  if (!fitness) fail(path, "missing fitness");
  reject_unknown(path, *fitness, "fitness", {"metric", "proximity", "case_cap", "summaries", "list_allocation_limit"});
  const auto* metric = field(*fitness, "metric");
  if (!metric) fail(path, "missing fitness.metric");
  task.fitness.metric = metric_named(path, text(path, *metric, "fitness.metric"));
  if (const auto* proximity = field(*fitness, "proximity")) {
    const auto value = text(path, *proximity, "fitness.proximity");
    if (value == "capped") task.fitness.proximity = FitnessProximity::Capped;
    else if (value == "discounted") task.fitness.proximity = FitnessProximity::Discounted;
    else fail(path, "fitness.proximity must be capped or discounted");
  }
  if (const auto* cap = field(*fitness, "case_cap")) task.case_cap = positive(path, *cap, "fitness.case_cap");
  if (const auto* summaries = field(*fitness, "summaries")) {
    if (task.fitness.metric != FitnessMetric::TupleSummary)
      fail(path, "fitness.summaries requires metric tuple_summary");
    reject_unknown(path, *summaries, "fitness.summaries", {"count", "weight", "cap"});
    const auto* count = field(*summaries, "count");
    if (!count || count->kind != JsonValue::Kind::Number || (count->number_v != 1 && count->number_v != 2))
      fail(path, "fitness.summaries.count must be 1 or 2");
    task.fitness.summary_count = static_cast<int>(count->number_v);
    if (const auto* weight = field(*summaries, "weight"))
      task.fitness.summary_weight = positive(path, *weight, "fitness.summaries.weight");
    if (const auto* cap = field(*summaries, "cap"))
      task.fitness.summary_cap = positive(path, *cap, "fitness.summaries.cap");
  } else if (task.fitness.metric == FitnessMetric::TupleSummary) {
    fail(path, "metric tuple_summary requires fitness.summaries");
  }
  if (const auto* limit = field(*fitness, "list_allocation_limit")) {
    const double raw = positive(path, *limit, "fitness.list_allocation_limit");
    if (std::floor(raw) != raw || raw > 1u << 30) fail(path, "fitness.list_allocation_limit must be an integer <= 2^30");
    task.fitness.list_allocation_limit = static_cast<unsigned>(raw);
  }
  if (!valid_fitness_policy(task.fitness)) fail(path, "invalid fitness policy");
  return task;
}

std::vector<evo::EvalCase> load_fitness_cases(const std::string& path) {
  return decode_fitness_cases_json(JsonParser(read_file(path)).parse());
}

}  // namespace gagp::cli_detail
