#include "gagp/evolution/grammar/structured.hpp"

#include <stdexcept>

namespace gagp::evo::grammar {
namespace {
void validate(const std::vector<RType>& state, RType result, std::uint32_t requests) {
  if (state.empty() || state.size() > kStructuredStateCapacity)
    throw std::invalid_argument("structured state requires 1..4 exact typed slots");
  if (!requests || requests > kStructuredRequestCapacity)
    throw std::invalid_argument("structured region requires 1..8 ordered request sites");
  for (auto type : state) (void)type_name(type);
  (void)type_name(result);
}
std::vector<RegionBinding> state_bindings(const std::vector<RType>& state) {
  std::vector<RegionBinding> result;
  for (std::size_t i = 0; i < state.size(); ++i) result.push_back({"state" + std::to_string(i), state[i]});
  return result;
}
void region(StructuredContract& contract, RType type, const std::vector<RegionBinding>& bindings) {
  contract.regions.push_back({static_cast<std::uint32_t>(contract.arguments.size()), bindings});
  contract.arguments.push_back(type);
}
void combine(StructuredContract& contract, std::vector<RegionBinding> bindings) {
  for (std::uint32_t i = 0; i < contract.requests; ++i)
    bindings.push_back({"result" + std::to_string(i), contract.result});
  contract.combine_body = static_cast<std::uint32_t>(contract.arguments.size());
  region(contract, contract.result, bindings);
}
std::string key(const char* family, const std::vector<RType>& state, RType result, std::uint32_t requests) {
  std::string out = std::string(family) + '<';
  for (std::size_t i = 0; i < state.size(); ++i) {
    if (i) out += ',';
    out += type_name(state[i]);
  }
  return out + ";requests=" + std::to_string(requests) + ">->" + std::string(type_name(result));
}
}  // namespace

StructuredContract recursive_contract(const std::vector<RType>& state, RType result, std::uint32_t requests) {
  validate(state, result, requests);
  StructuredContract contract;
  contract.family = StructuredFamily::BoundedRecursion;
  contract.state_types = state; contract.result = result; contract.requests = requests;
  contract.key = key("recur", state, result, requests);
  contract.arguments = state;
  const auto bindings = state_bindings(state);
  contract.base_predicate = static_cast<std::uint32_t>(contract.arguments.size());
  region(contract, RType::Bool, bindings);
  contract.base_body = static_cast<std::uint32_t>(contract.arguments.size());
  region(contract, result, bindings);
  for (std::uint32_t request = 0; request < requests; ++request)
    for (auto type : state) region(contract, type, bindings);
  combine(contract, bindings);
  return contract;
}

StructuredContract memoized_contract(std::uint32_t dimensions, RType result, std::uint32_t requests) {
  if (!dimensions || dimensions > kStructuredStateCapacity)
    throw std::invalid_argument("memoized rank requires 1..4 integer coordinates");
  const std::vector<RType> state(dimensions, RType::Int);
  validate(state, result, requests);
  StructuredContract contract;
  contract.family = StructuredFamily::MemoizedRecurrence;
  contract.state_types = state; contract.result = result; contract.requests = requests;
  contract.key = key("memo", state, result, requests);
  contract.arguments.assign(2 * dimensions, RType::Int);
  const auto bindings = state_bindings(state);
  contract.boundary_body = static_cast<std::uint32_t>(contract.arguments.size());
  region(contract, result, bindings);
  contract.base_predicate = static_cast<std::uint32_t>(contract.arguments.size());
  region(contract, RType::Bool, bindings);
  contract.base_body = static_cast<std::uint32_t>(contract.arguments.size());
  region(contract, result, bindings);
  combine(contract, bindings);
  return contract;
}

void require_structured_execution(const StructuredContract& contract) {
  throw std::invalid_argument("structured primitive is declared but execution is not implemented: " + contract.key);
}

}  // namespace gagp::evo::grammar
