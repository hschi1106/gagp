#pragma once

#include <string>
#include <vector>

#include "gagp/core/fitness_policy.hpp"
#include "gagp/evolution/case_set.hpp"

namespace gagp::cli_detail {

// A gagp-task-v1 document (spec/task.md): the problem a run searches. Paths in
// the document are relative to the document's directory.
struct Task {
  std::string name;
  std::string grammar_path;
  std::string train_path;
  std::string test_path;  // empty when the task has no held-out split
  FitnessPolicy fitness;
  double case_cap = 1000;  // per-case error cap (the evaluator's penalty)
};

inline constexpr const char* kTaskFormat = "gagp-task-v1";

Task load_task(const std::string& path);
std::vector<evo::EvalCase> load_fitness_cases(const std::string& path);

}  // namespace gagp::cli_detail
