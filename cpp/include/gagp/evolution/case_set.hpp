#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "gagp/core/value.hpp"
#include "gagp/evolution/input_spec.hpp"
#include "gagp/runtime/cpu/fitness_cpu.hpp"

namespace gagp::evo {

using NamedInputs = std::unordered_map<std::string, Value>;

struct EvalCase {
  NamedInputs inputs;
  Value expected = Value::invalid();
};

struct CaseSet {
  std::vector<std::string> input_names;
  std::vector<InputSpec> input_specs;
  std::vector<CaseBindings> bindings;
  std::vector<Value> expected_values;
  RType expected_return_type = RType::Invalid;
};

CaseSet prepare_case_set(const std::vector<EvalCase>& cases);
std::vector<InputSpec> canonical_input_specs(const std::vector<EvalCase>& cases);

}  // namespace gagp::evo
