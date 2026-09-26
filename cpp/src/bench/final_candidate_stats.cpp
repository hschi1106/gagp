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
#include "gagp/evolution/grammar/variation.hpp"

namespace {
std::vector<std::string> captured_populations;
std::vector<gagp::evo::InputSpec> capture_inputs;
std::size_t capture_case_count = 0;
bool capture_evaluation_pending = true;
std::unique_ptr<gagp::evo::grammar::VariationContext> capture_context;

void capture_population(const std::vector<gagp::evo::ProgramGenome>& population,
                        const char* phase) {
  std::map<int, std::size_t> nodes, depths, node_kinds;
  std::size_t eligible_sites = 0, eligible_occurrences = 0, closed_sites = 0;
  std::map<std::string, std::uint64_t> compatibility_counts;
  std::map<std::string, std::size_t> verification;
  std::set<std::string> unique;
  for (const auto& member : population) {
    ++nodes[member.meta.node_count];
    ++depths[member.meta.max_depth];
    unique.insert(member.meta.program_key);
    for (const auto& node : member.ast.nodes) ++node_kinds[static_cast<int>(node.kind)];
    if (capture_context) {
      const auto analysis = capture_context->analyze(member);
      eligible_sites += analysis->sites.size();
      for (const auto& site : analysis->sites) {
        eligible_occurrences += site.occurrences.size();
        closed_sites += site.crossover_closed;
        ++compatibility_counts[site.compatibility_key];
      }
    }
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
  histogram("node_kind_counts", node_kinds);
  std::uint64_t compatible_pairs = 0;
  for (const auto& count : compatibility_counts) compatible_pairs += count.second * count.second;
  out << ",\"eligible_logical_sites\":" << eligible_sites
      << ",\"eligible_physical_occurrences\":" << eligible_occurrences
      << ",\"closed_eligible_sites\":" << closed_sites
      << ",\"contract_compatible_ordered_site_pairs\":" << compatible_pairs;
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
#include "../evolution/evolve.cpp"
#undef rank_population_refs
#undef run_reproduction_backend
#undef run_gpu_repro_backend
#undef finish_gpu_reproduction_overlap

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
    capture_context.reset();
    if (!options.grammar.empty()) {
      auto grammar = std::make_shared<const gagp::evo::grammar::CompiledGrammar>(
          gagp::evo::grammar::compile_grammar(gagp::evo::grammar::load_definition(options.grammar)));
      const auto requests = options.population_roots.empty()
          ? std::vector<gagp::evo::grammar::GenerationRequest>{gagp::evo::grammar::entry_request(*grammar)}
          : gagp::evo::grammar::named_population_requests(*grammar, options.population_roots);
      capture_context = std::make_unique<gagp::evo::grammar::VariationContext>(
          grammar, requests, 128, config(options).offspring_resource_budget);
    }
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
