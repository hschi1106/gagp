#include <algorithm>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

#include "gagp/evolution/grammar/resource_projection.hpp"

using namespace gagp::evo::grammar;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Error, class Action> void rejects(Action action) {
  try { action(); } catch (const Error&) { return; }
  throw std::runtime_error("invalid resource projection accepted");
}
struct Tree { ResourceCharge charge; std::vector<Tree> children; };
std::size_t count(const Tree& tree) {
  std::size_t n = 1; for (const auto& child : tree.children) n += count(child); return n;
}
void flatten(const Tree& tree, std::vector<std::size_t>& ends, std::vector<ResourceCharge>& charges) {
  const auto index = ends.size(); ends.push_back(0); charges.push_back(tree.charge);
  for (const auto& child : tree.children) flatten(child, ends, charges);
  ends[index] = ends.size();
}
ResourceProjection projection(const Tree& tree) {
  std::vector<std::size_t> ends; std::vector<ResourceCharge> charges;
  flatten(tree, ends, charges); return ResourceProjection(ends, charges);
}
std::pair<std::uint64_t, std::uint64_t> oracle(const Tree& tree, std::uint64_t incoming = 0) {
  const auto depth = (tree.charge.resets_depth ? 0 : incoming) + tree.charge.depth;
  std::uint64_t nodes = tree.charge.nodes, peak = depth;
  for (const auto& child : tree.children) {
    const auto result = oracle(child, depth); nodes += result.first; peak = std::max(peak, result.second);
  }
  return {nodes, peak};
}
Tree splice(const Tree& tree, std::size_t& index, const std::set<std::size_t>& roots, const Tree& donor) {
  if (roots.count(index)) { index += count(tree); return donor; }
  ++index; Tree out{tree.charge, {}};
  for (const auto& child : tree.children) out.children.push_back(splice(child, index, roots, donor));
  return out;
}
void check_subtrees(const Tree& tree, const ResourceProjection& projected, std::size_t& index) {
  const auto root = index++;
  for (std::uint64_t incoming : {0, 2, 7}) {
    const auto expected = oracle(tree, incoming);
    check(projected.subtree(root).nodes == expected.first &&
          projected.subtree(root).peak(incoming) == std::max(incoming, expected.second),
          "subtree transform disagrees with direct weighted traversal");
  }
  for (const auto& child : tree.children) check_subtrees(child, projected, index);
}
void property_replacements() {
  std::uint64_t comparisons = 0;
  for (unsigned pattern = 0; pattern < 32; ++pattern) {
    const auto c = [&](unsigned i) {
      return ResourceCharge{(pattern + i) % 3, (pattern / 3 + i) % 3, ((pattern >> (i % 5)) & 1) != 0};
    };
    const Tree tree{c(0), {{c(1), {{c(2), {{c(3), {}}}}}}, {c(4), {{c(5), {{c(6), {}}}}}}}};
    const auto projected = projection(tree);
    std::size_t index = 0; check_subtrees(tree, projected, index);
    const std::vector<Tree> donors{
        {{0,0,false}, {}}, {{1,1,false}, {}}, {{2,2,true}, {}},
        {{1,1,false}, {{{0,0,false}, {{{1,2,false},{}}}}}},
        {{0,0,false}, {{{1,3,true}, {}}, {{2,2,false}, {}}}}};
    for (const auto& roots : std::vector<std::vector<std::size_t>>{{0},{1},{2},{3},{4},{5},{6},{1,4},{2,5},{3,6}})
      for (const auto& donor : donors) {
        index = 0; const auto actual = splice(tree, index, {roots.begin(), roots.end()}, donor);
        const auto expected = oracle(actual);
        const auto measured_donor = projection(donor).subtree();
        for (std::uint64_t nodes = 0; nodes <= 16; ++nodes)
          for (std::uint64_t depth = 0; depth <= 10; ++depth) {
            const bool predicted = projected.replacement(roots, nodes, depth).accepts(measured_donor);
            check(predicted == (expected.first <= nodes && expected.second <= depth),
                  "replacement allowance differs from actual atomic splicing");
            ++comparisons;
          }
      }
  }
  check(comparisons == 299200, "replacement property coverage changed");
}
void boundaries() {
  rejects<std::invalid_argument>([] { projected_replacement_allowance(1, 2, 1, 0, 0, 1, 1); });
  rejects<std::invalid_argument>([] { projected_replacement_allowance(1, 1, 0, 0, 0, 1, 1); });
  const ResourceProjection physical({7,4,4,4,7,7,7});
  check(physical.subtree().nodes == 7 && physical.subtree().peak() == 4, "default physical accounting changed");
  const auto allowance = physical.replacement({1,4}, 9, 4);
  check(allowance.accepts({4,3,0}) && !allowance.accepts({5,3,0}) && !allowance.accepts({4,4,0}),
        "atomic copy multiplicity or incoming depth was ignored");
  rejects<std::invalid_argument>([] { ResourceProjection tree({}); });
  rejects<std::invalid_argument>([] { ResourceProjection tree({2,2}, {{}}); });
  rejects<std::invalid_argument>([] { ResourceProjection tree({4,3,4,4}); });
  rejects<std::invalid_argument>([] { ResourceProjection tree({3,1,3}); });
  for (const auto& roots : std::vector<std::vector<std::size_t>>{{},{1,1},{4,1},{1,2},{7}})
    rejects<std::invalid_argument>([&] { physical.replacement(roots, 9, 4); });
  const auto max = std::numeric_limits<std::uint64_t>::max();
  rejects<std::overflow_error>([&] { ResourceProjection tree({2,2}, {{max,0,false},{1,0,false}}); });
  rejects<std::overflow_error>([&] { ResourceProjection tree({2,2}, {{0,max,false},{0,1,false}}); });
  rejects<std::overflow_error>([&] { (void)ProjectedResources{0,max,0}.peak(1); });
}
}  // namespace
int main() {
  try { property_replacements(); boundaries(); }
  catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
  std::cout << "resource projection: 299200 atomic replacement predictions verified\n";
}
