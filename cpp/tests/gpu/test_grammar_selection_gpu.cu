#include <cuda_runtime.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "../../src/evolution/repro/gpu/device/selection_kernels.cuh"

namespace {

using gagp::evo::RType;
using gagp::evo::repro::CandidateRange;
using gagp::evo::repro::PackedSelectionCounters;
using gagp::evo::repro::PackedProgramMeta;
using gagp::evo::repro::ReproductionContractMode;

void check_cuda(cudaError_t status, const char* operation) {
  if (status == cudaSuccess) return;
  throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorString(status));
}

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

__global__ void choose_pair_kernel(const CandidateRange* candidates,
                                   int candidates_per_program,
                                   const PackedProgramMeta* metas,
                                   ReproductionContractMode mode,
                                   int* result,
                                   PackedSelectionCounters* counters) {
  if (blockIdx.x != 0 || threadIdx.x != 0) return;
  gagp::evo::repro::d_choose_typed_candidate_pair(
      candidates, candidates_per_program, 0, 1, 0x123456789abcdef0ULL,
      &result[0], &result[1], mode, metas, counters);
}

CandidateRange compiled_candidate(std::uint32_t compatibility_id) {
  CandidateRange out;
  out.start = 0;
  out.stop = 3;
  out.aux = static_cast<int>(RType::Int);
  out.compatibility_id = compatibility_id;
  out.replacement_max_nodes = 3;
  out.replacement_max_depth = 2;
  out.remaining_template_nesting = 1;
  out.materialized_nodes = 3;
  out.materialized_depth = 2;
  out.template_nesting = 1;
  return out;
}

std::pair<int, int> choose_pair(const std::vector<CandidateRange>& candidates,
                                int candidates_per_program,
                                int candidate_count_a,
                                int candidate_count_b,
                                ReproductionContractMode mode,
                                PackedSelectionCounters* counters_out = nullptr) {
  check(candidates.size() == static_cast<std::size_t>(2 * candidates_per_program),
        "candidate fixture has the wrong padded size");
  PackedProgramMeta metas[2];
  metas[0].candidate_count = candidate_count_a;
  metas[1].candidate_count = candidate_count_b;

  CandidateRange* device_candidates = nullptr;
  PackedProgramMeta* device_metas = nullptr;
  int* device_result = nullptr;
  PackedSelectionCounters* device_counters = nullptr;
  check_cuda(cudaMalloc(reinterpret_cast<void**>(&device_candidates),
                        sizeof(CandidateRange) * candidates.size()),
             "cudaMalloc candidates");
  try {
    check_cuda(cudaMalloc(reinterpret_cast<void**>(&device_metas), sizeof(metas)),
               "cudaMalloc metas");
    check_cuda(cudaMalloc(reinterpret_cast<void**>(&device_result), 2 * sizeof(int)),
               "cudaMalloc result");
    if (counters_out != nullptr) {
      check_cuda(cudaMalloc(reinterpret_cast<void**>(&device_counters), sizeof(*device_counters)),
                 "cudaMalloc counters");
      check_cuda(cudaMemcpy(device_counters, counters_out, sizeof(*counters_out),
                            cudaMemcpyHostToDevice),
                 "cudaMemcpy initial counters");
    }
    check_cuda(cudaMemcpy(device_candidates, candidates.data(),
                          sizeof(CandidateRange) * candidates.size(),
                          cudaMemcpyHostToDevice),
               "cudaMemcpy candidates");
    check_cuda(cudaMemcpy(device_metas, metas, sizeof(metas), cudaMemcpyHostToDevice),
               "cudaMemcpy metas");
    choose_pair_kernel<<<1, 1>>>(device_candidates, candidates_per_program,
                                 device_metas, mode, device_result, device_counters);
    check_cuda(cudaGetLastError(), "choose_pair_kernel launch");
    int result[2] = {-2, -2};
    check_cuda(cudaMemcpy(result, device_result, sizeof(result), cudaMemcpyDeviceToHost),
               "cudaMemcpy result");
    if (counters_out != nullptr) {
      check_cuda(cudaMemcpy(counters_out, device_counters, sizeof(*counters_out),
                            cudaMemcpyDeviceToHost),
                 "cudaMemcpy counters");
      check_cuda(cudaFree(device_counters), "cudaFree counters");
      device_counters = nullptr;
    }
    check_cuda(cudaFree(device_result), "cudaFree result");
    device_result = nullptr;
    check_cuda(cudaFree(device_metas), "cudaFree metas");
    device_metas = nullptr;
    check_cuda(cudaFree(device_candidates), "cudaFree candidates");
    device_candidates = nullptr;
    return {result[0], result[1]};
  } catch (...) {
    if (device_counters != nullptr) cudaFree(device_counters);
    if (device_result != nullptr) cudaFree(device_result);
    if (device_metas != nullptr) cudaFree(device_metas);
    if (device_candidates != nullptr) cudaFree(device_candidates);
    throw;
  }
}

