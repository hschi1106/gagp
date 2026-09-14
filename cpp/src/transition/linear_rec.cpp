#include "gagp/evolution/transition/linear_rec.hpp"

#include <functional>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/node_descriptor.hpp"

namespace gagp::evo::transition {
namespace {
using Captures = std::map<int, int>;

class Adapter {
 public:
  Adapter(const AstProgram& source, const VerifiedAst& verified)
      : source_(source), verified_(verified) {
    node_map_.assign(source.nodes.size(), std::numeric_limits<std::size_t>::max());
    out_.version = source.version;
    out_.names = source.names;
    out_.consts = source.consts;
    for (const auto& region : source.lexical_regions)
      for (const auto& binding : region.bindings) used_.insert(binding.id);
  }

  AstProgram run() {
    copy(0, {});
    copy_rows(source_.asgp_dc_binders, out_.asgp_dc_binders);
    copy_rows(source_.asgp_dp1d_specs, out_.asgp_dp1d_specs);
    copy_rows(source_.asgp_dp2d_specs, out_.asgp_dp2d_specs);
    copy_rows(source_.lexical_regions, out_.lexical_regions);
    copy_rows(source_.traversal_specs, out_.traversal_specs);
    copy_rows(source_.fuel_specs, out_.fuel_specs);
    return std::move(out_);
  }

 private:
  int binder() {
    while (used_.count(next_)) ++next_;
    if (next_ == std::numeric_limits<int>::max())
      throw std::invalid_argument("LinearRec transition exhausted native binder IDs");
    used_.insert(next_);
    return next_++;
  }

  std::size_t emit(NodeKind kind, int i0 = 0, int i1 = 0) {
    const auto index = out_.nodes.size();
    out_.nodes.push_back({kind, i0, i1});
    return index;
  }

  void charge(std::size_t index, FuelEvent event, std::uint32_t cost) {
    out_.fuel_specs.push_back({index, {{event, cost}}});
  }

  void ref(int id, std::uint32_t cost) {
    charge(emit(NodeKind::REGION_VAR, id), FuelEvent::Operation, cost);
  }

  void constant(std::int64_t value, std::uint32_t cost) {
    const auto index = static_cast<int>(out_.consts.size());
    out_.consts.push_back(Value::from_int(value));
    charge(emit(NodeKind::CONST, index), FuelEvent::Operation, cost);
  }

  void let(int id, RType type, std::uint32_t cost,
      const std::function<void()>& init, const std::function<void()>& body) {
    const auto index = emit(NodeKind::LET_REGION);
    out_.lexical_regions.push_back({index, 1, {{id, type}}});
    charge(index, FuelEvent::Bind, cost);
    init(); body();
  }

  template <class Rows>
  void copy_rows(const Rows& source, Rows& target) {
    for (auto row : source) {
      row.node_index = node_map_.at(row.node_index);
      if (row.node_index == std::numeric_limits<std::size_t>::max())
        throw std::logic_error("LinearRec transition lost a retained metadata owner");
      target.push_back(std::move(row));
    }
  }

  void copy(std::size_t index, const Captures& captures) {
    const auto& node = source_.nodes.at(index);
    if (node.kind == NodeKind::LINEAR_REC) { linear(index, captures); return; }
    auto copied = node;
    if (node.kind == NodeKind::BOUND_VAR) {
      const auto found = captures.find(node.i0);
      if (found != captures.end()) { copied.kind = NodeKind::REGION_VAR; copied.i0 = found->second; }
    }
    const auto destination = emit(copied.kind, copied.i0, copied.i1);
    node_map_[index] = destination;
    auto child = index + 1;
    for (int argument = 0; argument < node_descriptor(node.kind).prefix_arity; ++argument) {
      auto visible = captures;
      if ((node.kind == NodeKind::MAP_LIST || node.kind == NodeKind::FILTER_LIST) && argument == 1)
        visible.erase(node.i0);
      if ((node.kind == NodeKind::ASGP_DC && argument >= 1) ||
          (node.kind == NodeKind::ASGP_DP1D && argument >= 1) ||
          (node.kind == NodeKind::ASGP_DP2D && argument >= 2)) visible.clear();
      copy(child, visible);
      child = verified_.subtree_end.at(child);
    }
  }

