#include <cuda_runtime.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "../../src/evolution/repro/gpu/device/compiled_mutation.cuh"

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
  explicit Managed(std::size_t count) : count_(std::max<std::size_t>(count, 1)) {
    check_cuda(cudaMallocManaged(reinterpret_cast<void**>(&data_),
                                 sizeof(T) * count_),
               "cudaMallocManaged");
  }
  ~Managed() { cudaFree(data_); }
  Managed(const Managed&) = delete;
  Managed& operator=(const Managed&) = delete;
  T* data() { return data_; }
  T& operator[](std::size_t index) { return data_[index]; }

 private:
  T* data_ = nullptr;
  std::size_t count_ = 0;
};

template <class T>
void copy_in(Managed<T>& output, const std::vector<T>& input) {
  for (std::size_t i = 0; i < input.size(); ++i) output[i] = input[i];
}

PlainNode node(NodeKind kind, int i0 = 0) {
  return {static_cast<int>(kind), i0, 0};
}

CandidateOccurrence occurrence(int begin, int end) {
  CandidateOccurrence out;
  out.start = begin;
  out.stop = end;
  return out;
}

CandidateRange candidate(int donor_offset = 0, int donor_count = 1) {
  CandidateRange out;
  out.start = 1;
  out.stop = 2;
  out.aux = static_cast<int>(RType::Int);
  out.compatibility_id = 7;
  out.occurrence_offset = 0;
  out.occurrence_count = 1;
  out.replacement_max_nodes = 3;
  out.replacement_max_depth = 3;
  out.remaining_template_nesting = 3;
  out.materialized_nodes = 1;
  out.materialized_depth = 1;
  out.template_nesting = 0;
  out.donor_offset = donor_offset;
  out.donor_count = donor_count;
  return out;
}

struct Fixture {
  GpuReproConfig config;
  std::vector<PlainNode> nodes;
  std::vector<PackedProgramMeta> metas;
  std::vector<CandidateRange> candidates;
  std::vector<CandidateOccurrence> occurrences;
  std::vector<std::uint64_t> names;
  std::vector<Value> consts;
  std::vector<PlainNode> donor_nodes;
  std::vector<int> donor_lens;
  std::vector<std::uint64_t> donor_names;
  std::vector<int> donor_name_counts;
  std::vector<Value> donor_consts;
  std::vector<int> donor_const_counts;
  std::vector<DonorContract> donor_contracts;
  std::vector<ConstantMutationDomain> domains;
  std::vector<Value> domain_values;
  std::vector<ConstantMutationGroup> groups;
  std::vector<int> origins;
  std::vector<ConstantMutationStream> streams;
  std::vector<int> parent_streams;
  std::vector<int> metadata_roots;
};

Fixture basic_fixture(int population_size = 1) {
  Fixture fixture;
  auto& config = fixture.config;
  config.compiled_pass = CompiledVariationPass::Mutation;
  config.population_size = population_size;
  config.pair_count = (population_size + 1) / 2;
  config.candidates_per_program = 1;
  config.compiled_occurrence_count = 1;
  config.compiled_donor_count = 1;
  config.max_nodes = 5;
  config.max_donor_nodes = 3;
  config.max_names = 2;
  config.max_consts = 3;
  config.mutation_ratio = 1.0;
  config.mutation_subtree_ratio = 1.0;
  config.seed = 17;

  fixture.nodes.assign(population_size * config.max_nodes, node(NodeKind::VAR));
  fixture.metas.resize(population_size);
  fixture.candidates.assign(population_size, candidate());
  fixture.names.assign(population_size * config.max_names, 0);
  fixture.consts.assign(population_size * config.max_consts, Value::from_int(-1));
  fixture.parent_streams.resize(population_size);
  for (int parent = 0; parent < population_size; ++parent) {
    fixture.nodes[parent * config.max_nodes] = node(NodeKind::CONST, 0);
    fixture.nodes[parent * config.max_nodes + 1] = node(NodeKind::VAR, 0);
    fixture.metas[parent].used_len = 2;
    fixture.metas[parent].name_count = 1;
    fixture.metas[parent].const_count = 1;
    fixture.metas[parent].candidate_count = 1;
    fixture.names[parent * config.max_names] = 100 + parent;
    fixture.consts[parent * config.max_consts] = Value::from_int(10 + parent);
    fixture.parent_streams[parent] = parent;
    ConstantMutationStream stream;
    stream.node_origin_offset = parent * 2;
    stream.node_count = 2;
    fixture.streams.push_back(stream);
    fixture.origins.push_back(0);
    fixture.origins.push_back(kNoConstantMutationGroup);
  }
  fixture.occurrences = {occurrence(1, 2)};
  fixture.donor_nodes.assign(config.max_donor_nodes, node(NodeKind::VAR));
  fixture.donor_nodes[0] = node(NodeKind::CONST, 0);
  fixture.donor_lens = {1};
  fixture.donor_names.assign(config.max_names, 0);
  fixture.donor_name_counts = {0};
  fixture.donor_consts.assign(config.max_consts, Value::from_int(-1));
  fixture.donor_consts[0] = Value::from_int(77);
  fixture.donor_const_counts = {1};
  fixture.donor_contracts = {{7, 1, 1, 0, 0, 0}};
  return fixture;
}

