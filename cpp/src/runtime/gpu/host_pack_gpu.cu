#include "gagp/core/semantic_fuel.hpp"
#include "gagp/core/bytecode_verify.hpp"
#include <limits>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <exception>
#include <future>
#include <thread>
#include <stdexcept>
#include <string>

#include "gagp/runtime/gpu/host_pack_gpu.hpp"

#include <cstdint>

#include "gagp/core/builtin.hpp"
#include "gagp/runtime/gpu/constants_gpu.hpp"
#include "opcode_map_gpu.hpp"

namespace gagp::gpu_detail {

namespace {

constexpr unsigned kPayloadMaskString = 1U << 0;
constexpr unsigned kPayloadMaskList = 1U << 1;

int checked_index(std::size_t value, const char* context) {
  if (value > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    throw std::invalid_argument(std::string(context) + " exceeds GPU index capacity");
  }
  return static_cast<int>(value);
}

void check_append_capacity(std::size_t current, std::size_t added,
                           const char* context) {
  const std::size_t limit =
      static_cast<std::size_t>(std::numeric_limits<int>::max());
  if (added > limit || current > limit - added) {
    throw std::invalid_argument(std::string(context) +
                                " exceeds GPU index capacity");
  }
}

int verify_bounded_program_or_throw(const BytecodeProgram& prog) {
  BytecodeVerifyOptions options;
  options.max_locals_per_code = MAX_LOCALS;
  options.max_stack_depth = MAX_STACK;
  const BytecodeVerifyResult verified = verify_bytecode(prog, options);
  if (!verified) {
    const BytecodeVerifyDiagnostic& diagnostic = verified.diagnostic;
    throw std::invalid_argument(
        std::string("bounded region bytecode verification failed: ") +
        bytecode_verify_code_name(diagnostic.code) + " at " + diagnostic.path +
        " instruction " + std::to_string(diagnostic.instruction_index) +
        ": " + diagnostic.message);
  }
  for (std::size_t i = 0; i < prog.bounded_region_segments.size(); ++i) {
    const RegionExecutionLimits& limits =
        prog.bounded_region_segments[i].plan.limits;
    if (limits.frames > DMAX_REGION_FRAMES) {
      throw std::invalid_argument(
          "bounded region segment " + std::to_string(i) +
          " frame limit exceeds GPU capacity 128");
    }
    if (limits.cells > DMAX_REGION_MEMO) {
      throw std::invalid_argument(
          "bounded region segment " + std::to_string(i) +
          " cell limit exceeds GPU capacity 128");
    }
  }
  return static_cast<int>(verified.verified.max_stack_depth);
}

unsigned value_payload_mask(const Value& v) {
  if (v.tag == ValueTag::String) return kPayloadMaskString;
  if (v.tag == ValueTag::IntList || v.tag == ValueTag::FloatList || v.tag == ValueTag::StringList) {
    return kPayloadMaskList;
  }
  return 0U;
}

unsigned program_payload_const_mask(const BytecodeProgram& prog) {
  unsigned mask = 0U;
  for (const Value& v : prog.consts) {
    mask |= value_payload_mask(v);
  }
  return mask;
}

bool program_has_exact_payload_builtin(const BytecodeProgram& prog) {
  for (const Instr& ins : prog.code) {
    if (ins.op != Opcode::CallBuiltin || !ins.has_a) {
      continue;
    }
    BuiltinId bid = BuiltinId::Abs;
    if (!builtin_id_from_int(ins.a, bid)) {
      continue;
    }
    if (bid == BuiltinId::Concat || bid == BuiltinId::Slice || bid == BuiltinId::Index ||
        bid == BuiltinId::Append || bid == BuiltinId::Prepend || bid == BuiltinId::Reverse ||
        bid == BuiltinId::Find || bid == BuiltinId::Contains || bid == BuiltinId::CharToString ||
        bid == BuiltinId::StringToChar || bid == BuiltinId::Singleton) {
      return true;
    }
  }
  return false;
}

DPayloadFlavor classify_payload_flavor(const BytecodeProgram& prog, unsigned shared_input_payload_mask) {
  if (!program_has_exact_payload_builtin(prog)) {
    return DPayloadFlavor::None;
  }
  const unsigned payload_mask = shared_input_payload_mask | program_payload_const_mask(prog);
  if (payload_mask == 0U) {
    return DPayloadFlavor::None;
  }
  if (payload_mask == kPayloadMaskString) {
    return DPayloadFlavor::StringOnly;
  }
  if (payload_mask == kPayloadMaskList) {
    return DPayloadFlavor::ListOnly;
  }
  return DPayloadFlavor::Mixed;
}

DInstr pack_instr(const Instr& ins, std::uint32_t fuel, bool* ok) {
  const int op = host_opcode(ins.op);
  if (op < 0) {
    *ok = false;
    return DInstr{};
  }
  DInstr di;
  di.op = static_cast<std::uint8_t>(op);
  di.flags = static_cast<std::uint8_t>((ins.has_a ? DINSTR_HAS_A : 0) | (ins.has_b ? DINSTR_HAS_B : 0));
  di.a = static_cast<std::int32_t>(ins.a);
  di.b = static_cast<std::int32_t>(ins.b);
  di.fuel = fuel;
  return di;
}

void validate_fuel_or_throw(const std::vector<Instr>& code,
                            const std::vector<std::uint32_t>& costs,
                            const char* context) {
  const SemanticFuelValidation validation =
      validate_semantic_fuel(code, costs);
  if (!validation) {
    throw std::invalid_argument(
        std::string(context) + " semantic fuel schedule is invalid at instruction " +
        std::to_string(validation.instruction_index) + ": " +
        validation.message);
  }
}

std::uint32_t instruction_fuel(
    const std::vector<std::uint32_t>& costs, std::size_t index) {
  return costs.empty() ? 1U : costs[index];
}

DPhaseMeta append_phase(const PhaseProgram& phase,
                        std::vector<DInstr>* all_phase_code,
                        std::vector<Value>* all_phase_consts,
                        bool* ok) {
  validate_fuel_or_throw(phase.code, phase.instruction_fuel, "phase");
  check_append_capacity(all_phase_code->size(), phase.code.size(),
                        "phase code table");
  check_append_capacity(all_phase_consts->size(), phase.consts.size(),
                        "phase constant table");
  DPhaseMeta meta;
  meta.code_offset = checked_index(all_phase_code->size(), "phase code offset");
  meta.const_offset = checked_index(all_phase_consts->size(), "phase constant offset");
  meta.n_locals = phase.n_locals;
  all_phase_consts->insert(all_phase_consts->end(), phase.consts.begin(), phase.consts.end());
  meta.const_len = checked_index(phase.consts.size(), "phase constant count");
  for (std::size_t ip = 0; ip < phase.code.size(); ++ip) {
    all_phase_code->push_back(pack_instr(
        phase.code[ip], instruction_fuel(phase.instruction_fuel, ip), ok));
  }
  meta.code_len = checked_index(phase.code.size(), "phase code count");
  if (phase.n_locals < 0 || phase.n_locals > MAX_LOCALS) {
    *ok = false;
  }
  return meta;
}

int append_region_phase(const RegionPhase& phase,
                        std::vector<DInstr>* all_phase_code,
                        std::vector<Value>* all_phase_consts,
                        std::vector<DRegionPhase>* region_phases,
                        std::vector<DRegionPhaseBinding>* region_bindings) {
  check_append_capacity(region_phases->size(), 1, "region phase table");
  check_append_capacity(region_bindings->size(), phase.bindings.size(),
                        "region phase binding table");
  const int phase_index = checked_index(region_phases->size(), "region phase index");
  DRegionPhase packed;
  bool ok = true;
  packed.program = append_phase(
      phase.program, all_phase_code, all_phase_consts, &ok);
  if (!ok) {
    throw std::invalid_argument("verified bounded region phase cannot be packed for GPU execution");
  }
  packed.binding_offset =
      checked_index(region_bindings->size(), "region phase binding offset");
  packed.binding_count =
      checked_index(phase.bindings.size(), "region phase binding count");
  for (const RegionPhaseBinding& binding : phase.bindings) {
    DRegionPhaseBinding packed_binding;
    packed_binding.bank = binding.source.bank;
    packed_binding.slot = binding.source.slot;
    packed_binding.local = binding.local;
    region_bindings->push_back(packed_binding);
  }
  region_phases->push_back(packed);
  return phase_index;
}

DRegionSegment pack_region_segment(
    const BoundedRegionSegment& segment, std::vector<DInstr>* all_phase_code,
    std::vector<Value>* all_phase_consts,
    std::vector<DRegionPhase>* region_phases,
    std::vector<DRegionPhaseBinding>* region_bindings) {
  const RegionPlan& plan = segment.plan;
  DRegionSegment packed;
  packed.state_count = static_cast<std::uint32_t>(plan.state_types.size());
  for (std::size_t i = 0; i < plan.state_types.size(); ++i) {
    packed.state_types[i] = plan.state_types[i];
  }
  packed.result_type = plan.result_type;

  packed.parameter_count = static_cast<std::uint32_t>(plan.parameter_types.size());
  for (std::size_t i = 0; i < plan.parameter_types.size(); ++i) {
    packed.parameter_types[i] = plan.parameter_types[i];
    packed.parameter_caller_locals[i] = segment.parameter_locals[i];
  }

  packed.preparation_count = static_cast<std::uint32_t>(plan.preparations.size());
  for (std::size_t i = 0; i < plan.preparations.size(); ++i) {
    packed.preparation_kinds[i] = plan.preparations[i].kind;
    packed.preparation_types[i] = plan.preparations[i].type;
  }
  packed.request_expression_count =
      static_cast<std::uint32_t>(plan.request_expression_types.size());
  for (std::size_t i = 0; i < plan.request_expression_types.size(); ++i) {
    packed.request_expression_types[i] = plan.request_expression_types[i];
  }

  packed.bound_operand_count = plan.bound_operand_count;
  packed.request_count = static_cast<std::uint32_t>(plan.requests.size());
  for (std::size_t request = 0; request < plan.requests.size(); ++request) {
    for (std::size_t state = 0; state < plan.state_types.size(); ++state) {
      packed.requests[request][state] = plan.requests[request].states[state];
    }
  }
  packed.limits = plan.limits;
  packed.memoized = plan.memoized;
  packed.progress = plan.progress;

  packed.coordinate_count =
      static_cast<std::uint32_t>(plan.coordinate_slots.size());
  for (std::size_t i = 0; i < plan.coordinate_slots.size(); ++i) {
    packed.coordinate_slots[i] = plan.coordinate_slots[i];
    packed.coordinate_rank[i] = plan.coordinate_rank[i];
    packed.coordinate_domains[i] = plan.coordinate_domains[i];
  }
  packed.coordinate_endpoint = plan.coordinate_endpoint;
  packed.sequence_state = plan.sequence_state;

  if (segment.boundary) {
    packed.boundary_phase = append_region_phase(
        *segment.boundary, all_phase_code, all_phase_consts, region_phases,
        region_bindings);
  }
  packed.base_predicate_phase = append_region_phase(
      segment.base_predicate, all_phase_code, all_phase_consts, region_phases,
      region_bindings);
  packed.base_body_phase = append_region_phase(
      segment.base_body, all_phase_code, all_phase_consts, region_phases,
      region_bindings);
  for (std::size_t i = 0; i < segment.preparations.size(); ++i) {
    packed.preparation_phases[i] = append_region_phase(
        segment.preparations[i], all_phase_code, all_phase_consts,
        region_phases, region_bindings);
  }
  for (std::size_t i = 0; i < segment.request_expressions.size(); ++i) {
    packed.request_expression_phases[i] = append_region_phase(
        segment.request_expressions[i], all_phase_code, all_phase_consts,
        region_phases, region_bindings);
  }
  packed.combine_phase = append_region_phase(
      segment.combine, all_phase_code, all_phase_consts, region_phases,
      region_bindings);
  return packed;
}

}  // namespace

DPayloadFlavor classify_payload_flavor_for_program(const BytecodeProgram& prog, unsigned shared_input_payload_mask) {
  return classify_payload_flavor(prog, shared_input_payload_mask);
}

PackResult pack_programs_with_shared_case_count(const std::vector<BytecodeProgram>& programs,
                                                int shared_case_count,
                                                unsigned shared_input_payload_mask) {
  PackResult out;
  if (shared_case_count < 0) {
    throw std::invalid_argument("shared case count must be non-negative");
  }
  checked_index(programs.size(), "program count");
  out.metas.resize(programs.size());
  std::size_t code_capacity = 0;
  std::size_t const_capacity = 0;
  for (const auto& program : programs) {
    check_append_capacity(code_capacity, program.code.size(), "root code table");
    check_append_capacity(const_capacity, program.consts.size(), "root constant table");
    code_capacity += program.code.size();
    const_capacity += program.consts.size();
  }
  out.all_code.reserve(code_capacity);
  out.all_consts.reserve(const_capacity);

  // The verifier only reads bytecode. Keep every check in this packing call,
  // but verify independent programs concurrently. Retain per-program errors
  // so the ordinary pack loop still reports the first invalid input in order.
  const bool parallel_verify = programs.size() >= 32 &&
      std::getenv("GAGP_SERIAL_PACK_VERIFY") == nullptr;
  std::vector<std::exception_ptr> verification_errors(programs.size());
  std::vector<int> verified_stack_bounds(programs.size(), MAX_STACK);
  if (parallel_verify) {
    std::atomic<std::size_t> next{0};
    const auto count = std::min<unsigned>(20, std::max(1u, std::thread::hardware_concurrency()));
    auto verify_next = [&] {
      for (;;) {
        const auto p = next.fetch_add(1, std::memory_order_relaxed);
        if (p >= programs.size()) break;
        try {
          if (has_bounded_region(programs[p])) verified_stack_bounds[p] = verify_bounded_program_or_throw(programs[p]);
        } catch (...) { verification_errors[p] = std::current_exception(); }
      }
    };
    std::vector<std::future<void>> workers;
    for (unsigned i = 1; i < count; ++i)
      workers.emplace_back(std::async(std::launch::async, verify_next));
    verify_next();
    for (auto& worker : workers) worker.get();
  }

  for (std::size_t p = 0; p < programs.size(); ++p) {
    const BytecodeProgram& prog = programs[p];
    const bool bounded = has_bounded_region(prog);
    if (bounded) {
      if (!parallel_verify) verified_stack_bounds[p] = verify_bounded_program_or_throw(prog);
      else if (verification_errors[p]) std::rethrow_exception(verification_errors[p]);
    }
    validate_fuel_or_throw(prog.code, prog.instruction_fuel, "root");

    DProgramMeta meta;
    check_append_capacity(out.all_code.size(), prog.code.size(), "root code table");
    check_append_capacity(out.all_consts.size(), prog.consts.size(), "root constant table");
    meta.code_offset = checked_index(out.all_code.size(), "root code offset");
    meta.const_offset = checked_index(out.all_consts.size(), "root constant offset");
    meta.n_locals = prog.n_locals;
    meta.region_offset = checked_index(out.region_segments.size(), "region offset");
    meta.case_offset = checked_index(out.total_cases, "case offset");
    meta.case_count = shared_case_count;
    meta.case_local_offset = 0;
    meta.is_valid = 1;
    meta.payload_flavor = classify_payload_flavor(prog, shared_input_payload_mask);
    meta.err_code = ErrCode::Value;

    out.all_consts.insert(out.all_consts.end(), prog.consts.begin(), prog.consts.end());
    meta.const_len = checked_index(prog.consts.size(), "root constant count");

    for (std::size_t ip = 0; ip < prog.code.size(); ++ip) {
      bool ok = true;
      out.all_code.push_back(pack_instr(
          prog.code[ip], instruction_fuel(prog.instruction_fuel, ip), &ok));
      if (!ok) {
        meta.is_valid = 0;
        meta.err_code = ErrCode::Type;
      }
    }

    if (bounded) {
      const auto phase_begin = out.region_phases.size();
      check_append_capacity(out.region_segments.size(),
                            prog.bounded_region_segments.size(),
                            "region segment table");
      for (const BoundedRegionSegment& segment : prog.bounded_region_segments) {
        out.region_segments.push_back(pack_region_segment(
            segment, &out.all_phase_code, &out.all_phase_consts,
            &out.region_phases, &out.region_bindings));
      }
      if (std::getenv("GAGP_GENERIC_PHASE_VM") == nullptr)
        for (auto i = phase_begin; i < out.region_phases.size(); ++i)
          out.region_phases[i].program.verified_stack_bound = verified_stack_bounds[p];
    }
    meta.region_count = checked_index(prog.bounded_region_segments.size(),
                                      "region segment count");

    meta.code_len = checked_index(prog.code.size(), "root code count");
    if (static_cast<std::size_t>(meta.code_len) > out.max_code_len) {
      out.max_code_len = static_cast<std::size_t>(meta.code_len);
    }
    if (prog.n_locals < 0 || prog.n_locals > MAX_LOCALS) {
      meta.is_valid = 0;
      meta.err_code = ErrCode::Value;
    }

    const std::size_t case_count = static_cast<std::size_t>(shared_case_count);
    check_append_capacity(out.total_cases, case_count, "case table");
    out.total_cases += case_count;
    out.metas[p] = meta;
  }

  return out;
}

void pack_shared_cases_only(const std::vector<CaseBindings>& shared_cases,
                            std::vector<Value>* packed_case_local_vals,
                            std::vector<unsigned char>* packed_case_local_set) {
  if (shared_cases.size() >
      std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(MAX_LOCALS)) {
    throw std::invalid_argument("shared case local table exceeds host size capacity");
  }
  const std::size_t local_count =
      shared_cases.size() * static_cast<std::size_t>(MAX_LOCALS);
  packed_case_local_vals->assign(local_count, Value::invalid());
  packed_case_local_set->assign(local_count, 0);
  for (std::size_t case_idx = 0; case_idx < shared_cases.size(); ++case_idx) {
    const std::size_t base = case_idx * MAX_LOCALS;
    for (const InputBinding& binding : shared_cases[case_idx]) {
      if (binding.idx >= 0 && binding.idx < MAX_LOCALS) {
        (*packed_case_local_vals)[base + static_cast<std::size_t>(binding.idx)] = binding.value;
        (*packed_case_local_set)[base + static_cast<std::size_t>(binding.idx)] = 1;
      }
    }
  }
}

DeviceArena::~DeviceArena() {
  if (d_consts) cudaFree(d_consts);
  if (d_code) cudaFree(d_code);
  if (d_phase_consts) cudaFree(d_phase_consts);
  if (d_phase_code) cudaFree(d_phase_code);
  if (d_region_segments) cudaFree(d_region_segments);
  if (d_region_phases) cudaFree(d_region_phases);
  if (d_region_bindings) cudaFree(d_region_bindings);
  if (d_region_frames) cudaFree(d_region_frames);
  if (d_region_memo_keys) cudaFree(d_region_memo_keys);
  if (d_region_memo_values) cudaFree(d_region_memo_values);
  if (d_metas) cudaFree(d_metas);
  if (d_shared_case_local_vals) cudaFree(d_shared_case_local_vals);
  if (d_shared_case_local_set) cudaFree(d_shared_case_local_set);
  if (d_out) cudaFree(d_out);
  if (d_expected) cudaFree(d_expected);
  if (d_fitness) cudaFree(d_fitness);
  if (d_case_counts) cudaFree(d_case_counts);
  if (d_string_payload_entries) cudaFree(d_string_payload_entries);
  if (d_string_payload_bytes) cudaFree(d_string_payload_bytes);
  if (d_list_payload_entries) cudaFree(d_list_payload_entries);
  if (d_list_payload_values) cudaFree(d_list_payload_values);
}

}  // namespace gagp::gpu_detail