__global__ void permutation_kernel(int count, int* indices) {
  const int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
  if (i < count) indices[i] = gagp::evo::repro::d_permuted_index_for_round(count, i, 1);
}

void test_large_tournament_permutation() {
  // Seed 1 selects stride 53787: its largest index product exceeds INT_MAX.
  constexpr int count = 65536;
  std::vector<int> indices(count);
  int* device_indices = nullptr;
  check_cuda(cudaMalloc(&device_indices, sizeof(int) * count), "cudaMalloc permutation");
  try {
    permutation_kernel<<<count / 128, 128>>>(count, device_indices);
    check_cuda(cudaGetLastError(), "permutation launch");
    check_cuda(cudaMemcpy(indices.data(), device_indices, sizeof(int) * count,
                          cudaMemcpyDeviceToHost), "permutation copyback");
    check_cuda(cudaFree(device_indices), "cudaFree permutation");
    device_indices = nullptr;
  } catch (...) {
    if (device_indices) cudaFree(device_indices);
    throw;
  }
  std::sort(indices.begin(), indices.end());
  for (int i = 0; i < count; ++i)
    check(indices[i] == i, "large tournament permutation overflowed or repeated a parent");
}

void test_compiled_contract_ids_and_budgets() {
  constexpr int capacity = 1;
  auto left = compiled_candidate(7);
  auto right = compiled_candidate(8);
  PackedSelectionCounters counters{91, 92};
  check(choose_pair({left, right}, capacity, 1, 1,
                    ReproductionContractMode::CompiledGrammar, &counters) ==
            std::make_pair(-1, -1),
        "compiled selection accepted different compatibility IDs");
  check(counters.contract_rejections == 1 && counters.budget_rejections == 0,
        "compiled selection miscounted an incompatible ID pair");

  right.compatibility_id = left.compatibility_id;
  left.replacement_max_nodes = right.materialized_nodes - 1;
  counters = {91, 92};
  check(choose_pair({left, right}, capacity, 1, 1,
                    ReproductionContractMode::CompiledGrammar, &counters) ==
            std::make_pair(-1, -1),
        "compiled selection accepted an asymmetric destination budget");
  check(counters.contract_rejections == 0 && counters.budget_rejections == 1,
        "compiled selection miscounted a reciprocal budget failure");

  left.replacement_max_nodes = right.materialized_nodes;
  left.replacement_max_depth = right.materialized_depth - 1;
  counters = {91, 92};
  check(choose_pair({left, right}, capacity, 1, 1,
                    ReproductionContractMode::CompiledGrammar, &counters) ==
            std::make_pair(-1, -1),
        "compiled selection accepted an asymmetric depth budget");
  check(counters.contract_rejections == 0 && counters.budget_rejections == 1,
        "compiled selection miscounted a reciprocal depth failure");

  left.replacement_max_depth = right.materialized_depth;
  left.remaining_template_nesting = right.template_nesting - 1;
  counters = {91, 92};
  check(choose_pair({left, right}, capacity, 1, 1,
                    ReproductionContractMode::CompiledGrammar, &counters) ==
            std::make_pair(-1, -1),
        "compiled selection accepted an asymmetric nesting budget");
  check(counters.contract_rejections == 0 && counters.budget_rejections == 1,
        "compiled selection miscounted a reciprocal nesting failure");

  left.remaining_template_nesting = right.template_nesting;
  right.replacement_max_nodes = left.materialized_nodes;
  right.replacement_max_depth = left.materialized_depth;
  right.remaining_template_nesting = left.template_nesting;
  counters = {91, 92};
  check(choose_pair({left, right}, capacity, 1, 1,
                    ReproductionContractMode::CompiledGrammar, &counters) ==
            std::make_pair(0, 0),
        "compiled selection rejected exact reciprocal budget boundaries");
  check(counters.contract_rejections == 0 && counters.budget_rejections == 0,
        "compiled selection counted a compatible pair");

  left.stop = left.start;
  counters = {91, 92};
  check(choose_pair({left, right}, capacity, 1, 1,
                    ReproductionContractMode::CompiledGrammar, &counters) ==
            std::make_pair(-1, -1),
        "compiled selection accepted an invalid actual candidate");
  check(counters.contract_rejections == 1 && counters.budget_rejections == 0,
        "compiled selection miscounted an invalid actual candidate");

  left = compiled_candidate(gagp::evo::repro::kNoCompatibilityId);
  counters = {91, 92};
  check(choose_pair({left, right}, capacity, 1, 1,
                    ReproductionContractMode::CompiledGrammar, &counters) ==
            std::make_pair(-1, -1),
        "compiled selection accepted the compatibility sentinel");
  check(counters.contract_rejections == 1 && counters.budget_rejections == 0,
        "compiled selection miscounted the compatibility sentinel");
}

