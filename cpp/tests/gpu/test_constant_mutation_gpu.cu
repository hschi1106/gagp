#include <cuda_runtime.h>

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/evolution/grammar/random.hpp"
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "../../src/evolution/repro/gpu/device/constant_mutation.cuh"

namespace {

using gagp::Value;
using gagp::ValueTag;
using gagp::evo::NodeKind;
using gagp::evo::RType;
using gagp::evo::grammar::GrammarRandom;
using namespace gagp::evo::repro;

void check(bool condition, const std::string& message) {
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
    if (count > 0)
      check_cuda(cudaMallocManaged(reinterpret_cast<void**>(&data_),
                                   count * sizeof(T)), "cudaMallocManaged");
  }
  ~Managed() { if (data_ != nullptr) cudaFree(data_); }
  Managed(const Managed&) = delete;
  Managed& operator=(const Managed&) = delete;
  T* data() { return data_; }
  T& operator[](std::size_t index) { return data_[index]; }

 private:
  T* data_ = nullptr;
  std::size_t count_ = 0;
};

struct Input {
  std::vector<PlainNode> nodes;
  std::vector<AtomicSpliceOrigin> origins;
  std::vector<Value> consts;
  int capacity = 0;
  std::vector<ConstantMutationDomain> domains;
  std::vector<Value> domain_values;
  std::vector<ConstantMutationGroup> groups;
  std::vector<int> base_origins;
  std::vector<int> source_origins;
  std::vector<int> pinned;
  std::uint64_t seed = 0;
};

struct Result {
  DConstantMutationResult status = DConstantMutationResult::Invalid;
  int const_count = -1;
  std::vector<PlainNode> nodes;
  std::vector<Value> consts;
  std::vector<int> pinned_remaps;
};

__global__ void mutate_kernel(
    PlainNode* nodes, int node_count, const AtomicSpliceOrigin* origins,
    Value* consts, int* const_count, int capacity, Value* work,
    DConstantMutationTableView table, DConstantMutationStreamView base,
    DConstantMutationStreamView source, DConstantMutationPinnedRoots pinned,
    std::uint64_t seed, DConstantMutationResult* result) {
  if (blockIdx.x != 0 || threadIdx.x != 0) return;
  *result = d_mutate_compiled_constants(
      nodes, node_count, origins, consts, const_count, capacity, work, table,
      base, source, pinned, seed);
}

template <class T>
void copy_in(Managed<T>& destination, const std::vector<T>& source) {
  for (std::size_t i = 0; i < source.size(); ++i) destination[i] = source[i];
}

Result run(const Input& input) {
  check(input.nodes.size() == input.origins.size(), "fixture origin length");
  check(input.capacity >= static_cast<int>(input.consts.size()),
        "fixture constant capacity");
  Managed<PlainNode> nodes(input.nodes.size());
  Managed<AtomicSpliceOrigin> origins(input.origins.size());
  Managed<Value> consts(static_cast<std::size_t>(input.capacity));
  Managed<Value> work(static_cast<std::size_t>(input.capacity));
  Managed<ConstantMutationDomain> domains(input.domains.size());
  Managed<Value> domain_values(input.domain_values.size());
  Managed<ConstantMutationGroup> groups(input.groups.size());
  Managed<int> base_origins(input.base_origins.size());
  Managed<int> source_origins(input.source_origins.size());
  Managed<int> pinned(input.pinned.size());
  Managed<int> pinned_remaps(input.pinned.size());
  Managed<int> const_count(1);
  Managed<DConstantMutationResult> status(1);
  copy_in(nodes, input.nodes);
  copy_in(origins, input.origins);
  copy_in(consts, input.consts);
  copy_in(domains, input.domains);
  copy_in(domain_values, input.domain_values);
  copy_in(groups, input.groups);
  copy_in(base_origins, input.base_origins);
  copy_in(source_origins, input.source_origins);
  copy_in(pinned, input.pinned);
  for (std::size_t i = 0; i < input.pinned.size(); ++i)
    pinned_remaps[i] = 0x13579;
  const_count[0] = static_cast<int>(input.consts.size());
  status[0] = DConstantMutationResult::Invalid;

  mutate_kernel<<<1, 1>>>(
      nodes.data(), static_cast<int>(input.nodes.size()), origins.data(),
      consts.data(), const_count.data(), input.capacity, work.data(),
      {domains.data(), static_cast<int>(input.domains.size()),
       domain_values.data(), static_cast<int>(input.domain_values.size()),
       groups.data(), static_cast<int>(input.groups.size())},
      {base_origins.data(), static_cast<int>(input.base_origins.size())},
      {source_origins.data(), static_cast<int>(input.source_origins.size())},
      {pinned.data(), pinned_remaps.data(), static_cast<int>(input.pinned.size())},
      input.seed, status.data());
  check_cuda(cudaGetLastError(), "mutate_kernel launch");
  check_cuda(cudaDeviceSynchronize(), "mutate_kernel execution");

  Result result;
  result.status = status[0];
  result.const_count = const_count[0];
  result.nodes.assign(nodes.data(), nodes.data() + input.nodes.size());
  result.consts.assign(consts.data(), consts.data() + input.consts.size());
  if (result.const_count > static_cast<int>(result.consts.size()))
    result.consts.assign(consts.data(), consts.data() + result.const_count);
  result.pinned_remaps.assign(pinned_remaps.data(),
                              pinned_remaps.data() + input.pinned.size());
  return result;
}