struct Result {
  int output_count = 0;
  std::vector<PlainNode> nodes;
  std::vector<int> used_lens;
  std::vector<std::uint64_t> names;
  std::vector<int> name_counts;
  std::vector<Value> consts;
  std::vector<int> const_counts;
  std::vector<PackedChildMeta> metas;
  std::vector<PackedChildSplice> splices;
};

Result run(const Fixture& fixture) {
  const auto& config = fixture.config;
  const int output_count = config.pair_count * 2;
  Managed<PlainNode> nodes(fixture.nodes.size());
  Managed<PackedProgramMeta> metas(fixture.metas.size());
  Managed<CandidateRange> candidates(fixture.candidates.size());
  Managed<CandidateOccurrence> occurrences(fixture.occurrences.size());
  Managed<std::uint64_t> names(fixture.names.size());
  Managed<Value> consts(fixture.consts.size());
  Managed<PlainNode> donor_nodes(fixture.donor_nodes.size());
  Managed<int> donor_lens(fixture.donor_lens.size());
  Managed<std::uint64_t> donor_names(fixture.donor_names.size());
  Managed<int> donor_name_counts(fixture.donor_name_counts.size());
  Managed<Value> donor_consts(fixture.donor_consts.size());
  Managed<int> donor_const_counts(fixture.donor_const_counts.size());
  Managed<DonorContract> donor_contracts(fixture.donor_contracts.size());
  Managed<ConstantMutationDomain> domains(fixture.domains.size());
  Managed<Value> domain_values(fixture.domain_values.size());
  Managed<ConstantMutationGroup> groups(fixture.groups.size());
  Managed<int> origins(fixture.origins.size());
  Managed<ConstantMutationStream> streams(fixture.streams.size());
  Managed<int> parent_streams(fixture.parent_streams.size());
  Managed<int> metadata_roots(fixture.metadata_roots.size());
  Managed<PlainNode> child_nodes(output_count * config.max_nodes);
  Managed<int> child_lens(output_count);
  Managed<std::uint64_t> child_names(output_count * config.max_names);
  Managed<int> child_name_counts(output_count);
  Managed<Value> child_consts(output_count * config.max_consts);
  Managed<int> child_const_counts(output_count);
  Managed<PackedChildMeta> child_metas(output_count);
  Managed<PackedChildSplice> child_splices(output_count);

  copy_in(nodes, fixture.nodes); copy_in(metas, fixture.metas);
  copy_in(candidates, fixture.candidates); copy_in(occurrences, fixture.occurrences);
  copy_in(names, fixture.names); copy_in(consts, fixture.consts);
  copy_in(donor_nodes, fixture.donor_nodes); copy_in(donor_lens, fixture.donor_lens);
  copy_in(donor_names, fixture.donor_names);
  copy_in(donor_name_counts, fixture.donor_name_counts);
  copy_in(donor_consts, fixture.donor_consts);
  copy_in(donor_const_counts, fixture.donor_const_counts);
  copy_in(donor_contracts, fixture.donor_contracts);
  copy_in(domains, fixture.domains); copy_in(domain_values, fixture.domain_values);
  copy_in(groups, fixture.groups); copy_in(origins, fixture.origins);
  copy_in(streams, fixture.streams); copy_in(parent_streams, fixture.parent_streams);
  copy_in(metadata_roots, fixture.metadata_roots);

  CompiledMutationPointers pointers;
  pointers.common.program_nodes = nodes.data();
  pointers.common.metas = metas.data();
  pointers.common.candidates = candidates.data();
  pointers.common.occurrences = occurrences.data();
  pointers.common.program_name_ids = names.data();
  pointers.common.program_consts = consts.data();
  pointers.common.child_nodes = child_nodes.data();
  pointers.common.child_used_len = child_lens.data();
  pointers.common.child_name_ids = child_names.data();
  pointers.common.child_name_counts = child_name_counts.data();
  pointers.common.child_consts = child_consts.data();
  pointers.common.child_const_counts = child_const_counts.data();
  pointers.common.child_meta = child_metas.data();
  pointers.common.child_splices = child_splices.data();
  pointers.donor_nodes = donor_nodes.data(); pointers.donor_lens = donor_lens.data();
  pointers.donor_name_ids = donor_names.data();
  pointers.donor_name_counts = donor_name_counts.data();
  pointers.donor_consts = donor_consts.data();
  pointers.donor_const_counts = donor_const_counts.data();
  pointers.donor_contracts = donor_contracts.data();
  pointers.constant_domains = domains.data();
  pointers.constant_values = domain_values.data();
  pointers.constant_groups = groups.data(); pointers.constant_origins = origins.data();
  pointers.constant_streams = streams.data();
  pointers.parent_constant_streams = parent_streams.data();
  pointers.metadata_roots = metadata_roots.data();

  compiled_mutation_kernel<<<config.pair_count, 2>>>(config, pointers);
  check_cuda(cudaGetLastError(), "compiled_mutation_kernel launch");
  check_cuda(cudaDeviceSynchronize(), "compiled_mutation_kernel execution");

  Result result;
  result.output_count = output_count;
  result.nodes.assign(child_nodes.data(), child_nodes.data() + output_count * config.max_nodes);
  result.used_lens.assign(child_lens.data(), child_lens.data() + output_count);
  result.names.assign(child_names.data(), child_names.data() + output_count * config.max_names);
  result.name_counts.assign(child_name_counts.data(), child_name_counts.data() + output_count);
  result.consts.assign(child_consts.data(), child_consts.data() + output_count * config.max_consts);
  result.const_counts.assign(child_const_counts.data(), child_const_counts.data() + output_count);
  result.metas.assign(child_metas.data(), child_metas.data() + output_count);
  result.splices.assign(child_splices.data(), child_splices.data() + output_count);
  return result;
}

