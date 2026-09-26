#include "gagp/evolution/transition/linear_rec.hpp"
#include "linear_rec_internal.hpp"

#include <functional>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

#include "gagp/evolution/ast_verify.hpp"
#include "gagp/runtime/payload/payload.hpp"

namespace gagp::evo::transition {
namespace {
using Captures = std::map<int, int>;
namespace legacy = gagp::migration::legacy_v1;

class Adapter {
 public:
  Adapter(const legacy::AstProgram& source, const legacy::VerifiedAst& verified)
      : source_(source), verified_(verified) {
    node_map_.assign(source.nodes.size(), std::numeric_limits<std::size_t>::max());
    out_.version = source.version;
    out_.names = source.names;
    out_.consts = source.consts;
    for (const auto& region : source.lexical_regions)
      for (const auto& binding : region.bindings) used_.insert(binding.id);
    for (const auto& region : source.bounded_region_specs)
      for (const auto& phase : region.phases)
        for (const auto& binding : phase.bindings) used_.insert(binding.binder_id);
  }

  legacy::AstProgram run() {
    copy(0, {});
    copy_rows(source_.asgp_dc_binders, out_.asgp_dc_binders);
    copy_rows(source_.asgp_dp1d_specs, out_.asgp_dp1d_specs);
    copy_rows(source_.asgp_dp2d_specs, out_.asgp_dp2d_specs);
    copy_rows(source_.lexical_regions, out_.lexical_regions);
    copy_rows(source_.traversal_specs, out_.traversal_specs);
    copy_rows(source_.fuel_specs, out_.fuel_specs);
    copy_rows(source_.bounded_region_specs, out_.bounded_region_specs);
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

  std::size_t emit(legacy::NodeKind kind, int i0 = 0, int i1 = 0) {
    const auto index = out_.nodes.size();
    out_.nodes.push_back({kind, i0, i1});
    return index;
  }

  void charge(std::size_t index, FuelEvent event, std::uint32_t cost) {
    out_.fuel_specs.push_back({index, {{event, cost}}});
  }

  void ref(int id, std::uint32_t cost) {
    charge(emit(legacy::NodeKind::REGION_VAR, id), FuelEvent::Operation, cost);
  }

  void constant(std::int64_t value, std::uint32_t cost) {
    constant(Value::from_int(value), cost);
  }

  void constant(Value value, std::uint32_t cost) {
    const auto index = static_cast<int>(out_.consts.size());
    out_.consts.push_back(value);
    charge(emit(legacy::NodeKind::CONST, index), FuelEvent::Operation, cost);
  }

  Value empty_list(RType type) {
    if (type == RType::IntList) return payload::make_int_list_value({});
    if (type == RType::FloatList) return payload::make_float_list_value({});
    if (type == RType::StringList) return payload::make_string_list_value({});
    throw std::logic_error("map/filter transition requires a typed-list result");
  }

  void let(int id, RType type, std::uint32_t cost,
      const std::function<void()>& init, const std::function<void()>& body) {
    const auto index = emit(legacy::NodeKind::LET_REGION);
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
    if (node.kind == legacy::NodeKind::LINEAR_REC) { linear(index, captures); return; }
    if (node.kind == legacy::NodeKind::MAP_LIST ||
        node.kind == legacy::NodeKind::FILTER_LIST) {
      map_filter(index, captures);
      return;
    }
    auto copied = node;
    if (node.kind == legacy::NodeKind::BOUND_VAR) {
      const auto found = captures.find(node.i0);
      if (found != captures.end()) { copied.kind = legacy::NodeKind::REGION_VAR; copied.i0 = found->second; }
    }
    const auto destination = emit(copied.kind, copied.i0, copied.i1);
    node_map_[index] = destination;
    auto child = index + 1;
    for (int argument = 0; argument < legacy::prefix_arity(source_, index); ++argument) {
      auto visible = captures;
      if ((node.kind == legacy::NodeKind::MAP_LIST || node.kind == legacy::NodeKind::FILTER_LIST) && argument == 1)
        visible.erase(node.i0);
      if ((node.kind == legacy::NodeKind::ASGP_DC && argument >= 1) ||
          (node.kind == legacy::NodeKind::ASGP_DP1D && argument >= 1) ||
          (node.kind == legacy::NodeKind::ASGP_DP2D && argument >= 2)) visible.clear();
      copy(child, visible);
      child = verified_.subtree_end.at(child);
    }
  }

  void map_filter(std::size_t index, const Captures& captures) {
    const auto& old = source_.nodes.at(index);
    const bool filter = old.kind == legacy::NodeKind::FILTER_LIST;
    const std::size_t source = index + 1;
    const std::size_t body = verified_.subtree_end.at(source);
    const RType source_type = verified_.expression_types.at(source);
    const RType result_type = verified_.expression_types.at(index);
    const RType element_type = source_type == RType::IntList ? RType::Int :
        (source_type == RType::FloatList ? RType::Float : RType::String);
    const int element = binder(), semantic_index = binder(), state = binder();
    const auto traversal = emit(legacy::NodeKind::TRAVERSE);
    node_map_[index] = traversal;
    out_.lexical_regions.push_back({traversal, 3,
        {{element, element_type}, {semantic_index, RType::Int},
         {state, result_type}}});
    out_.traversal_specs.push_back({traversal, TraversalDirection::Forward});
    out_.fuel_specs.push_back({traversal, {
        {FuelEvent::StoreSequence, 1}, {FuelEvent::StoreStart, 0},
        {FuelEvent::CheckStart, 0}, {FuelEvent::ObserveSequence, 0},
        {FuelEvent::SetBegin, 0}, {FuelEvent::SetEnd, 0},
        {FuelEvent::InitializeState, 1},
        {FuelEvent::InitializeCursor, 2}, {FuelEvent::TestCursor, 5},
        {FuelEvent::ReadElement, 3}, {FuelEvent::BindElement, 1},
        {FuelEvent::ComputeIndex, 0}, {FuelEvent::UpdateState, filter ? 0U : 1U},
        {FuelEvent::AdvanceCursor, 4}, {FuelEvent::Repeat, 1},
        {FuelEvent::Result, 1}}});
    // Preserve the old check before storing the sequence. The cached length and
    // generic start/begin/end bookkeeping are administrative, not legacy work.
    emit(legacy::NodeKind::CHECK_LIST);
    copy(source, captures);
    constant(0, 0);
    constant(empty_list(result_type), filter ? 2 : 1);
    auto body_captures = captures;
    body_captures[old.i0] = element;
    if (filter) {
      emit(legacy::NodeKind::IF_EXPR);
      copy(body, body_captures);
      emit(legacy::NodeKind::CALL_APPEND);
      ref(state, 1);
      ref(element, 1);
      // The true branch's merge charge accounts for the old output store;
      // the false branch neither stores nor loads an observable legacy value.
      ref(state, 0);
    } else {
      const int value = binder();
      const RType value_type = result_type == RType::IntList ? RType::Int :
          (result_type == RType::FloatList ? RType::Float : RType::String);
      let(value, value_type, 1,
          [&] { copy(body, body_captures); }, [&] {
            emit(legacy::NodeKind::CALL_APPEND);
            ref(state, 1);
            ref(value, 1);
          });
    }
  }

  void linear(std::size_t index, const Captures& captures) {
    const legacy::LinearRecBinders* roles = nullptr;
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
      let(start, RType::Int, 1, [&] { emit(legacy::NodeKind::CHECK_INT); copy(child[1], captures); }, [&] {
        let(checked, sequence_type, 1, [&] { emit(legacy::NodeKind::CHECK_LIST); ref(xs, 0); }, [&] {
          let(length, RType::Int, 1, [&] { emit(legacy::NodeKind::CALL_LEN); ref(checked, 1); }, [&] {
            emit(legacy::NodeKind::IF_EXPR);
            emit(legacy::NodeKind::EQ); ref(length, 1); constant(0, 1);
            copy(child[2], captures);
            let(last_offset, RType::Int, 1, [&] {
              emit(legacy::NodeKind::SUB); ref(length, 1); constant(1, 1);
            }, [&] {
              let(last_element, element_type, 1, [&] {
                emit(legacy::NodeKind::CALL_INDEX); ref(checked, 1); ref(last_offset, 1);
              }, [&] {
                let(last_index, RType::Int, 1, [&] {
                  emit(legacy::NodeKind::ADD); ref(start, 1); ref(last_offset, 1);
                }, [&] {
                  const auto traversal = emit(legacy::NodeKind::TRAVERSE_RANGE);
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

  const legacy::AstProgram& source_;
  const legacy::VerifiedAst& verified_;
  legacy::AstProgram out_;
  std::vector<std::size_t> node_map_;
  std::set<int> used_;
  int next_ = 0;
};

AstProgram current_ast(const legacy::AstProgram& source) {
  if (!source.linear_rec_binders.empty() || !source.asgp_dc_binders.empty() ||
      !source.asgp_dp1d_specs.empty() || !source.asgp_dp2d_specs.empty()) {
    throw std::invalid_argument(
        "LinearRec-only migration cannot emit an AST containing another legacy specialized form");
  }
  AstProgram out;
  out.names = source.names;
  out.consts = source.consts;
  out.version = k_ast_prefix_version_current;
  out.lexical_regions = source.lexical_regions;
  out.traversal_specs = source.traversal_specs;
  out.fuel_specs = source.fuel_specs;
  out.bounded_region_specs = source.bounded_region_specs;
  out.nodes.reserve(source.nodes.size());
  for (const auto& node : source.nodes)
    out.nodes.push_back({legacy::current_kind(node.kind), node.i0, node.i1});
  return out;
}
}  // namespace

namespace detail {
legacy::AstProgram lower_linear_rec_legacy_ast(
    const legacy::AstProgram& source, const std::vector<InputSpec>& inputs) {
  const auto verified = legacy::verify(source, inputs);
  auto result = Adapter(source, verified).run();
  (void)legacy::verify(result, inputs);
  return result;
}
}  // namespace detail

ProgramGenome lower_linear_rec(const legacy::AstProgram& source,
                               const std::vector<InputSpec>& inputs) {
  ProgramGenome result;
  result.ast = current_ast(detail::lower_linear_rec_legacy_ast(source, inputs));
  const auto lowered = verify_ast(result.ast, inputs);
  if (!lowered) throw std::logic_error("LinearRec transition result: " + lowered.diagnostic.message);
  result.meta = build_genome_meta(result.ast);
  return result;
}

}  // namespace gagp::evo::transition
