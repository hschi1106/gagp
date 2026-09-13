// GAGP_GPU_FITNESS_SOURCE names the immutable reference fitness_gpu.cu.
// Its host packing helpers and device evaluator remain the production source.
#include GAGP_GPU_FITNESS_SOURCE
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include "migration_snapshot.hpp"

namespace gagp::migration {
using namespace gpu_detail;

struct Observation {
  DResult ordinary;
  DResult inspected;
  int fuel_left;
  DMixedPayloadState scratch;
};

template <bool Asgp>
__global__ void inspect_execution(DProgramMeta meta, const DInstr* code, const Value* constants,
    const Value* inputs, const unsigned char* input_set, DPayloadTables payloads,
    DAsgpTables phases, int fuel, Observation* out) {
  out->ordinary = d_execute_bytecode_impl<DPayloadFlavor::Mixed, Asgp>(
      meta, code, constants, inputs, input_set, payloads, phases, 0, fuel);
  out->fuel_left = fuel;
  if (!meta.is_valid) {
    out->inspected = out->ordinary;
    return;
  }
  DPayloadStateStorage<DPayloadFlavor::Mixed> storage;
  const DCodeView view{code, meta.code_len, constants + meta.const_offset, meta.const_len,
      meta.n_locals, meta.asgp_dc_offset, meta.asgp_dc_count, meta.asgp_dp1d_offset,
      meta.asgp_dp1d_count, meta.asgp_dp2d_offset, meta.asgp_dp2d_count};
  out->inspected = d_run_code_core<DPayloadFlavor::Mixed, Asgp>(view, inputs, input_set,
      0, nullptr, 0, payloads, storage.ref(), phases, out->fuel_left, true);
  out->scratch = storage.state;
}

void checked(cudaError_t result) {
  if (result != cudaSuccess) throw std::runtime_error(cudaGetErrorString(result));
}

template <class T>
void upload(const std::vector<T>& values, T** pointer) {
  if (!cuda_alloc_and_copy_in(values, pointer)) throw std::runtime_error("GPU diagnostic upload failed");
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
  for (const auto& segment : packed.asgp_dp1d_segments) append_payload_tokens_from_values({segment.boundary_value}, &strings, &lists);
  for (const auto& segment : packed.asgp_dp2d_segments) append_payload_tokens_from_values({segment.boundary_value}, &strings, &lists);
  HostStringPayloadLookup string_cache;
  HostListPayloadLookup list_cache;
  populate_payload_cache(strings, lists, &string_cache, &list_cache);
  const auto host_payload = build_payload_pack(string_cache, list_cache, strings, lists);
  DeviceArena device;
  upload(packed.all_code, &device.d_code);
  upload(packed.all_consts, &device.d_consts);
  upload(packed.all_phase_code, &device.d_phase_code);
  upload(packed.all_phase_consts, &device.d_phase_consts);
  upload(packed.asgp_dc_segments, &device.d_asgp_dc_segments);
  upload(packed.asgp_dp1d_segments, &device.d_asgp_dp1d_segments);
  upload(packed.asgp_dp2d_segments, &device.d_asgp_dp2d_segments);
  upload(packed.packed_case_local_vals, &device.d_shared_case_local_vals);
  upload(packed.packed_case_local_set, &device.d_shared_case_local_set);
  upload(host_payload.string_entries, &device.d_string_payload_entries);
  upload(host_payload.string_bytes, &device.d_string_payload_bytes);
  upload(host_payload.list_entries, &device.d_list_payload_entries);
  upload(host_payload.list_values, &device.d_list_payload_values);
  const DPayloadTables payloads{device.d_string_payload_entries, static_cast<int>(host_payload.string_entries.size()),
      device.d_string_payload_bytes, device.d_list_payload_entries, static_cast<int>(host_payload.list_entries.size()), device.d_list_payload_values};
  const DAsgpTables phases{device.d_phase_code, device.d_phase_consts,
      device.d_asgp_dc_segments, static_cast<int>(packed.asgp_dc_segments.size()),
      device.d_asgp_dp1d_segments, static_cast<int>(packed.asgp_dp1d_segments.size()),
      device.d_asgp_dp2d_segments, static_cast<int>(packed.asgp_dp2d_segments.size())};
  Observation* result = nullptr;
  checked(cudaMallocManaged(&result, sizeof(Observation)));
  try {
    checked(cudaMemset(result, 0, sizeof(Observation)));
    const auto meta = packed.metas.at(0);
    if (meta.asgp_dc_count || meta.asgp_dp1d_count || meta.asgp_dp2d_count) {
      inspect_execution<true><<<1, 1>>>(meta, device.d_code, device.d_consts,
          device.d_shared_case_local_vals, device.d_shared_case_local_set, payloads, phases, fuel, result);
    } else {
      inspect_execution<false><<<1, 1>>>(meta, device.d_code, device.d_consts,
          device.d_shared_case_local_vals, device.d_shared_case_local_set, payloads, phases, fuel, result);
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

int main(int argc, char** argv) {
  using namespace gagp;
  using namespace gagp::cli_detail;
  try {
    if (argc != 4) throw std::runtime_error("usage: capture_gpu_results INPUT.jsonl OUTPUT.jsonl DEVICE");
    setenv("GAGP_CUDA_DEVICE", argv[3], 1);
    migration::checked(cudaSetDevice(std::stoi(argv[3])));
    std::ifstream in(argv[1]); std::ofstream out(argv[2]);
    if (!in || !out) throw std::runtime_error("cannot open diagnostic input/output");
    std::string line;
    while (std::getline(in, line)) {
      const auto row = JsonParser(line).parse();
      if (require_string(require_object_field(row, "kind"), "kind") != "execution") continue;
      payload::clear();
      const auto program = migration::decode_bytecode(require_object_field(row, "bytecode"));
      CaseBindings inputs;
      for (const auto& input : require_object_field(row, "inputs").array_v) {
        inputs.push_back({require_int(input.array_v.at(0), "local"), migration::decode_value(input.array_v.at(1))});
      }
      const int fuel = require_int(require_object_field(row, "fuel"), "fuel");
      const int cap = require_int(require_object_field(row, "probe_cap"), "probe_cap");
      bool timed_out = false;
      const auto at_fuel = migration::probe(program, inputs, fuel, &timed_out);
      const auto at_cap = migration::probe(program, inputs, cap, &timed_out);
      out << "{\"kind\":\"gpu_execution\",\"ordinal\":" << require_int(require_object_field(row, "ordinal"), "ordinal")
          << ",\"fuel\":" << fuel << ",\"probe_cap\":" << cap
          << ",\"result\":" << at_fuel << ",\"at_probe_cap\":" << at_cap;
      if (!timed_out) {
        int low = 0, high = cap;
        while (low < high) {
          const int mid = low + (high - low) / 2;
          migration::probe(program, inputs, mid, &timed_out);
          if (timed_out) low = mid + 1; else high = mid;
        }
        out << ",\"first_non_timeout_fuel\":" << low << ",\"at_boundary\":"
            << migration::probe(program, inputs, low, &timed_out);
        if (low > 0) out << ",\"below_boundary\":" << migration::probe(program, inputs, low - 1, &timed_out);
      } else out << ",\"first_non_timeout_fuel\":null";
      out << "}\n";
      out.flush();
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
