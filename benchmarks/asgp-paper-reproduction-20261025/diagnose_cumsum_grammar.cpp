// A structural witness for the frozen author's CumulativeSum configuration.
// No evolution and no changes to the author library.
#include <iostream>
#include <set>
#include <stdexcept>
#include "task/registry.h"

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void values(const std::vector<int64_t>& xs) {
  std::cout << '[';
  for (size_t i = 0; i < xs.size(); ++i) {
    if (i) std::cout << ',';
    std::cout << xs[i];
  }
  std::cout << ']';
}
}

int main() {
  using namespace asgp;
  const auto spec = *BuildTask("cumulative_sum", DataConfig{});
  const auto grammar = SchemeGrammar::Build(spec.Grammar());
  require(spec.scheme == Scheme::kDivideConquer && spec.base_size == 1,
          "Witness requires singleton DC leaves");
  require(spec.carrier == Type::kIntList && spec.scalars.empty() && !spec.positional,
          "Witness requires nonpositional IntList output without scalar inputs");
  for (uint16_t i = 0; i < grammar[dc::kSolve].size(); ++i) {
    const auto& p = grammar[dc::kSolve][i];
    require(p.kind != NodeKind::kHeadIndex && p.kind != NodeKind::kInputScalar &&
            p.kind != NodeKind::kResult && p.kind != NodeKind::kStateA &&
            p.kind != NodeKind::kStateB && p.kind != NodeKind::kStateValue,
            "Solve has context reads outside the singleton input");
    if (p.result == Type::kIntList) {
      require(p.kind == NodeKind::kInputList || p.kind == NodeKind::kEmptyList ||
              p.kind == NodeKind::kIf || p.kind == NodeKind::kSingleton,
              "Solve can create a list longer than one element");
    }
  }
  const auto& combine = grammar[dc::kAndCombine];
  for (uint16_t i = 0; i < combine.size(); ++i) {
    require(combine[i].result == Type::kIntList &&
            (combine[i].kind == NodeKind::kConcat || combine[i].kind == NodeKind::kResult),
            "Combine can create or transform scalar values");
  }
  const auto& e = spec.test.front();
  const std::set<int64_t> inputs(e.elements.begin(), e.elements.end());
  const std::set<int64_t> targets(e.expected.list.begin(), e.expected.list.end());
  require(targets.size() > inputs.size(), "First test is not a cardinality witness");
  // Each valid Solve returns [] or [f(x)] for a singleton x. Repeated x has
  // identical visible context. Combine only concatenates/selects results, so
  // output distinct values <= input distinct values for every legal program.
  std::cout << "{\"task\":\"cumulative_sum\",\"split\":\"test\",\"case_index\":0,"
               "\"base_size\":1,\"max_leaf_output_length\":1,"
               "\"combine_only_concat_or_result\":true,\"combine_productions\":[";
  for (uint16_t i = 0; i < combine.size(); ++i) {
    if (i) std::cout << ',';
    std::cout << "{\"kind\":\"" << Symbol(combine[i].kind)
              << "\",\"op\":" << static_cast<int>(combine[i].op) << '}';
  }
  std::cout << "],\"input\":"; values(e.elements);
  std::cout << ",\"expected\":"; values(e.expected.list);
  std::cout << ",\"distinct_inputs\":" << inputs.size()
            << ",\"distinct_expected\":" << targets.size()
            << ",\"zero_error_possible_for_legal_program\":false}\n";
}