void test_compiled_candidate_counts() {
  constexpr int capacity = 2;
  auto incompatible_a = compiled_candidate(1);
  auto incompatible_b = compiled_candidate(2);
  auto padding_a = compiled_candidate(9);
  auto padding_b = compiled_candidate(9);
  const std::vector<CandidateRange> candidates = {
      incompatible_a, padding_a, incompatible_b, padding_b};
  PackedSelectionCounters counters{91, 92};
  check(choose_pair(candidates, capacity, 1, 1,
                    ReproductionContractMode::CompiledGrammar, &counters) ==
            std::make_pair(-1, -1),
        "compiled selection inspected padding beyond actual candidate counts");
  check(counters.contract_rejections == 1 && counters.budget_rejections == 0,
        "compiled selection counted padded candidates");
  counters = {91, 92};
  check(choose_pair(candidates, capacity, 0, 1,
                    ReproductionContractMode::CompiledGrammar, &counters) ==
            std::make_pair(-1, -1),
        "compiled selection did not report an empty candidate table");
  check(counters.contract_rejections == 0 && counters.budget_rejections == 0,
        "compiled selection counted outside an empty actual candidate table");

  auto valid_a = compiled_candidate(7);
  auto other_a = compiled_candidate(8);
  auto valid_b = compiled_candidate(7);
  auto other_b = compiled_candidate(9);
  counters = {91, 92};
  check(choose_pair({valid_a, other_a, valid_b, other_b}, capacity, 2, 2,
                    ReproductionContractMode::CompiledGrammar, &counters) ==
            std::make_pair(0, 0),
        "compiled selection did not choose its sole compatible pair");
  check(counters.contract_rejections == 3 && counters.budget_rejections == 0,
        "compiled selection double-counted the rank scan");
}

