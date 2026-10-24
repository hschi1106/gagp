// Isolated CUDA storage oracle. This does not execute or benchmark the VM.
#include "private_window_frames_gpu.hpp"
#include <cuda_runtime.h>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace gagp;
using namespace gagp::gpu_detail;
constexpr unsigned kThreads = 32;
constexpr unsigned kDepth = 4;
constexpr unsigned kGuardWords = 32;

void checked(cudaError_t error, const char* operation) {
  if (error != cudaSuccess) throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorString(error));
}
void select_device() {
  int count = 0;
  checked(cudaGetDeviceCount(&count), "device count");
  if (!count) throw std::runtime_error("No CUDA device");
  int selected = -1;
  if (const char* requested = std::getenv("GAGP_CUDA_DEVICE")) {
    char* end = nullptr;
    const auto index = std::strtol(requested, &end, 10);
    if (!end || end == requested || *end || index < 0 || index >= count) throw std::runtime_error("Invalid GAGP_CUDA_DEVICE");
    selected = static_cast<int>(index);
  } else {
    std::size_t least_used = std::numeric_limits<std::size_t>::max();
    for (int index = 0; index < count; ++index) {
      checked(cudaSetDevice(index), "probe device");
      std::size_t free = 0, total = 0;
      checked(cudaMemGetInfo(&free, &total), "probe memory");
      if (total - free < least_used) {least_used = total - free; selected = index;}
    }
  }
  checked(cudaSetDevice(selected), "select device");
}

__device__ bool claim_word(unsigned* owners, std::int64_t* base, std::int64_t* word,
    std::size_t word_count, unsigned owner, unsigned* errors) {
  const auto index = word - base;
  if (index < 0 || static_cast<std::size_t>(index) >= word_count) {atomicAdd(errors, 1u); return false;}
  const auto previous = atomicCAS(owners + index, 0u, owner);
  if (previous && previous != owner) {atomicAdd(errors, 1u); return false;}
  return true;
}

template<unsigned States, bool SoA>
__global__ void storage_kernel(std::int64_t* scratch, const Value* input, Value* output,
    unsigned* errors, unsigned* owners, std::size_t body_words, unsigned count, unsigned epoch) {
  const unsigned index = blockIdx.x * blockDim.x + threadIdx.x;
  if (index >= count) return;
  constexpr unsigned fields = States + 3;
  for (unsigned depth = 0; depth < kDepth; ++depth) {
    auto frame = [&]() {
      if constexpr (SoA) {
        auto* base = scratch + static_cast<std::size_t>(blockIdx.x) * blockDim.x *
            kDepth * (States + 4) + threadIdx.x;
        return packed_window_soa_view<States>(base, depth * blockDim.x, blockDim.x);
      } else {
        auto* storage = reinterpret_cast<PackedPrivateWindowFrameStorage<States>*>(scratch);
        return packed_window_aos_view(storage[blockIdx.x * blockDim.x * kDepth + depth * blockDim.x + threadIdx.x]);
      }
    }();
    const auto owner = (index * kDepth + depth) * (fields + 1) + 1;
    if (reinterpret_cast<char*>(&frame.next_request) - reinterpret_cast<char*>(&frame.tags) != 4) {
      atomicAdd(errors, 1u); return;
    }
    if (!claim_word(owners, scratch, reinterpret_cast<std::int64_t*>(&frame.tags), body_words,
                    owner + fields, errors)) return;
    for (unsigned state = 0; state < States; ++state)
      if (!claim_word(owners, scratch, &frame.state[state].i, body_words, owner + state, errors)) return;
    if (!claim_word(owners, scratch, &frame.prepared[0].i, body_words, owner + States, errors)) return;
    for (unsigned result = 0; result < 2; ++result)
      if (!claim_word(owners, scratch, &frame.results[result].i, body_words, owner + States + 1 + result, errors)) return;
    frame.initialize_tags();
    Value value = input[index];
    if (epoch) value = packed_window_restore(packed_window_value_bits(value) ^ 1,
                                            value.tag, !value.b);
    frame.state[0] = value;
    if constexpr (States == 2) frame.state[1] = Value::from_int(static_cast<std::int64_t>(index) - depth);
    frame.prepared[0] = Value::from_int(static_cast<std::int64_t>(depth) * 100 + epoch);
    frame.results[0] = Value::from_fallback_token((static_cast<std::int64_t>(index) << 40) | depth);
    frame.results[1] = value;
    frame.next_request = -1;
    if (frame.state[0].tag != value.tag || static_cast<bool>(frame.state[0].b) != value.b ||
        frame.next_request++ != -1 || frame.next_request != 0) atomicAdd(errors, 1u);
    const auto offset = (static_cast<std::size_t>(index) * kDepth + depth) * fields;
    for (unsigned state = 0; state < States; ++state) output[offset + state] = frame.state[state];
    output[offset + States] = frame.prepared[0];
    output[offset + States + 1] = frame.results[0];
    output[offset + States + 2] = frame.results[1];
  }
}

struct DeviceMemory {
  void* pointer = nullptr;
  explicit DeviceMemory(std::size_t bytes) {checked(cudaMalloc(&pointer, bytes), "allocate");}
  ~DeviceMemory() {if (pointer) cudaFree(pointer);}
  template<class T> T* as() {return static_cast<T*>(pointer);}
};
void same(Value actual, Value expected) {
  if (actual.tag != expected.tag || actual.b != expected.b ||
      packed_window_value_bits(actual) != packed_window_value_bits(expected))
    throw std::runtime_error("CUDA storage changed payload/tag/bool");
}

