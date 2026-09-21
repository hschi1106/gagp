#include <cuda_runtime.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/evolution/grammar/random.hpp"
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "../../src/evolution/repro/gpu/device/grammar_random.cuh"

namespace {

using gagp::evo::grammar::GrammarRandom;
using gagp::evo::repro::DGrammarRandom;

constexpr int kSamples = 128;

void check(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

void check_cuda(cudaError_t status, const char* operation) {
  if (status == cudaSuccess) return;
  throw std::runtime_error(std::string(operation) + ": " +
                           cudaGetErrorString(status));
}

enum class Operation : int { Next, Bounded, Integer };

__global__ void grammar_random_kernel(std::uint64_t seed, Operation operation,
                                      std::uint64_t bound,
                                      std::int64_t minimum,
                                      std::int64_t maximum,
                                      std::uint64_t* output) {
  if (blockIdx.x != 0 || threadIdx.x != 0) return;
  DGrammarRandom random(seed);
  for (int i = 0; i < kSamples; ++i) {
    if (operation == Operation::Next) {
      output[i] = random.next();
    } else if (operation == Operation::Bounded) {
      output[i] = random.bounded(bound);
    } else {
      output[i] = static_cast<std::uint64_t>(random.integer(minimum, maximum));
    }
  }
}

std::vector<std::uint64_t> run_device(std::uint64_t seed,
                                      Operation operation,
                                      std::uint64_t bound = 1,
                                      std::int64_t minimum = 0,
                                      std::int64_t maximum = 0) {
  std::uint64_t* device_output = nullptr;
  check_cuda(cudaMalloc(reinterpret_cast<void**>(&device_output),
                        sizeof(std::uint64_t) * kSamples),
             "cudaMalloc grammar RNG output");
  try {
    grammar_random_kernel<<<1, 1>>>(seed, operation, bound, minimum, maximum,
                                    device_output);
    check_cuda(cudaGetLastError(), "grammar_random_kernel launch");
    std::vector<std::uint64_t> output(kSamples);
    check_cuda(cudaMemcpy(output.data(), device_output,
                          sizeof(std::uint64_t) * output.size(),
                          cudaMemcpyDeviceToHost),
               "cudaMemcpy grammar RNG output");
    check_cuda(cudaFree(device_output), "cudaFree grammar RNG output");
    return output;
  } catch (...) {
    if (device_output != nullptr) cudaFree(device_output);
    throw;
  }
}

void check_equal(const std::vector<std::uint64_t>& device,
                 const std::vector<std::uint64_t>& host,
                 const std::string& description) {
  check(device.size() == host.size(), description + ": result size differs");
  for (std::size_t i = 0; i < host.size(); ++i) {
    check(device[i] == host[i], description + ": mismatch at sample " +
                                      std::to_string(i));
  }
}

std::int64_t signed_bits(std::uint64_t bits) {
  constexpr std::uint64_t sign = UINT64_C(1) << 63;
  if (bits < sign) return static_cast<std::int64_t>(bits);
  constexpr std::int64_t lowest =
      -INT64_C(9223372036854775807) - INT64_C(1);
  return lowest + static_cast<std::int64_t>(bits - sign);
}

void test_next() {
  for (const std::uint64_t seed : {
           UINT64_C(0), UINT64_C(1), UINT64_C(42),
           UINT64_C(0x0123456789abcdef), UINT64_MAX}) {
    GrammarRandom random(seed);
    std::vector<std::uint64_t> expected(kSamples);
    for (auto& value : expected) value = random.next();
    check_equal(run_device(seed, Operation::Next), expected,
                "next seed " + std::to_string(seed));
  }
}

void test_bounded() {
  // 2^63 + 1 rejects almost half of all draws, so equality also verifies the
  // rejection loop's state consumption rather than only range reduction.
  constexpr std::uint64_t rejection_prone =
      (UINT64_C(1) << 63) + UINT64_C(1);
  for (const std::uint64_t seed : {
           UINT64_C(0), UINT64_C(42), UINT64_C(0xfedcba9876543210)}) {
    for (const std::uint64_t bound : {
             UINT64_C(1), UINT64_C(3), UINT64_C(10), rejection_prone,
             UINT64_MAX}) {
      GrammarRandom random(seed);
      GrammarRandom no_rejection(seed);
      std::vector<std::uint64_t> expected(kSamples);
      bool observed_rejection = false;
      for (auto& value : expected) {
        value = random.bounded(bound);
        observed_rejection |= value != no_rejection.next() % bound;
      }
      const auto actual = run_device(seed, Operation::Bounded, bound);
      check_equal(actual, expected, "bounded seed " + std::to_string(seed) +
                                        " bound " + std::to_string(bound));
      for (const auto value : actual) {
        check(value < bound, "bounded result escaped its exclusive upper bound");
      }
      if (bound == rejection_prone) {
        check(observed_rejection,
              "rejection-prone fixture did not exercise rejection sampling");
      }
    }
  }
}

void test_integer() {
  constexpr std::int64_t lowest = std::numeric_limits<std::int64_t>::min();
  constexpr std::int64_t highest = std::numeric_limits<std::int64_t>::max();
  struct Range {
    std::int64_t minimum;
    std::int64_t maximum;
    const char* name;
  };
  const Range ranges[] = {
      {lowest, highest, "full width"},
      {lowest, lowest, "minimum singleton"},
      {highest, highest, "maximum singleton"},
      {-1, highest, "rejection-prone signed width"},
      {-17, 23, "cross-zero"},
  };

  for (const std::uint64_t seed : {
           UINT64_C(0), UINT64_C(42), UINT64_C(0x8000000000000000),
           UINT64_MAX}) {
    for (const auto& range : ranges) {
      GrammarRandom random(seed);
      std::vector<std::uint64_t> expected(kSamples);
      for (auto& value : expected) {
        value = static_cast<std::uint64_t>(
            random.integer(range.minimum, range.maximum));
      }
      const auto actual = run_device(seed, Operation::Integer, 1,
                                     range.minimum, range.maximum);
      check_equal(actual, expected, std::string("integer ") + range.name +
                                        " seed " + std::to_string(seed));
      for (const auto bits : actual) {
        const auto value = signed_bits(bits);
        check(value >= range.minimum && value <= range.maximum,
              std::string("integer result escaped ") + range.name);
      }
    }
  }
}

}  // namespace

int main() {
  try {
    // Reuse production device selection, including GAGP_CUDA_DEVICE override.
    gagp::FitnessSessionGpu session;
    const auto initialized =
        session.init({{}}, {gagp::Value::from_int(0)}, 1, 1, 1.0);
    check(initialized.ok,
          "could not initialize CUDA device for grammar RNG test");
    test_next();
    test_bounded();
    test_integer();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "device grammar RNG matches host next, bounded, and integer sequences\n";
  return 0;
}
