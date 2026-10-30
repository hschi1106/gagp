// Static audit of the original frozen population; no runtime or grammar edits.
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include "gagp/cli/commands.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/node_descriptor.hpp"

template<class Program>
std::uint64_t acyclic_cost(const Program& p, bool allow_region = false) {
  std::uint64_t cost = 0;
  for (std::size_t i = 0; i < p.code.size(); ++i) {
    const auto& ins = p.code[i];
    if ((ins.op == gagp::Opcode::Jmp || ins.op == gagp::Opcode::JmpIfFalse ||
         ins.op == gagp::Opcode::JmpIfTrue) && ins.a <= static_cast<int>(i))
      throw std::runtime_error("backward jump invalidates bound");
    if (!allow_region && ins.op == gagp::Opcode::BoundedRegion)
      throw std::runtime_error("nested region invalidates bound");
    cost += p.instruction_fuel.empty() ? 1 : p.instruction_fuel.at(i);
  }
  return cost;
}

int main(int argc, char** argv) try {
  if (argc != 2) throw std::runtime_error("audit_limits INPUTS");
  std::ifstream file(argv[1]);
  if (!file) throw std::runtime_error("missing input");
  std::ostringstream text;
  text << file.rdbuf();
  const auto root = gagp::cli_detail::JsonParser(text.str(), {true, 512}).parse();
  std::size_t max_length = 0;
  for (const auto& c : root.object_v.at("cases").array_v)
    max_length = std::max(max_length, c.object_v.at("input").array_v.size());
  if (max_length != 50) throw std::runtime_error("unexpected case length");
  std::cout << "program,nodes,physical_depth,expression_depth,fuel_upper_bound_per_case\n";
  int index = 0;
  for (const auto& value : root.object_v.at("gagp_ast").array_v) {
    gagp::evo::ProgramGenome g;
    g.ast = gagp::cli_detail::decode_ast_json(value);
    g.meta = gagp::evo::build_genome_meta(g.ast);
    std::vector<int> pending{1};
    std::size_t depth = 0;
    for (const auto& node : g.ast.nodes) {
      while (!pending.empty() && !pending.back()) pending.pop_back();
      if (pending.empty()) throw std::runtime_error("trailing node");
      depth = std::max(depth, pending.size());
      --pending.back();
      const auto arity = gagp::evo::node_prefix_arity(node);
      if (arity) pending.push_back(arity);
    }
    const auto code = gagp::evo::compile_for_eval(g, {"source"});
    if (code.bounded_region_segments.size() != 1)
      throw std::runtime_error("expected one DC segment");
    const auto& segment = code.bounded_region_segments[0];
    if (segment.plan.progress != gagp::RegionProgressKind::SequenceWindows ||
        segment.plan.memoized || segment.plan.requests.size() != 2)
      throw std::runtime_error("unexpected DC skeleton");
    std::uint64_t phases = acyclic_cost(segment.base_predicate.program) +
        acyclic_cost(segment.base_body.program) + acyclic_cost(segment.combine.program);
    for (const auto& p : segment.preparations) phases += acyclic_cost(p.program);
    for (const auto& p : segment.request_expressions) phases += acyclic_cost(p.program);
    // Frozen skeleton partitions each list into two nonempty disjoint windows.
    // At length n it visits 2*n-1 frames. Counting every phase at every frame
    // overestimates execution (including both branches of all conditionals).
    const auto upper = acyclic_cost(code, true) + (2 * max_length - 1) *
        (phases + segment.plan.limits.entry_fuel);
    std::cout << index++ << ',' << g.ast.nodes.size() << ',' << depth << ','
              << g.meta.max_depth << ',' << upper << '\n';
  }
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