void check_base_copy(const Fixture& fixture, const Result& result, int child,
                     int parent, const char* message) {
  check(result.used_lens[child] == fixture.metas[parent].used_len &&
            result.name_counts[child] == fixture.metas[parent].name_count &&
            result.const_counts[child] == fixture.metas[parent].const_count,
        message);
  for (int i = 0; i < result.used_lens[child]; ++i) {
    const PlainNode actual = result.nodes[child * fixture.config.max_nodes + i];
    const PlainNode expected = fixture.nodes[parent * fixture.config.max_nodes + i];
    check(actual.kind == expected.kind && actual.i0 == expected.i0 &&
              actual.i1 == expected.i1, message);
  }
  for (int i = 0; i < result.const_counts[child]; ++i)
    check(result.consts[child * fixture.config.max_consts + i].i ==
              fixture.consts[parent * fixture.config.max_consts + i].i,
          message);
}

void test_mutation_ratio_zero_and_odd_padding() {
  Fixture fixture = basic_fixture(3);
  fixture.config.mutation_ratio = 0.0;
  const Result result = run(fixture);
  for (int child = 0; child < 3; ++child) {
    check_base_copy(fixture, result, child, child, "ratio-zero base copy");
    check(result.splices[child].mutation_outcome == CompiledMutationOutcome::None &&
              result.splices[child].applied == 0,
          "ratio-zero outcome");
  }
  check_base_copy(fixture, result, 3, 2, "odd padded base copy");
  check(result.splices[3].mutation_outcome == CompiledMutationOutcome::None &&
            result.splices[3].base_parent == 2,
        "odd padded outcome");
}

