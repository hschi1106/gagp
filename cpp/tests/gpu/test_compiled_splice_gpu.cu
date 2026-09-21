#include <cuda_runtime.h>

#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "../../src/evolution/repro/splice_metadata.hpp"
#include "../../src/evolution/repro/gpu/device/compiled_splice.cuh"

namespace {

using gagp::Value;
using gagp::evo::NodeKind;
using gagp::evo::repro::AtomicSpliceOrigin;
using gagp::evo::repro::CandidateOccurrence;
using gagp::evo::repro::PackedChildSplice;
using gagp::evo::repro::PlainNode;
using gagp::evo::repro::SpliceSourceKind;

constexpr int kSentinel = 0x24680;

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void check_cuda(cudaError_t status, const char* operation) {
  if (status == cudaSuccess) return;
  throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorString(status));
}

template <typename T>
class Managed {
 public:
  explicit Managed(std::size_t count) : count_(count) {
    if (count == 0) return;
    check_cuda(cudaMallocManaged(reinterpret_cast<void**>(&data_), sizeof(T) * count),
               "cudaMallocManaged");
  }
  ~Managed() {
    if (data_ != nullptr) cudaFree(data_);
  }
  Managed(const Managed&) = delete;
  Managed& operator=(const Managed&) = delete;
  T* data() { return data_; }
  const T* data() const { return data_; }
  T& operator[](std::size_t index) { return data_[index]; }
  const T& operator[](std::size_t index) const { return data_[index]; }
  std::size_t size() const { return count_; }

 private:
  T* data_ = nullptr;
  std::size_t count_ = 0;
};

CandidateOccurrence occurrence(int start, int stop) {
  CandidateOccurrence out;
  out.start = start;
  out.stop = stop;
  return out;
}

PlainNode node(NodeKind kind, int i0 = 0) {
  return PlainNode{static_cast<int>(kind), i0, 0};
}

__global__ void prepare_splice_kernel(
    const PlainNode* base_nodes, int base_len,
    const std::uint64_t* base_names, int base_name_count,
    const Value* base_consts, int base_const_count,
    const CandidateOccurrence* occurrences, int occurrence_count,
    const PlainNode* source_nodes, int source_len, int source_begin,
    int source_end, const std::uint64_t* source_names, int source_name_count,
    const Value* source_consts, int source_const_count,
    PlainNode* child_nodes, AtomicSpliceOrigin* origins, int node_capacity,
    std::uint64_t* child_names, int name_capacity, Value* child_consts,
    int const_capacity, int* child_len, int* child_name_count,
    int* child_const_count, PackedChildSplice descriptor,
    PackedChildSplice* provenance, int* accepted) {
  if (blockIdx.x != 0 || threadIdx.x != 0) return;
  *accepted = gagp::evo::repro::d_prepare_compiled_splice(
                  base_nodes, base_len, base_names, base_name_count, base_consts,
                  base_const_count, occurrences, occurrence_count, source_nodes,
                  source_len, source_begin, source_end, source_names,
                  source_name_count, source_consts, source_const_count,
                  child_nodes, origins, node_capacity, child_names, name_capacity,
                  child_consts, const_capacity, child_len, child_name_count,
                  child_const_count, descriptor, provenance)
                  ? 1
                  : 0;
}

struct Result {
  bool accepted = false;
  int child_len = kSentinel;
  int name_count = kSentinel;
  int const_count = kSentinel;
  std::vector<PlainNode> nodes;
  std::vector<std::uint64_t> names;
  std::vector<Value> consts;
  PackedChildSplice provenance;
};

