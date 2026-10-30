// Diagnostic variant: keep the frozen solver/data; vary TaskSpec::metric only.
// kExact and kAbsolute also differ in invalid-case penalties in the source.
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

#include "gp/engine.h"
#include "task/registry.h"

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const std::filesystem::path out(argv[1]);
  if (!std::filesystem::is_directory(out)) return 2;
  std::cout << std::boolalpha << std::setprecision(17);
  for (const char* task : {"median", "smallest"}) {
    for (int seed = 0; seed < 4; ++seed) {
      // Alternate the two arms; timings are not compared.
      for (int slot = 0; slot < 2; ++slot) {
        const bool absolute = ((slot + seed) % 2) != 0;
        const std::string metric = absolute ? "absolute" : "exact";
        const std::string stem = std::string(task)+"-"+metric+"-seed"+std::to_string(seed);
        if (std::filesystem::exists(out/(stem+".program.txt")))
          throw std::runtime_error("Refusing to overwrite diagnostic run");
        asgp::DataConfig data;
        auto spec = *asgp::BuildTask(task, data);
        spec.metric = absolute ? asgp::Metric::kAbsolute : asgp::Metric::kExact;
        asgp::RunConfig config;
        config.seed = static_cast<uint64_t>(seed);
        config.threads = 4;
        config.simplify = false;
        std::ofstream progress(out/(stem+".progress.csv"));
        progress << "generation,nfe,best_error,best_wrong,best_nodes\n";
        config.on_generation = [&](const asgp::Progress& p) {
          progress << p.generation << ',' << p.evaluations << ','
                   << std::setprecision(17) << p.best.error << ','
                   << p.best.wrong << ',' << p.best.nodes << '\n';
        };
        const auto r = asgp::Run(spec, config);
        std::ofstream(out/(stem+".program.txt")) << r.program_text;
        std::cout << "{\"task\":\"" << task << "\",\"seed\":" << seed
                  << ",\"metric\":\"" << metric << "\",\"train_solved\":" << r.train_solved
                  << ",\"success\":" << r.raw_success << ",\"nfe\":" << r.evaluations
                  << ",\"generations\":" << r.generations
                  << ",\"nodes\":" << r.best_train.nodes
                  << ",\"train_wrong\":" << r.best_train.wrong
                  << ",\"train_error_sum\":" << r.best_train.error
                  << ",\"seconds\":" << r.seconds << "}\n" << std::flush;
      }
    }
  }
}
