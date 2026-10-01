#pragma once

#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "gagp/core/bytecode.hpp"
#include "gagp/runtime/cpu/builtins_cpu.hpp"
#include "bounded_region.hpp"

namespace gagp::detail {

struct RegionPhaseScratch {
  std::vector<std::pair<int, Value>> presets;
  std::vector<std::pair<int, ValueTag>> types;
  std::vector<std::int64_t> cuts;
};

// PhaseRunner executes ordinary bytecode with explicit local presets and shared
// fuel. It cannot dispatch another region: phases have no root program handle.
template <class PhaseRunner>
struct RegionAdapter {
  const BoundedRegionSegment& segment;
  const std::vector<std::optional<Value>>& parameters;
  const std::vector<CoordinateDomain>& domains;
  PhaseRunner phase_runner;
  // The iterative driver invokes phases serially. Keep bounded binding storage
  // across visits instead of allocating it for every predicate/body evaluation.
  RegionPhaseScratch& scratch;

  ExecResult run(const RegionPhase& phase, ValueTag expected,
                 const RegionFrame& frame, int& fuel, bool result_phase = false) {
    const auto& program = phase.program;
    // Verified constant phases have no observable local reads. Avoid building
    // bindings and an interpreter frame, preserving the full instruction charge
    // (including an optional explicit Return) before exposing the value.
    if ((program.code.size() == 1 ||
         (program.code.size() == 2 && program.code[1].op == Opcode::Return)) &&
        program.code[0].op == Opcode::PushConst) {
      std::uint64_t cost = program.code.size();
      if (!program.instruction_fuel.empty()) {
        cost = 0;
        for (const auto charge : program.instruction_fuel) cost += charge;
      }
      if (fuel < 0 || cost > static_cast<std::uint64_t>(fuel))
        return region_failure(ErrCode::Timeout, "out of fuel");
      fuel -= static_cast<int>(cost);
      const auto value = program.consts[program.code[0].a];
      if (value.tag != expected && !(result_phase && value.tag == ValueTag::FallbackToken))
        return region_failure(ErrCode::Type, "bounded region phase result type mismatch");
      return {false, value, {ErrCode::Value, ""}};
    }
    auto& presets = scratch.presets;
    auto& types = scratch.types;
    presets.clear();
    types.clear();
    presets.reserve(phase.bindings.size());
    types.reserve(std::min(phase.bindings.size(), segment.plan.parameter_types.size()));
    for (const auto& binding : phase.bindings) {
      const auto slot = binding.source.slot;
      Value value;
      switch (binding.source.bank) {
        case RegionSlotBank::State: value = frame.state[slot]; break;
        case RegionSlotBank::Parameter:
          // An absent capture remains an unset local. Only LOAD raises Name.
          if (!parameters[slot]) continue;
          types.emplace_back(binding.local, segment.plan.parameter_types[slot]);
          value = *parameters[slot];
          break;
        case RegionSlotBank::Prepared: value = frame.prepared[slot]; break;
        case RegionSlotBank::Result: value = frame.results[slot]; break;
        case RegionSlotBank::Measure:
          value = Value::from_int(Value::container_len(frame.state[segment.plan.sequence_state]));
          break;
      }
      presets.emplace_back(binding.local, value);
    }
    auto result = phase_runner(phase.program, presets, types, fuel);
    if (!result.is_error && result.value.tag != expected &&
        !(result_phase && result.value.tag == ValueTag::FallbackToken))
      return region_failure(ErrCode::Type, "bounded region phase result type mismatch");
    return result;
  }

  RegionEntry enter(RegionFrame& frame, int& fuel) {
    const auto& plan = segment.plan;
    for (std::size_t i = 0; i < plan.state_types.size(); ++i)
      if (frame.state[i].tag != plan.state_types[i])
        return {false, region_failure(ErrCode::Type, "bounded region state type mismatch")};
    if (plan.progress == RegionProgressKind::Coordinates) {
      for (std::size_t i = 0; i < domains.size(); ++i) {
        const auto value = frame.state[plan.coordinate_slots[i]].i;
        if (value < domains[i].lower ||
            (plan.coordinate_endpoint == DomainEndpoint::Exclusive
                 ? value >= domains[i].upper : value > domains[i].upper))
          return {true, run(*segment.boundary, plan.result_type, frame, fuel, true)};
      }
    } else if (Value::container_len(frame.state[plan.sequence_state]) <= 1) {
      return {true, run(segment.base_body, plan.result_type, frame, fuel, true)};
    }
    auto predicate = run(segment.base_predicate, ValueTag::Bool, frame, fuel);
    if (predicate.is_error) return {false, predicate};
    if (predicate.value.b)
      return {true, run(segment.base_body, plan.result_type, frame, fuel, true)};
    return {};
  }