Result run(const std::vector<PlainNode>& base_nodes,
           const std::vector<std::uint64_t>& base_names,
           const std::vector<Value>& base_consts,
           const std::vector<CandidateOccurrence>& occurrences,
           const std::vector<PlainNode>& source_nodes,
           int source_begin, int source_end,
           const std::vector<std::uint64_t>& source_names,
           const std::vector<Value>& source_consts,
           int node_capacity, int name_capacity, int const_capacity) {
  Managed<PlainNode> d_base(base_nodes.size());
  Managed<std::uint64_t> d_base_names(base_names.size());
  Managed<Value> d_base_consts(base_consts.size());
  Managed<CandidateOccurrence> d_occurrences(occurrences.size());
  Managed<PlainNode> d_source(source_nodes.size());
  Managed<std::uint64_t> d_source_names(source_names.size());
  Managed<Value> d_source_consts(source_consts.size());
  Managed<PlainNode> d_child(static_cast<std::size_t>(node_capacity));
  Managed<AtomicSpliceOrigin> d_origins(static_cast<std::size_t>(node_capacity));
  Managed<std::uint64_t> d_child_names(static_cast<std::size_t>(name_capacity));
  Managed<Value> d_child_consts(static_cast<std::size_t>(const_capacity));
  Managed<int> d_child_len(1);
  Managed<int> d_name_count(1);
  Managed<int> d_const_count(1);
  Managed<PackedChildSplice> d_provenance(1);
  Managed<int> d_accepted(1);

  for (std::size_t i = 0; i < base_nodes.size(); ++i) d_base[i] = base_nodes[i];
  for (std::size_t i = 0; i < base_names.size(); ++i) d_base_names[i] = base_names[i];
  for (std::size_t i = 0; i < base_consts.size(); ++i) d_base_consts[i] = base_consts[i];
  for (std::size_t i = 0; i < occurrences.size(); ++i) d_occurrences[i] = occurrences[i];
  for (std::size_t i = 0; i < source_nodes.size(); ++i) d_source[i] = source_nodes[i];
  for (std::size_t i = 0; i < source_names.size(); ++i) d_source_names[i] = source_names[i];
  for (std::size_t i = 0; i < source_consts.size(); ++i) d_source_consts[i] = source_consts[i];
  d_child_len[0] = kSentinel;
  d_name_count[0] = kSentinel;
  d_const_count[0] = kSentinel;
  d_provenance[0] = PackedChildSplice{};
  d_accepted[0] = 0;

  PackedChildSplice descriptor;
  descriptor.base_parent = 4;
  descriptor.destination_candidate = 2;
  descriptor.source_kind = SpliceSourceKind::CompiledDonor;
  descriptor.source_index = 7;
  descriptor.source_candidate = -1;
  descriptor.source_begin = source_begin;
  descriptor.source_end = source_end;
  prepare_splice_kernel<<<1, 1>>>(
      d_base.data(), static_cast<int>(base_nodes.size()), d_base_names.data(),
      static_cast<int>(base_names.size()), d_base_consts.data(),
      static_cast<int>(base_consts.size()), d_occurrences.data(),
      static_cast<int>(occurrences.size()), d_source.data(),
      static_cast<int>(source_nodes.size()), source_begin, source_end,
      d_source_names.data(), static_cast<int>(source_names.size()),
      d_source_consts.data(), static_cast<int>(source_consts.size()),
      d_child.data(), d_origins.data(), node_capacity, d_child_names.data(),
      name_capacity, d_child_consts.data(), const_capacity, d_child_len.data(),
      d_name_count.data(), d_const_count.data(), descriptor, d_provenance.data(),
      d_accepted.data());
  check_cuda(cudaGetLastError(), "prepare_splice_kernel launch");
  check_cuda(cudaDeviceSynchronize(), "prepare_splice_kernel execution");

  Result result;
  result.accepted = d_accepted[0] != 0;
  result.child_len = d_child_len[0];
  result.name_count = d_name_count[0];
  result.const_count = d_const_count[0];
  result.provenance = d_provenance[0];
  if (result.accepted) {
    result.nodes.assign(d_child.data(), d_child.data() + result.child_len);
    if (result.name_count > 0)
      result.names.assign(d_child_names.data(), d_child_names.data() + result.name_count);
    if (result.const_count > 0)
      result.consts.assign(d_child_consts.data(), d_child_consts.data() + result.const_count);
  }
  return result;
}

