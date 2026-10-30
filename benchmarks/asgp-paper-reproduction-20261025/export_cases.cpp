// Read-only export through the provided ASGP task factory. Link unmodified asgp_core.
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

#include "gp/unified.h"
#include "task/registry.h"

namespace {
void text(std::string_view value) {
  std::cout << '"';
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') std::cout << '\\' << c;
    else if (c < 32) {
      std::cout << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                << static_cast<unsigned>(c) << std::dec;
    } else std::cout << c;
  }
  std::cout << '"';
}

void ints(const std::vector<int64_t>& values) {
  std::cout << '[';
  for (size_t i = 0; i < values.size(); ++i) {
    if (i) std::cout << ',';
    std::cout << values[i];
  }
  std::cout << ']';
}

void cases(const std::vector<asgp::Example>& values) {
  std::cout << '[';
  for (size_t i = 0; i < values.size(); ++i) {
    if (i) std::cout << ',';
    const auto& e = values[i];
    std::cout << "{\"elements\":"; ints(e.elements);
    std::cout << ",\"scalars\":"; ints(e.scalars);
    std::cout << ",\"start\":[" << e.start.a << ',' << e.start.b
              << "],\"extent\":[" << e.extent.a << ',' << e.extent.b
              << "],\"expected\":{\"bool\":" << e.expected.b
              << ",\"int\":" << e.expected.i << ",\"list\":";
    ints(e.expected.list);
    std::cout << ",\"summary1\":" << e.expected.summary1
              << ",\"summary2\":" << e.expected.summary2 << "}}";
  }
  std::cout << ']';
}
}

int main(int argc, char** argv) {
  if (argc != 3 || (std::string(argv[2]) != "legacy" &&
                    std::string(argv[2]) != "count-aligned")) {
    std::cerr << "usage: export_cases TASK legacy|count-aligned\n";
    return 2;
  }
  asgp::DataConfig config;
  config.legacy_finite = std::string(argv[2]) == "legacy";
  const auto task = asgp::BuildTask(argv[1], config);
  if (!task) return 2;
  const auto& s = *task;
  std::cout << std::boolalpha << "{\"format\":\"asgp-source-cases-v1\",\"task\":";
  text(s.id);
  std::cout << ",\"data_seed\":" << config.seed << ",\"profile\":"; text(argv[2]);
  std::cout << ",\"scheme\":"; text(asgp::Name(s.scheme));
  std::cout << ",\"carrier\":"; text(asgp::Name(s.carrier));
  std::cout << ",\"output\":"; text(asgp::Name(s.output));
  std::cout << ",\"list_type\":"; text(asgp::Name(s.list_type));
  std::cout << ",\"sequence_input\":" << s.sequence_input
            << ",\"positional\":" << s.positional
            << ",\"base_size\":" << s.base_size
            << ",\"projection_enum\":" << static_cast<int>(s.projection)
            << ",\"metric_enum\":" << static_cast<int>(s.metric)
            << ",\"proximity_enum\":" << static_cast<int>(s.proximity)
            << ",\"summary_weight\":" << s.summary_weight
            << ",\"summary_cap\":" << s.summary_cap << ",\"scalar_names\":[";
  for (size_t i = 0; i < s.scalars.size(); ++i) {
    if (i) std::cout << ',';
    text(s.scalars[i]);
  }
  std::cout << "],\"original_unified_schemes\":[";
  const auto plans = asgp::UnifiedPlans(s);
  for (size_t i = 0; i < plans.size(); ++i) {
    if (i) std::cout << ',';
    text(asgp::Name(plans[i].scheme));
  }
  std::cout << "],\"train\":"; cases(s.train);
  std::cout << ",\"test\":"; cases(s.test);
  std::cout << "}\n";
}