ConstantMutationDomain finite_domain(RType type, int offset, int count) {
  return {static_cast<int>(type), offset, count, 0, 0, 0};
}

ConstantMutationGroup group(int domain) {
  ConstantMutationGroup result;
  result.domain = domain;
  return result;
}

PlainNode constant_node(int index) {
  return {static_cast<int>(NodeKind::CONST), index, 0};
}

bool equal(const Value& a, const Value& b) {
  if (a.tag != b.tag) return false;
  if (a.tag == ValueTag::Bool) return a.b == b.b;
  if (a.tag == ValueTag::Float) {
    union {
      double floating;
      std::uint64_t bits;
    } left{}, right{};
    left.floating = a.f;
    right.floating = b.f;
    return left.bits == right.bits;
  }
  if (a.tag == ValueTag::Invalid) return true;
  return a.i == b.i;
}

std::uint64_t seed_for_rank(std::uint64_t rank, std::uint64_t count) {
  for (std::uint64_t seed = 0; seed < 10000; ++seed) {
    GrammarRandom random(seed);
    if (random.bounded(count) == rank) return seed;
  }
  throw std::runtime_error("could not find deterministic group seed");
}

void test_source_occurrences_and_base_namespace() {
  Input input;
  input.nodes = {constant_node(0), constant_node(0), constant_node(0)};
  input.origins = {{0, -1}, {0, 0}, {0, 1}};
  input.consts = {Value::from_int(7)};
  input.capacity = 3;
  input.domains = {finite_domain(RType::Int, 0, 1)};
  input.domain_values = {Value::from_int(99)};
  input.groups = {group(0)};
  input.base_origins = {0};
  input.source_origins = {0};
  input.seed = seed_for_rank(1, 2);
  const Result result = run(input);
  check(result.status == DConstantMutationResult::Applied, "source mutation status");
  check(result.const_count == 2, "source mutation compact count");
  check(result.nodes[0].i0 != result.nodes[1].i0,
        "base/source namespace was collapsed");
  check(result.nodes[1].i0 == result.nodes[2].i0,
        "repeated source occurrences did not collapse");
  check(result.consts[result.nodes[0].i0].i == 7 &&
            result.consts[result.nodes[1].i0].i == 99,
        "source group mutation changed the wrong aliases");
}

void test_fixed_alias_and_pinned_root() {
  Input input;
  input.nodes = {constant_node(0), constant_node(0)};
  input.origins = {{0, -1}, {1, -1}};
  input.consts = {Value::from_int(7)};
  input.capacity = 2;
  input.domains = {finite_domain(RType::Int, 0, 1)};
  input.domain_values = {Value::from_int(12)};
  input.groups = {group(0)};
  input.base_origins = {0, -1};
  input.pinned = {0};
  const Result result = run(input);
  check(result.status == DConstantMutationResult::Applied, "fixed mutation status");
  check(result.const_count == 2, "fixed alias was not retained");
  check(result.consts[result.nodes[0].i0].i == 12, "mutable alias not changed");
  check(result.consts[result.nodes[1].i0].i == 7, "fixed alias changed");
  check(result.consts[result.pinned_remaps[0]].i == 7, "pinned root remap wrong");
}