  void linear(std::size_t index, const Captures& captures) {
    const LinearRecBinders* roles = nullptr;
    for (const auto& row : source_.linear_rec_binders) if (row.node_index == index) roles = &row;
    if (!roles) throw std::logic_error("verified LinearRec lost its binding metadata");
    std::size_t child[5];
    child[0] = index + 1;
    for (int i = 1; i < 5; ++i) child[i] = verified_.subtree_end.at(child[i - 1]);
    const auto sequence_type = verified_.expression_types.at(child[0]);
    const auto result_type = verified_.expression_types.at(index);
    const auto element_type = sequence_type == RType::IntList ? RType::Int :
        (sequence_type == RType::FloatList ? RType::Float : RType::String);
    const int xs = binder(), start = binder(), checked = binder(), length = binder();
    const int last_offset = binder(), last_element = binder(), last_index = binder();
    const int element = binder(), semantic_index = binder(), state = binder();

    // Store xs administratively before evaluating start, then reproduce the old
    // checked start/store and checked sequence/store event ordering explicitly.
    let(xs, sequence_type, 0, [&] { copy(child[0], captures); }, [&] {
      let(start, RType::Int, 1, [&] { emit(NodeKind::CHECK_INT); copy(child[1], captures); }, [&] {
        let(checked, sequence_type, 1, [&] { emit(NodeKind::CHECK_LIST); ref(xs, 0); }, [&] {
          let(length, RType::Int, 1, [&] { emit(NodeKind::CALL_LEN); ref(checked, 1); }, [&] {
            emit(NodeKind::IF_EXPR);
            emit(NodeKind::EQ); ref(length, 1); constant(0, 1);
            copy(child[2], captures);
            let(last_offset, RType::Int, 1, [&] {
              emit(NodeKind::SUB); ref(length, 1); constant(1, 1);
            }, [&] {
              let(last_element, element_type, 1, [&] {
                emit(NodeKind::CALL_INDEX); ref(checked, 1); ref(last_offset, 1);
              }, [&] {
                let(last_index, RType::Int, 1, [&] {
                  emit(NodeKind::ADD); ref(start, 1); ref(last_offset, 1);
                }, [&] {
                  const auto traversal = emit(NodeKind::TRAVERSE_RANGE);
                  out_.lexical_regions.push_back({traversal, 5,
                      {{element, element_type}, {semantic_index, RType::Int}, {state, result_type}}});
                  out_.traversal_specs.push_back({traversal, TraversalDirection::Reverse});
                  out_.fuel_specs.push_back({traversal, {
                      {FuelEvent::StoreSequence, 0}, {FuelEvent::StoreStart, 0},
                      {FuelEvent::StoreBegin, 0}, {FuelEvent::StoreEnd, 0},
                      {FuelEvent::CheckStart, 0}, {FuelEvent::CheckBegin, 0}, {FuelEvent::CheckEnd, 0},
                      {FuelEvent::ObserveSequence, 0}, {FuelEvent::ClampBegin, 0}, {FuelEvent::ClampEnd, 0},
                      {FuelEvent::InitializeState, 1}, {FuelEvent::InitializeCursor, 0},
                      {FuelEvent::TestCursor, 4}, {FuelEvent::ReadElement, 7},
                      {FuelEvent::BindElement, 1}, {FuelEvent::ComputeIndex, 4},
                      {FuelEvent::UpdateState, 1}, {FuelEvent::AdvanceCursor, 0},
                      {FuelEvent::Repeat, 1}, {FuelEvent::Result, 1}}});
                  ref(checked, 0); ref(start, 0); constant(0, 0); ref(last_offset, 0);
                  auto last = captures;
                  last[roles->elem_name] = last_element; last[roles->index_name] = last_index;
                  copy(child[4], last);
                  auto step = captures;
                  step[roles->elem_name] = element; step[roles->index_name] = semantic_index;
                  step[roles->accum_name] = state;
                  copy(child[3], step);
                });
              });
            });
          });
        });
      });
    });
  }

  const AstProgram& source_;
  const VerifiedAst& verified_;
  AstProgram out_;
  std::vector<std::size_t> node_map_;
  std::set<int> used_;
  int next_ = 0;
};
}  // namespace

ProgramGenome lower_linear_rec(const ProgramGenome& source, const std::vector<InputSpec>& inputs) {
  const auto verified = verify_ast(source.ast, inputs);
  if (!verified) throw std::invalid_argument("LinearRec transition source: " + verified.diagnostic.message);
  ProgramGenome result;
  result.ast = Adapter(source.ast, verified.verified).run();
  const auto lowered = verify_ast(result.ast, inputs);
  if (!lowered) throw std::logic_error("LinearRec transition result: " + lowered.diagnostic.message);
  result.meta = build_genome_meta(result.ast);
  return result;
}

}  // namespace gagp::evo::transition
