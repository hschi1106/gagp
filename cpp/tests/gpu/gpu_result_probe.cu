// GAGP_GPU_FITNESS_SOURCE names the immutable reference fitness_gpu.cu.
// Its host packing helpers and device evaluator remain the production source.
#ifndef GAGP_CAPTURE_HAS_GENERIC_REGIONS
#if defined(__has_include)
#if __has_include("gagp/runtime/gpu/region_types_gpu.hpp")
#define GAGP_CAPTURE_HAS_GENERIC_REGIONS 1
#endif
#endif
#endif
#ifndef GAGP_CAPTURE_HAS_GENERIC_REGIONS
#define GAGP_CAPTURE_HAS_GENERIC_REGIONS 0
#endif
#include GAGP_GPU_FITNESS_SOURCE
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#if GAGP_CAPTURE_HAS_GENERIC_REGIONS
#include "gagp/cli/codec.hpp"
#endif

namespace gagp::migration {
using namespace gpu_detail;

#ifdef GAGP_CAPTURE_LIBRARY_ONLY
std::string encode_value(const Value& value, bool require_payload) {
  std::uint64_t bits = 0;
  if (value.tag != ValueTag::Bool && value.tag != ValueTag::Invalid)
    std::memcpy(&bits, &value.i, sizeof(bits));
  std::ostringstream out;
  out << "{\"tag\":" << static_cast<int>(value.tag)
      << ",\"bits\":\"" << std::hex << std::setw(16) << std::setfill('0')
      << bits << "\",\"bool\":" << (value.b ? "true" : "false");
  const auto hex = [](const std::string& bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(bytes.size() * 2);
    for (unsigned char byte : bytes) {
      encoded.push_back(digits[byte >> 4]);
      encoded.push_back(digits[byte & 15]);
    }
    return encoded;
  };
  if (value.tag == ValueTag::String) {
    std::string data;
    if (!payload::lookup_string(value, &data)) {
      if (require_payload) throw std::runtime_error("missing diagnostic string payload");
      return out.str() + ",\"materialized\":false}";
    }
    out << ",\"bytes_hex\":\"" << hex(data) << '"';
  } else if (value.tag == ValueTag::IntList || value.tag == ValueTag::FloatList ||
             value.tag == ValueTag::StringList) {
    std::vector<Value> elements;
    if (!payload::lookup_list(value, &elements)) {
      if (require_payload) throw std::runtime_error("missing diagnostic list payload");
      return out.str() + ",\"materialized\":false}";
    }
    out << ",\"elements\":[";
    for (std::size_t i = 0; i < elements.size(); ++i) {
      if (i) out << ',';
      out << encode_value(elements[i], require_payload);
    }
    out << ']';
  }
  out << '}';
  return out.str();
}
#endif

struct Observation {
  DResult ordinary;
  DResult inspected;
  int fuel_left;
  DMixedPayloadState scratch;
};

template <bool Regions = false>
__global__ void inspect_execution(DProgramMeta meta, const DInstr* code, const Value* constants,
    const Value* inputs, const unsigned char* input_set, DPayloadTables payloads,
    DExecutionTables tables, int fuel, Observation* out,
    DRegionWorkspace workspace = {}) {
  out->ordinary = d_execute_bytecode_impl<DPayloadFlavor::Mixed, Regions>(
      meta, code, constants, inputs, input_set, payloads, tables, 0, fuel,
      workspace);
  out->fuel_left = fuel;
  if (!meta.is_valid) {
    out->inspected = out->ordinary;
    return;
  }
  DPayloadStateStorage<DPayloadFlavor::Mixed> storage;
  const DCodeView view{code, meta.code_len, constants + meta.const_offset, meta.const_len,
      meta.n_locals, meta.region_offset, meta.region_count};
  out->inspected = d_run_code_core<DPayloadFlavor::Mixed, Regions>(
      view, inputs, input_set, 0, nullptr, 0, payloads, storage.ref(), tables,
      out->fuel_left, true, workspace);
  out->scratch = storage.state;
}

void checked(cudaError_t result) {
  if (result != cudaSuccess) throw std::runtime_error(cudaGetErrorString(result));
}

template <class T>
void upload(const std::vector<T>& values, T** pointer) {
  if (!cuda_alloc_and_copy_in(values, pointer)) throw std::runtime_error("GPU diagnostic upload failed");
}

BytecodeProgram decode_probe_bytecode(const cli_detail::JsonValue& raw) {
#if GAGP_CAPTURE_HAS_GENERIC_REGIONS
  if (raw.kind != cli_detail::JsonValue::Kind::Object) {
    throw std::runtime_error("diagnostic bytecode must be an object");
  }
  const auto format = raw.object_v.find("format_version");
  if (format != raw.object_v.end()) {
    const std::string version =
        cli_detail::require_string(format->second, "bytecode.format_version");
#ifndef GAGP_CAPTURE_LIBRARY_ONLY
    if (version == "migration-bytecode-v1") return decode_bytecode(raw);
#endif
    throw std::runtime_error("unsupported diagnostic bytecode format_version: " + version);
  }
  for (const char* field : {"n_locals", "consts", "code"}) {
    if (raw.object_v.find(field) == raw.object_v.end()) {
      throw std::runtime_error(
          std::string("unrecognized diagnostic bytecode schema; missing public field: ") + field);
    }
  }
  return cli_detail::decode_program(raw);
#else
  return decode_bytecode(raw);
#endif
}

std::string probe(const BytecodeProgram& program, const CaseBindings& inputs, int fuel,
                  bool* timeout) {
  struct RegistryGuard {
    std::vector<payload::StringSnapshot> strings = payload::snapshot_strings();
    std::vector<payload::ListSnapshot> lists = payload::snapshot_lists();
    ~RegistryGuard() {
      payload::clear();
      for (const auto& entry : strings) payload::register_string(entry.key, entry.data);
      for (const auto& entry : lists) payload::register_list(entry.key, entry.elems);
    }
  } registry_guard;
  auto packed = pack_programs_with_shared_case_count({program}, 1, 3);
  pack_shared_cases_only({inputs}, &packed.packed_case_local_vals, &packed.packed_case_local_set);
  std::vector<std::int64_t> strings;
  std::vector<Value> lists;
  append_payload_tokens_from_values(packed.packed_case_local_vals, &strings, &lists);
  append_payload_tokens_from_values(program.consts, &strings, &lists);
  append_payload_tokens_from_values(packed.all_phase_consts, &strings, &lists);
  HostStringPayloadLookup string_cache;
  HostListPayloadLookup list_cache;
  populate_payload_cache(strings, lists, &string_cache, &list_cache);
  const auto host_payload = build_payload_pack(string_cache, list_cache, strings, lists);
  DeviceArena device;
  upload(packed.all_code, &device.d_code);
  upload(packed.all_consts, &device.d_consts);
  upload(packed.all_phase_code, &device.d_phase_code);
  upload(packed.all_phase_consts, &device.d_phase_consts);
#if GAGP_CAPTURE_HAS_GENERIC_REGIONS
  upload(packed.region_segments, &device.d_region_segments);
  upload(packed.region_phases, &device.d_region_phases);
  upload(packed.region_bindings, &device.d_region_bindings);
  std::uint32_t region_frame_capacity = 0;
  std::uint32_t region_memo_capacity = 0;
  for (const auto& segment : packed.region_segments) {
    region_frame_capacity = std::max(region_frame_capacity, segment.limits.frames);
    region_memo_capacity = std::max(region_memo_capacity, segment.limits.cells);
  }
  const std::size_t region_memo_key_capacity =
      static_cast<std::size_t>(region_memo_capacity) * DMAX_REGION_STATES;
  if (region_frame_capacity) {
    checked(cudaMalloc(reinterpret_cast<void**>(&device.d_region_frames),
                       sizeof(DRegionFrame) * region_frame_capacity));
  }
  if (region_memo_key_capacity) {
    checked(cudaMalloc(reinterpret_cast<void**>(&device.d_region_memo_keys),
                       sizeof(std::int64_t) * region_memo_key_capacity));
  }
  if (region_memo_capacity) {
    checked(cudaMalloc(reinterpret_cast<void**>(&device.d_region_memo_values),
                       sizeof(Value) * region_memo_capacity));
  }
  const DRegionWorkspace region_workspace{
      device.d_region_frames, device.d_region_memo_keys, device.d_region_memo_values,
      region_frame_capacity, region_memo_capacity};
#endif
  upload(packed.packed_case_local_vals, &device.d_shared_case_local_vals);
  upload(packed.packed_case_local_set, &device.d_shared_case_local_set);
  upload(host_payload.string_entries, &device.d_string_payload_entries);
  upload(host_payload.string_bytes, &device.d_string_payload_bytes);
  upload(host_payload.list_entries, &device.d_list_payload_entries);
  upload(host_payload.list_values, &device.d_list_payload_values);
  const DPayloadTables payloads{device.d_string_payload_entries, static_cast<int>(host_payload.string_entries.size()),
      device.d_string_payload_bytes, device.d_list_payload_entries, static_cast<int>(host_payload.list_entries.size()), device.d_list_payload_values};
  const DExecutionTables execution_tables{
      device.d_phase_code, device.d_phase_consts,
      device.d_region_segments, static_cast<int>(packed.region_segments.size()),
      device.d_region_phases, static_cast<int>(packed.region_phases.size()),
      device.d_region_bindings, static_cast<int>(packed.region_bindings.size()),
  };
  Observation* result = nullptr;
  checked(cudaMallocManaged(&result, sizeof(Observation)));
  try {
    checked(cudaMemset(result, 0, sizeof(Observation)));
    const auto meta = packed.metas.at(0);
#if GAGP_CAPTURE_HAS_GENERIC_REGIONS
    if (meta.region_count) {
      inspect_execution<true><<<1, 1>>>(meta, device.d_code, device.d_consts,
          device.d_shared_case_local_vals, device.d_shared_case_local_set, payloads,
          execution_tables, fuel,
          result, region_workspace);
    } else
#endif
    {
      inspect_execution<false><<<1, 1>>>(meta, device.d_code, device.d_consts,
          device.d_shared_case_local_vals, device.d_shared_case_local_set, payloads,
          execution_tables, fuel, result, region_workspace);
    }
    checked(cudaGetLastError());
    checked(cudaDeviceSynchronize());
    const auto& actual = result->ordinary;
    const auto& inspected = result->inspected;
    if (actual.is_error != inspected.is_error || (actual.is_error ? actual.err_code != inspected.err_code :
        encode_value(actual.value, false) != encode_value(inspected.value, false))) {
      throw std::runtime_error("diagnostic core differs from ordinary production evaluator");
    }
    const auto& state = result->scratch;
    for (int i = 0; i < state.string_entry_count; ++i) {
      const auto entry = state.string_entries[i];
      Value key = Value::from_int(entry.packed); key.tag = ValueTag::String;
      payload::register_string(key, std::string(state.string_bytes + entry.offset, entry.len));
    }
    for (int i = 0; i < state.list_entry_count; ++i) {
      const auto entry = state.list_entries[i];
      Value key = Value::from_int(entry.packed); key.tag = entry.tag;
      payload::register_list(key, std::vector<Value>(state.list_values + entry.offset, state.list_values + entry.offset + entry.len));
    }
    *timeout = actual.is_error && actual.err_code == ErrCode::Timeout;
    std::ostringstream out;
    out << "{\"error\":";
    if (actual.is_error) out << '"' << err_code_name(actual.err_code) << '"';
    else out << "null,\"value\":" << encode_value(actual.value, false);
    out << ",\"fuel_left\":" << result->fuel_left << ",\"ordinary_core_match\":true}";
    checked(cudaFree(result));
    return out.str();
  } catch (...) {
    cudaFree(result);
    throw;
  }
}
}  // namespace gagp::migration