void test_finite_domains_all_public_tags() {
  const std::vector<Value> samples = {
      Value::from_int(-4), Value::from_float(1.25), Value::from_bool(true),
      Value::from_char(0x03bb), Value::from_string_hash_len(11, 2),
      Value::from_int_list_hash_len(12, 3),
      Value::from_float_list_hash_len(13, 4),
      Value::from_string_list_hash_len(14, 5)};
  const RType types[] = {RType::Int, RType::Float, RType::Bool, RType::Char,
                         RType::String, RType::IntList, RType::FloatList,
                         RType::StringList};
  for (int i = 0; i < 8; ++i) {
    Input input;
    input.nodes = {constant_node(0)};
    input.origins = {{0, -1}};
    input.consts = {Value::from_int(0)};
    input.capacity = 1;
    // Repeated declarations are retained in the domain table and sampled by
    // their declared slot, even though the resulting Value is identical.
    input.domains = {finite_domain(types[i], 0, 3)};
    input.domain_values = {samples[i], samples[i], samples[i]};
    input.groups = {group(0)};
    input.base_origins = {0};
    input.seed = static_cast<std::uint64_t>(100 + i);
    const Result result = run(input);
    check(result.status == DConstantMutationResult::Applied,
          "finite domain rejected public tag " + std::to_string(i));
    check(result.const_count == 1 && equal(result.consts[0], samples[i]),
          "finite domain materialized wrong public tag " + std::to_string(i));
    input.consts = {samples[i]};
    input.domains[0].mutation = gagp::evo::grammar::ConstantMutationPolicy::Keep;
    input.domains[0].value_count = 0;
    input.domain_values.clear();
    const auto kept = run(input);
    check(kept.status == DConstantMutationResult::Applied && kept.const_count == 1 &&
          equal(kept.consts[0],samples[i]), "keep did not preserve public tag " + std::to_string(i));
  }
}

void test_keep_and_flip() {
  using Policy = gagp::evo::grammar::ConstantMutationPolicy;
  Input input;
  input.nodes = {constant_node(0),constant_node(0),constant_node(0)};
  input.origins = {{0,-1},{1,-1},{2,-1}};
  input.consts = {Value::from_bool(false)};
  input.capacity = 2;
  input.domains = {finite_domain(RType::Bool,0,2)};
  input.domains[0].mutation = Policy::Flip;
  input.domain_values = {Value::from_bool(false),Value::from_bool(true)};
  input.groups = {group(0)};
  input.base_origins = {0,0,-1};
  input.pinned = {0};
  for (bool before : {false,true}) {
    input.consts[0] = Value::from_bool(before);
    const auto result = run(input);
    check(result.status == DConstantMutationResult::Applied && result.const_count == 2,
          "flip failed or compacted distinct values together");
    check(result.consts[result.nodes[0].i0].b == !before && result.consts[result.nodes[1].i0].b == !before &&
          result.consts[result.nodes[2].i0].b == before && result.consts[result.pinned_remaps[0]].b == before,
          "flip did not preserve atomic groups, fixed aliases and pinned roots");
  }
  input.domains[0].mutation = Policy::Keep;
  input.domains[0].value_count = 0;
  input.domain_values.clear();
  const auto kept = run(input);
  check(kept.status == DConstantMutationResult::Applied && kept.const_count == 1 &&
        kept.nodes[0].i0 == 0 && kept.nodes[1].i0 == 0 && kept.nodes[2].i0 == 0 &&
        kept.pinned_remaps[0] == 0 && kept.consts[0].b == input.consts[0].b,
        "keep requested fallback or changed the child");
  for (auto policy : {Policy::Flip,static_cast<Policy>(99)}) {
    input.domains[0].mutation = policy;
    const auto invalid = run(input);
    check(invalid.status == DConstantMutationResult::Invalid && invalid.const_count == 1 &&
          invalid.pinned_remaps[0] == 0x13579 && invalid.consts[0].b == input.consts[0].b,
          "invalid policy/domain was not transactional");
  }
}

__global__ void delta_grid_kernel(double* values) {
  const unsigned index = blockIdx.x * blockDim.x + threadIdx.x;
  if (index <= 65535) values[index] = gagp::evo::grammar::sample_float_grid(-1,1,index,65535);
}

