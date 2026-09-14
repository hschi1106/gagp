#include "gagp/evolution/grammar/frame.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace gagp::evo::grammar {

std::vector<InputSpec> frame_inputs(const CompiledGrammar& grammar,
    const GenerationRequest& request, const GenerationFrame& frame) {
  (void)validate_request(grammar, request);

  const auto& target = grammar.nonterminals()[request.nonterminal];
  if (target.category == NodeCategory::Program && !frame.locals.empty())
    throw std::invalid_argument("Program generation frame must not contain locals");

  std::set<std::string> names;
  for (const auto& binding : frame.locals) {
    if (std::find(value_types().begin(), value_types().end(), binding.type) == value_types().end())
      throw std::invalid_argument("generation frame locals require exact public value types");
    if (!names.insert(binding.name).second)
      throw std::invalid_argument("generation frame has a duplicate local binding: " + binding.name);
    if (std::any_of(grammar.inputs().begin(), grammar.inputs().end(), [&](const auto& input) {
          return input.name == binding.name;
        }))
      throw std::invalid_argument("generation frame local conflicts with input name: " + binding.name);
    const auto declared = std::find_if(grammar.locals().begin(), grammar.locals().end(),
        [&](const auto& local) { return local.name == binding.name; });
    if (declared == grammar.locals().end())
      throw std::invalid_argument("generation frame has an unknown local binding: " + binding.name);
    if (declared->type != binding.type)
      throw std::invalid_argument("generation frame local has the wrong exact type: " + binding.name);
  }

  std::vector<InputSpec> inputs;
  inputs.reserve(grammar.inputs().size() + frame.locals.size());
  for (const auto& input : grammar.inputs()) inputs.push_back({input.name, input.type});
  for (const auto& local : frame.locals) inputs.push_back({local.name, local.type});
  return inputs;
}

}  // namespace gagp::evo::grammar
