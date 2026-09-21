#include <cuda_runtime.h>

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "../../src/evolution/repro/gpu/device/atomic_splice.cuh"

namespace {

using gagp::Value;
using gagp::evo::repro::AtomicSpliceOrigin;
using gagp::evo::repro::CandidateOccurrence;
using gagp::evo::repro::PlainNode;

constexpr int kSentinel = 0x13579;

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void check_cuda(cudaError_t status, const char* operation) {
  if (status == cudaSuccess) return;
  throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorString(status));
}

CandidateOccurrence occurrence(int start, int stop) {
  CandidateOccurrence out;
  out.start = start;
  out.stop = stop;
  return out;
}

std::vector<PlainNode> nodes(int count, int value_base) {
  std::vector<PlainNode> out;
  out.reserve(static_cast<std::size_t>(count));
  for (int i = 0; i < count; ++i) {
    out.push_back(PlainNode{value_base + i, value_base + 100 + i,
                            value_base + 200 + i});
  }
  return out;
}

__global__ void atomic_splice_kernel(
    const PlainNode* base, int base_len,
    const CandidateOccurrence* occurrences, int occurrence_count,
    const PlainNode* source, int source_len, int source_start, int source_stop,
    PlainNode* output, AtomicSpliceOrigin* origins, int output_capacity,
    int* output_len, int* accepted) {
  if (blockIdx.x != 0 || threadIdx.x != 0) return;
  *accepted = gagp::evo::repro::d_atomic_splice(
                  base, base_len, occurrences, occurrence_count, source,
                  source_len, source_start, source_stop, output, origins,
                  output_capacity, output_len)
                  ? 1
                  : 0;
}

struct SpliceResult {
  bool accepted = false;
  int output_len = kSentinel;
  std::vector<PlainNode> output;
  std::vector<AtomicSpliceOrigin> origins;
};

SpliceResult run_splice(const std::vector<PlainNode>& base,
                        const std::vector<CandidateOccurrence>& occurrences,
                        const std::vector<PlainNode>& source,
                        int source_start, int source_stop,
                        int output_capacity, int output_storage) {
  check(output_storage > 0 && output_capacity >= 0 &&
            output_capacity <= output_storage,
        "invalid test output storage");
  PlainNode* device_base = nullptr;
  CandidateOccurrence* device_occurrences = nullptr;
  PlainNode* device_source = nullptr;
  PlainNode* device_output = nullptr;
  AtomicSpliceOrigin* device_origins = nullptr;
  int* device_output_len = nullptr;
  int* device_accepted = nullptr;

  const std::vector<PlainNode> initial_output(
      static_cast<std::size_t>(output_storage),
      PlainNode{kSentinel, kSentinel, kSentinel});
  const std::vector<AtomicSpliceOrigin> initial_origins(
      static_cast<std::size_t>(output_storage),
      AtomicSpliceOrigin{kSentinel, kSentinel});
  int initial_output_len = kSentinel;

  auto allocate = [](auto** pointer, std::size_t bytes, const char* operation) {
    check_cuda(cudaMalloc(reinterpret_cast<void**>(pointer), bytes), operation);
  };
  try {
    allocate(&device_base, sizeof(PlainNode) * base.size(), "cudaMalloc base");
    allocate(&device_occurrences,
             sizeof(CandidateOccurrence) * occurrences.size(),
             "cudaMalloc occurrences");
    allocate(&device_source, sizeof(PlainNode) * source.size(),
             "cudaMalloc source");
    allocate(&device_output, sizeof(PlainNode) * initial_output.size(),
             "cudaMalloc output");
    allocate(&device_origins,
             sizeof(AtomicSpliceOrigin) * initial_origins.size(),
             "cudaMalloc origins");
    allocate(&device_output_len, sizeof(int), "cudaMalloc output_len");
    allocate(&device_accepted, sizeof(int), "cudaMalloc accepted");
    check_cuda(cudaMemcpy(device_base, base.data(), sizeof(PlainNode) * base.size(),
                          cudaMemcpyHostToDevice),
               "cudaMemcpy base");
    check_cuda(cudaMemcpy(device_occurrences, occurrences.data(),
                          sizeof(CandidateOccurrence) * occurrences.size(),
                          cudaMemcpyHostToDevice),
               "cudaMemcpy occurrences");
    check_cuda(cudaMemcpy(device_source, source.data(),
                          sizeof(PlainNode) * source.size(), cudaMemcpyHostToDevice),
               "cudaMemcpy source");
    check_cuda(cudaMemcpy(device_output, initial_output.data(),
                          sizeof(PlainNode) * initial_output.size(),
                          cudaMemcpyHostToDevice),
               "cudaMemcpy initial output");
    check_cuda(cudaMemcpy(device_origins, initial_origins.data(),
                          sizeof(AtomicSpliceOrigin) * initial_origins.size(),
                          cudaMemcpyHostToDevice),
               "cudaMemcpy initial origins");
    check_cuda(cudaMemcpy(device_output_len, &initial_output_len, sizeof(int),
                          cudaMemcpyHostToDevice),
               "cudaMemcpy initial output_len");

    atomic_splice_kernel<<<1, 1>>>(
        device_base, static_cast<int>(base.size()), device_occurrences,
        static_cast<int>(occurrences.size()), device_source,
        static_cast<int>(source.size()), source_start, source_stop, device_output,
        device_origins, output_capacity, device_output_len, device_accepted);
    check_cuda(cudaGetLastError(), "atomic_splice_kernel launch");

    SpliceResult result;
    result.output.resize(initial_output.size());
    result.origins.resize(initial_origins.size());
    int accepted = 0;
    check_cuda(cudaMemcpy(&accepted, device_accepted, sizeof(int),
                          cudaMemcpyDeviceToHost),
               "cudaMemcpy accepted");
    check_cuda(cudaMemcpy(&result.output_len, device_output_len, sizeof(int),
                          cudaMemcpyDeviceToHost),
               "cudaMemcpy output_len");
    check_cuda(cudaMemcpy(result.output.data(), device_output,
                          sizeof(PlainNode) * result.output.size(),
                          cudaMemcpyDeviceToHost),
               "cudaMemcpy output");
    check_cuda(cudaMemcpy(result.origins.data(), device_origins,
                          sizeof(AtomicSpliceOrigin) * result.origins.size(),
                          cudaMemcpyDeviceToHost),
               "cudaMemcpy origins");
    result.accepted = accepted != 0;

    cudaFree(device_accepted);
    cudaFree(device_output_len);
    cudaFree(device_origins);
    cudaFree(device_output);
    cudaFree(device_source);
    cudaFree(device_occurrences);
    cudaFree(device_base);
    return result;
  } catch (...) {
    if (device_accepted != nullptr) cudaFree(device_accepted);
    if (device_output_len != nullptr) cudaFree(device_output_len);
    if (device_origins != nullptr) cudaFree(device_origins);
    if (device_output != nullptr) cudaFree(device_output);
    if (device_source != nullptr) cudaFree(device_source);
    if (device_occurrences != nullptr) cudaFree(device_occurrences);
    if (device_base != nullptr) cudaFree(device_base);
    throw;
  }
}