void test_additive_policy() {
  using Policy = gagp::evo::grammar::ConstantMutationPolicy;
  Managed<double> grid(65536);
  delta_grid_kernel<<<256,256>>>(grid.data());
  check_cuda(cudaGetLastError(),"delta grid launch");
  check_cuda(cudaDeviceSynchronize(),"delta grid execution");
  for (unsigned i = 0; i < 65536; ++i) {
    volatile double fraction = static_cast<double>(i)/65535.0;
    volatile double scaled = fraction * 2.0;
    check(equal(Value::from_float(grid[i]),Value::from_float(scaled-1.0)),
          "device grid differs from frozen 16-bit Float delta law");
  }
  Input input;
  input.nodes = {constant_node(0),constant_node(0),constant_node(0)};
  input.origins = {{0,-1},{1,-1},{2,-1}};
  input.consts = {Value::from_int(42)};
  input.capacity = 2;
  input.domains = {{static_cast<int>(RType::Int),0,0,1,INT64_MIN,INT64_MAX}};
  input.domains[0].mutation = Policy::Add;
  input.groups = {group(0)};
  input.base_origins = {0,0,-1};
  for (auto previous : {INT64_MIN,INT64_C(-1),INT64_C(0),INT64_C(42),INT64_MAX})
    for (auto amount : {INT64_MIN,INT64_C(-2),INT64_C(0),INT64_C(2),INT64_MAX}) {
      input.consts[0] = Value::from_int(previous);
      input.domains[0].delta.integer_minimum = input.domains[0].delta.integer_maximum = amount;
      const __int128 mathematical = static_cast<__int128>(previous)+amount;
      const auto expected = mathematical < INT64_MIN || mathematical > INT64_MAX ? previous : static_cast<std::int64_t>(mathematical);
      const auto result = run(input);
      check(result.status == DConstantMutationResult::Applied &&
            result.consts[result.nodes[0].i0].i == expected && result.consts[result.nodes[1].i0].i == expected &&
            result.consts[result.nodes[2].i0].i == previous,"device Int addition overflow or logical-group error");
    }
  auto& domain = input.domains[0];
  domain.type = static_cast<int>(RType::Float); domain.integer_range = 0; domain.float_range = 1;
  domain.float_minimum = -100; domain.float_maximum = 100;
  domain.delta.float_minimum = -1; domain.delta.float_maximum = 1; domain.delta.gpu_grid_steps = 65535;
  input.consts[0] = Value::from_float(42.125);
  for (input.seed = 0; input.seed < 64; ++input.seed) {
    GrammarRandom random(input.seed); (void)random.bounded(1);
    const auto index = random.bounded(65536);
    volatile double fraction = static_cast<double>(index)/65535.0;
    volatile double scaled = fraction*2.0;
    volatile double delta = scaled-1.0;
    const auto expected = Value::from_float(42.125+delta);
    const auto result = run(input);
    check(result.status == DConstantMutationResult::Applied &&
          equal(result.consts[result.nodes[0].i0],expected) && equal(result.consts[result.nodes[1].i0],expected) &&
          equal(result.consts[result.nodes[2].i0],input.consts[0]),"device Float addition resampled or changed fixed alias");
  }
  domain.delta.float_minimum = domain.delta.float_maximum = 2;
  input.consts[0] = Value::from_float(99);
  const auto retained = run(input);
  check(retained.status == DConstantMutationResult::Applied && retained.consts[retained.nodes[0].i0].f == 99,
        "device out-of-domain Float proposal clamped or failed");
  domain.delta.float_minimum = 3;
  const auto invalid = run(input);
  check(invalid.status == DConstantMutationResult::Invalid && invalid.const_count == 1 && invalid.consts[0].f == 99,
        "invalid additive descriptor was not transactional");
  const double maximum = std::numeric_limits<double>::max();
  domain.float_minimum = -maximum; domain.float_maximum = maximum;
  domain.delta.gpu_grid_steps = 0;
  for (const auto pair : std::vector<std::pair<double,double>>{{maximum,maximum},{-maximum,-maximum},
      {-0.0,-0.0},{0.0,-0.0},{std::numeric_limits<double>::denorm_min(),std::numeric_limits<double>::denorm_min()}}) {
    input.consts[0] = Value::from_float(pair.first);
    domain.delta.float_minimum = domain.delta.float_maximum = pair.second;
    const double sum = pair.first + pair.second;
    const auto expected = Value::from_float(std::isfinite(sum) ? sum : pair.first);
    const auto result = run(input);
    check(result.status == DConstantMutationResult::Applied && equal(result.consts[result.nodes[0].i0],expected),
          "device Float addition mishandled overflow, signed zero or subnormals");
  }
}

