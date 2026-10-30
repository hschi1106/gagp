// Bounded 2x2: source loss path x source phase-variation policy. No library edits.
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include "gp/engine.h"
#include "task/registry.h"

int main(int argc, char** argv) {
  if (argc != 2 || !std::filesystem::is_directory(argv[1])) return 2;
  const std::filesystem::path out(argv[1]);
  std::cout << std::boolalpha << std::setprecision(17);
  for (const char* task : {"median", "smallest"}) {
    for (int seed = 0; seed < 4; ++seed) {
      for (bool every : {false, true}) {
        for (bool absolute : {false, true}) {
          const std::string reach = every ? "every" : "one";
          const std::string metric = absolute ? "absolute" : "exact";
          const auto file = out/(std::string(task)+"-"+reach+"-"+metric+"-seed"+
                                  std::to_string(seed)+".program.txt");
          if (std::filesystem::exists(file)) throw std::runtime_error("Existing attempt retained");
          auto spec = *asgp::BuildTask(task, asgp::DataConfig{});
          spec.metric = absolute ? asgp::Metric::kAbsolute : asgp::Metric::kExact;
          asgp::RunConfig config;
          config.seed = static_cast<uint64_t>(seed);
          config.threads = 4;
          config.simplify = true;
          config.reach = every ? asgp::Reach::kEveryPhase : asgp::Reach::kOnePhase;
          const auto r = asgp::Run(spec, config);
          std::ofstream(file) << r.program_text;
          std::cout << "{\"task\":\"" << task << "\",\"seed\":" << seed
                    << ",\"metric\":\"" << metric << "\",\"reach\":\"" << reach
                    << "\",\"train_solved\":" << r.train_solved
                    << ",\"raw_success\":" << r.raw_success
                    << ",\"post_success\":" << r.success
                    << ",\"nfe\":" << r.evaluations
                    << ",\"before_nodes\":" << r.best_train.nodes
                    << ",\"after_nodes\":" << r.simplification.after
                    << ",\"train_wrong\":" << r.best_train.wrong
                    << ",\"train_error_sum\":" << r.best_train.error
                    << ",\"seconds\":" << r.seconds << "}\n" << std::flush;
        }
      }
    }
  }
}