void check_untouched(const SpliceResult& result, const char* message) {
  check(!result.accepted && result.output_len == kSentinel, message);
  for (const auto& node : result.output) {
    check(node.kind == kSentinel && node.i0 == kSentinel &&
              node.i1 == kSentinel,
          message);
  }
  for (const auto& origin : result.origins) {
    check(origin.original_index == kSentinel && origin.occurrence == kSentinel,
          message);
  }
}

void check_origins(const SpliceResult& result,
                   const std::vector<PlainNode>& base,
                   const std::vector<PlainNode>& source,
                   const std::vector<AtomicSpliceOrigin>& expected) {
  check(result.accepted && result.output_len == static_cast<int>(expected.size()),
        "accepted splice reported the wrong output length");
  for (std::size_t i = 0; i < expected.size(); ++i) {
    const auto& actual_origin = result.origins[i];
    const auto& expected_origin = expected[i];
    check(actual_origin.original_index == expected_origin.original_index &&
              actual_origin.occurrence == expected_origin.occurrence,
          "atomic splice reported the wrong node origin");
    const PlainNode& expected_node = expected_origin.occurrence < 0
        ? base.at(static_cast<std::size_t>(expected_origin.original_index))
        : source.at(static_cast<std::size_t>(expected_origin.original_index));
    const PlainNode& actual_node = result.output[i];
    check(actual_node.kind == expected_node.kind && actual_node.i0 == expected_node.i0 &&
              actual_node.i1 == expected_node.i1,
          "atomic splice copied the wrong source node");
  }
}

void test_growing_repeated_splice_and_origins() {
  const auto base = nodes(8, 0);
  const auto source = nodes(5, 1000);
  const std::vector<CandidateOccurrence> occurrences = {
      occurrence(1, 3), occurrence(5, 6)};
  const auto result = run_splice(base, occurrences, source, 1, 4, 11, 12);
  check_origins(result, base, source,
                {{0, -1}, {1, 0}, {2, 0}, {3, 0}, {3, -1}, {4, -1},
                 {1, 1}, {2, 1}, {3, 1}, {6, -1}, {7, -1}});
}

void test_shrinking_repeated_splice() {
  const auto base = nodes(6, 0);
  const auto source = nodes(4, 1000);
  const std::vector<CandidateOccurrence> occurrences = {
      occurrence(1, 4), occurrence(5, 6)};
  const auto result = run_splice(base, occurrences, source, 2, 3, 4, 6);
  check_origins(result, base, source,
                {{0, -1}, {2, 0}, {4, -1}, {2, 1}});
}

void test_preflight_rejections_leave_storage_untouched() {
  const auto base = nodes(8, 0);
  const auto source = nodes(5, 1000);
  check_untouched(run_splice(base, {occurrence(1, 4), occurrence(3, 5)},
                             source, 1, 3, 8, 12),
                  "overlapping occurrences wrote output storage");
  check_untouched(run_splice(base, {occurrence(5, 6), occurrence(1, 2)},
                             source, 1, 3, 10, 12),
                  "out-of-order occurrences wrote output storage");
  check_untouched(run_splice(base, {occurrence(1, 2)}, source, -1, 3, 10, 12),
                  "bad source bounds wrote output storage");
  check_untouched(run_splice(base, {occurrence(1, 2)}, source, 2, 6, 10, 12),
                  "source stop beyond its stream wrote output storage");

  const std::vector<CandidateOccurrence> growing = {
      occurrence(1, 3), occurrence(5, 6)};
  const auto exact = run_splice(base, growing, source, 1, 4, 11, 12);
  check(exact.accepted && exact.output_len == 11,
        "atomic splice rejected exact output capacity");
  check_untouched(run_splice(base, growing, source, 1, 4, 10, 12),
                  "output-capacity rejection wrote output storage");
}

}  // namespace

int main() {
  try {
    gagp::FitnessSessionGpu session;
    const auto initialized = session.init({{}}, {Value::from_int(0)}, 1, 1, 1.0);
    check(initialized.ok, "could not initialize CUDA device for atomic splice test");
    test_growing_repeated_splice_and_origins();
    test_shrinking_repeated_splice();
    test_preflight_rejections_leave_storage_untouched();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "GPU atomic splice: preflight, repeated ranges, and origins passed\n";
  return 0;
}
