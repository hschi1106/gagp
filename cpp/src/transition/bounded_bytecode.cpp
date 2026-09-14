#include "gagp/evolution/transition/bounded_bytecode.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/core/bytecode_verify.hpp"

namespace gagp::evo::transition {
namespace {

constexpr std::uint32_t kZeroFuel = 0;

Instr instruction(Opcode op) {
  Instr out;
  out.op = op;
  return out;
}

Instr instruction(Opcode op, int a) {
  Instr out = instruction(op);
  out.a = a;
  out.has_a = true;
  return out;
}

void emit(std::vector<Instr>* code, std::vector<std::uint32_t>* fuel,
          Instr value, std::uint32_t cost) {
  code->push_back(value);
  fuel->push_back(cost);
}

bool public_type(ValueTag type) {
  switch (type) {
    case ValueTag::Int:
    case ValueTag::Float:
    case ValueTag::Bool:
    case ValueTag::Char:
    case ValueTag::String:
    case ValueTag::IntList:
    case ValueTag::FloatList:
    case ValueTag::StringList:
      return true;
    case ValueTag::FallbackToken:
    case ValueTag::Invalid:
      return false;
  }
  return false;
}

bool dc_source_type(ValueTag type) {
  return type == ValueTag::String || type == ValueTag::IntList ||
         type == ValueTag::FloatList || type == ValueTag::StringList;
}

[[noreturn]] void invalid(const std::string& message) {
  throw std::invalid_argument("bounded-bytecode transition: " + message);
}

void require_verified(const BytecodeProgram& program, const char* which) {
  const BytecodeVerifyResult checked = verify_bytecode(program);
  if (!checked) {
    invalid(std::string(which) + " bytecode is invalid at " +
            checked.diagnostic.path + " (" +
            bytecode_verify_code_name(checked.diagnostic.code) + "): " +
            checked.diagnostic.message);
  }
}

void verify_source_structure(const BytecodeProgram& source) {
  if (verify_bytecode(source)) return;
  // The frozen runtime checks boundary before base, including probes whose base
  // is outside the domain. The old verifier rejects that unreachable base.
  // Validate all remaining source structure using an in-domain verification
  // copy; conversion retains the original base predicate and domain unchanged.
  BytecodeProgram structural = source;
  bool outside_base = false;
  for (auto& segment : structural.asgp_dp1d_segments) {
    if (segment.lo <= segment.hi &&
        (segment.base_state < segment.lo || segment.base_state > segment.hi)) {
      segment.base_state = segment.lo;
      outside_base = true;
    }
  }
  for (auto& segment : structural.asgp_dp2d_segments) {
    if (segment.i_lo <= segment.i_hi && segment.j_lo <= segment.j_hi &&
        (segment.base_i < segment.i_lo || segment.base_i > segment.i_hi ||
         segment.base_j < segment.j_lo || segment.base_j > segment.j_hi)) {
      segment.base_i = segment.i_lo;
      segment.base_j = segment.j_lo;
      outside_base = true;
    }
  }
  require_verified(outside_base ? structural : source, "source structure");
}

void reject_structured_phase(const PhaseProgram& phase,
                             const std::string& path) {
  for (std::size_t i = 0; i < phase.code.size(); ++i) {
    switch (phase.code[i].op) {
      case Opcode::AsgpDc:
      case Opcode::AsgpDp1d:
      case Opcode::AsgpDp2d:
      case Opcode::BoundedRegion:
        invalid(path + ".code[" + std::to_string(i) +
                "] contains a structured call");
      default:
        break;
    }
  }
}

RegionValueSlot slot(RegionSlotBank bank, std::uint32_t index) {
  RegionValueSlot out;
  out.bank = bank;
  out.slot = index;
  return out;
}

RegionPhase copy_phase(
    const PhaseProgram& source,
    const std::vector<std::pair<RegionValueSlot, int>>& named_sources,
    const std::string& path) {
  reject_structured_phase(source, path);
  RegionPhase out;
  out.program = source;
  out.program.var2idx.clear();
  out.program.binder_locals.clear();
  out.bindings.reserve(named_sources.size());
  for (const auto& item : named_sources) {
    const auto found = source.binder_locals.find(item.second);
    if (found == source.binder_locals.end())
      invalid(path + " is missing required binder " +
              std::to_string(item.second));
    out.bindings.push_back({item.first, found->second});
  }
  return out;
}

RegionPhase constant_phase(Value value) {
  RegionPhase out;
  out.program.consts.push_back(value);
  out.program.code.push_back(instruction(Opcode::PushConst, 0));
  out.program.instruction_fuel.push_back(kZeroFuel);
  return out;
}

RegionPhase equality_phase(std::int64_t base) {
  RegionPhase out;
  out.program.n_locals = 1;
  out.program.consts.push_back(Value::from_int(base));
  out.program.code = {
      instruction(Opcode::Load, 0), instruction(Opcode::PushConst, 0),
      instruction(Opcode::Eq)};
  out.program.instruction_fuel.assign(out.program.code.size(), kZeroFuel);
  out.bindings.push_back({slot(RegionSlotBank::State, 0), 0});
  return out;
}

RegionPhase equality_pair_phase(std::int64_t first, std::int64_t second) {
  RegionPhase out;
  out.program.n_locals = 2;
  out.program.consts = {Value::from_int(first), Value::from_int(second),
                        Value::from_bool(false)};
  out.program.code = {
      instruction(Opcode::Load, 0),
      instruction(Opcode::PushConst, 0),
      instruction(Opcode::Eq),
      instruction(Opcode::JmpIfFalse, 8),
      instruction(Opcode::Load, 1),
      instruction(Opcode::PushConst, 1),
      instruction(Opcode::Eq),
      instruction(Opcode::Return),
      instruction(Opcode::PushConst, 2),
      instruction(Opcode::Return),
  };
  out.program.instruction_fuel.assign(out.program.code.size(), kZeroFuel);
  out.bindings = {{slot(RegionSlotBank::State, 0), 0},
                  {slot(RegionSlotBank::State, 1), 1}};
  return out;
}

RegionPhase dc_request_expression() {
  RegionPhase out;
  out.program.n_locals = 2;
  out.program.code = {instruction(Opcode::Load, 0),
                      instruction(Opcode::Load, 1),
                      instruction(Opcode::Add)};
  out.program.instruction_fuel.assign(out.program.code.size(), kZeroFuel);
  out.bindings = {{slot(RegionSlotBank::State, 1), 0},
                  {slot(RegionSlotBank::Prepared, 0), 1}};
  return out;
}

RegionBound literal_bound(std::int64_t value) {
  RegionBound out;
  out.kind = RegionBoundKind::Literal;
  out.literal = value;
  return out;
}

RegionStateTransition copy_state(std::uint32_t source) {
  RegionStateTransition out;
  out.kind = RegionTransitionKind::CopyState;
  out.source_state = source;
  return out;
}

RegionStateTransition coordinate_offset(std::uint32_t source,
                                        std::int64_t offset) {
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

RegionStateTransition window(WindowEndpoint begin, WindowEndpoint end) {
  RegionStateTransition out;
  out.kind = RegionTransitionKind::SequenceWindow;
  out.source_state = 0;
  out.window = {begin, end};
  return out;
}

RegionStateTransition expression(std::uint32_t index) {
  RegionStateTransition out;
  out.kind = RegionTransitionKind::Expression;
  out.expression = index;
  return out;
}

RegionPlan coordinate_plan(ValueTag result, std::size_t dimensions,
                           std::uint32_t frames, std::uint32_t cells) {
  RegionPlan plan;
  plan.state_types.assign(dimensions, ValueTag::Int);
  plan.result_type = result;
  plan.limits.frames = frames;
  plan.limits.cells = cells;
  plan.limits.entry_fuel = 1;
  plan.memoized = true;
  plan.duplicate_policy = DuplicatePolicy::Reject;
  plan.progress = RegionProgressKind::Coordinates;
  plan.coordinate_endpoint = DomainEndpoint::Inclusive;
  plan.coordinate_slots.resize(dimensions);
  for (std::size_t i = 0; i < dimensions; ++i)
    plan.coordinate_slots[i] = static_cast<std::uint32_t>(i);
  return plan;
}

BoundedRegionSegment convert_dc(const AsgpDcSegment& source,
                                ValueTag source_type, ValueTag result_type,
                                const BoundedBytecodeProfile& profile,
                                std::size_t index) {
  if (!dc_source_type(source_type))
    invalid("DC hint " + std::to_string(index) +
            " source must be String or a typed list");
  if (!public_type(result_type))
    invalid("DC hint " + std::to_string(index) +
            " result must be an exact public type");

  BoundedRegionSegment out;
  RegionPlan& plan = out.plan;
  plan.state_types = {source_type, ValueTag::Int};
  plan.result_type = result_type;
  plan.preparations = {{ValueTag::Int, RegionPreparationKind::InteriorCut}};
  plan.request_expression_types = {ValueTag::Int};
  plan.requests.resize(2);
  plan.requests[0].states = {
      window(endpoint(WindowEndpointKind::Begin),
             endpoint(WindowEndpointKind::InteriorCut, 0)),
      copy_state(1)};
  plan.requests[1].states = {
      window(endpoint(WindowEndpointKind::InteriorCut, 0),
             endpoint(WindowEndpointKind::End)),
      expression(0)};
  plan.limits.frames = profile.dc_frames;
  plan.limits.cells = 0;
  plan.limits.entry_fuel = 1;
  plan.memoized = false;
  plan.duplicate_policy = DuplicatePolicy::Reject;
  plan.progress = RegionProgressKind::SequenceWindows;
  plan.sequence_state = 0;

  const std::string path = "$.segments.asgp_dc[" + std::to_string(index) + "]";
  out.base_predicate = constant_phase(Value::from_bool(false));
  out.base_body = copy_phase(
      source.solve,
      {{slot(RegionSlotBank::State, 0), source.solve_xs_name},
       {slot(RegionSlotBank::Measure, 0), source.solve_n_name},
       {slot(RegionSlotBank::State, 1), source.solve_lo_name}},
      path + ".solve");
  out.preparations.push_back(copy_phase(
      source.divide,
      {{slot(RegionSlotBank::Measure, 0), source.divide_n_name}},
      path + ".divide"));
  out.request_expressions.push_back(dc_request_expression());
  out.combine = copy_phase(
      source.combine,
      {{slot(RegionSlotBank::Result, 0), source.combine_left_name},
       {slot(RegionSlotBank::Result, 1), source.combine_right_name}},
      path + ".combine");
  return out;
}

BoundedRegionSegment convert_dp1(const AsgpDp1dSegment& source,
                                 ValueTag result_type,
                                 const BoundedBytecodeProfile& profile,
                                 std::size_t index) {
  if (!public_type(result_type))
    invalid("DP1D hint " + std::to_string(index) +
            " must be an exact public type");
  BoundedRegionSegment out;
  RegionPlan& plan = out.plan;
  plan = coordinate_plan(result_type, 1, profile.dp_frames, profile.dp_cells);
  plan.duplicate_policy = DuplicatePolicy::Allow;
  plan.coordinate_rank = {{0, source.dep_kind < 0 ? 1 : -1}};
  plan.coordinate_domains = {{literal_bound(source.lo), literal_bound(source.hi)}};
  plan.requests.resize(source.dep_offsets.size());
  for (std::size_t i = 0; i < source.dep_offsets.size(); ++i) {
    const std::int64_t magnitude = source.dep_offsets[i];
    const std::int64_t delta = source.dep_kind < 0 ? -magnitude : magnitude;
    plan.requests[i].states = {coordinate_offset(0, delta)};
  }

  const std::string path = "$.segments.asgp_dp1d[" + std::to_string(index) + "]";
  out.boundary = constant_phase(source.boundary_value);
  out.base_predicate = equality_phase(source.base_state);
  out.base_body = copy_phase(
      source.solve, {{slot(RegionSlotBank::State, 0), source.solve_state_name}},
      path + ".solve");
  std::vector<std::pair<RegionValueSlot, int>> combine_sources;
  combine_sources.push_back(
      {slot(RegionSlotBank::State, 0), source.transition_state_name});
  for (std::size_t i = 0; i < source.transition_dep_names.size(); ++i)
    combine_sources.push_back(
        {slot(RegionSlotBank::Result, static_cast<std::uint32_t>(i)),
         source.transition_dep_names[i]});
  out.combine = copy_phase(source.transition, combine_sources,
                           path + ".transition");
  return out;
}

std::vector<std::pair<std::int64_t, std::int64_t>> dp2_offsets(int kind) {
  switch (kind) {
    case 0: return {{-1, 0}, {0, -1}};
    case 1: return {{1, 0}, {0, 1}};
    case 2: return {{-1, -1}};
    case 3: return {{1, 1}};
    case 4: return {{-1, 0}, {0, -1}, {-1, -1}};
    case 5: return {{1, 0}, {0, 1}, {1, 1}};
    default: invalid("DP2D dependency kind is invalid");
  }
}

BoundedRegionSegment convert_dp2(const AsgpDp2dSegment& source,
                                 ValueTag result_type,
                                 const BoundedBytecodeProfile& profile,
                                 std::size_t index) {
  if (!public_type(result_type))
    invalid("DP2D hint " + std::to_string(index) +
            " must be an exact public type");
  BoundedRegionSegment out;
  RegionPlan& plan = out.plan;
  plan = coordinate_plan(result_type, 2, profile.dp_frames, profile.dp_cells);
  const int direction = (source.dep_kind % 2 == 0) ? 1 : -1;
  plan.coordinate_rank = {{0, direction}, {1, direction}};
  plan.coordinate_domains = {
      {literal_bound(source.i_lo), literal_bound(source.i_hi)},
      {literal_bound(source.j_lo), literal_bound(source.j_hi)}};
  for (const auto& offset : dp2_offsets(source.dep_kind)) {
    RegionRequest request;
    request.states = {coordinate_offset(0, offset.first),
                      coordinate_offset(1, offset.second)};
    plan.requests.push_back(std::move(request));
  }

  const std::string path = "$.segments.asgp_dp2d[" + std::to_string(index) + "]";
  out.boundary = constant_phase(source.boundary_value);
  out.base_predicate = equality_pair_phase(source.base_i, source.base_j);
  out.base_body = copy_phase(
      source.solve,
      {{slot(RegionSlotBank::State, 0), source.solve_i_name},
       {slot(RegionSlotBank::State, 1), source.solve_j_name}},
      path + ".solve");
  std::vector<std::pair<RegionValueSlot, int>> combine_sources = {
      {slot(RegionSlotBank::State, 0), source.transition_i_name},
      {slot(RegionSlotBank::State, 1), source.transition_j_name}};
  for (std::size_t i = 0; i < source.transition_dep_names.size(); ++i)
    combine_sources.push_back(
        {slot(RegionSlotBank::Result, static_cast<std::uint32_t>(i)),
         source.transition_dep_names[i]});
  out.combine = copy_phase(source.transition, combine_sources,
                           path + ".transition");
  return out;
}

std::uint32_t source_cost(const BytecodeProgram& source, std::size_t ip) {
  return source.instruction_fuel.empty() ? 1U : source.instruction_fuel[ip];
}

bool jump_opcode(Opcode op) {
  return op == Opcode::Jmp || op == Opcode::JmpIfFalse ||
         op == Opcode::JmpIfTrue;
}

int checked_index(std::size_t index, const char* what) {
  if (index > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    invalid(std::string(what) + " exceeds INT_MAX");
  return static_cast<int>(index);
}

}  // namespace

BytecodeProgram lower_bounded_bytecode(const BytecodeProgram& source,
                                       const BoundedBytecodeHints& hints,
                                       const BoundedBytecodeProfile& profile) {
  verify_source_structure(source);
  if (hints.dc.size() != source.asgp_dc_segments.size() ||
      hints.dp1d.size() != source.asgp_dp1d_segments.size() ||
      hints.dp2d.size() != source.asgp_dp2d_segments.size()) {
    invalid("hint counts must exactly match legacy segment table counts");
  }

  BytecodeProgram out = source;
  const std::size_t dc_base = out.bounded_region_segments.size();
  for (std::size_t i = 0; i < source.asgp_dc_segments.size(); ++i)
    out.bounded_region_segments.push_back(convert_dc(
        source.asgp_dc_segments[i], hints.dc[i].first, hints.dc[i].second,
        profile, i));
  const std::size_t dp1_base = out.bounded_region_segments.size();
  for (std::size_t i = 0; i < source.asgp_dp1d_segments.size(); ++i)
    out.bounded_region_segments.push_back(convert_dp1(
        source.asgp_dp1d_segments[i], hints.dp1d[i], profile, i));
  const std::size_t dp2_base = out.bounded_region_segments.size();
  for (std::size_t i = 0; i < source.asgp_dp2d_segments.size(); ++i)
    out.bounded_region_segments.push_back(convert_dp2(
        source.asgp_dp2d_segments[i], hints.dp2d[i], profile, i));

  bool needs_dp2_scratch = false;
  for (const Instr& ins : source.code)
    needs_dp2_scratch |= ins.op == Opcode::AsgpDp2d;
  int dp2_scratch = 0;
  if (needs_dp2_scratch) {
    if (out.n_locals == std::numeric_limits<int>::max())
      invalid("DP2D lowering requires one scratch local beyond INT_MAX");
    dp2_scratch = out.n_locals++;
  }

  out.code.clear();
  out.instruction_fuel.clear();
  std::vector<std::size_t> old_to_new(source.code.size() + 1);
  for (std::size_t ip = 0; ip < source.code.size(); ++ip) {
    old_to_new[ip] = out.code.size();
    const Instr& ins = source.code[ip];
    const std::uint32_t cost = source_cost(source, ip);
    if (ins.op == Opcode::AsgpDc) {
      const std::size_t constant_index = out.consts.size();
      out.consts.push_back(Value::from_int(0));
      emit(&out.code, &out.instruction_fuel,
           instruction(Opcode::PushConst,
                       checked_index(constant_index, "constant index")),
           kZeroFuel);
      emit(&out.code, &out.instruction_fuel,
           instruction(Opcode::BoundedRegion,
                       checked_index(dc_base + static_cast<std::size_t>(ins.a),
                                     "bounded segment index")),
           cost);
    } else if (ins.op == Opcode::AsgpDp1d) {
      emit(&out.code, &out.instruction_fuel, instruction(Opcode::CheckInt), cost);
      emit(&out.code, &out.instruction_fuel,
           instruction(Opcode::BoundedRegion,
                       checked_index(dp1_base + static_cast<std::size_t>(ins.a),
                                     "bounded segment index")),
           kZeroFuel);
    } else if (ins.op == Opcode::AsgpDp2d) {
      emit(&out.code, &out.instruction_fuel,
           instruction(Opcode::Store, dp2_scratch), kZeroFuel);
      emit(&out.code, &out.instruction_fuel, instruction(Opcode::CheckInt), cost);
      emit(&out.code, &out.instruction_fuel,
           instruction(Opcode::Load, dp2_scratch), kZeroFuel);
      emit(&out.code, &out.instruction_fuel, instruction(Opcode::CheckInt),
           kZeroFuel);
      emit(&out.code, &out.instruction_fuel,
           instruction(Opcode::BoundedRegion,
                       checked_index(dp2_base + static_cast<std::size_t>(ins.a),
                                     "bounded segment index")),
           kZeroFuel);
    } else {
      emit(&out.code, &out.instruction_fuel, ins, cost);
    }
  }
  old_to_new[source.code.size()] = out.code.size();

  // Retained instructions are the first instruction emitted for their old IP.
  // Synthetic expansions contain no jumps, so patching those positions is
  // sufficient and includes legal jumps to the old end sentinel.
  for (std::size_t old_ip = 0; old_ip < source.code.size(); ++old_ip) {
    if (!jump_opcode(source.code[old_ip].op)) continue;
    Instr& rewritten = out.code[old_to_new[old_ip]];
    rewritten.a = checked_index(
        old_to_new[static_cast<std::size_t>(source.code[old_ip].a)],
        "jump target");
  }

  out.asgp_dc_segments.clear();
  out.asgp_dp1d_segments.clear();
  out.asgp_dp2d_segments.clear();
  require_verified(out, "lowered");
  return out;
}

}  // namespace gagp::evo::transition
