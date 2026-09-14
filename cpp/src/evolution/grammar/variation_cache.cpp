#include "gagp/evolution/grammar/variation_cache.hpp"

#include <stdexcept>
#include <string_view>

#include "gagp/evolution/grammar/cache.hpp"

namespace gagp::evo::grammar {
namespace {

void append_field(std::string& output, std::string_view value) {
  output += std::to_string(value.size());
  output += ':';
  output.append(value.data(), value.size());
}

void append_number(std::string& output, std::uint32_t value) {
  append_field(output, std::to_string(value));
}

}  // namespace

VariationAnalysisCache::VariationAnalysisCache(
    std::shared_ptr<const CompiledGrammar> grammar, std::size_t capacity)
    : grammar_(std::move(grammar)), capacity_(capacity) {
  if (!grammar_) throw std::invalid_argument("variation analysis cache requires a grammar");
  if (capacity_ == 0) throw std::invalid_argument("variation analysis cache capacity must be positive");
  input_names_.reserve(grammar_->inputs().size());
  for (const auto& input : grammar_->inputs()) input_names_.push_back(input.name);
  entries_.reserve(capacity_);
}

std::string VariationAnalysisCache::key(
    const ProgramGenome& genome, const GenerationRequest& request) const {
  std::string result;
  append_field(result, "grammar-variation-analysis-cache-v1");
  append_field(result, grammar_->content_hash());
  append_field(result, runtime_cache_identity(
      genome, input_names_, grammar_->execution_limits().fuel));
  append_number(result, request.nonterminal);
  append_number(result, static_cast<std::uint32_t>(request.type));
  append_number(result, static_cast<std::uint32_t>(request.visible_environment.size()));
  for (const auto& binding : request.visible_environment) {
    append_field(result, binding.name);
    append_number(result, static_cast<std::uint32_t>(binding.type));
  }
  append_number(result, request.budget.max_nodes);
  append_number(result, request.budget.max_depth);
  return result;
}

std::shared_ptr<const VariationAnalysis> VariationAnalysisCache::analyze(
    const ProgramGenome& genome, const GenerationRequest& request) {
  // Invalid requests never participate in cache lookup or accounting.
  (void)validate_request(*grammar_, request);
  const auto cache_key = key(genome, request);
  const auto found = entries_.find(cache_key);
  if (found != entries_.end()) {
    ++counters_.hits;
    return found->second;
  }

  ++counters_.misses;
  auto analysis = std::make_shared<const VariationAnalysis>(
      analyze_variation(*grammar_, genome, request, &registry_));
  if (entries_.size() == capacity_) {
    entries_.erase(insertion_order_.front());
    insertion_order_.pop_front();
    ++counters_.evictions;
  }
  insertion_order_.push_back(cache_key);
  entries_.emplace(std::move(cache_key), analysis);
  return analysis;
}

std::shared_ptr<const VariationAnalysis> VariationAnalysisCache::analyze(
    const ProgramGenome& genome) {
  return analyze(genome, entry_request(*grammar_));
}

}  // namespace gagp::evo::grammar