template<unsigned States, bool SoA> void run() {
  constexpr unsigned count = 53; // Inactive lanes in the final warp must remain independent.
  constexpr unsigned allocated = ((count + kThreads - 1) / kThreads) * kThreads;
  constexpr unsigned fields = States + 3;
  constexpr auto body_words = static_cast<std::size_t>(allocated) * kDepth * (States + 4);
  const std::array<std::int64_t, 8> patterns{0, -1, std::numeric_limits<std::int64_t>::min(),
      std::numeric_limits<std::int64_t>::max(), 0x7ff0000000000000LL, 0x7ff8000000000001LL,
      0x7ff0000000000001LL, static_cast<std::int64_t>(0xfff8000000000001ULL)};
  std::vector<Value> input;
  constexpr unsigned tag_count = static_cast<unsigned>(ValueTag::Invalid) + 1;
  for (unsigned index = 0; index < count; ++index) {
    // Decouple tag/payload cycles: Float inputs include Inf, quiet/signaling
    // NaNs, negative NaNs and zero; epoch1 also checks subnormal bit patterns.
    const auto pattern = patterns[(index / tag_count + 4) % patterns.size()];
    input.push_back(packed_window_restore(pattern, static_cast<ValueTag>(index % tag_count), index & 1));
  }
  DeviceMemory scratch((body_words + 2 * kGuardWords) * sizeof(std::int64_t));
  DeviceMemory inputs(input.size() * sizeof(Value));
  DeviceMemory outputs(count * kDepth * fields * sizeof(Value));
  DeviceMemory errors(sizeof(unsigned));
  DeviceMemory owners(body_words * sizeof(unsigned));
  checked(cudaMemset(scratch.pointer, 0x5a, (body_words + 2 * kGuardWords) * sizeof(std::int64_t)), "guard initialization");
  auto* body = scratch.as<std::int64_t>() + kGuardWords;
  checked(cudaMemset(body, 0xa5, body_words * sizeof(std::int64_t)), "dirty storage");
  checked(cudaMemcpy(inputs.pointer, input.data(), input.size() * sizeof(Value), cudaMemcpyHostToDevice), "inputs");
  for (unsigned epoch = 0; epoch < 2; ++epoch) {
    // Keep all previous frame bytes for epoch1; only original ordered writes may be relied on.
    checked(cudaMemset(errors.pointer, 0, sizeof(unsigned)), "error reset");
    checked(cudaMemset(owners.pointer, 0, body_words * sizeof(unsigned)), "owner reset");
    storage_kernel<States, SoA><<<allocated / kThreads, kThreads>>>(body, inputs.as<Value>(), outputs.as<Value>(),
        errors.as<unsigned>(), owners.as<unsigned>(), body_words, count, epoch);
    checked(cudaGetLastError(), "storage launch");
    checked(cudaDeviceSynchronize(), "storage synchronization");
    unsigned error_count = 0;
    checked(cudaMemcpy(&error_count, errors.pointer, sizeof(error_count), cudaMemcpyDeviceToHost), "errors");
    if (error_count) throw std::runtime_error("CUDA field/counter/word-ownership mismatch");
    std::vector<unsigned> observed_owners(body_words);
    checked(cudaMemcpy(observed_owners.data(), owners.pointer, body_words * sizeof(unsigned), cudaMemcpyDeviceToHost), "owners");
    std::size_t claimed = 0;
    for (const auto owner : observed_owners) claimed += owner != 0;
    if (claimed != static_cast<std::size_t>(count) * kDepth * (fields + 1))
      throw std::runtime_error("CUDA frame fields did not occupy distinct words");
    std::vector<Value> result(count * kDepth * fields);
    checked(cudaMemcpy(result.data(), outputs.pointer, result.size() * sizeof(Value), cudaMemcpyDeviceToHost), "results");
    for (unsigned index = 0; index < count; ++index) for (unsigned depth = 0; depth < kDepth; ++depth) {
      auto value = input[index];
      if (epoch) value = packed_window_restore(packed_window_value_bits(value) ^ 1, value.tag, !value.b);
      const auto offset = (static_cast<std::size_t>(index) * kDepth + depth) * fields;
      same(result[offset], value);
      if constexpr (States == 2) same(result[offset + 1], Value::from_int(static_cast<std::int64_t>(index) - depth));
      same(result[offset + States], Value::from_int(depth * 100 + epoch));
      same(result[offset + States + 1], Value::from_fallback_token((static_cast<std::int64_t>(index) << 40) | depth));
      same(result[offset + States + 2], value);
    }
    std::array<std::int64_t, kGuardWords> guard;
    for (const auto offset : {std::size_t{0}, body_words + kGuardWords}) {
      checked(cudaMemcpy(guard.data(), scratch.as<std::int64_t>() + offset, sizeof(guard), cudaMemcpyDeviceToHost), "guards");
      for (const auto word : guard) if (word != 0x5a5a5a5a5a5a5a5aLL)
        throw std::runtime_error("CUDA frame address exceeded its allocation");
    }
  }
}
int main() {
  try {
    select_device();
    run<1, false>(); run<1, true>(); run<2, false>(); run<2, true>();
    std::cout << "Packed private-window CUDA storage oracle passed: AoS/SoA,1/2states,53lanes,2dirty epochs\n";
    return 0;
  } catch (const std::exception& error) {std::cerr << error.what() << '\n'; return 1;}
}