void test_full_width_integer_range() {
  constexpr std::int64_t low = std::numeric_limits<std::int64_t>::min();
  constexpr std::int64_t high = std::numeric_limits<std::int64_t>::max();
  Input input;
  input.nodes = {constant_node(0)};
  input.origins = {{0, -1}};
  input.consts = {Value::from_int(0)};
  input.capacity = 1;
  input.domains = {{static_cast<int>(RType::Int), 0, 0, 1, low, high}};
  input.groups = {group(0)};
  input.base_origins = {0};
  input.seed = UINT64_C(0xfedcba9876543210);
  GrammarRandom expected(input.seed);
  (void)expected.bounded(1);
  const std::int64_t expected_value = expected.integer(low, high);
  const Result result = run(input);
  check(result.status == DConstantMutationResult::Applied,
        "full-width integer mutation status");
  check(result.consts[0].tag == ValueTag::Int &&
            result.consts[0].i == expected_value,
        "full-width integer sampling diverged from host RNG");
}

void test_capacity_is_transactional() {
  Input input;
  input.nodes = {constant_node(0), constant_node(1)};
  input.origins = {{0, -1}, {1, -1}};
  input.consts = {Value::from_int(1), Value::from_int(2)};
  input.capacity = 2;
  input.domains = {finite_domain(RType::Int, 0, 1)};
  input.domain_values = {Value::from_int(3)};
  input.groups = {group(0)};
  input.base_origins = {0, -1};
  input.pinned = {0};
  const Result result = run(input);
  check(result.status == DConstantMutationResult::Capacity, "capacity status");
  check(result.const_count == 2 && result.nodes[0].i0 == 0 &&
            result.nodes[1].i0 == 1 && result.consts[0].i == 1 &&
            result.consts[1].i == 2,
        "capacity failure modified published child");
  check(result.pinned_remaps[0] == 0x13579,
        "capacity failure published pinned remap");
}

__global__ void quantization_kernel(const double* values, double* results, int count) {
  const int index = static_cast<int>(threadIdx.x);
  if (index < count) results[index] = gagp::evo::grammar::quantize_float(values[index],1000);
}

void test_quantization_rounding() {
  const std::vector<double> inputs{-0.0005,0.0005,-0.0001,0.0001,-0.0,0.0,-8,8,1.001};
  Managed<double> values(inputs.size()), results(inputs.size());
  copy_in(values,inputs);
  quantization_kernel<<<1,32>>>(values.data(),results.data(),static_cast<int>(inputs.size()));
  check_cuda(cudaGetLastError(), "quantization kernel launch");
  check_cuda(cudaDeviceSynchronize(), "quantization kernel execution");
  for (std::size_t i = 0; i < inputs.size(); ++i)
    check(equal(Value::from_float(results[i]),Value::from_float(
        gagp::evo::grammar::quantize_float(inputs[i],1000))),
        "quantization changed rounding or signed-zero bits on GPU");
}

void test_float_ranges() {
  const double hi = std::numeric_limits<double>::max();
  for (const auto bounds : std::vector<std::pair<double,double>>{{-8,8}, {-5,5}, {-hi,hi}, {hi/2,hi},
                            {-0.0,0.0}, {0,std::numeric_limits<double>::denorm_min()},
                            {1,std::nextafter(1.0,2.0)}}) {
    Input input;
    input.nodes = {constant_node(0)};
    input.origins = {{0,-1}};
    input.consts = {Value::from_float(0)};
    input.capacity = 1;
    ConstantMutationDomain domain;
    domain.type = static_cast<int>(RType::Float);
    domain.float_range = 1;
    domain.float_minimum = bounds.first;
    domain.float_maximum = bounds.second;
    domain.float_quantization_scale = bounds.first == -8 ? 1000 : 0;
    input.domains = {domain};
    input.groups = {group(0)};
    input.base_origins = {0};
    for (std::uint64_t seed : {UINT64_C(0),UINT64_C(1),UINT64_C(42),UINT64_MAX}) {
      input.seed = seed;
      GrammarRandom random(seed);
      (void)random.bounded(1);
      const auto expected = Value::from_float(gagp::evo::grammar::quantize_float(
          gagp::evo::grammar::sample_float_interval(bounds.first,bounds.second,random.next()),
          domain.float_quantization_scale));
      const auto result = run(input);
      check(result.status == DConstantMutationResult::Applied && equal(result.consts[0],expected),
            "Float interval device sampling differs from CPU bits");
    }
    for (int invalid = 0; invalid < 5; ++invalid) {
      input.domains[0] = domain;
      if (invalid == 0) input.domains[0].integer_range = 1;
      if (invalid == 1) input.domains[0].float_range = 2;
      if (invalid == 2) input.domains[0].type = static_cast<int>(RType::Int);
      if (invalid == 3) input.domains[0].float_maximum = std::numeric_limits<double>::infinity();
      if (invalid == 4) input.domains[0].float_quantization_scale = -1;
      const auto result = run(input);
      check(result.status == DConstantMutationResult::Invalid && equal(result.consts[0],input.consts[0]),
            "invalid Float interval changed published child");
    }
  }
}