std::vector<PackedSelectionCounters> tournament_counter_rows(
    ReproductionContractMode mode) {
  constexpr int population_size = 2;
  constexpr int pair_count = 3;
  constexpr int candidates_per_program = 2;
  auto invalid = compiled_candidate(7);
  invalid.stop = invalid.start;
  const auto valid = compiled_candidate(7);
  const std::vector<CandidateRange> candidates = {invalid, valid, invalid, valid};
  const double fitness[population_size] = {1.0, 0.0};
  PackedProgramMeta metas[population_size];
  metas[0].candidate_count = candidates_per_program;
  metas[1].candidate_count = candidates_per_program;
  std::vector<PackedSelectionCounters> counters(
      pair_count, PackedSelectionCounters{91, 92});

  double* device_fitness = nullptr;
  CandidateRange* device_candidates = nullptr;
  PackedProgramMeta* device_metas = nullptr;
  int* device_indices = nullptr;
  PackedSelectionCounters* device_counters = nullptr;
  try {
    check_cuda(cudaMalloc(reinterpret_cast<void**>(&device_fitness), sizeof(fitness)),
               "cudaMalloc tournament fitness");
    check_cuda(cudaMalloc(reinterpret_cast<void**>(&device_candidates),
                          sizeof(CandidateRange) * candidates.size()),
               "cudaMalloc tournament candidates");
    check_cuda(cudaMalloc(reinterpret_cast<void**>(&device_metas), sizeof(metas)),
               "cudaMalloc tournament metas");
    check_cuda(cudaMalloc(reinterpret_cast<void**>(&device_indices),
                          4 * pair_count * sizeof(int)),
               "cudaMalloc tournament indices");
    check_cuda(cudaMalloc(reinterpret_cast<void**>(&device_counters),
                          pair_count * sizeof(PackedSelectionCounters)),
               "cudaMalloc tournament counters");
    check_cuda(cudaMemcpy(device_fitness, fitness, sizeof(fitness), cudaMemcpyHostToDevice),
               "cudaMemcpy tournament fitness");
    check_cuda(cudaMemcpy(device_candidates, candidates.data(),
                          sizeof(CandidateRange) * candidates.size(), cudaMemcpyHostToDevice),
               "cudaMemcpy tournament candidates");
    check_cuda(cudaMemcpy(device_metas, metas, sizeof(metas), cudaMemcpyHostToDevice),
               "cudaMemcpy tournament metas");
    check_cuda(cudaMemcpy(device_counters, counters.data(),
                          pair_count * sizeof(PackedSelectionCounters), cudaMemcpyHostToDevice),
               "cudaMemcpy initial tournament counters");

    gagp::evo::repro::tournament_select_kernel<<<1, pair_count>>>(
        device_fitness, device_candidates, population_size, pair_count,
        candidates_per_program, 1, 23, device_indices,
        device_indices + pair_count, device_indices + 2 * pair_count,
        device_indices + 3 * pair_count, mode, device_metas, device_counters);
    check_cuda(cudaGetLastError(), "tournament_select_kernel launch");
    check_cuda(cudaMemcpy(counters.data(), device_counters,
                          pair_count * sizeof(PackedSelectionCounters), cudaMemcpyDeviceToHost),
               "cudaMemcpy tournament counters");

    check_cuda(cudaFree(device_counters), "cudaFree tournament counters");
    device_counters = nullptr;
    check_cuda(cudaFree(device_indices), "cudaFree tournament indices");
    device_indices = nullptr;
    check_cuda(cudaFree(device_metas), "cudaFree tournament metas");
    device_metas = nullptr;
    check_cuda(cudaFree(device_candidates), "cudaFree tournament candidates");
    device_candidates = nullptr;
    check_cuda(cudaFree(device_fitness), "cudaFree tournament fitness");
    device_fitness = nullptr;
  } catch (...) {
    if (device_counters != nullptr) cudaFree(device_counters);
    if (device_indices != nullptr) cudaFree(device_indices);
    if (device_metas != nullptr) cudaFree(device_metas);
    if (device_candidates != nullptr) cudaFree(device_candidates);
    if (device_fitness != nullptr) cudaFree(device_fitness);
    throw;
  }
  return counters;
}

void test_tournament_counter_rows() {
  const auto compiled = tournament_counter_rows(ReproductionContractMode::CompiledGrammar);
  for (const auto& row : compiled) {
    check(row.contract_rejections == 3 && row.budget_rejections == 0,
          "compiled tournament did not write one exact counter row per pair");
  }
  const auto legacy = tournament_counter_rows(ReproductionContractMode::Legacy);
  for (const auto& row : legacy) {
    check(row.contract_rejections == 0 && row.budget_rejections == 0,
          "legacy tournament did not zero one counter row per pair");
  }
}

void test_legacy_keys_are_unchanged() {
  auto left = compiled_candidate(1);
  auto right = compiled_candidate(2);
  left.replacement_max_nodes = 0;
  right.replacement_max_nodes = 0;
  left.scope_signature = right.scope_signature = 9;
  auto different_a = left;
  auto different_b = right;
  different_a.scope_signature = 1;
  different_b.scope_signature = 2;
  // Only pair (1,1) has matching legacy keys. The no-match sentinel is (0,0),
  // so this also proves a real match rather than merely the legacy default.
  PackedSelectionCounters counters{91, 92};
  check(choose_pair({different_a, left, different_b, right}, 2, 0, 0,
                    ReproductionContractMode::Legacy, &counters) == std::make_pair(1, 1),
        "legacy selection stopped using its structural compatibility key");
  check(counters.contract_rejections == 0 && counters.budget_rejections == 0,
        "legacy selection did not zero supplied counters");
}

}  // namespace

int main() {
  try {
    // Reuse production least-used/override device selection without launching
    // fitness; subsequent direct selection probes run on that same device.
    gagp::FitnessSessionGpu session;
    const auto initialized = session.init({{}}, {gagp::Value::from_int(0)}, 1, 1, 1.0);
    check(initialized.ok, "could not initialize CUDA device for selection test");
    test_large_tournament_permutation();
    test_compiled_contract_ids_and_budgets();
    test_compiled_candidate_counts();
    test_tournament_counter_rows();
    test_legacy_keys_are_unchanged();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "compiled grammar GPU selection: IDs, budgets, counts, and legacy keys passed\n";
  return 0;
}
