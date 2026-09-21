#include <cuda_runtime.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "../../src/evolution/repro/gpu/device/compiled_variation.cuh"

namespace {

using gagp::Value;
using gagp::evo::NodeKind;
using gagp::evo::RType;
using namespace gagp::evo::repro;

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void check_cuda(cudaError_t status, const char* operation) {
  if (status == cudaSuccess) return;
  throw std::runtime_error(std::string(operation) + ": " +
                           cudaGetErrorString(status));
}

template <class T>
class Managed {
 public:
  explicit Managed(std::size_t count) : count_(count) {
    check_cuda(cudaMallocManaged(reinterpret_cast<void**>(&data_),
                                 sizeof(T) * count),
               "cudaMallocManaged");
  }
  ~Managed() { cudaFree(data_); }
  Managed(const Managed&) = delete;
  Managed& operator=(const Managed&) = delete;
  T* data() { return data_; }
  T& operator[](std::size_t index) { return data_[index]; }
  const T& operator[](std::size_t index) const { return data_[index]; }

 private:
  T* data_ = nullptr;
  std::size_t count_ = 0;
};

PlainNode node(NodeKind kind, int i0 = 0) {
  return {static_cast<int>(kind), i0, 0};
}

CandidateOccurrence occurrence(int start, int stop) {
  CandidateOccurrence out;
  out.start = start;
  out.stop = stop;
  return out;
}

CandidateRange candidate(int start, int stop, std::uint32_t compatibility,
                         int occurrence_offset, int occurrence_count) {
  CandidateRange out;
  out.start = start;
  out.stop = stop;
  out.aux = static_cast<int>(RType::Int);
  out.compatibility_id = compatibility;
  out.occurrence_offset = occurrence_offset;
  out.occurrence_count = occurrence_count;
  out.replacement_max_nodes = 8;
  out.replacement_max_depth = 8;
  out.remaining_template_nesting = 8;
  out.materialized_nodes = stop - start;
  out.materialized_depth = 1;
  out.template_nesting = 0;
  return out;
}

struct Fixture {
  int max_nodes = 4;
  int max_names = 3;
  int max_consts = 3;
  int candidates_per_program = 2;
  std::vector<PlainNode> nodes;
  PackedProgramMeta metas[2];
  std::vector<CandidateRange> candidates;
  std::vector<CandidateOccurrence> occurrences;
  std::vector<std::uint64_t> names;
  std::vector<Value> consts;
  int parent_a = 0;
  int parent_b = 1;
  int cand_a = 0;
  int cand_b = 0;
};

Fixture basic_fixture() {
  Fixture fixture;
  fixture.nodes.assign(2 * fixture.max_nodes, node(NodeKind::CONST));
  fixture.nodes[0] = node(NodeKind::CONST, 0);
  fixture.nodes[1] = node(NodeKind::VAR, 0);
  fixture.nodes[fixture.max_nodes] = node(NodeKind::VAR, 0);
  fixture.nodes[fixture.max_nodes + 1] = node(NodeKind::CONST, 0);
  fixture.metas[0].used_len = fixture.metas[1].used_len = 2;
  fixture.metas[0].name_count = fixture.metas[1].name_count = 1;
  fixture.metas[0].const_count = fixture.metas[1].const_count = 1;
  fixture.metas[0].candidate_count = fixture.metas[1].candidate_count = 1;
  fixture.candidates.assign(2 * fixture.candidates_per_program,
                            CandidateRange{});
  fixture.candidates[0] = candidate(0, 1, 7, 0, 1);
  fixture.candidates[fixture.candidates_per_program] =
      candidate(0, 1, 7, 1, 1);
  fixture.occurrences = {occurrence(0, 1), occurrence(0, 1)};
  fixture.names.assign(2 * fixture.max_names, 0);
  fixture.names[0] = 10;
  fixture.names[fixture.max_names] = 20;
  fixture.consts.assign(2 * fixture.max_consts, Value::from_int(-1));
  fixture.consts[0] = Value::from_int(1);
  fixture.consts[fixture.max_consts] = Value::from_int(2);
  return fixture;
}

struct Result {
  std::vector<PlainNode> nodes;
  int used_len[2] = {};
  std::vector<std::uint64_t> names;
  int name_count[2] = {};
  std::vector<Value> consts;
  int const_count[2] = {};
  PackedChildMeta meta[2];
  PackedChildSplice splice[2];
};

Result run(const Fixture& fixture) {
  const std::size_t node_slots = static_cast<std::size_t>(2 * fixture.max_nodes);
  const std::size_t name_slots = static_cast<std::size_t>(2 * fixture.max_names);
  const std::size_t const_slots = static_cast<std::size_t>(2 * fixture.max_consts);
  Managed<PlainNode> d_nodes(node_slots), d_child_nodes(node_slots);
  Managed<PackedProgramMeta> d_metas(2);
  Managed<CandidateRange> d_candidates(fixture.candidates.size());
  Managed<CandidateOccurrence> d_occurrences(fixture.occurrences.size());
  Managed<std::uint64_t> d_names(name_slots), d_child_names(name_slots);
  Managed<Value> d_consts(const_slots), d_child_consts(const_slots);
  Managed<int> d_parent_a(1), d_parent_b(1), d_cand_a(1), d_cand_b(1);
  Managed<int> d_used_len(2), d_name_count(2), d_const_count(2);
  Managed<PackedChildMeta> d_meta(2);
  Managed<PackedChildSplice> d_splice(2);

  for (std::size_t i = 0; i < node_slots; ++i) d_nodes[i] = fixture.nodes[i];
  for (int i = 0; i < 2; ++i) d_metas[i] = fixture.metas[i];
  for (std::size_t i = 0; i < fixture.candidates.size(); ++i)
    d_candidates[i] = fixture.candidates[i];
  for (std::size_t i = 0; i < fixture.occurrences.size(); ++i)
    d_occurrences[i] = fixture.occurrences[i];
  for (std::size_t i = 0; i < name_slots; ++i) d_names[i] = fixture.names[i];
  for (std::size_t i = 0; i < const_slots; ++i) d_consts[i] = fixture.consts[i];
  d_parent_a[0] = fixture.parent_a;
  d_parent_b[0] = fixture.parent_b;
  d_cand_a[0] = fixture.cand_a;
  d_cand_b[0] = fixture.cand_b;

  GpuReproConfig config;
  config.contract_mode = ReproductionContractMode::CompiledGrammar;
  config.population_size = 2;
  config.pair_count = 1;
  config.candidates_per_program = fixture.candidates_per_program;
  config.compiled_occurrence_count = static_cast<int>(fixture.occurrences.size());
  config.max_nodes = fixture.max_nodes;
  config.max_names = fixture.max_names;
  config.max_consts = fixture.max_consts;
  CompiledVariationPointers pointers;
  pointers.program_nodes = d_nodes.data();
  pointers.metas = d_metas.data();
  pointers.candidates = d_candidates.data();
  pointers.occurrences = d_occurrences.data();
  pointers.program_name_ids = d_names.data();
  pointers.program_consts = d_consts.data();
  pointers.parent_a = d_parent_a.data();
  pointers.parent_b = d_parent_b.data();
  pointers.cand_a = d_cand_a.data();
  pointers.cand_b = d_cand_b.data();
  pointers.child_nodes = d_child_nodes.data();
  pointers.child_used_len = d_used_len.data();
  pointers.child_name_ids = d_child_names.data();
  pointers.child_name_counts = d_name_count.data();
  pointers.child_consts = d_child_consts.data();
  pointers.child_const_counts = d_const_count.data();
  pointers.child_meta = d_meta.data();
  pointers.child_splices = d_splice.data();
  compiled_crossover_kernel<<<1, 2>>>(config, pointers);
  check_cuda(cudaGetLastError(), "compiled_crossover_kernel launch");
  check_cuda(cudaDeviceSynchronize(), "compiled_crossover_kernel execution");

  Result result;
  result.nodes.assign(d_child_nodes.data(), d_child_nodes.data() + node_slots);
  result.names.assign(d_child_names.data(), d_child_names.data() + name_slots);
  result.consts.assign(d_child_consts.data(), d_child_consts.data() + const_slots);
  for (int i = 0; i < 2; ++i) {
    result.used_len[i] = d_used_len[i];
    result.name_count[i] = d_name_count[i];
    result.const_count[i] = d_const_count[i];
    result.meta[i] = d_meta[i];
    result.splice[i] = d_splice[i];
  }
  return result;
}

void check_unchanged(const Fixture& fixture, const Result& result, int child,
                     int parent, const char* message) {
  check(result.used_len[child] == fixture.metas[parent].used_len &&
            result.name_count[child] == fixture.metas[parent].name_count &&
            result.const_count[child] == fixture.metas[parent].const_count,
        message);
  for (int i = 0; i < result.used_len[child]; ++i) {
    const auto actual = result.nodes[child * fixture.max_nodes + i];
    const auto expected = fixture.nodes[parent * fixture.max_nodes + i];
    check(actual.kind == expected.kind && actual.i0 == expected.i0 &&
              actual.i1 == expected.i1,
          message);
  }
  for (int i = 0; i < result.name_count[child]; ++i)
    check(result.names[child * fixture.max_names + i] ==
              fixture.names[parent * fixture.max_names + i], message);
  for (int i = 0; i < result.const_count[child]; ++i)
    check(result.consts[child * fixture.max_consts + i].i ==
              fixture.consts[parent * fixture.max_consts + i].i, message);
  check(result.splice[child].applied == 0 &&
            result.splice[child].base_parent == parent &&
            result.meta[child].valid == 1 &&
            result.meta[child].node_count == result.used_len[child] &&
            result.meta[child].max_depth == 0,
        message);
}

void test_paired_crossover_and_table_remap() {
  const Fixture fixture = basic_fixture();
  const Result result = run(fixture);
  check(result.splice[0].applied == 1 && result.splice[1].applied == 1 &&
            result.splice[0].base_parent == 0 &&
            result.splice[0].source_index == 1 &&
            result.splice[1].base_parent == 1 &&
            result.splice[1].source_index == 0,
        "paired crossover provenance was not reciprocal");
  check(result.used_len[0] == 2 && result.nodes[0].kind ==
            static_cast<int>(NodeKind::VAR) && result.nodes[0].i0 == 1 &&
            result.name_count[0] == 2 && result.names[1] == 20,
        "child A did not remap the selected parent B name");
  const int child_b = fixture.max_nodes;
  check(result.used_len[1] == 2 && result.nodes[child_b].kind ==
            static_cast<int>(NodeKind::CONST) && result.nodes[child_b].i0 == 1 &&
            result.const_count[1] == 2 &&
            result.consts[fixture.max_consts + 1].i == 1,
        "child B did not remap the selected parent A constant");
  check(result.meta[0].valid == 1 && result.meta[0].node_count == 2 &&
            result.meta[0].max_depth == 0 && result.meta[1].valid == 1 &&
            result.meta[1].node_count == 2,
        "compiled crossover did not publish bounded child metadata");
}

void test_repeated_occurrences() {
  Fixture fixture = basic_fixture();
  fixture.nodes[0] = node(NodeKind::CONST, 0);
  fixture.nodes[1] = node(NodeKind::CONST, 0);
  fixture.nodes[2] = node(NodeKind::CONST, 0);
  fixture.metas[0].used_len = 3;
  fixture.candidates[0] = candidate(0, 1, 7, 0, 2);
  fixture.candidates[fixture.candidates_per_program] =
      candidate(0, 1, 7, 2, 1);
  fixture.occurrences = {
      occurrence(0, 1), occurrence(2, 3), occurrence(0, 1)};
  const Result result = run(fixture);
  check(result.splice[0].applied == 1 &&
            result.splice[0].occurrence_count == 2 &&
            result.used_len[0] == 3 && result.name_count[0] == 2 &&
            result.nodes[0].i0 == 1 && result.nodes[2].i0 == 1,
        "compiled crossover did not replace every prepared occurrence");
}

void test_numeric_compatibility_and_budgets() {
  Fixture mismatch = basic_fixture();
  mismatch.candidates[mismatch.candidates_per_program].compatibility_id = 8;
  Result result = run(mismatch);
  check_unchanged(mismatch, result, 0, 0,
                  "numeric compatibility mismatch changed child A");
  check_unchanged(mismatch, result, 1, 1,
                  "numeric compatibility mismatch changed child B");

  Fixture budget = basic_fixture();
  budget.candidates[0].replacement_max_nodes = 0;
  result = run(budget);
  check_unchanged(budget, result, 0, 0,
                  "replacement budget violation changed child A");
  check_unchanged(budget, result, 1, 1,
                  "reciprocal replacement budget violation changed child B");
}

void test_capacity_failure_is_transactional() {
  Fixture fixture = basic_fixture();
  fixture.max_nodes = 3;
  fixture.nodes.assign(2 * fixture.max_nodes, node(NodeKind::CONST, 0));
  fixture.nodes[0] = node(NodeKind::CONST, 0);
  fixture.nodes[1] = node(NodeKind::VAR, 0);
  fixture.nodes[2] = node(NodeKind::CONST, 0);
  fixture.nodes[3] = node(NodeKind::VAR, 0);
  fixture.nodes[4] = node(NodeKind::CONST, 0);
  fixture.nodes[5] = node(NodeKind::CONST, 0);
  fixture.metas[0].used_len = fixture.metas[1].used_len = 3;
  fixture.candidates[0] = candidate(0, 1, 7, 0, 1);
  fixture.candidates[0].replacement_max_nodes = 2;
  fixture.candidates[fixture.candidates_per_program] =
      candidate(0, 2, 7, 1, 1);
  fixture.occurrences = {occurrence(0, 1), occurrence(0, 2)};
  const Result result = run(fixture);
  check_unchanged(fixture, result, 0, 0,
                  "node-capacity failure leaked a partial child");
  check(result.splice[1].applied == 1 && result.used_len[1] == 2,
        "capacity failure in child A incorrectly rejected independent child B");
}

void test_no_candidate_and_actual_counts_copy_base() {
  Fixture none = basic_fixture();
  none.cand_a = none.cand_b = -1;
  Result result = run(none);
  check_unchanged(none, result, 0, 0, "no-candidate child A was not unchanged");
  check_unchanged(none, result, 1, 1, "no-candidate child B was not unchanged");

  Fixture padded = basic_fixture();
  padded.candidates[1] = padded.candidates[0];
  padded.candidates[3] = padded.candidates[2];
  padded.cand_a = padded.cand_b = 1;
  result = run(padded);
  check_unchanged(padded, result, 0, 0,
                  "candidate padding beyond actual count changed child A");
  check_unchanged(padded, result, 1, 1,
                  "candidate padding beyond actual count changed child B");
}

}  // namespace

int main() {
  try {
    gagp::FitnessSessionGpu session;
    const auto initialized =
        session.init({{}}, {Value::from_int(0)}, 1, 1, 1.0);
    check(initialized.ok,
          "could not initialize CUDA device for compiled variation test");
    test_paired_crossover_and_table_remap();
    test_repeated_occurrences();
    test_numeric_compatibility_and_budgets();
    test_capacity_failure_is_transactional();
    test_no_candidate_and_actual_counts_copy_base();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "compiled GPU crossover: pairs, contracts, capacity, and fallback passed\n";
  return 0;
}