void test_exact_float_compaction_and_nan_pin() {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  Input input;
  input.nodes = {constant_node(0), constant_node(1), constant_node(2)};
  input.origins = {{0, -1}, {1, -1}, {2, -1}};
  input.consts = {Value::from_int(1), Value::from_float(0.0),
                  Value::from_float(-0.0), Value::from_float(nan)};
  input.capacity = 4;
  input.domains = {finite_domain(RType::Int, 0, 1)};
  input.domain_values = {Value::from_int(9)};
  input.groups = {group(0)};
  input.base_origins = {0, -1, -1};
  input.pinned = {3};
  const Result result = run(input);
  check(result.status == DConstantMutationResult::Applied,
        "exact float compaction status");
  check(result.const_count == 4, "signed zero constants were merged");
  check(result.nodes[1].i0 != result.nodes[2].i0,
        "positive and negative zero aliases collapsed");
  check(equal(result.consts[result.nodes[1].i0], Value::from_float(0.0)) &&
            equal(result.consts[result.nodes[2].i0], Value::from_float(-0.0)),
        "signed zero payload bits changed");
  check(result.pinned_remaps[0] >= 0 &&
            equal(result.consts[result.pinned_remaps[0]], Value::from_float(nan)),
        "pinned NaN was not retained and remapped");
}

void test_empty_groups_and_invalid_origin() {
  Input empty;
  empty.nodes = {constant_node(0)};
  empty.origins = {{0, -1}};
  empty.consts = {Value::from_bool(false)};
  empty.capacity = 1;
  empty.base_origins = {-1};
  const Result no_groups = run(empty);
  check(no_groups.status == DConstantMutationResult::NoGroups,
        "fixed-only child did not request subtree fallback");
  check(no_groups.nodes[0].i0 == 0 && !no_groups.consts[0].b,
        "NoGroups modified published child");

  Input invalid = empty;
  invalid.origins = {{1, -1}};
  const Result bad_origin = run(invalid);
  check(bad_origin.status == DConstantMutationResult::Invalid,
        "out-of-range atomic origin was accepted");
  check(bad_origin.nodes[0].i0 == 0 && !bad_origin.consts[0].b,
        "invalid origin modified published child");

  Input invalid_kind;
  invalid_kind.nodes = {
      {static_cast<int>(NodeKind::ADD), 0x2468, 0x1357}};
  invalid_kind.origins = {{0, -1}};
  invalid_kind.consts = {Value::from_int(4)};
  invalid_kind.capacity = 1;
  invalid_kind.domains = {finite_domain(RType::Int, 0, 1)};
  invalid_kind.domain_values = {Value::from_int(8)};
  invalid_kind.groups = {group(0)};
  invalid_kind.base_origins = {0};
  const Result bad_kind = run(invalid_kind);
  check(bad_kind.status == DConstantMutationResult::Invalid,
        "mutable group provenance on non-constant node was accepted");
  check(bad_kind.nodes[0].i0 == 0x2468 && bad_kind.consts[0].i == 4,
        "invalid non-constant provenance modified published child");
}

}  // namespace

int main() {
  try {
    gagp::FitnessSessionGpu session;
    const auto initialized =
        session.init({{}}, {Value::from_int(0)}, 1, 1, 1.0);
    check(initialized.ok, "could not initialize CUDA for constant mutation test");
    test_source_occurrences_and_base_namespace();
    test_fixed_alias_and_pinned_root();
    test_finite_domains_all_public_tags();
    test_keep_and_flip();
    test_additive_policy();
    test_full_width_integer_range();
    test_float_ranges();
    test_quantization_rounding();
    test_capacity_is_transactional();
    test_exact_float_compaction_and_nan_pin();
    test_empty_groups_and_invalid_origin();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "compiled logical constant mutation GPU tests passed\n";
  return 0;
}
