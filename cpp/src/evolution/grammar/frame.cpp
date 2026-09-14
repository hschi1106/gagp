#include "gagp/evolution/grammar/frame.hpp"

#include <algorithm>
#include <set>
#include <limits>
#include <map>
#include <stdexcept>

namespace gagp::evo::grammar {

std::vector<InputSpec> frame_inputs(const CompiledGrammar& grammar,
    const GenerationRequest& request, const GenerationFrame& frame) {
  (void)validate_request(grammar, request);
  if (!frame.binder_ids.empty() && frame.binder_ids.size() != request.visible_environment.size())
    throw std::invalid_argument("generation frame binder IDs must match the visible environment");
  std::set<int> binder_ids;
  for (int id : frame.binder_ids) {
    if (id < 0 || id == std::numeric_limits<int>::max() || !binder_ids.insert(id).second)
      throw std::invalid_argument("generation frame requires unique valid native binder IDs");
  }

  const auto& target = grammar.nonterminals()[request.nonterminal];
  if (target.category == NodeCategory::Program && (!frame.locals.empty() || !frame.binder_ids.empty()))
    throw std::invalid_argument("Program generation frame must not contain locals or binders");

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

std::vector<int> frame_environment(const CompiledGrammar& grammar,
    const GenerationRequest& request, const GenerationFrame& frame) {
  (void)frame_inputs(grammar, request, frame);
  const auto mapping = validate_request(grammar, request);
  std::vector<int> result(mapping.size(), -1);
  if (!frame.binder_ids.empty())
    for (std::size_t i = 0; i < mapping.size(); ++i) result[i] = frame.binder_ids.at(mapping[i]);
  return result;
}

FramedProgram project_frame(const CompiledGrammar& grammar,
    const GenerationRequest& request, const GenerationFrame& frame, const AstProgram& ast) {
  FramedProgram result{ast, frame_inputs(grammar, request, frame)};
  std::set<int> external(frame.binder_ids.begin(), frame.binder_ids.end());
  for (const auto& region : ast.lexical_regions)
    for (const auto& binding : region.bindings)
      if (external.count(binding.id))
        throw std::invalid_argument("generation frame binder collides with an introduced region binding");
  std::set<std::string> names(ast.names.begin(), ast.names.end());
  for (const auto& input : result.inputs) names.insert(input.name);
  std::map<int, int> projected;
  std::size_t suffix = 0;
  for (std::size_t i = 0; i < frame.binder_ids.size(); ++i) {
    std::string name;
    do { name = "__gagp_frame_binder_" + std::to_string(suffix++); } while (names.count(name));
    names.insert(name);
    projected.emplace(frame.binder_ids[i], static_cast<int>(result.ast.names.size()));
    result.ast.names.push_back(name);
    result.inputs.push_back({name, request.visible_environment.at(i).type});
  }
  for (auto& node : result.ast.nodes) {
    if (node.kind != NodeKind::REGION_VAR) continue;
    const auto found = projected.find(node.i0);
    if (found != projected.end()) { node.kind = NodeKind::VAR; node.i0 = found->second; }
  }
  return result;
}

}  // namespace gagp::evo::grammar