void test_forced_constant_with_pinned_metadata_root() {
  Fixture fixture = basic_fixture();
  fixture.config.mutation_subtree_ratio = 0.0;
  fixture.domains = {{static_cast<int>(RType::Int), 0, 1, 0, 0, 0}};
  fixture.domain_values = {Value::from_int(42)};
  fixture.groups.resize(1);
  fixture.groups[0].domain = 0;
  fixture.metadata_roots = {0};
  fixture.streams[0].metadata_root_offset = 0;
  fixture.streams[0].metadata_root_count = 1;
  fixture.config.constant_domain_count = 1;
  fixture.config.constant_value_count = 1;
  fixture.config.constant_group_count = 1;
  fixture.config.constant_origin_count = 2;
  fixture.config.constant_stream_count = 1;
  fixture.config.constant_root_count = 1;
  const Result result = run(fixture);
  check(result.splices[0].mutation_outcome == CompiledMutationOutcome::Constant &&
            result.splices[0].applied == 0,
        "forced constant outcome");
  check(result.consts[result.nodes[0].i0].i == 42,
        "forced constant value");
  check(result.const_counts[0] == 2,
        "pinned metadata root was not retained by value");
}

void test_forced_subtree() {
  Fixture fixture = basic_fixture();
  const Result result = run(fixture);
  check(result.splices[0].mutation_outcome == CompiledMutationOutcome::Subtree &&
            result.splices[0].applied == 1 &&
            result.splices[0].source_kind == SpliceSourceKind::CompiledDonor &&
            result.splices[0].source_index == 0 &&
            result.splices[0].source_candidate == -1,
        "forced subtree provenance");
  check(result.nodes[1].kind == static_cast<int>(NodeKind::CONST) &&
            result.consts[result.nodes[1].i0].i == 77,
        "forced subtree payload");
}

void test_no_constant_groups_falls_through_to_subtree() {
  Fixture fixture = basic_fixture();
  fixture.config.mutation_subtree_ratio = 0.0;
  fixture.origins[0] = kNoConstantMutationGroup;
  fixture.config.constant_origin_count = 2;
  fixture.config.constant_stream_count = 1;
  const Result result = run(fixture);
  check(result.splices[0].mutation_outcome == CompiledMutationOutcome::Subtree &&
            result.splices[0].applied == 1,
        "no-groups did not fall through to subtree");
}

void test_empty_site_and_empty_donor_outcomes() {
  Fixture no_site = basic_fixture();
  no_site.metas[0].candidate_count = 0;
  no_site.config.compiled_occurrence_count = 0;
  no_site.occurrences.clear();
  Result result = run(no_site);
  check(result.splices[0].mutation_outcome == CompiledMutationOutcome::NoSite,
        "empty-site outcome");
  check_base_copy(no_site, result, 0, 0, "empty-site restoration");

  Fixture no_donor = basic_fixture();
  no_donor.candidates[0].donor_count = 0;
  result = run(no_donor);
  check(result.splices[0].mutation_outcome == CompiledMutationOutcome::NoDonor,
        "empty-donor outcome");
  check_base_copy(no_donor, result, 0, 0, "empty-donor restoration");
}

void test_invalid_and_capacity_restore_base() {
  Fixture invalid = basic_fixture();
  invalid.donor_contracts[0].compatibility_id = 9;
  Result result = run(invalid);
  check(result.splices[0].mutation_outcome == CompiledMutationOutcome::Invalid,
        "invalid donor outcome");
  check_base_copy(invalid, result, 0, 0, "invalid donor restoration");

  Fixture capacity = basic_fixture();
  capacity.metas[0].used_len = capacity.config.max_nodes;
  capacity.nodes[2] = node(NodeKind::VAR, 0);
  capacity.nodes[3] = node(NodeKind::VAR, 0);
  capacity.nodes[4] = node(NodeKind::VAR, 0);
  capacity.donor_lens[0] = 2;
  capacity.donor_nodes[1] = node(NodeKind::VAR, 0);
  capacity.donor_contracts[0].materialized_nodes = 2;
  capacity.candidates[0].replacement_max_nodes = 2;
  result = run(capacity);
  check(result.splices[0].mutation_outcome == CompiledMutationOutcome::Capacity,
        "capacity outcome");
  check_base_copy(capacity, result, 0, 0, "capacity restoration");
}

}  // namespace

int main() {
  try {
    gagp::FitnessSessionGpu session;
    const auto initialized =
        session.init({{}}, {Value::from_int(0)}, 1, 1, 1.0);
    check(initialized.ok,
          "could not initialize CUDA device for compiled mutation test");
    test_mutation_ratio_zero_and_odd_padding();
    test_forced_constant_with_pinned_metadata_root();
    test_forced_subtree();
    test_no_constant_groups_falls_through_to_subtree();
    test_empty_site_and_empty_donor_outcomes();
    test_invalid_and_capacity_restore_base();
    std::cout << "compiled mutation gpu tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