void test_device_splice_to_executable_metadata() {
  using namespace gagp::evo;
  AstProgram base;
  base.consts = {Value::from_int(10), Value::from_int(0)};
  base.nodes = {{NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
                {NodeKind::LET_REGION}, {NodeKind::CONST, 0}, {NodeKind::CONST, 1},
                {NodeKind::BLOCK_NIL}};
  base.lexical_regions = {{3, 1, {{10, RType::Int}}}};
  AstProgram donor;
  donor.consts = {Value::from_int(0), Value::from_int(2)};
  donor.nodes = {{NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
                 {NodeKind::LET_REGION}, {NodeKind::CONST, 0}, {NodeKind::ADD},
                 {NodeKind::REGION_VAR, 40}, {NodeKind::CONST, 1}, {NodeKind::BLOCK_NIL}};
  donor.lexical_regions = {{3, 1, {{40, RType::Int}}}};
  check(verify_ast(base, {}).ok && verify_ast(donor, {}).ok,
        "device-to-metadata source fixture is invalid");
  std::vector<PlainNode> base_nodes, donor_nodes;
  for (const auto& n : base.nodes) base_nodes.push_back({static_cast<int>(n.kind), n.i0, n.i1});
  for (const auto& n : donor.nodes) donor_nodes.push_back({static_cast<int>(n.kind), n.i0, n.i1});
  const auto result = run(base_nodes, {}, base.consts, {occurrence(5, 6)},
                          donor_nodes, 5, 8, {}, donor.consts, 16, 0, 4);
  check(result.accepted && result.provenance.applied == 1,
        "device splice did not publish a child for metadata decode");
  AstProgram child;
  child.consts = result.consts;
  // These are the actual device nodes; no host structural splice runs here.
  for (const auto& n : result.nodes)
    child.nodes.push_back({static_cast<NodeKind>(n.kind), n.i0, n.i1});
  repro::reconstruct_splice_metadata(child, base, donor, {{5, 6, {10}}},
                                     result.provenance.source_begin,
                                     result.provenance.source_end, {40});
  const auto verified = verify_ast(child, {});
  check(verified.ok, "device child failed native verification after metadata decode");
  ProgramGenome genome;
  genome.ast = child;
  genome.meta = build_genome_meta(child);
  const auto executed = gagp::execute_bytecode_cpu(compile_for_eval(genome, verified.verified), {}, 100);
  check(!executed.is_error && executed.value.tag == gagp::ValueTag::Int && executed.value.i == 12,
        "device splice plus metadata reconstruction changed capture semantics");
}

void test_empty_tables() {
  const auto result = run({node(NodeKind::REGION_VAR, 7)}, {}, {}, {occurrence(0, 1)},
                          {node(NodeKind::REGION_VAR, 42)}, 0, 1, {}, {}, 1, 0, 0);
  check(result.accepted && result.name_count == 0 && result.const_count == 0 &&
            result.child_len == 1 && result.nodes[0].i0 == 42,
        "compiled splice rejected valid empty name/constant tables");
}

void test_repeated_copies_share_table_slots() {
  const std::vector<PlainNode> base = {
      node(NodeKind::CONST, 0), node(NodeKind::VAR, 0), node(NodeKind::CONST, 1)};
  const std::vector<PlainNode> source = {
      node(NodeKind::VAR, 0), node(NodeKind::CONST, 0)};
  const auto result = run(base, {10}, {Value::from_int(7), Value::from_int(99)},
                          {occurrence(0, 1), occurrence(2, 3)}, source, 0, 2,
                          {20}, {Value::from_int(42)}, 5, 2, 3);
  check(result.accepted && result.child_len == 5,
        "compiled repeated splice was rejected");
  check(result.name_count == 2 && result.names[0] == 10 && result.names[1] == 20,
        "source name was not appended exactly once");
  check(result.const_count == 3 && result.consts[0].i == 7 &&
            result.consts[1].i == 99 && result.consts[2].i == 42,
        "base constants changed or source constant was not appended exactly once");
  check(result.nodes[0].i0 == 1 && result.nodes[1].i0 == 2 &&
            result.nodes[2].i0 == 0 && result.nodes[3].i0 == 1 &&
            result.nodes[4].i0 == 2,
        "repeated source nodes did not share remapped table slots");
  check(result.provenance.applied == 1 && result.provenance.base_parent == 4 &&
            result.provenance.destination_candidate == 2 &&
            result.provenance.source_kind == SpliceSourceKind::CompiledDonor &&
            result.provenance.source_index == 7 &&
            result.provenance.occurrence_count == 2,
        "compiled splice did not publish its caller-supplied provenance");
}

void test_float_bit_identity_preserves_negative_zero() {
  const auto result = run({node(NodeKind::CONST, 0)}, {10},
                          {Value::from_float(0.0)}, {occurrence(0, 1)},
                          {node(NodeKind::CONST, 0)}, 0, 1, {20},
                          {Value::from_float(-0.0)}, 1, 1, 2);
  check(result.accepted && result.const_count == 2 && result.nodes[0].i0 == 1,
        "bit-distinct floating constants shared one slot");
  check(!std::signbit(result.consts[0].f) && std::signbit(result.consts[1].f),
        "compiled splice did not preserve positive and negative zero bits");

  const auto booleans = run({node(NodeKind::CONST, 0)}, {10},
                            {Value::from_bool(false)}, {occurrence(0, 1)},
                            {node(NodeKind::CONST, 0)}, 0, 1, {20},
                            {Value::from_bool(true)}, 1, 1, 2);
  check(booleans.accepted && booleans.const_count == 2 &&
            !booleans.consts[0].b && booleans.consts[1].b &&
            booleans.nodes[0].i0 == 1,
        "compiled splice ignored the Bool payload bit during constant identity");
}

void test_region_binder_ids_remain_raw() {
  const auto result = run({node(NodeKind::CONST, 0)}, {10},
                          {Value::from_int(1)}, {occurrence(0, 1)},
                          {node(NodeKind::REGION_VAR, 77)}, 0, 1, {20},
                          {Value::from_int(2)}, 1, 1, 1);
  check(result.accepted && result.nodes[0].i0 == 77,
        "compiled splice remapped a raw REGION_VAR binder ID");
}

void test_invalid_indices_and_capacity_do_not_publish() {
  auto invalid_const = run({node(NodeKind::CONST, 0)}, {10},
                           {Value::from_int(1)}, {occurrence(0, 1)},
                           {node(NodeKind::CONST, 1)}, 0, 1, {20},
                           {Value::from_int(2)}, 1, 2, 2);
  check(!invalid_const.accepted && invalid_const.child_len == kSentinel &&
            invalid_const.name_count == kSentinel &&
            invalid_const.const_count == kSentinel &&
            invalid_const.provenance.applied == 0,
        "invalid source constant index published a child");

  auto exhausted_names = run({node(NodeKind::VAR, 0)}, {10},
                             {Value::from_int(1)}, {occurrence(0, 1)},
                             {node(NodeKind::VAR, 0)}, 0, 1, {20},
                             {Value::from_int(2)}, 1, 1, 1);
  check(!exhausted_names.accepted && exhausted_names.child_len == kSentinel &&
            exhausted_names.provenance.applied == 0,
        "exhausted name capacity published a child");

  auto invalid_name = run({node(NodeKind::VAR, 0)}, {10},
                          {Value::from_int(1)}, {occurrence(0, 1)},
                          {node(NodeKind::VAR, 1)}, 0, 1, {20},
                          {Value::from_int(2)}, 1, 2, 1);
  check(!invalid_name.accepted && invalid_name.child_len == kSentinel &&
            invalid_name.provenance.applied == 0,
        "invalid source name index published a child");

  auto exhausted_consts = run({node(NodeKind::CONST, 0)}, {10},
                              {Value::from_int(1)}, {occurrence(0, 1)},
                              {node(NodeKind::CONST, 0)}, 0, 1, {20},
                              {Value::from_int(2)}, 1, 1, 1);
  check(!exhausted_consts.accepted && exhausted_consts.child_len == kSentinel &&
            exhausted_consts.provenance.applied == 0,
        "exhausted constant capacity published a child");
}

}  // namespace

int main() {
  try {
    gagp::FitnessSessionGpu session;
    const auto initialized = session.init({{}}, {Value::from_int(0)}, 1, 1, 1.0);
    check(initialized.ok, "could not initialize CUDA device for compiled splice test");
    test_device_splice_to_executable_metadata();
    test_empty_tables();
    test_repeated_copies_share_table_slots();
    test_float_bit_identity_preserves_negative_zero();
    test_region_binder_ids_remain_raw();
    test_invalid_indices_and_capacity_do_not_publish();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "compiled GPU splice: table remapping and provenance passed\n";
  return 0;
}
