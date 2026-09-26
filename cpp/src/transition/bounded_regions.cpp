#include "gagp/evolution/transition/bounded_regions.hpp"

#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/node_descriptor.hpp"
#include "gagp/evolution/transition/linear_rec.hpp"
#include "linear_rec_internal.hpp"

namespace gagp::evo::transition {
namespace {

using Captures = std::map<int, int>;
namespace legacy = gagp::migration::legacy_v1;

std::uint32_t inclusive_domain_cells(
    const std::vector<std::pair<int, int>>& domains) {
  constexpr auto limit = std::numeric_limits<std::uint32_t>::max();
  std::uint64_t cells = 1;
  for (const auto& [lower, upper] : domains) {
    const auto span = static_cast<std::uint64_t>(
        static_cast<std::int64_t>(upper) - static_cast<std::int64_t>(lower)) + 1U;
    if (span > limit || cells > limit / span) return limit;
    cells *= span;
  }
  return static_cast<std::uint32_t>(cells);
}

std::uint32_t frame_capacity_for_cells(std::uint32_t cells) {
  if (cells == std::numeric_limits<std::uint32_t>::max()) return cells;
  return cells + 1U;
}

ValueTag value_tag(RType type) {
  switch (type) {
    case RType::Int: return ValueTag::Int;
    case RType::Float: return ValueTag::Float;
    case RType::Bool: return ValueTag::Bool;
    case RType::Char: return ValueTag::Char;
    case RType::String: return ValueTag::String;
    case RType::IntList: return ValueTag::IntList;
    case RType::FloatList: return ValueTag::FloatList;
    case RType::StringList: return ValueTag::StringList;
    default: throw std::logic_error("bounded-region transition requires an exact value type");
  }
}

RegionStateTransition copy_state(std::uint32_t source) {
  RegionStateTransition out;
  out.kind = RegionTransitionKind::CopyState;
  out.source_state = source;
  return out;
}

RegionStateTransition offset_state(std::uint32_t source, std::int64_t offset) {
  RegionStateTransition out;
  out.kind = RegionTransitionKind::CoordinateOffset;
  out.source_state = source;
  out.offset = offset;
  return out;
}

WindowEndpoint endpoint(WindowEndpointKind kind, std::uint32_t cut = 0) {
  WindowEndpoint out;
  out.kind = kind;
  out.cut = cut;
  return out;
}

RegionStateTransition window_state(WindowEndpoint begin, WindowEndpoint end) {
  RegionStateTransition out;
  out.kind = RegionTransitionKind::SequenceWindow;
  out.source_state = 0;
  out.window = SequenceWindow{begin, end};
  return out;
}

RegionStateTransition expression_state(std::uint32_t expression) {
  RegionStateTransition out;
  out.kind = RegionTransitionKind::Expression;
  out.expression = expression;
  return out;
}

class Adapter {
 public:
  Adapter(const legacy::AstProgram& source, const legacy::VerifiedAst& verified,
          std::uint32_t minimum_dc_frames)
      : source_(source), verified_(verified), minimum_dc_frames_(minimum_dc_frames) {
    node_map_.assign(source.nodes.size(), unmapped());
    out_.version = k_ast_prefix_version_current;
    out_.names = source.names;
    out_.consts = source.consts;
    for (const auto& region : source.lexical_regions)
      for (const auto& binding : region.bindings) used_.insert(binding.id);
    for (const auto& region : source.bounded_region_specs)
      for (const auto& phase : region.phases)
        for (const auto& binding : phase.bindings) used_.insert(binding.binder_id);
  }

  AstProgram run() {
    copy(0, {});
    copy_rows(source_.lexical_regions, out_.lexical_regions);
    copy_rows(source_.traversal_specs, out_.traversal_specs);
    copy_fuel_rows();
    copy_rows(source_.bounded_region_specs, out_.bounded_region_specs);
    return std::move(out_);
  }

 private:
  static constexpr std::size_t unmapped() {
    return std::numeric_limits<std::size_t>::max();
  }