  ExecResult prepare(RegionFrame& frame, int& fuel) {
    for (std::size_t i = 0; i < segment.preparations.size(); ++i) {
      const auto& preparation = segment.plan.preparations[i];
      auto result = run(segment.preparations[i], preparation.type, frame, fuel);
      if (result.is_error) return result;
      if (preparation.kind == RegionPreparationKind::InteriorCut)
        result.value = Value::from_int(clamp_interior_cut(result.value.i,
            Value::container_len(frame.state[segment.plan.sequence_state])));
      frame.prepared[i] = result.value;
    }
    return {};
  }

  ExecResult request(RegionFrame& frame, std::uint32_t ordinal,
                     RegionState& next, int& fuel) {
    const auto& plan = segment.plan;
    // Check each delivered sibling before constructing another request. Internal
    // fallback representations must agree across all results of one frame.
    if (ordinal > 1 && frame.results[ordinal - 1].tag != frame.results[0].tag)
      return region_failure(ErrCode::Type, "bounded region child result type mismatch");
    const auto& request = plan.requests[ordinal];
    for (std::size_t i = 0; i < request.states.size(); ++i) {
      const auto& transition = request.states[i];
      switch (transition.kind) {
        case RegionTransitionKind::CopyState:
          next[i] = frame.state[transition.source_state];
          break;
        case RegionTransitionKind::CoordinateOffset: {
          const auto value = frame.state[transition.source_state].i;
          const auto offset = transition.offset;
          if ((offset > 0 && value > std::numeric_limits<std::int64_t>::max() - offset) ||
              (offset < 0 && value < std::numeric_limits<std::int64_t>::min() - offset))
            return region_failure(ErrCode::Value, "bounded region coordinate overflow");
          next[i] = Value::from_int(value + offset);
          break;
        }
        case RegionTransitionKind::SequenceWindow: {
          auto& cuts = scratch.cuts;
          cuts.assign(plan.preparations.size(), 0);
          for (std::size_t j = 0; j < cuts.size(); ++j)
            if (plan.preparations[j].kind == RegionPreparationKind::InteriorCut)
              cuts[j] = frame.prepared[j].i;
          const auto source = frame.state[transition.source_state];
          const auto window = resolve_sequence_window(transition.window,
              Value::container_len(source), cuts);
          const Value args[]{source, Value::from_int(window.first), Value::from_int(window.second)};
          const auto result = builtin_call(BuiltinId::Slice, args, 3);
          if (result.is_error) return {true, Value::invalid(), result.err};
          // Preserve the charged child-entry type check, including fallback tags.
          next[i] = result.value;
          break;
        }
        case RegionTransitionKind::Expression: {
          auto result = run(segment.request_expressions[transition.expression],
                            plan.state_types[i], frame, fuel);
          if (result.is_error) return result;
          next[i] = result.value;
          break;
        }
      }
    }
    return {};
  }

  ExecResult combine(RegionFrame& frame, int& fuel) {
    for (std::size_t i = 0; i < segment.plan.requests.size(); ++i)
      if ((frame.results[i].tag != segment.plan.result_type &&
           frame.results[i].tag != ValueTag::FallbackToken) ||
          frame.results[i].tag != frame.results[0].tag)
        return region_failure(ErrCode::Type, "bounded region child result type mismatch");
    auto result = run(segment.combine, segment.plan.result_type, frame, fuel, true);
    if (!result.is_error && result.value.tag != frame.results[0].tag)
      return region_failure(ErrCode::Type, "bounded region combine result type mismatch");
    return result;
  }
};

}  // namespace gagp::detail
