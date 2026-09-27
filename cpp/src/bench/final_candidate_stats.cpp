// Diagnostic-only interception of the actual evolution implementation. Do not
// use this executable for timings: observers deliberately inspect every member.
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/lifecycle.hpp"
#include "gagp/evolution/repro/gpu.hpp"
#include "gagp/evolution/selection.hpp"
#include "../evolution/repro/owned_overlap.hpp"

namespace {
std::vector<std::string> captured_populations;
std::vector<gagp::evo::InputSpec> capture_inputs;
std::size_t capture_case_count = 0;
bool capture_evaluation_pending = true;

void capture_population(const std::vector<gagp::evo::ProgramGenome>& population,
                        const char* phase) {
  std::map<int, std::size_t> nodes, depths;
  std::map<std::string, std::size_t> verification;
  std::set<std::string> unique;
  for (const auto& member : population) {
    ++nodes[member.meta.node_count];
    ++depths[member.meta.max_depth];
    unique.insert(member.meta.program_key);
    const auto checked = gagp::evo::verify_ast(member.ast, capture_inputs);
    ++verification[gagp::evo::verify_code_name(checked.diagnostic.code)];
  }
  std::ostringstream out;
  out << "{\"phase\":\"" << phase << "\",\"population_size\":" << population.size()
      << ",\"unique_programs\":" << unique.size();
  if (std::string(phase) == "evaluation")
    out << ",\"fitness_evaluations_requested\":" << population.size()
        << ",\"case_score_requests\":" << population.size() * capture_case_count;
  const auto histogram = [&](const char* name, const std::map<int, std::size_t>& values) {
    out << ",\"" << name << "\":[";
    bool first = true;
    for (const auto& item : values) {
      if (!first) out << ',';
      first = false;
      out << '[' << item.first << ',' << item.second << ']';
    }
    out << ']';
  };
  histogram("node_counts", nodes);
  histogram("depths", depths);
  out << ",\"post_backend_verifier_counts\":{";
  bool first = true;
  for (const auto& item : verification) {
    if (!first) out << ',';
    first = false;
    out << '"' << item.first << "\":" << item.second;
  }
  out << "}}";
  captured_populations.push_back(out.str());
  if (std::string(phase) == "reproduction") capture_evaluation_pending = true;
}
}  // namespace

namespace gagp::evo {
std::vector<ScoredGenomeRef> capture_rank(const std::vector<ProgramGenome>& population,
                                        const std::vector<double>& fitness, bool sort_output) {
  // Direct GPU reproduction also ranks the already evaluated fitness vector
  // into source order. Count the first ranking of each generation only; the
  // reproduction observer opens the next evaluation (including final eval).
  if (capture_evaluation_pending) {
    capture_population(population, "evaluation");
    capture_evaluation_pending = false;
  }
  return rank_population_refs(population, fitness, sort_output);
}
namespace repro {
OwnedGpuReproOverlap capture_owned_overlap(std::vector<ProgramGenome> population,
    const EvolutionConfig& cfg, std::uint64_t seed,
    std::shared_ptr<GpuReproRunResources> resources) {
  auto result = start_owned_gpu_repro_overlap(
      std::move(population), cfg, seed, std::move(resources));
  // Keep preparation asynchronous; intercept only the completed reproduction
  // in this diagnostic executable, after fitness has been supplied.
  result.completion = std::async(std::launch::deferred,
      [prepared = std::move(result.completion)]() mutable {
        auto complete = prepared.get();
        return std::function<ReproductionResult(const std::vector<double>&)>(
            [complete = std::move(complete)](const std::vector<double>& fitness) mutable {
              auto reproduction = complete(fitness);
              capture_population(reproduction.next_population, "reproduction");
              return reproduction;
            });
      });
  return result;
}
ReproductionResult capture_reproduction(const std::vector<ScoredGenomeRef>& scored,
                                        const EvolutionConfig& cfg, std::mt19937_64& rng) {
  auto result = run_reproduction_backend(scored, cfg, rng);
  capture_population(result.next_population, "reproduction");
  return result;
}
ReproductionResult capture_gpu_reproduction(const std::vector<ScoredGenomeRef>& scored,
    const EvolutionConfig& cfg, std::mt19937_64& rng,
    std::shared_ptr<GpuReproRunResources> resources) {
  auto result = run_gpu_repro_backend(scored, cfg, rng, std::move(resources));
  capture_population(result.next_population, "reproduction");
  return result;
}
}  // namespace repro
repro::ReproductionResult capture_overlap(std::future<OverlapPrepared>* future,
    const std::vector<ProgramGenome>& population, const std::vector<double>& fitness,
    const EvolutionConfig& cfg) {
  auto result = finish_gpu_reproduction_overlap(future, population, fitness, cfg);
  capture_population(result.next_population, "reproduction");
  return result;
}
}  // namespace gagp::evo

#define rank_population_refs capture_rank
#define run_reproduction_backend capture_reproduction
#define run_gpu_repro_backend capture_gpu_reproduction
#define finish_gpu_reproduction_overlap capture_overlap
#define start_owned_gpu_repro_overlap capture_owned_overlap
#include "../evolution/evolve.cpp"
#undef rank_population_refs
#undef run_reproduction_backend
#undef run_gpu_repro_backend
#undef finish_gpu_reproduction_overlap
#undef start_owned_gpu_repro_overlap

#define main final_candidate_benchmark_main
#include "final_candidate_bench.cpp"
#undef main

int main(int argc, char** argv) {
  try {
    const auto options = parse(argc, argv);
    if (options.action != "run")
      throw std::invalid_argument("population statistics require --action run");
    const auto cases = gagp::cli_detail::decode_fitness_cases_json(read_json(options.cases));
    capture_inputs = gagp::evo::prepare_case_set(cases).input_specs;
    capture_case_count = cases.size();
    captured_populations.clear();
    capture_evaluation_pending = true;
    const int status = final_candidate_benchmark_main(argc, argv);
    if (status != 0) return status;
    std::string result = read_text(options.output);
    const auto end = result.find_last_of('}');
    if (end == std::string::npos) throw std::runtime_error("benchmark output is not an object");
    result.resize(end);
    result += ",\"diagnostic_only\":true,\"population_observations\":[";
    for (std::size_t i = 0; i < captured_populations.size(); ++i) {
      if (i) result += ',';
      result += captured_populations[i];
    }
    result += "]}";
    write_text(options.output, result);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "final candidate statistics: " << error.what() << '\n';
    return 2;
  }
}