  int binder() {
    while (next_ < std::numeric_limits<int>::max() && used_.count(next_)) ++next_;
    if (next_ == std::numeric_limits<int>::max())
      throw std::invalid_argument("bounded-region transition exhausted native binder IDs");
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

  void charge(std::size_t index, std::vector<FuelCharge> charges) {
    out_.fuel_specs.push_back({index, std::move(charges)});
  }

  void ref(int id, std::uint32_t cost) {
    charge(emit(NodeKind::REGION_VAR, id), FuelEvent::Operation, cost);
  }

  void constant(Value value, std::uint32_t cost) {
    const auto index = static_cast<int>(out_.consts.size());
    out_.consts.push_back(value);
    charge(emit(NodeKind::CONST, index), FuelEvent::Operation, cost);
  }

  std::size_t begin_let(int id, RType type, std::uint32_t cost) {
    const auto index = emit(NodeKind::LET_REGION);
    out_.lexical_regions.push_back({index, 1, {{id, type}}});
    charge(index, FuelEvent::Bind, cost);
    return index;
  }

  RegionAstBinding binding(RegionSlotBank bank, std::uint32_t slot, int id) {
    return {{bank, slot}, id};
  }

  template <class Rows>
  void copy_rows(const Rows& source, Rows& target) {
    for (auto row : source) {
      const auto mapped = node_map_.at(row.node_index);
      if (mapped == unmapped())
        throw std::logic_error("bounded-region transition lost retained metadata");
      row.node_index = mapped;
      target.push_back(std::move(row));
    }
  }

  void copy_fuel_rows() {
    for (auto row : source_.fuel_specs) {
      if (source_.nodes.at(row.node_index).kind == legacy::NodeKind::ASGP_DC ||
          source_.nodes.at(row.node_index).kind == legacy::NodeKind::ASGP_DP1D ||
          source_.nodes.at(row.node_index).kind == legacy::NodeKind::ASGP_DP2D) {
        continue;
      }
      const auto mapped = node_map_.at(row.node_index);
      if (mapped == unmapped())
        throw std::logic_error("bounded-region transition lost retained fuel metadata");
      row.node_index = mapped;
      out_.fuel_specs.push_back(std::move(row));
    }
  }

  const BoundedRegionSpec* bounded(std::size_t owner) const {
    for (const auto& spec : source_.bounded_region_specs)
      if (spec.node_index == owner) return &spec;
    return nullptr;
  }

  const legacy::AsgpDcBinders& dc_spec(std::size_t owner) const {
    for (const auto& spec : source_.asgp_dc_binders)
      if (spec.node_index == owner) return spec;
    throw std::logic_error("verified ASGP-DC lost binding metadata");
  }

  const legacy::AsgpDp1dSpec& dp1_spec(std::size_t owner) const {
    for (const auto& spec : source_.asgp_dp1d_specs)
      if (spec.node_index == owner) return spec;
    throw std::logic_error("verified ASGP-DP1D lost metadata");
  }

  const legacy::AsgpDp2dSpec& dp2_spec(std::size_t owner) const {
    for (const auto& spec : source_.asgp_dp2d_specs)
      if (spec.node_index == owner) return spec;
    throw std::logic_error("verified ASGP-DP2D lost metadata");
  }

  void copy(std::size_t index, const Captures& captures) {
    const auto& node = source_.nodes.at(index);
    if (node.kind == legacy::NodeKind::ASGP_DC) { dc(index, captures); return; }
    if (node.kind == legacy::NodeKind::ASGP_DP1D) { dp1(index, captures); return; }
    if (node.kind == legacy::NodeKind::ASGP_DP2D) { dp2(index, captures); return; }

    auto copied_kind = legacy::current_kind(node.kind);
    int copied_i0 = node.i0;
    if (node.kind == legacy::NodeKind::BOUND_VAR) {
      const auto found = captures.find(node.i0);
      if (found != captures.end()) {
        copied_kind = NodeKind::REGION_VAR;
        copied_i0 = found->second;
      }
    }
    const auto destination = emit(copied_kind, copied_i0, node.i1);
    node_map_[index] = destination;
    auto child = index + 1;
    const auto* bounded_spec = node.kind == legacy::NodeKind::BOUNDED_REGION ? bounded(index) : nullptr;
    const std::size_t bounded_operands = bounded_spec == nullptr ? 0 :
        bounded_spec->plan.state_types.size() + bounded_spec->plan.bound_operand_count;
    for (int argument = 0; argument < legacy::prefix_arity(source_, index); ++argument) {
      auto visible = captures;
      if ((node.kind == legacy::NodeKind::MAP_LIST || node.kind == legacy::NodeKind::FILTER_LIST) &&
          argument == 1) {
        visible.erase(node.i0);
      }
      if (bounded_spec != nullptr && static_cast<std::size_t>(argument) >= bounded_operands)
        visible.clear();
      copy(child, visible);
      child = verified_.subtree_end.at(child);
    }
  }

  std::vector<std::size_t> children(std::size_t owner, int count) const {
    std::vector<std::size_t> result;
    result.reserve(static_cast<std::size_t>(count));
    auto child = owner + 1;
    for (int i = 0; i < count; ++i) {
      result.push_back(child);
      child = verified_.subtree_end.at(child);
    }
    return result;
  }

  RegionPlan common_coordinate_plan(
      std::size_t owner, std::size_t states, std::size_t requests,
      const std::vector<std::pair<int, int>>& domains) const {
    RegionPlan plan;
    plan.state_types.assign(states, ValueTag::Int);
    plan.result_type = value_tag(verified_.expression_types.at(owner));
    plan.requests.resize(requests);
    plan.limits.cells = inclusive_domain_cells(domains);
    // Coordinate requests strictly advance the verified rank. A live DFS path
    // can therefore contain at most every in-domain cell plus one boundary
    // frame, while memoization can retain at most every in-domain cell.
    plan.limits.frames = frame_capacity_for_cells(plan.limits.cells);
    plan.limits.entry_fuel = 1;
    plan.memoized = true;
    plan.progress = RegionProgressKind::Coordinates;
    plan.coordinate_endpoint = DomainEndpoint::Inclusive;
    plan.sequence_state = 0;
    return plan;
  }

  void dc(std::size_t index, const Captures& captures) {
    const auto child = children(index, 4);
    const auto& roles = dc_spec(index);
    const RType sequence_type = verified_.expression_types.at(child[0]);

    RegionPlan plan;
    plan.state_types = {value_tag(sequence_type), ValueTag::Int};
    plan.result_type = value_tag(verified_.expression_types.at(index));
    plan.preparations = {{ValueTag::Int, RegionPreparationKind::InteriorCut}};
    plan.request_expression_types = {ValueTag::Int};
    plan.requests = {
        {{window_state(endpoint(WindowEndpointKind::Begin),
                       endpoint(WindowEndpointKind::InteriorCut, 0)), copy_state(1)}},
        {{window_state(endpoint(WindowEndpointKind::InteriorCut, 0),
                       endpoint(WindowEndpointKind::End)), expression_state(0)}}};
    const auto& source_node = source_.nodes.at(child[0]);
    plan.limits.frames = Value::k_container_len_max;
    if (source_node.kind == legacy::NodeKind::CONST) {
      const auto& source_value = source_.consts.at(
          static_cast<std::size_t>(source_node.i0));
      const auto length = Value::container_len(source_value);
      plan.limits.frames = length == 0 ? 1U : length;
    }
    plan.limits.frames = std::max(plan.limits.frames, minimum_dc_frames_);
    plan.limits.cells = 0;
    plan.limits.entry_fuel = 1;
    plan.memoized = false;
    plan.duplicate_policy = DuplicatePolicy::Reject;
    plan.progress = RegionProgressKind::SequenceWindows;
    plan.sequence_state = 0;

    const auto owner = emit(NodeKind::BOUNDED_REGION,
                            static_cast<int>(bounded_region_arity(plan)));
    node_map_[index] = owner;
    BoundedRegionSpec spec;
    spec.node_index = owner;
    spec.plan = plan;

    copy(child[0], captures);
    constant(Value::from_int(0), 0);

    spec.phases.push_back({2, {}});
    constant(Value::from_bool(false), 0);

    const int solve_xs = binder(), solve_n = binder(), solve_lo = binder();
    spec.phases.push_back({3, {binding(RegionSlotBank::State, 0, solve_xs),
                               binding(RegionSlotBank::Measure, 0, solve_n),
                               binding(RegionSlotBank::State, 1, solve_lo)}});
    copy(child[1], {{roles.solve_xs_name, solve_xs},
                    {roles.solve_n_name, solve_n},
                    {roles.solve_lo_name, solve_lo}});

    const int divide_n = binder();
    spec.phases.push_back({4, {binding(RegionSlotBank::Measure, 0, divide_n)}});
    copy(child[2], {{roles.divide_n_name, divide_n}});

    const int request_lo = binder(), request_split = binder();
    spec.phases.push_back({5, {binding(RegionSlotBank::State, 1, request_lo),
                               binding(RegionSlotBank::Prepared, 0, request_split)}});
    const auto add = emit(NodeKind::ADD);
    charge(add, FuelEvent::Operation, 0);
    ref(request_lo, 0);
    ref(request_split, 0);

    const int left = binder(), right = binder();
    spec.phases.push_back({6, {binding(RegionSlotBank::Result, 0, left),
                               binding(RegionSlotBank::Result, 1, right)}});
    copy(child[3], {{roles.combine_left_name, left},
                    {roles.combine_right_name, right}});
    out_.bounded_region_specs.push_back(std::move(spec));
  }

  void dp1(std::size_t index, const Captures& captures) {
    const auto child = children(index, 3);
    const auto& old = dp1_spec(index);
    const bool backward = old.dep_kind == legacy::NodeKind::DP1_BACKWARD1 ||
                          old.dep_kind == legacy::NodeKind::DP1_BACKWARD2 ||
                          old.dep_kind == legacy::NodeKind::DP1_BACKWARD3;
    RegionPlan plan = common_coordinate_plan(
        index, 1, old.dep_offsets.size(), {{old.lo, old.hi}});
    plan.duplicate_policy = DuplicatePolicy::Allow;
    plan.coordinate_slots = {0};
    plan.coordinate_rank = {{0, backward ? 1 : -1}};
    plan.coordinate_domains = {{{RegionBoundKind::Literal, old.lo, 0},
                                {RegionBoundKind::Literal, old.hi, 0}}};
    for (std::size_t i = 0; i < old.dep_offsets.size(); ++i) {
      const std::int64_t offset = backward ? -old.dep_offsets[i] : old.dep_offsets[i];
      plan.requests[i].states = {offset_state(0, offset)};
    }

    const auto owner = emit(NodeKind::BOUNDED_REGION,
                            static_cast<int>(bounded_region_arity(plan)));
    node_map_[index] = owner;
    charge(owner, FuelEvent::Operation, 0);
    BoundedRegionSpec spec;
    spec.node_index = owner;
    spec.plan = plan;

    const auto check = emit(NodeKind::CHECK_INT);
    charge(check, FuelEvent::Operation, 1);
    copy(child[0], captures);

    const int predicate_state = binder();
    spec.phases.push_back({1, {binding(RegionSlotBank::State, 0, predicate_state)}});
    const auto equal = emit(NodeKind::EQ);
    charge(equal, FuelEvent::Operation, 0);
    ref(predicate_state, 0);
    constant(Value::from_int(old.base_state), 0);

    const int solve_state = binder();
    spec.phases.push_back({2, {binding(RegionSlotBank::State, 0, solve_state)}});
    copy(child[1], {{old.solve_state_name, solve_state}});

    const int transition_state = binder();
    Captures transition{{old.transition_state_name, transition_state}};
    std::vector<RegionAstBinding> combine_bindings{
        binding(RegionSlotBank::State, 0, transition_state)};
    for (std::size_t i = 0; i < old.transition_dep_names.size(); ++i) {
      const int result = binder();
      transition[old.transition_dep_names[i]] = result;
      combine_bindings.push_back(binding(RegionSlotBank::Result,
                                         static_cast<std::uint32_t>(i), result));
    }
    spec.phases.push_back({3, std::move(combine_bindings)});
    copy(child[2], transition);

    spec.phases.push_back({4, {}});
    constant(source_.consts.at(static_cast<std::size_t>(old.boundary_const)), 0);
    out_.bounded_region_specs.push_back(std::move(spec));
  }

  std::vector<std::pair<std::int64_t, std::int64_t>> dp2_offsets(legacy::NodeKind kind) const {
    switch (kind) {
      case legacy::NodeKind::DP2_CROSS_BACKWARD: return {{-1, 0}, {0, -1}};
      case legacy::NodeKind::DP2_CROSS_FORWARD: return {{1, 0}, {0, 1}};
      case legacy::NodeKind::DP2_DIAGONAL_BACKWARD: return {{-1, -1}};
      case legacy::NodeKind::DP2_DIAGONAL_FORWARD: return {{1, 1}};
      case legacy::NodeKind::DP2_NEIGHBORHOOD_BACKWARD3:
        return {{-1, 0}, {0, -1}, {-1, -1}};
      case legacy::NodeKind::DP2_NEIGHBORHOOD_FORWARD3:
        return {{1, 0}, {0, 1}, {1, 1}};
      default: throw std::logic_error("verified ASGP-DP2D has an invalid dependency kind");
    }
  }

  void dp2(std::size_t index, const Captures& captures) {
    const auto child = children(index, 4);
    const auto& old = dp2_spec(index);
    const auto offsets = dp2_offsets(old.dep_kind);
    const bool backward = offsets.front().first < 0 || offsets.front().second < 0;
    RegionPlan plan = common_coordinate_plan(
        index, 2, offsets.size(),
        {{old.i_lo, old.i_hi}, {old.j_lo, old.j_hi}});
    plan.duplicate_policy = DuplicatePolicy::Reject;
    plan.coordinate_slots = {0, 1};
    plan.coordinate_rank = {{0, backward ? 1 : -1}, {1, backward ? 1 : -1}};
    plan.coordinate_domains = {
        {{RegionBoundKind::Literal, old.i_lo, 0},
         {RegionBoundKind::Literal, old.i_hi, 0}},
        {{RegionBoundKind::Literal, old.j_lo, 0},
         {RegionBoundKind::Literal, old.j_hi, 0}}};
    for (std::size_t i = 0; i < offsets.size(); ++i)
      plan.requests[i].states = {offset_state(0, offsets[i].first),
                                 offset_state(1, offsets[i].second)};

    const int raw_i = binder(), raw_j = binder();
    const auto outer = begin_let(raw_i, RType::Int, 0);
    node_map_[index] = outer;
    copy(child[0], captures);
    begin_let(raw_j, RType::Int, 0);
    copy(child[1], captures);

    const auto owner = emit(NodeKind::BOUNDED_REGION,
                            static_cast<int>(bounded_region_arity(plan)));
    charge(owner, FuelEvent::Operation, 0);
    BoundedRegionSpec spec;
    spec.node_index = owner;
    spec.plan = plan;

    auto check = emit(NodeKind::CHECK_INT);
    charge(check, FuelEvent::Operation, 1);
    ref(raw_i, 0);
    check = emit(NodeKind::CHECK_INT);
    charge(check, FuelEvent::Operation, 0);
    ref(raw_j, 0);

    const int predicate_i = binder(), predicate_j = binder();
    spec.phases.push_back({2, {binding(RegionSlotBank::State, 0, predicate_i),
                               binding(RegionSlotBank::State, 1, predicate_j)}});
    const auto conditional = emit(NodeKind::IF_EXPR);
    charge(conditional, {{FuelEvent::BranchTest, 0}, {FuelEvent::BranchMerge, 0}});
    auto equal = emit(NodeKind::EQ);
    charge(equal, FuelEvent::Operation, 0);
    ref(predicate_i, 0);
    constant(Value::from_int(old.base_i), 0);
    equal = emit(NodeKind::EQ);
    charge(equal, FuelEvent::Operation, 0);
    ref(predicate_j, 0);
    constant(Value::from_int(old.base_j), 0);
    constant(Value::from_bool(false), 0);

    const int solve_i = binder(), solve_j = binder();
    spec.phases.push_back({3, {binding(RegionSlotBank::State, 0, solve_i),
                               binding(RegionSlotBank::State, 1, solve_j)}});
    copy(child[2], {{old.solve_i_name, solve_i}, {old.solve_j_name, solve_j}});

    const int transition_i = binder(), transition_j = binder();
    Captures transition{{old.transition_i_name, transition_i},
                        {old.transition_j_name, transition_j}};
    std::vector<RegionAstBinding> combine_bindings{
        binding(RegionSlotBank::State, 0, transition_i),
        binding(RegionSlotBank::State, 1, transition_j)};
    for (std::size_t i = 0; i < old.transition_dep_names.size(); ++i) {
      const int result = binder();
      transition[old.transition_dep_names[i]] = result;
      combine_bindings.push_back(binding(RegionSlotBank::Result,
                                         static_cast<std::uint32_t>(i), result));
    }
    spec.phases.push_back({4, std::move(combine_bindings)});
    copy(child[3], transition);

    spec.phases.push_back({5, {}});
    constant(source_.consts.at(static_cast<std::size_t>(old.boundary_const)), 0);
    out_.bounded_region_specs.push_back(std::move(spec));
  }

  const legacy::AstProgram& source_;
  const legacy::VerifiedAst& verified_;
  std::uint32_t minimum_dc_frames_ = 0;
  AstProgram out_;
  std::vector<std::size_t> node_map_;
  std::set<int> used_;
  int next_ = 0;
};

}  // namespace

ProgramGenome lower_bounded_regions(const legacy::AstProgram& source,
                                    const std::vector<InputSpec>& inputs,
                                    std::uint32_t minimum_dc_frames) {
  if (minimum_dc_frames > Value::k_container_len_max)
    throw std::invalid_argument("minimum DC frames exceeds the source container limit");
  // Keep the already differential-tested LinearRec rewrite as the single owner
  // of its scope and semantic-fuel mapping. The second verification supplies
  // exact subtree/type annotations for the DC/DP rewrite below.
  legacy::AstProgram linear = detail::lower_linear_rec_legacy_ast(source, inputs);
  const auto verified = legacy::verify(linear, inputs);
  ProgramGenome result;
  result.ast = Adapter(linear, verified, minimum_dc_frames).run();
  const auto lowered = verify_ast(result.ast, inputs);
  if (!lowered)
    throw std::logic_error("bounded-region transition result: " +
                           lowered.diagnostic.message);
  result.meta = build_genome_meta(result.ast);
  return result;
}

}  // namespace gagp::evo::transition
