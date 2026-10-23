#include "owned_programs.hpp"
#include "gagp/core/semantic_fuel.hpp"

#include "gagp/runtime/gpu/fitness_gpu.hpp"

#include <cuda_runtime.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gagp/runtime/payload/payload.hpp"
#include "gagp/runtime/gpu/constants_gpu.hpp"
#include "gagp/runtime/gpu/host_pack_gpu.hpp"
#include "device/kernels.cuh"
#include "opcode_map_gpu.hpp"
#include "view_profile.hpp"
#include "payload_workspace_budget.hpp"
#include "case_sort_policy.hpp"
#include "case_reuse.hpp"
#include "shape_result_cache.hpp"
#include "shape_partition_cache.hpp"
#include "short_edit_patterns.hpp"
#include "private_global_offsets.hpp"
#include "private_tuple_policy.hpp"
#include "stage_storage_gpu.hpp"
#include "gagp/runtime/gpu/device_types_gpu.hpp"

namespace gagp {

namespace {

using gpu_detail::DInstr;
using gpu_detail::DListPayloadEntry;
using gpu_detail::DProgramMeta;
using gpu_detail::DResult;
using gpu_detail::DStringPayloadEntry;

// One backing only: unused retained capacity counts against every current stage.
struct ReusablePrivateWorkspace {
  void* data=nullptr;std::size_t capacity=0,allocations=0,peak_bytes=0;int device=-1;bool busy=false;
  bool clear() {
    if(!data)return true;
    int previous=-1;cudaGetDevice(&previous);
    const auto status=cudaSetDevice(device)==cudaSuccess?gpu_detail::cuda_pool_free(data):cudaErrorInvalidDevice;
    if(previous>=0 && previous!=device)cudaSetDevice(previous);
    if(status!=cudaSuccess)return false;
    data=nullptr;capacity=0;return true;
  }
  ~ReusablePrivateWorkspace(){clear();}
  bool trim(std::size_t limit){return capacity<=limit || clear();}
  bool reserve(std::size_t bytes,std::size_t limit,std::size_t extra) {
    if(bytes>limit || !trim(limit))return false;
    if(bytes>capacity) {
      if(!clear() || cudaGetDevice(&device)!=cudaSuccess || gpu_detail::cuda_stage_workspace_malloc(&data,bytes)!=cudaSuccess)return false;
      capacity=bytes;++allocations;
    }
    peak_bytes=std::max(peak_bytes,capacity+extra);return true;
  }
};

// Shared bounded storage for initial generic payload evaluation and replay.
// All buffers are per physical lane; logical lane reduction remains unchanged.
struct PayloadWorkspaceStorage {
  static constexpr int threads=32;
  const std::size_t budget;
  ReusablePrivateWorkspace* reusable=nullptr;
  explicit PayloadWorkspaceStorage(std::size_t bytes,ReusablePrivateWorkspace* pool=nullptr):budget(bytes),reusable(pool) {}
  std::vector<void*> allocations;
  gpu_detail::DRegionWorkspace workspace{};
  std::size_t blocks=0;
  ~PayloadWorkspaceStorage(){clear();}
  void clear(){for(auto p:allocations)gpu_detail::cuda_pool_free(p);allocations.clear();}
  bool allocate(void** p,std::size_t bytes) {
    if(!bytes){*p=nullptr;return true;}
    if(gpu_detail::cuda_stage_workspace_malloc(p,bytes)!=cudaSuccess)return false;
    allocations.push_back(*p);return true;
  }
  bool prepare(gpu_detail::DRegionWorkspace prototype,std::size_t programs,int lanes,
      int entries,int values,int bytes,bool indexed,std::size_t extra_bytes=0) {
    workspace=prototype;
    workspace.compact_intlist_cells=workspace.private_intlists && workspace.compact_intlist_cells;
    if(workspace.compact_intlist_cells)workspace.payload_list_values=nullptr;
    else workspace.payload_intlist_cells=nullptr;
    workspace.window_executor=workspace.private_intlists && workspace.window_executor;workspace.coordinate_executor=false;
    workspace.unboxed_window_frames=false;workspace.window_soa=false;
    workspace.measure_phase_cache=workspace.private_intlists && workspace.measure_phase_cache;
    workspace.fitness_lanes=lanes;
    workspace.payload_entry_capacity=entries;
    workspace.payload_value_capacity=values;
    workspace.payload_byte_capacity=bytes;
    const auto frame_size=!workspace.compact_frames?sizeof(gpu_detail::DRegionFrame):
        workspace.two_state_frames?sizeof(gpu_detail::DCompactPairRegionFrame):sizeof(gpu_detail::DCompactRegionFrame);
    const auto frame_bytes=frame_size*workspace.frame_capacity;
    const auto key_bytes=sizeof(std::int64_t)*gpu_detail::DMAX_REGION_STATES*workspace.memo_capacity;
    const auto memo_bytes=sizeof(Value)*workspace.memo_capacity;
    // Private lists use checked offsets; only the immutable external table
    // retains a registry. No per-lane string/registry/index scratch is needed.
    const bool registry=!workspace.private_intlists;
    const bool epoch_index=registry && indexed && workspace.payload_epoch_index;
    if(!registry) {
      workspace.payload_string_entries=nullptr;workspace.payload_list_entries=nullptr;
      workspace.payload_string_bytes=nullptr;workspace.payload_list_index=nullptr;
      workspace.payload_list_epoch_index=nullptr;workspace.payload_list_epoch=nullptr;
    }
    const auto payload_bytes=(registry?std::size_t(entries)*(sizeof(gpu_detail::DStringPayloadEntry)+sizeof(gpu_detail::DListPayloadEntry))+
        bytes+(indexed?std::size_t(entries)*2*(epoch_index?sizeof(std::uint64_t):sizeof(int)):0)+
        (epoch_index?sizeof(unsigned):0):0)+std::size_t(values)*
        (workspace.compact_intlist_cells?sizeof(std::int64_t):sizeof(Value))+
        (workspace.concat_cache_enabled?sizeof(gpu_detail::DConcatCacheEntry)*gpu_detail::DCONCAT_CACHE_SLOTS:0);
    blocks=gpu_detail::payload_workspace_block_count(budget,programs,
        payload_bytes+frame_bytes+key_bytes+memo_bytes,extra_bytes);
    if(!blocks)return false;
    const auto slots=blocks*threads;
    if(reusable) {
      if(!workspace.private_intlists || !workspace.compact_intlist_cells || workspace.concat_cache_enabled)return false;
      // Every size is a multiple of eight; cudaMalloc supplies aligned backing.
      static_assert(alignof(gpu_detail::DRegionFrame)<=8 && alignof(gpu_detail::DCompactPairRegionFrame)<=8 &&
          alignof(gpu_detail::DCompactRegionFrame)<=8 && alignof(Value)<=8);
      const auto cell_bytes=slots*std::size_t(values)*sizeof(std::int64_t);
      const auto required=cell_bytes+slots*(frame_bytes+key_bytes+memo_bytes);
      if(!reusable->reserve(required,budget-extra_bytes,extra_bytes))return false;
      auto* base=static_cast<unsigned char*>(reusable->data);
      workspace.payload_intlist_cells=reinterpret_cast<std::int64_t*>(base);
      workspace.frames=frame_bytes?reinterpret_cast<gpu_detail::DRegionFrame*>(base+cell_bytes):nullptr;
      workspace.memo_keys=key_bytes?reinterpret_cast<std::int64_t*>(base+cell_bytes+slots*frame_bytes):nullptr;
      workspace.memo_values=memo_bytes?reinterpret_cast<Value*>(base+cell_bytes+slots*(frame_bytes+key_bytes)):nullptr;
      return true;
    }
    if(workspace.concat_cache_enabled && !allocate(reinterpret_cast<void**>(&workspace.concat_cache),
        slots*sizeof(gpu_detail::DConcatCacheEntry)*gpu_detail::DCONCAT_CACHE_SLOTS))return false;
    if(epoch_index) {
      const auto index_bytes=slots*entries*2*sizeof(std::uint64_t);
      if(!allocate(reinterpret_cast<void**>(&workspace.payload_list_epoch_index),index_bytes) ||
         !allocate(reinterpret_cast<void**>(&workspace.payload_list_epoch),slots*sizeof(unsigned)) ||
         cudaMemset(workspace.payload_list_epoch_index,0,index_bytes)!=cudaSuccess ||
         cudaMemset(workspace.payload_list_epoch,0,slots*sizeof(unsigned))!=cudaSuccess)return false;
    }
    return (!registry || !indexed || epoch_index || allocate(reinterpret_cast<void**>(&workspace.payload_list_index),slots*entries*2*sizeof(int))) &&
        (!registry || allocate(reinterpret_cast<void**>(&workspace.payload_string_entries),slots*entries*sizeof(gpu_detail::DStringPayloadEntry))) &&
        (!registry || allocate(reinterpret_cast<void**>(&workspace.payload_list_entries),slots*entries*sizeof(gpu_detail::DListPayloadEntry))) &&
        (!registry || allocate(reinterpret_cast<void**>(&workspace.payload_string_bytes),slots*bytes)) &&
        (workspace.compact_intlist_cells?
            allocate(reinterpret_cast<void**>(&workspace.payload_intlist_cells),slots*values*sizeof(std::int64_t)):
            allocate(reinterpret_cast<void**>(&workspace.payload_list_values),slots*values*sizeof(Value))) &&
        allocate(reinterpret_cast<void**>(&workspace.frames),slots*frame_bytes) &&
        allocate(reinterpret_cast<void**>(&workspace.memo_keys),slots*key_bytes) &&
        allocate(reinterpret_cast<void**>(&workspace.memo_values),slots*memo_bytes);
  }
};

struct HostPayloadPack {
  std::vector<DStringPayloadEntry> string_entries;
  std::vector<char> string_bytes;
  std::vector<DListPayloadEntry> list_entries;
  std::vector<Value> list_values;
};

// Only one root call per program owns the reusable key slice. Inspect current
// packed instructions, including freshly GPU-lowered phases, never cold caches.
bool dense_coordinate_memo_supported(const gpu_detail::PackResult& packed) {
  if(packed.metas.empty() || std::any_of(packed.all_phase_code.begin(),packed.all_phase_code.end(),
      [](const auto& ins){return ins.op==gpu_detail::OP_BOUNDED_REGION;}))return false;
  bool any=false;
  for(const auto& meta:packed.metas) {
    if(!meta.is_valid || meta.region_count!=1)return false;
    unsigned calls=0;
    for(int i=0;i<meta.code_len;++i)
      calls+=packed.all_code[meta.code_offset+i].op==gpu_detail::OP_BOUNDED_REGION;
    if(calls!=1)return false;
    any|=gpu_detail::dense_coordinate_memo_cells(packed.region_segments[meta.region_offset])!=0;
  }
  return any;
}

using HostStringPayloadLookup = std::unordered_map<std::int64_t, std::string>;
struct HostListPayloadKey {
  ValueTag tag = ValueTag::Invalid;
  std::int64_t packed = 0;

  bool operator==(const HostListPayloadKey& other) const {
    return tag == other.tag && packed == other.packed;
  }
};

struct HostListPayloadKeyHash {
  std::size_t operator()(const HostListPayloadKey& key) const {
    const std::uint64_t a = static_cast<std::uint64_t>(key.packed);
    return static_cast<std::size_t>((a * 11400714819323198485ULL) ^
                                    static_cast<std::uint64_t>(key.tag));
  }
};

using HostListPayloadLookup = std::unordered_map<HostListPayloadKey, std::vector<Value>, HostListPayloadKeyHash>;

constexpr unsigned kPayloadMaskString = 1U << 0;
constexpr unsigned kPayloadMaskList = 1U << 1;

unsigned value_payload_mask(const Value& v) {
  if (v.tag == ValueTag::String) return kPayloadMaskString;
  if (v.tag == ValueTag::IntList || v.tag == ValueTag::FloatList || v.tag == ValueTag::StringList) {
    return kPayloadMaskList;
  }
  return 0U;
}

void sort_and_unique_tokens(std::vector<std::int64_t>* tokens) {
  std::sort(tokens->begin(), tokens->end());
  tokens->erase(std::unique(tokens->begin(), tokens->end()), tokens->end());
}

void sort_and_unique_list_tokens(std::vector<Value>* tokens) {
  std::sort(tokens->begin(), tokens->end(), [](const Value& a, const Value& b) {
    if (a.tag != b.tag) return static_cast<int>(a.tag) < static_cast<int>(b.tag);
    return a.i < b.i;
  });
  tokens->erase(std::unique(tokens->begin(), tokens->end(), [](const Value& a, const Value& b) {
                  return a.tag == b.tag && a.i == b.i;
                }),
                tokens->end());
}

void append_payload_tokens_from_values(const std::vector<Value>& values,
                                       std::vector<std::int64_t>* string_tokens,
                                       std::vector<Value>* list_tokens) {
  for (const Value& v : values) {
    if (v.tag == ValueTag::String) {
      string_tokens->push_back(v.i);
    } else if (v.tag == ValueTag::IntList || v.tag == ValueTag::FloatList || v.tag == ValueTag::StringList) {
      list_tokens->push_back(v);
      if (v.tag == ValueTag::StringList) {
        std::vector<Value> elems;
        if (payload::lookup_list(v, &elems)) {
          for (const Value& elem : elems) {
            if (elem.tag == ValueTag::String) {
              string_tokens->push_back(elem.i);
            }
          }
        }
      }
    }
  }
}

HostPayloadPack build_payload_pack(const HostStringPayloadLookup& string_lookup,
                                   const HostListPayloadLookup& list_lookup,
                                   std::vector<std::int64_t> string_tokens,
                                   std::vector<Value> list_tokens) {
  HostPayloadPack out;
  sort_and_unique_tokens(&string_tokens);
  sort_and_unique_list_tokens(&list_tokens);

  out.string_entries.reserve(string_tokens.size());
  for (const std::int64_t packed : string_tokens) {
    const auto it = string_lookup.find(packed);
    if (it == string_lookup.end()) {
      continue;
    }
    DStringPayloadEntry e;
    e.packed = packed;
    e.offset = static_cast<int>(out.string_bytes.size());
    e.len = static_cast<int>(it->second.size());
    out.string_entries.push_back(e);
    out.string_bytes.insert(out.string_bytes.end(), it->second.begin(), it->second.end());
  }

  out.list_entries.reserve(list_tokens.size());
  for (const Value& list_value : list_tokens) {
    const auto it = list_lookup.find(HostListPayloadKey{list_value.tag, list_value.i});
    if (it == list_lookup.end()) {
      continue;
    }
    DListPayloadEntry e;
    e.tag = list_value.tag;
    e.packed = list_value.i;
    e.offset = static_cast<int>(out.list_values.size());
    e.len = static_cast<int>(it->second.size());
    out.list_entries.push_back(e);
    out.list_values.insert(out.list_values.end(), it->second.begin(), it->second.end());
  }
  return out;
}

void populate_payload_cache(const std::vector<std::int64_t>& string_tokens,
                            const std::vector<Value>& list_tokens,
                            HostStringPayloadLookup* string_lookup,
                            HostListPayloadLookup* list_lookup) {
  for (const std::int64_t packed : string_tokens) {
    if (string_lookup->find(packed) != string_lookup->end()) {
      continue;
    }
    std::string payload_value;
    if (!payload::lookup_string_packed(packed, &payload_value)) {
      continue;
    }
    (*string_lookup)[packed] = std::move(payload_value);
  }
  for (const Value& list_value : list_tokens) {
    const HostListPayloadKey key{list_value.tag, list_value.i};
    if (list_lookup->find(key) != list_lookup->end()) {
      continue;
    }
    std::vector<Value> payload_value;
    if (!payload::lookup_list_packed(list_value.tag, list_value.i, &payload_value)) {
      continue;
    }
    (*list_lookup)[key] = std::move(payload_value);
  }
}

FitnessSessionInitResult fitness_init_single_error(ErrCode code, const std::string& message) {
  FitnessSessionInitResult out;
  out.ok = false;
  out.err = Err{code, message};
  return out;
}

FitnessEvalResult fitness_eval_single_error(ErrCode code, const std::string& message) {
  FitnessEvalResult out;
  out.ok = false;
  out.err = Err{code, message};
  return out;
}

double ms_between(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}

bool query_device(int dev, cudaDeviceProp& props_out, std::string& message_out) {
  const cudaError_t set_err = cudaSetDevice(dev);
  if (set_err != cudaSuccess) {
    message_out = "cuda device select failure err=";
    message_out += cudaGetErrorString(set_err);
    return false;
  }

  const cudaError_t prop_err = cudaGetDeviceProperties(&props_out, dev);
  if (prop_err != cudaSuccess) {
    message_out = "cuda device query failure err=";
    message_out += cudaGetErrorString(prop_err);
    return false;
  }
  return true;
}

bool parse_env_device_override(int* out_dev) {
  const char* raw = std::getenv("GAGP_CUDA_DEVICE");
  if (raw == nullptr || *raw == '\0') {
    return false;
  }
  char* end = nullptr;
  const long parsed = std::strtol(raw, &end, 10);
  if (end == raw || *end != '\0' || parsed < 0 || parsed > std::numeric_limits<int>::max()) {
    return false;
  }
  *out_dev = static_cast<int>(parsed);
  return true;
}

bool select_gpu_device(cudaDeviceProp& props_out, std::string& message_out) {
  int device_count = 0;
  const cudaError_t count_err = cudaGetDeviceCount(&device_count);
  if (count_err != cudaSuccess || device_count <= 0) {
    message_out = "cuda device unavailable";
    if (count_err != cudaSuccess) {
      message_out += " err=";
      message_out += cudaGetErrorString(count_err);
    }
    return false;
  }

  int override_dev = -1;
  if (parse_env_device_override(&override_dev)) {
    if (override_dev >= device_count) {
      message_out = "cuda device override out of range";
      return false;
    }
    return query_device(override_dev, props_out, message_out);
  }

  int best_dev = -1;
  double best_used_ratio = 2.0;
  std::size_t best_used_bytes = std::numeric_limits<std::size_t>::max();

  for (int dev = 0; dev < device_count; ++dev) {
    if (cudaSetDevice(dev) != cudaSuccess) {
      continue;
    }
    std::size_t free_bytes = 0;
    std::size_t total_bytes = 0;
    if (cudaMemGetInfo(&free_bytes, &total_bytes) != cudaSuccess || total_bytes == 0) {
      continue;
    }
    const std::size_t used_bytes = total_bytes - free_bytes;
    const double used_ratio = static_cast<double>(used_bytes) / static_cast<double>(total_bytes);

    if (best_dev < 0 || used_ratio < best_used_ratio ||
        (std::fabs(used_ratio - best_used_ratio) <= 1e-12 && used_bytes < best_used_bytes)) {
      best_dev = dev;
      best_used_ratio = used_ratio;
      best_used_bytes = used_bytes;
    }
  }

  if (best_dev < 0) {
    message_out = "cuda device unavailable err=no usable device found";
    return false;
  }

  return query_device(best_dev, props_out, message_out);
}

unsigned shared_cases_payload_mask(const std::vector<CaseBindings>& shared_cases) {
  unsigned mask = 0U;
  for (const CaseBindings& case_bindings : shared_cases) {
    for (const gagp::InputBinding& binding : case_bindings) {
      mask |= value_payload_mask(binding.value);
    }
  }
  return mask;
}

std::size_t kernel_shared_bytes(std::size_t max_code_len, int blocksize) {
  return max_code_len * sizeof(DInstr) + (alignof(double) - 1u) +
         static_cast<std::size_t>(blocksize) * sizeof(double);
}

}  // namespace

namespace {std::atomic<std::uint64_t> next_fitness_revision{1};}
struct FitnessSessionGpu::Impl {
  std::uint64_t evaluation_revision=0;
  ReusablePrivateWorkspace private_workspace;
  bool ready = false;
  bool short_edit_requested = false,short_edit_enabled = false;
  gpu_detail::DShortEditPattern* d_short_edit_patterns = nullptr;
  gpu_detail::DShortEditEntry* d_short_edit_entries = nullptr;
  int short_edit_entry_count = 0;
  int device_id = -1;
  int fuel = 10000;
  int blocksize = 1024;
  double penalty = 1.0;
  FitnessPolicy policy;
  int shared_case_count = 0;
  unsigned shared_input_payload_mask = 0U;
  std::uint32_t shared_max_intlist_input_length = 0;
  std::array<ValueTag, 64> shared_input_types{};
  bool private_answers_prepared = false;
  std::vector<Value> private_answer_tokens;
  std::size_t shared_bytes = 0;
  std::vector<Value> host_case_local_vals;
  Value* d_shared_case_local_vals = nullptr;
  unsigned char* d_shared_case_local_set = nullptr;
  Value* d_expected = nullptr;
  int* d_case_order = nullptr;
  int* d_arena_case_order = nullptr;
  gpu_detail::IntegerCaseReusePlan integer_case_reuse;
  int* d_reuse_representatives = nullptr;
  unsigned* d_reuse_weights = nullptr;
  bool case_reuse_requested = false;
  gpu_detail::ShapePartitionCache shape_partitions;
  std::vector<unsigned char> host_case_local_set;
  bool project_shape_inputs=false;
  int* d_shape_representatives = nullptr;
  int* d_shape_groups = nullptr;
  bool shape_cache_requested = false;
  mutable HostStringPayloadLookup host_string_payloads;
  mutable HostListPayloadLookup host_list_payloads;
  std::vector<std::int64_t> shared_string_tokens;
  std::vector<Value> shared_list_tokens;
  cudaDeviceProp props{};
  Err last_err{ErrCode::Value, ""};
  gpu_detail::DRegionFrame* region_frames = nullptr;
  std::int64_t* region_memo_keys = nullptr;
  Value* region_memo_values = nullptr;
  std::size_t region_frame_bytes = 0;
  std::size_t region_key_bytes = 0;
  std::size_t region_value_bytes = 0;

  void release_region_workspace() {
    if (region_frames) gpu_detail::cuda_pool_free(region_frames);
    if (region_memo_keys) gpu_detail::cuda_pool_free(region_memo_keys);
    if (region_memo_values) gpu_detail::cuda_pool_free(region_memo_values);
    region_frames = nullptr;
    region_memo_keys = nullptr;
    region_memo_values = nullptr;
    region_frame_bytes = region_key_bytes = region_value_bytes = 0;
  }

  bool reserve_region_workspace(std::size_t frames, std::size_t keys,
                                std::size_t values) {
    if (frames == region_frame_bytes && keys == region_key_bytes &&
        values == region_value_bytes) return true;
    // Replace the whole layout so retained high-water marks from different
    // frame/memo configurations cannot exceed the workspace budget.
    release_region_workspace();
    if ((frames && gpu_detail::cuda_pool_malloc(reinterpret_cast<void**>(&region_frames), frames) != cudaSuccess) ||
        (keys && gpu_detail::cuda_pool_malloc(reinterpret_cast<void**>(&region_memo_keys), keys) != cudaSuccess) ||
        (values && gpu_detail::cuda_pool_malloc(reinterpret_cast<void**>(&region_memo_values), values) != cudaSuccess)) {
      release_region_workspace();
      return false;
    }
    region_frame_bytes = frames;
    region_key_bytes = keys;
    region_value_bytes = values;
    return true;
  }

  ~Impl() {
    release_region_workspace();
    if (d_shared_case_local_vals) gpu_detail::cuda_pool_free(d_shared_case_local_vals);
    if (d_shared_case_local_set) gpu_detail::cuda_pool_free(d_shared_case_local_set);
    if (d_expected) gpu_detail::cuda_pool_free(d_expected);
    if (d_short_edit_patterns) gpu_detail::cuda_pool_free(d_short_edit_patterns);
    if (d_short_edit_entries) gpu_detail::cuda_pool_free(d_short_edit_entries);
    if (d_case_order) gpu_detail::cuda_pool_free(d_case_order);
    if (d_arena_case_order) gpu_detail::cuda_pool_free(d_arena_case_order);
    if (d_reuse_representatives) gpu_detail::cuda_pool_free(d_reuse_representatives);
    if (d_reuse_weights) gpu_detail::cuda_pool_free(d_reuse_weights);
    if (d_shape_representatives) gpu_detail::cuda_pool_free(d_shape_representatives);
    if (d_shape_groups) gpu_detail::cuda_pool_free(d_shape_groups);
  }
};

FitnessSessionGpu::FitnessSessionGpu() : impl_(std::make_unique<Impl>()) {}
FitnessSessionGpu::~FitnessSessionGpu() = default;
FitnessSessionGpu::FitnessSessionGpu(FitnessSessionGpu&&) noexcept = default;
FitnessSessionGpu& FitnessSessionGpu::operator=(FitnessSessionGpu&&) noexcept = default;

bool FitnessSessionGpu::is_ready() const { return impl_ && impl_->ready; }
std::uint64_t FitnessSessionGpu::evaluation_revision() const {
  return impl_ && impl_->ready?impl_->evaluation_revision:0;
}

bool FitnessSessionGpu::release_private_workspace() const {
  return impl_ && !impl_->private_workspace.busy && impl_->private_workspace.clear();
}

FitnessSessionInitResult FitnessSessionGpu::init(const std::vector<CaseBindings>& shared_cases,
                                                 const std::vector<Value>& shared_answer,
                                                 int fuel,
                                                 int blocksize,
                                                 double penalty, FitnessPolicy policy) {
  impl_->short_edit_enabled=false;
  const char* short_edit_setting=std::getenv("GAGP_GPU_SHORT_EDIT_DISTANCE");
  impl_->short_edit_requested=short_edit_setting && std::string(short_edit_setting)=="1";
  // Every init attempt invalidates prior owner scores, including failed reinit.
  impl_->evaluation_revision=next_fitness_revision.fetch_add(1,std::memory_order_relaxed);
  if(!valid_fitness_policy(policy))return fitness_init_single_error(ErrCode::Value,"invalid fitness policy");
  if(policy.metric==FitnessMetric::Levenshtein || policy.metric==FitnessMetric::TupleSummary)
    for(const auto& answer:shared_answer)
      if(!is_container(answer) || Value::container_len(answer)>kFitnessMaxSequence)
        return fitness_init_single_error(ErrCode::Value,"sequence fitness requires a container answer of at most 1024 elements");
  if(policy.metric==FitnessMetric::TupleSummary) {
    for(const auto& answer:shared_answer) {
      std::vector<Value> tuple;
      if(answer.tag!=ValueTag::IntList || !payload::lookup_list(answer,&tuple) ||
          !valid_fitness_tuple(tuple.data(),int(tuple.size()),policy))
        return fitness_init_single_error(ErrCode::Value,"TupleSummary answer must contain exactly summary_count+1 Int fields");
    }
  }
  if (shared_cases.empty()) {
    return fitness_init_single_error(ErrCode::Value, "shared_cases must not be empty");
  }
  if (shared_answer.size() != shared_cases.size()) {
    return fitness_init_single_error(ErrCode::Value, "shared_answer size mismatch");
  }
  if (blocksize <= 0) {
    return fitness_init_single_error(ErrCode::Value, "invalid gpu blocksize");
  }

  const auto all_t0 = std::chrono::steady_clock::now();
  if(impl_->private_workspace.busy)return fitness_init_single_error(ErrCode::Value,"GPU private workspace is in use");
  if(!impl_->private_workspace.clear())return fitness_init_single_error(ErrCode::Value,"cuda private workspace release failure");
  const auto device_select_t0 = all_t0;
  std::string select_msg;
  if (!select_gpu_device(impl_->props, select_msg)) {
    return fitness_init_single_error(ErrCode::Value, select_msg);
  }
  if (blocksize > impl_->props.maxThreadsPerBlock) {
    return fitness_init_single_error(ErrCode::Value, "gpu blocksize exceeds maxThreadsPerBlock");
  }
  int device_id = -1;
  if (cudaGetDevice(&device_id) != cudaSuccess) {
    return fitness_init_single_error(ErrCode::Value, "cuda device query failure");
  }
  const auto device_select_t1 = std::chrono::steady_clock::now();

  std::vector<Value> packed_case_local_vals;
  std::vector<unsigned char> packed_case_local_set;
  const auto shared_case_pack_t0 = std::chrono::steady_clock::now();
  gpu_detail::pack_shared_cases_only(shared_cases, &packed_case_local_vals, &packed_case_local_set);
  impl_->shared_input_types.fill(ValueTag::Invalid);
  impl_->shared_max_intlist_input_length = 0;
  for (int local = 0; local < 64; ++local) {
    auto type = packed_case_local_set[local] ? packed_case_local_vals[local].tag : ValueTag::Invalid;
    for (std::size_t c = 0; c < shared_cases.size(); ++c) {
      const auto i = c * 64 + local;
      if (!packed_case_local_set[i] || packed_case_local_vals[i].tag != type) type = ValueTag::Invalid;
      // Scheduling observes only effective bindings after duplicate/local normalization.
      if (packed_case_local_set[i] && packed_case_local_vals[i].tag == ValueTag::IntList)
        impl_->shared_max_intlist_input_length = std::max(
            impl_->shared_max_intlist_input_length, Value::container_len(packed_case_local_vals[i]));
      // Container b is semantically unused; reserve the marker for device views.
      if (packed_case_local_vals[i].tag == ValueTag::IntList) packed_case_local_vals[i].b = false;
    }
    impl_->shared_input_types[local] = type;
  }
  std::vector<int> case_order;
  // Only view kernels consume this permutation: their type-flow proof excludes
  // Float outputs. Integer Absolute/Capped and Exact therefore have integral
  // case scores, including errors. Bound the sum below the exact integer range
  // of the canonical reduction. Discounted/TupleSummary keep original order.
  const bool sortable_policy = policy.metric == FitnessMetric::Legacy ||
      policy.metric == FitnessMetric::Exact ||
      (policy.metric == FitnessMetric::Absolute && policy.proximity == FitnessProximity::Capped);
  const double case_ceiling = fitness_error_ceiling(policy, penalty);
  if (sortable_policy && std::getenv("GAGP_SORT_CASES") && std::isfinite(case_ceiling) && std::floor(case_ceiling) == case_ceiling &&
      shared_cases.size() * std::max(1.0, case_ceiling) < 1099511627776.0 &&
      std::all_of(shared_answer.begin(), shared_answer.end(), [](const auto& v) { return v.tag == ValueTag::Int || v.tag == ValueTag::Bool; })) {
    std::vector<std::uint64_t> lengths(shared_cases.size(), 0);
    for (std::size_t c = 0; c < shared_cases.size(); ++c)
      for (int local = 0; local < 64; ++local) {
        const auto& v = packed_case_local_vals[c * 64 + local];
        if (packed_case_local_set[c * 64 + local] && v.tag == ValueTag::IntList) lengths[c] += Value::container_len(v);
      }
    case_order.resize(shared_cases.size());
    std::iota(case_order.begin(), case_order.end(), 0);
    std::stable_sort(case_order.begin(), case_order.end(), [&](int a, int b) { return lengths[a] < lengths[b]; });
  }
  std::vector<int> arena_case_order;
  if(std::getenv("GAGP_GPU_SORT_ARENA_CASES") && std::string(std::getenv("GAGP_GPU_SORT_ARENA_CASES"))=="1" &&
      gpu_detail::arena_case_sort_is_exact(policy,penalty,shared_cases.size())) {
    std::vector<std::uint64_t> lengths(shared_cases.size(),0);
    for(std::size_t c=0;c<shared_cases.size();++c)
      for(int local=0;local<64;++local) {
        const auto& value=packed_case_local_vals[c*64+local];
        if(packed_case_local_set[c*64+local] && value.tag==ValueTag::IntList)
          lengths[c]+=Value::container_len(value);
      }
    arena_case_order.resize(shared_cases.size());
    std::iota(arena_case_order.begin(),arena_case_order.end(),0);
    std::stable_sort(arena_case_order.begin(),arena_case_order.end(),
        [&](int a,int b){return lengths[a]<lengths[b];});
  }
  const auto shared_case_pack_t1 = std::chrono::steady_clock::now();

  const char* reuse_cases=std::getenv("GAGP_GPU_INTEGER_CASE_REUSE");
  impl_->case_reuse_requested=reuse_cases && std::string(reuse_cases)=="1";
  impl_->integer_case_reuse=impl_->case_reuse_requested?
      gpu_detail::integer_case_reuse_plan(packed_case_local_vals,packed_case_local_set,shared_answer,policy,penalty):
      gpu_detail::IntegerCaseReusePlan{};

  const auto payload_cache_t0 = std::chrono::steady_clock::now();
  impl_->host_string_payloads.clear();
  impl_->host_list_payloads.clear();
  impl_->shared_string_tokens.clear();
  impl_->shared_list_tokens.clear();
  append_payload_tokens_from_values(packed_case_local_vals, &impl_->shared_string_tokens, &impl_->shared_list_tokens);
  impl_->private_answers_prepared=std::getenv("GAGP_GPU_PRIVATE_INTLIST_OFFSETS") &&
      std::string(std::getenv("GAGP_GPU_PRIVATE_INTLIST_OFFSETS"))=="1";
  if(policy.metric==FitnessMetric::Levenshtein || policy.metric==FitnessMetric::TupleSummary)
    append_payload_tokens_from_values(shared_answer, &impl_->shared_string_tokens, &impl_->shared_list_tokens);
  impl_->private_answer_tokens.clear();
  if(impl_->private_answers_prepared && policy.metric==FitnessMetric::Exact) {
    std::vector<std::int64_t> unused_strings;
    append_payload_tokens_from_values(shared_answer,&unused_strings,&impl_->private_answer_tokens);
  }
  sort_and_unique_tokens(&impl_->shared_string_tokens);
  sort_and_unique_list_tokens(&impl_->shared_list_tokens);
  populate_payload_cache(impl_->shared_string_tokens, impl_->shared_list_tokens, &impl_->host_string_payloads,
                         &impl_->host_list_payloads);
  impl_->host_case_local_vals = packed_case_local_vals;
  const auto* shape_setting=std::getenv("GAGP_GPU_SHAPE_RESULT_CACHE");
  impl_->shape_cache_requested=shape_setting && std::string(shape_setting)=="1";
  const auto* project_setting=std::getenv("GAGP_GPU_PROJECT_SHAPE_INPUTS");
  impl_->project_shape_inputs=project_setting && std::string(project_setting)=="1";
  impl_->shape_partitions.clear();
  impl_->host_case_local_set=packed_case_local_set;
  gpu_detail::HostShortEditPatterns short_edit_plan;
  if(impl_->short_edit_requested && policy.metric==FitnessMetric::Levenshtein) {
    short_edit_plan=gpu_detail::prepare_short_edit_patterns(shared_answer,[&](const Value& answer)->const std::vector<Value>* {
      const auto found=impl_->host_list_payloads.find(HostListPayloadKey{answer.tag,answer.i});
      return found==impl_->host_list_payloads.end()?nullptr:&found->second;
    });
  }
  const auto payload_cache_t1 = std::chrono::steady_clock::now();
  const auto upload_t0 = std::chrono::steady_clock::now();
  if(impl_->d_short_edit_patterns){gpu_detail::cuda_pool_free(impl_->d_short_edit_patterns);impl_->d_short_edit_patterns=nullptr;}
  if(impl_->d_short_edit_entries){gpu_detail::cuda_pool_free(impl_->d_short_edit_entries);impl_->d_short_edit_entries=nullptr;}
  impl_->short_edit_entry_count=0;
  if(short_edit_plan.eligible) {
    if(!gpu_detail::cuda_alloc_and_copy_in(short_edit_plan.patterns,&impl_->d_short_edit_patterns) ||
        !gpu_detail::cuda_alloc_and_copy_in(short_edit_plan.entries,&impl_->d_short_edit_entries))
      return fitness_init_single_error(ErrCode::Value,"cuda short edit pattern allocation failure");
    impl_->short_edit_entry_count=static_cast<int>(short_edit_plan.entries.size());
  }
  if(impl_->d_shape_representatives){gpu_detail::cuda_pool_free(impl_->d_shape_representatives);impl_->d_shape_representatives=nullptr;}
  if(impl_->d_shape_groups){gpu_detail::cuda_pool_free(impl_->d_shape_groups);impl_->d_shape_groups=nullptr;}

  if(impl_->d_reuse_representatives){gpu_detail::cuda_pool_free(impl_->d_reuse_representatives);impl_->d_reuse_representatives=nullptr;}
  if(impl_->d_reuse_weights){gpu_detail::cuda_pool_free(impl_->d_reuse_weights);impl_->d_reuse_weights=nullptr;}
  if(!impl_->integer_case_reuse.representatives.empty() &&
      (!gpu_detail::cuda_alloc_and_copy_in(impl_->integer_case_reuse.representatives,&impl_->d_reuse_representatives) ||
       !gpu_detail::cuda_alloc_and_copy_in(impl_->integer_case_reuse.weights,&impl_->d_reuse_weights)))
    return fitness_init_single_error(ErrCode::Value,"cuda exact case reuse allocation failure");
  if (impl_->d_case_order) { gpu_detail::cuda_pool_free(impl_->d_case_order); impl_->d_case_order = nullptr; }
  if (!case_order.empty() && !gpu_detail::cuda_alloc_and_copy_in(case_order, &impl_->d_case_order))
    return fitness_init_single_error(ErrCode::Value, "cuda case permutation allocation failure");
  if(impl_->d_arena_case_order){gpu_detail::cuda_pool_free(impl_->d_arena_case_order);impl_->d_arena_case_order=nullptr;}
  if(!arena_case_order.empty() && !gpu_detail::cuda_alloc_and_copy_in(arena_case_order,&impl_->d_arena_case_order))
    return fitness_init_single_error(ErrCode::Value,"cuda arena case permutation allocation failure");
  std::vector<Value> normalized_answers;
  const auto* device_answers=&shared_answer;
  if(impl_->private_answers_prepared) {
    normalized_answers=shared_answer;
    for(auto& value:normalized_answers)if(value.tag==ValueTag::IntList)value.b=false;
    device_answers=&normalized_answers;
  }
  if (!gpu_detail::cuda_alloc_and_copy_in(packed_case_local_vals, &impl_->d_shared_case_local_vals) ||
      !gpu_detail::cuda_alloc_and_copy_in(packed_case_local_set, &impl_->d_shared_case_local_set) ||
      !gpu_detail::cuda_alloc_and_copy_in(*device_answers, &impl_->d_expected)) {
    return fitness_init_single_error(ErrCode::Value, "cuda allocation failure");
  }
  const auto upload_t1 = std::chrono::steady_clock::now();

  impl_->short_edit_enabled=impl_->short_edit_requested && policy.metric==FitnessMetric::Levenshtein;
  impl_->ready = true;
  impl_->device_id = device_id;
  impl_->fuel = fuel;
  impl_->blocksize = blocksize;
  impl_->penalty = penalty;
  impl_->policy = policy;
  impl_->shared_case_count = static_cast<int>(shared_cases.size());
  impl_->shared_input_payload_mask = shared_cases_payload_mask(shared_cases);
  impl_->shared_bytes = 0;
  impl_->last_err = Err{ErrCode::Value, ""};
  FitnessSessionInitResult out;
  out.ok = true;
  out.timing.device_select_ms = ms_between(device_select_t0, device_select_t1);
  out.timing.shared_case_pack_ms = ms_between(shared_case_pack_t0, shared_case_pack_t1);
  out.timing.payload_cache_warm_ms = ms_between(payload_cache_t0, payload_cache_t1);
  out.timing.upload_ms = ms_between(upload_t0, upload_t1);
  out.timing.total_ms = ms_between(all_t0, upload_t1);
  out.err = Err{ErrCode::Value, ""};
  return out;
}

FitnessEvalResult FitnessSessionGpu::eval_programs(const std::vector<BytecodeProgram>& programs, bool capture_case_counts,
    const std::function<void()>& while_gpu_runs) const {
  return eval_programs_impl(programs,capture_case_counts,while_gpu_runs,nullptr);
}
FitnessEvalResult FitnessSessionGpu::eval_owned_programs(const gpu_detail::OwnedGpuPrograms& owned,
    bool capture_case_counts,const std::vector<int>* reused_parents,const FitnessEvalResult* completed,
    std::uint64_t expected_revision) const {
  return eval_programs_impl(owned.programs(),capture_case_counts,{},&owned,reused_parents,completed,expected_revision);
}
FitnessEvalResult FitnessSessionGpu::eval_programs_impl(const std::vector<BytecodeProgram>& programs,
    bool capture_case_counts,const std::function<void()>& while_gpu_runs,
    const gpu_detail::OwnedGpuPrograms* owned,const std::vector<int>* reused_parents,
    const FitnessEvalResult* completed,std::uint64_t expected_revision) const {
  if (!impl_ || !impl_->ready) {
    return fitness_eval_single_error(ErrCode::Value, "gpu fitness session is not initialized");
  }
  if(impl_->private_workspace.busy)return fitness_eval_single_error(ErrCode::Value,"GPU private workspace is in use");
  struct PoolLease {
    ReusablePrivateWorkspace* pool=nullptr;
    ~PoolLease(){if(pool)pool->busy=false;}
  } pool_lease;
  if (programs.empty()) {
    return fitness_eval_single_error(ErrCode::Value, "programs must not be empty");
  }
  if (cudaSetDevice(impl_->device_id) != cudaSuccess) {
    return fitness_eval_single_error(ErrCode::Value, "cuda set device failure");
  }

  std::size_t payload_workspace_budget=0;
  if(!gpu_detail::parse_payload_workspace_budget(std::getenv("GAGP_GPU_PAYLOAD_WORKSPACE_MIB"),
      payload_workspace_budget))
    return fitness_eval_single_error(ErrCode::Value,
        "GAGP_GPU_PAYLOAD_WORKSPACE_MIB must be a decimal integer in [64,16384]");

  const auto all_t0 = std::chrono::steady_clock::now();
  const auto pack_t0 = std::chrono::steady_clock::now();
  // The private packed carrier keeps a native reference on demand. A session
  // with different input types must perform its ordinary capability analysis
  // on current phase bytecode, not the owner's cold import snapshot.
  const bool request_private=std::getenv("GAGP_GPU_PRIVATE_INTLIST_OFFSETS") &&
      std::string(std::getenv("GAGP_GPU_PRIVATE_INTLIST_OFFSETS"))=="1";
  const bool request_private_globals=std::getenv("GAGP_GPU_PRIVATE_GLOBAL_OFFSETS") &&
      std::string(std::getenv("GAGP_GPU_PRIVATE_GLOBAL_OFFSETS"))=="1";
  const bool request_compact=std::getenv("GAGP_GPU_COMPACT_INTLIST_CELLS") &&
      std::string(std::getenv("GAGP_GPU_COMPACT_INTLIST_CELLS"))=="1";
  const char* tuple_setting=std::getenv("GAGP_GPU_PRIVATE_TUPLE_SCORE");
  const bool request_private_tuple=tuple_setting && std::string(tuple_setting)=="1";
  // TupleSummary observes Int fields and the primary result, never identity.
  // The same whole-program private-list proof excludes container Eq and list
  // memo keys. Zero logical cap retains its existing policy; physical scratch
  // exhaustion still replays on GPU. No policy/fuel/grammar limit is changed.
  const bool private_tuple_policy=request_private_tuple && impl_->policy.metric==FitnessMetric::TupleSummary &&
      impl_->policy.list_allocation_limit<=65536;
  const bool private_sequence_policy=(impl_->policy.metric==FitnessMetric::Exact ||
      impl_->policy.metric==FitnessMetric::Levenshtein) && impl_->policy.list_allocation_limit>0 &&
      impl_->policy.list_allocation_limit<=65536;
  const bool private_static_eligible=request_private && impl_->private_answers_prepared &&
      (private_sequence_policy || private_tuple_policy) &&
      std::getenv("GAGP_GPU_GLOBAL_PAYLOAD") && std::string(std::getenv("GAGP_GPU_GLOBAL_PAYLOAD"))!="0";
  const bool tuple_zero_cap=private_tuple_policy && impl_->policy.list_allocation_limit==0;
  // Fresh code is needed for the extra bounded-allocation proof. A stale cold
  // executable or typeflow-only certificate cannot establish this property.
  const bool deferred_private_reference=!tuple_zero_cap && owned && owned->prepacked() &&
      !owned->views_supported_for(impl_->shared_input_types) && private_static_eligible &&
      owned->private_intlists_supported_for(impl_->shared_input_types);
  if(owned && owned->prepacked() && !owned->views_supported_for(impl_->shared_input_types) && !deferred_private_reference)
    owned->materialize_reference();
  gpu_detail::PackResult packed;
  try {
    packed = owned ? gpu_detail::pack_owned_programs(*owned,impl_->shared_case_count,impl_->shared_input_payload_mask)
        : gpu_detail::pack_programs_with_shared_case_count(programs,impl_->shared_case_count,impl_->shared_input_payload_mask);
  } catch (const std::invalid_argument& error) {
    return fitness_eval_single_error(ErrCode::Value, error.what());
  }
  if (packed.total_cases == 0) {
    return fitness_eval_single_error(ErrCode::Value, "cases must not be empty");
  }

  std::vector<std::int64_t> needed_string_tokens = impl_->shared_string_tokens;
  std::vector<Value> needed_list_tokens = impl_->shared_list_tokens;
  for (const BytecodeProgram& program : programs) {
    append_payload_tokens_from_values(program.consts, &needed_string_tokens, &needed_list_tokens);
  }
  append_payload_tokens_from_values(packed.all_phase_consts, &needed_string_tokens, &needed_list_tokens);
  sort_and_unique_tokens(&needed_string_tokens);
  sort_and_unique_list_tokens(&needed_list_tokens);
  const bool private_program_eligible=private_static_eligible &&
      (owned?owned->private_intlists_supported_for(impl_->shared_input_types):
       std::all_of(programs.begin(),programs.end(),[&](const auto& program) {
         return gpu_detail::private_intlist_program_supported(program,impl_->shared_input_types);
       })) && (!tuple_zero_cap || std::all_of(programs.begin(),programs.end(),[&](const auto& program) {
         return gpu_detail::bounded_tuple_allocations(program,impl_->shared_input_types);
       }));
  std::vector<Value> normal_needed_lists;
  std::vector<std::int64_t> normal_needed_strings;
  const bool extra_private_answers=private_program_eligible && !impl_->private_answer_tokens.empty();
  if(extra_private_answers) {
    normal_needed_lists=needed_list_tokens;normal_needed_strings=needed_string_tokens;
    needed_list_tokens.insert(needed_list_tokens.end(),impl_->private_answer_tokens.begin(),impl_->private_answer_tokens.end());
    sort_and_unique_list_tokens(&needed_list_tokens);
  }
  const auto known_strings=impl_->host_string_payloads.size(),known_lists=impl_->host_list_payloads.size();
  populate_payload_cache(needed_string_tokens, needed_list_tokens, &impl_->host_string_payloads, &impl_->host_list_payloads);
  // An unresolved token may become available after init. Existing cached
  // backing is immutable, but newly resolved backing changes this context.
  if(known_strings!=impl_->host_string_payloads.size() || known_lists!=impl_->host_list_payloads.size())
    impl_->evaluation_revision=next_fitness_revision.fetch_add(1,std::memory_order_relaxed);
  auto required_list_count = needed_list_tokens.size();
  HostPayloadPack payload_pack =
      build_payload_pack(impl_->host_string_payloads, impl_->host_list_payloads, std::move(needed_string_tokens),
                         std::move(needed_list_tokens));
  const bool private_intlists=private_program_eligible &&
      payload_pack.string_entries.empty() && payload_pack.list_entries.size()==required_list_count &&
      payload_pack.list_values.size()<=static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
      std::all_of(payload_pack.list_values.begin(),payload_pack.list_values.end(),
          [](const auto& value){return value.tag==ValueTag::Int;}) &&
      std::all_of(payload_pack.list_entries.begin(),payload_pack.list_entries.end(),[](const auto& entry) {
        const auto encoded=static_cast<unsigned>(static_cast<std::uint64_t>(entry.packed)>>48);
        // Public payload factories saturate; legacy device construction masks.
        // Both encodings can name a checked 65536-cell immutable payload.
        return entry.tag==ValueTag::IntList && entry.len>=0 && entry.len<=65536 &&
            (encoded==static_cast<unsigned>(entry.len&65535) ||
             (entry.len==65536 && encoded==65535));
      });
  if(private_intlists) {
    for(auto& value:packed.all_consts)if(value.tag==ValueTag::IntList)value.b=false;
    for(auto& value:packed.all_phase_consts)if(value.tag==ValueTag::IntList)value.b=false;
  }
  if(!private_intlists) {
    if(deferred_private_reference)owned->materialize_reference();
    // Exact-policy answers were added only for private content scoring. Restore
    // the ordinary token closure before selecting an existing fallback backend.
    if(extra_private_answers) {
      required_list_count=normal_needed_lists.size();
      payload_pack=build_payload_pack(impl_->host_string_payloads,impl_->host_list_payloads,
          std::move(normal_needed_strings),std::move(normal_needed_lists));
    }
  }
  const bool use_views = !private_intlists && std::getenv("GAGP_VIEW_PROFILE") != nullptr && !packed.region_segments.empty() &&
      payload_pack.list_entries.size() == required_list_count &&
      std::all_of(payload_pack.list_entries.begin(), payload_pack.list_entries.end(), [](const auto& entry) { return entry.tag == ValueTag::IntList; }) &&
      std::all_of(payload_pack.list_values.begin(), payload_pack.list_values.end(), [](const auto& value) { return value.tag == ValueTag::Int; }) &&
      ((owned && owned->views_supported_for(impl_->shared_input_types)) ||
       std::all_of(programs.begin(), programs.end(), [&](const auto& program) {
         return gpu_detail::view_program_supported(program, impl_->shared_input_types);
       }));
  if (use_views) {
    if (std::getenv("GAGP_TYPED_VIEW_PHASE"))
      for (auto& phase : packed.region_phases) phase.program.typed_view = true;
    for (auto& value : packed.all_consts) if (value.tag == ValueTag::IntList) value.b = false;
    for (auto& value : packed.all_phase_consts) if (value.tag == ValueTag::IntList) value.b = false;
  }
  // Only the existing complete-payload/type-flow proof admits private views.
  // Bounded integral scores below 2^40 have an exact sum under the existing
  // 48-bit reduction. Preserve multiplicities, including errors and wrongs.
  // Public/generic or fractional programs never receive weighted execution.
  const bool reuse_integer_cases=use_views && !impl_->integer_case_reuse.representatives.empty();
  int execution_threads=impl_->blocksize;
  const int* execution_case_order=impl_->d_case_order;
  if(reuse_integer_cases) {
    const auto count=static_cast<int>(impl_->integer_case_reuse.representatives.size());
    for(auto& meta:packed.metas)meta.case_count=count;
    execution_case_order=impl_->d_reuse_representatives;
    execution_threads=32;
    while(execution_threads<count && execution_threads<impl_->blocksize)execution_threads*=2;
    execution_threads=std::min(execution_threads,impl_->blocksize);
  }
  const int fitness_lanes=execution_threads;
  const auto* physical_setting=std::getenv("GAGP_GPU_VIEW_PHYSICAL_BLOCK");
  const std::string physical_text=physical_setting?physical_setting:"0";
  if(physical_text!="0" && physical_text!="32" && physical_text!="64" && physical_text!="128" &&
      physical_text!="256" && physical_text!="512")
    return fitness_eval_single_error(ErrCode::Value,"GAGP_GPU_VIEW_PHYSICAL_BLOCK must be0,32,64,128,256 or512");
  const int requested_physical_block=std::stoi(physical_text);
  const bool virtual_view_lanes=use_views && requested_physical_block>0;
  if(virtual_view_lanes)execution_threads=std::min(execution_threads,requested_physical_block);
  // Resolve immutable captures against this evaluation's table, without changing
  // raw session values or surfacing an error for an unused/missing capture.
  std::vector<Value> prepared_case_views;
  const bool private_global_offsets=private_intlists && request_private_globals;
  const char* private_typed_setting=std::getenv("GAGP_GPU_PRIVATE_TYPED_PHASES");
  const bool request_private_typed=private_typed_setting && std::string(private_typed_setting)=="1";
  bool complete_private_normalization=private_global_offsets;
  if(private_global_offsets) {
    const auto prepare=[&](Value& value) {
      if(value.tag==ValueTag::IntList && !gpu_detail::prepare_private_global_intlist(value,payload_pack.list_entries,
          static_cast<int>(payload_pack.list_values.size())))complete_private_normalization=false;
    };
    for(auto& value:packed.all_consts)prepare(value);
    for(auto& value:packed.all_phase_consts)prepare(value);
    if(!impl_->shared_list_tokens.empty()) {
      prepared_case_views=impl_->host_case_local_vals;
      for(auto& value:prepared_case_views)prepare(value);
    } else if(request_private_typed && std::any_of(impl_->host_case_local_vals.begin(),impl_->host_case_local_vals.end(),
        [](const auto& value){return value.tag==ValueTag::IntList;}))complete_private_normalization=false;
  }
  if (use_views && !impl_->shared_list_tokens.empty() &&
      std::getenv("GAGP_PREPARED_CASE_VIEWS") &&
      std::any_of(packed.region_segments.begin(), packed.region_segments.end(), [](const auto& segment) {
        return std::find(segment.parameter_types, segment.parameter_types + segment.parameter_count,
            ValueTag::IntList) != segment.parameter_types + segment.parameter_count;
      })) {
    prepared_case_views = impl_->host_case_local_vals;
    for (auto& value : prepared_case_views) {
      if (value.tag != ValueTag::IntList) continue;
      const auto entry = std::lower_bound(payload_pack.list_entries.begin(), payload_pack.list_entries.end(), value.i,
          [](const auto& candidate, std::int64_t packed) { return candidate.packed < packed; });
      // Retain the ordinary lazy lookup if a future capability profile permits
      // unresolved inputs. Never manufacture an offset or eager runtime error.
      if (entry == payload_pack.list_entries.end() || entry->packed != value.i) continue;
      value = Value::from_int_list_hash_len(entry->offset, Value::container_len(value));
      value.b = true;
    }
  }
  const int direct_stack_limit = std::getenv("GAGP_HYBRID_PHASE_EXECUTOR") ? gpu_detail::MAX_STACK : 16;
  bool direct_phases = use_views && !std::getenv("GAGP_GENERIC_PHASE_VM") && std::getenv("GAGP_DIRECT_PHASE") &&
      std::all_of(packed.region_phases.begin(), packed.region_phases.end(), [&](const auto& phase) {
        return phase.program.typed_view && phase.program.n_locals <= 8 &&
            phase.program.verified_stack_bound <= direct_stack_limit;
      });
  // Partition the owned population by its already-verified stack capability.
  // Both kernels consume the same packed tables; indices restore the original
  // fitness/diagnostic rows directly, without bytecode copies or repacking.
  std::vector<int> compact_indices,generic_indices;
  if (!direct_phases && owned && use_views && !std::getenv("GAGP_GENERIC_PHASE_VM") && std::getenv("GAGP_DIRECT_PHASE") &&
      std::getenv("GAGP_PHASE_CAP_BUCKETS") &&
      std::all_of(packed.region_phases.begin(),packed.region_phases.end(),[](const auto& phase) {
        return phase.program.typed_view && phase.program.n_locals<=8;
      })) {
    for(std::size_t p=0;p<programs.size();++p)
      (owned->stack_bounds()[p]<=16?compact_indices:generic_indices).push_back(static_cast<int>(p));
    if(compact_indices.empty() || generic_indices.empty()){compact_indices.clear();generic_indices.clear();}
    else direct_phases=true;
  }
  const bool capability_buckets=!compact_indices.empty();
  const bool direct_root = direct_phases && std::getenv("GAGP_DIRECT_ROOT") &&
      ((owned && owned->direct_root_supported()) || std::all_of(programs.begin(), programs.end(), gpu_detail::direct_region_root_supported));
  const auto* relaxed_setting=std::getenv("GAGP_GPU_RELAXED_VIEW_REGISTERS");
  const bool request_relaxed=relaxed_setting && std::string(relaxed_setting)=="1";
  const bool relaxed_view_registers=request_relaxed && direct_root && execution_threads<=512;
  const auto pack_t1 = std::chrono::steady_clock::now();

  // Compilation geometry only: the arena still launches 32 physical threads
  // and keeps the same logical score lanes, workspace layout and case order.
  const char* private_launch_setting=std::getenv("GAGP_GPU_PRIVATE_LAUNCH_BOUND");
  int requested_private_launch=0;
  if(private_launch_setting) {
    const std::string value(private_launch_setting);
    if(value=="32")requested_private_launch=32;
    else if(value=="512")requested_private_launch=512;
    else if(value=="1024")requested_private_launch=1024;
    else if(value!="0")return fitness_eval_single_error(ErrCode::Value,"invalid private GPU launch bound");
  }
  const int private_launch_bound=private_intlists && requested_private_launch?
      requested_private_launch:32;
  const auto launch_prep_t0 = std::chrono::steady_clock::now();
  const bool has_regions = !packed.region_segments.empty();
  bool extended_payload = private_intlists;
  for (const auto& program : programs) {
    auto needs_payload = [](const auto& code) {
      for (const auto& ins : code) {
        if (ins.op != Opcode::CallBuiltin) continue;
        const auto id = static_cast<BuiltinId>(ins.a);
        if (id == BuiltinId::Concat || id == BuiltinId::Singleton ||
            id == BuiltinId::Append || id == BuiltinId::Prepend ||
            id == BuiltinId::CharToString || id == BuiltinId::ToString ||
            id == BuiltinId::Slice || id == BuiltinId::Reverse) return true;
      }
      return false;
    };
    extended_payload |= needs_payload(program.code);
    for (const auto& segment : program.bounded_region_segments) {
      // Sequence-window transitions materialize Slice even without a phase opcode.
      extended_payload |= segment.plan.progress == RegionProgressKind::SequenceWindows;
      extended_payload |= needs_payload(segment.base_body.program.code) ||
                          needs_payload(segment.combine.program.code) ||
                          (segment.boundary && needs_payload(segment.boundary->program.code)) ||
                          needs_payload(segment.base_predicate.program.code);
      for (const auto& phase : segment.preparations)
        extended_payload |= needs_payload(phase.program.code);
      for (const auto& phase : segment.request_expressions)
        extended_payload |= needs_payload(phase.program.code);
    }
  }
  const bool global_payload = extended_payload && !use_views && std::getenv("GAGP_GPU_GLOBAL_PAYLOAD") &&
      std::string(std::getenv("GAGP_GPU_GLOBAL_PAYLOAD"))!="0";
  const bool global_window_aliases = extended_payload && !use_views && std::getenv("GAGP_GPU_GLOBAL_WINDOW_ALIASES") &&
      std::string(std::getenv("GAGP_GPU_GLOBAL_WINDOW_ALIASES"))!="0";
  const bool payload_index = global_payload || (extended_payload && !use_views && std::getenv("GAGP_GPU_PAYLOAD_INDEX") &&
      std::string(std::getenv("GAGP_GPU_PAYLOAD_INDEX"))!="0");

  std::size_t shared_bytes = kernel_shared_bytes(packed.max_code_len, fitness_lanes);
  if (shared_bytes > static_cast<std::size_t>(impl_->props.sharedMemPerBlock)) {
    return fitness_eval_single_error(ErrCode::Value, "shared memory requirement exceeded");
  }
  // Keep generic frame/memo storage out of ordinary kernel call graphs.
  gpu_detail::DRegionWorkspace region_workspace;
  region_workspace.integer_case_weights=reuse_integer_cases?impl_->d_reuse_weights:nullptr;
  region_workspace.fitness_lanes=fitness_lanes;
  const char* count_setting=std::getenv("GAGP_GPU_COALESCED_COUNTS");
  region_workspace.coalesce_case_counts=count_setting && std::string(count_setting)=="1";
  region_workspace.payload_window_aliases=global_window_aliases;
  region_workspace.private_intlists=private_intlists;
  region_workspace.short_edit_enabled=impl_->short_edit_enabled && private_intlists;
  region_workspace.short_edit_patterns=impl_->d_short_edit_patterns;
  region_workspace.short_edit_entries=impl_->d_short_edit_entries;
  region_workspace.short_edit_entry_count=impl_->short_edit_entry_count;
  const char* trivial_setting=std::getenv("GAGP_GPU_TRIVIAL_PHASES");
  const bool request_trivial=trivial_setting && std::string(trivial_setting)=="1";
  region_workspace.trivial_phases=request_trivial && private_intlists;
  const char* private_bound_setting=std::getenv("GAGP_GPU_PRIVATE_BOUND_PHASES");
  const bool request_private_bound=private_bound_setting && std::string(private_bound_setting)=="1";
  region_workspace.private_bound_phases=request_private_bound && private_intlists;
  region_workspace.private_typed_phases=request_private_typed && complete_private_normalization;
  const char* simple_setting=std::getenv("GAGP_GPU_SIMPLE_PHASES");
  const bool request_simple=simple_setting && std::string(simple_setting)=="1";
  region_workspace.simple_phases=request_simple && region_workspace.private_typed_phases;
  const auto* register_stack_setting=std::getenv("GAGP_GPU_REGISTER_PHASE_STACK");
  const bool request_register_stack=register_stack_setting && std::string(register_stack_setting)=="1";
  region_workspace.register_phase_stack=request_register_stack && (direct_phases || region_workspace.private_typed_phases);
  region_workspace.compact_intlist_cells=private_intlists && request_compact;
  const char* cell_soa_setting=std::getenv("GAGP_GPU_PRIVATE_CELL_SOA");
  const bool request_cell_soa=cell_soa_setting && std::string(cell_soa_setting)=="1";
  region_workspace.private_cell_soa=request_cell_soa && region_workspace.compact_intlist_cells;
  const char* adjacent_setting=std::getenv("GAGP_GPU_PRIVATE_ADJACENT_CONCAT");
  const bool request_adjacent=adjacent_setting && std::string(adjacent_setting)=="1";
  // Whole-program private admission excludes memoized list states and public
  // handle escape. Keep the tested compact, contiguous per-lane backing only.
  region_workspace.private_adjacent_concat=request_adjacent &&
      region_workspace.compact_intlist_cells && !region_workspace.private_cell_soa;
  const char* warp_copy_setting=std::getenv("GAGP_GPU_PRIVATE_WARP_COPY");
  const bool request_warp_copy=warp_copy_setting && std::string(warp_copy_setting)=="1";
  region_workspace.private_warp_copy=request_warp_copy && region_workspace.compact_intlist_cells;
  region_workspace.shared_list_value_count=static_cast<int>(payload_pack.list_values.size());
  region_workspace.payload_epoch_index=payload_index && std::getenv("GAGP_GPU_PAYLOAD_EPOCH_INDEX") &&
      std::string(std::getenv("GAGP_GPU_PAYLOAD_EPOCH_INDEX"))=="1";
  region_workspace.fused_list_concat=std::getenv("GAGP_GPU_FUSED_LIST_CONCAT") &&
      std::string(std::getenv("GAGP_GPU_FUSED_LIST_CONCAT"))!="0";
  region_workspace.small_int_hash=region_workspace.fused_list_concat &&
      std::getenv("GAGP_GPU_SMALL_INT_HASH") && std::string(std::getenv("GAGP_GPU_SMALL_INT_HASH"))!="0";
  region_workspace.concat_cache_enabled=!private_intlists && region_workspace.fused_list_concat &&
      std::getenv("GAGP_GPU_CONCAT_CACHE") && std::string(std::getenv("GAGP_GPU_CONCAT_CACHE"))!="0";
  const bool concat_diagnostics=region_workspace.concat_cache_enabled &&
      std::getenv("GAGP_GPU_CONCAT_CACHE_DIAGNOSTICS") && std::string(std::getenv("GAGP_GPU_CONCAT_CACHE_DIAGNOSTICS"))!="0";
  region_workspace.compact_frames = std::getenv("GAGP_COMPACT_FRAMES") &&
      std::all_of(packed.region_segments.begin(), packed.region_segments.end(), [](const auto& segment) {
        return segment.state_count <= 2 && segment.preparation_count <= 1 && segment.request_count <= 2;
      });
  region_workspace.two_state_frames = region_workspace.compact_frames &&
      std::any_of(packed.region_segments.begin(), packed.region_segments.end(), [](const auto& segment) {
        return segment.state_count > 1;
      });
  const char* private_window_setting=std::getenv("GAGP_GPU_PRIVATE_WINDOW_EXECUTOR");
  const bool request_private_window=private_window_setting && std::string(private_window_setting)=="1";
  region_workspace.window_executor = ((direct_phases && std::getenv("GAGP_WINDOW_EXECUTOR")) ||
      (private_intlists && request_private_window)) &&
      has_regions && region_workspace.compact_frames &&
      std::all_of(packed.region_segments.begin(),packed.region_segments.end(),[&](const auto& s) {
        return s.progress==RegionProgressKind::SequenceWindows && s.sequence_state==0 && (s.state_count==1 ||
            (s.state_count==2 && s.state_types[1]==ValueTag::Int &&
             (s.requests[0][1].kind==RegionTransitionKind::Expression ||
              (s.requests[0][1].kind==RegionTransitionKind::CopyState && s.requests[0][1].source_state==1)) &&
             (s.requests[1][1].kind==RegionTransitionKind::Expression ||
              (s.requests[1][1].kind==RegionTransitionKind::CopyState && s.requests[1][1].source_state==1)))) &&
            s.state_types[0]==ValueTag::IntList && s.result_type==(private_intlists?ValueTag::IntList:ValueTag::Int) &&
            !s.memoized && s.bound_operand_count==0 && s.preparation_count==1 &&
            s.preparation_kinds[0]==RegionPreparationKind::InteriorCut && s.preparation_types[0]==ValueTag::Int &&
            s.request_count==2 && s.requests[0][0].kind==RegionTransitionKind::SequenceWindow &&
            s.requests[1][0].kind==RegionTransitionKind::SequenceWindow &&
            s.requests[0][0].source_state==0 && s.requests[1][0].source_state==0;
      });
  region_workspace.coordinate_executor = direct_phases && std::getenv("GAGP_COORDINATE_EXECUTOR") &&
      std::all_of(packed.region_segments.begin(),packed.region_segments.end(),[](const auto& s) {
        if(s.progress!=RegionProgressKind::Coordinates || s.state_count!=1 || s.state_types[0]!=ValueTag::Int ||
            s.result_type!=ValueTag::Int || s.coordinate_count!=1 || s.coordinate_slots[0]!=0 ||
            s.bound_operand_count!=0 || s.preparation_count!=0 || s.request_expression_count!=0 ||
            s.request_count<1 || s.request_count>2 ||
            s.coordinate_domains[0].lower.kind!=RegionBoundKind::Literal ||
            s.coordinate_domains[0].upper.kind!=RegionBoundKind::Literal)return false;
        for(unsigned r=0;r<s.request_count;++r)
          if(s.requests[r][0].kind!=RegionTransitionKind::CoordinateOffset || s.requests[r][0].source_state!=0)return false;
        return true;
      });
  region_workspace.coordinate_ring_memo=region_workspace.coordinate_executor && std::getenv("GAGP_COORDINATE_RING_MEMO") &&
      std::all_of(packed.region_segments.begin(),packed.region_segments.end(),[&](const auto& s) {
        if(!s.memoized || s.requests[0][0].offset!=-1)return false;
        for(unsigned r=0;r<s.request_count;++r)if(s.requests[r][0].offset>=0 || s.requests[r][0].offset < -4)return false;
        const auto& phase=packed.region_phases[s.base_predicate_phase];
        if(phase.program.compare_binding<0)return false;
        const auto& binding=packed.region_bindings[phase.program.compare_binding];
        if(binding.bank!=RegionSlotBank::State || binding.slot!=0)return false;
        const auto* code=packed.all_phase_code.data()+phase.program.code_offset;
        // A lower base interval ensures the first -1 request completes every
        // nonbase coordinate in order; later requests need at most four values.
        return code[0].op==gpu_detail::OP_LOAD ?
            (code[2].op==gpu_detail::OP_LT || code[2].op==gpu_detail::OP_LE) :
            (code[2].op==gpu_detail::OP_GT || code[2].op==gpu_detail::OP_GE);
      });
  const char* dense_memo_setting=std::getenv("GAGP_GPU_DENSE_COORDINATE_MEMO");
  const bool request_dense_memo=dense_memo_setting && std::string(dense_memo_setting)=="1";
  region_workspace.dense_coordinate_memo=request_dense_memo && !region_workspace.coordinate_executor &&
      dense_coordinate_memo_supported(packed);
  // The block validates the actual phase dependencies before filling a table.
  // Large-stack capability buckets need this reuse just as compact ones do.
  const std::size_t measure_bytes = sizeof(gpu_detail::DMeasurePhaseCacheEntry) *
      (gpu_detail::DMEASURE_CACHE_MAX + 1) + sizeof(int);
  const char* private_measure_setting=std::getenv("GAGP_GPU_PRIVATE_MEASURE_CACHE");
  const bool request_private_measure=private_measure_setting && std::string(private_measure_setting)=="1";
  region_workspace.measure_phase_cache = (use_views || (private_intlists && request_private_measure)) && has_regions &&
      std::getenv("GAGP_MEASURE_PHASE_CACHE") &&
      shared_bytes + measure_bytes <= static_cast<std::size_t>(impl_->props.sharedMemPerBlock);
  if (region_workspace.measure_phase_cache && !private_intlists) shared_bytes += measure_bytes;
  std::size_t shape_result_programs=0,shape_result_invocations=0,max_shape_groups=0;
  if(direct_root && impl_->shape_cache_requested &&
      std::all_of(payload_pack.list_entries.begin(),payload_pack.list_entries.end(),[](const auto& entry) {
        return entry.len>=0 && entry.len<65536 &&
            entry.len==static_cast<int>(static_cast<std::uint64_t>(entry.packed)>>48);
      })) {
    const auto base_cases=reuse_integer_cases?impl_->integer_case_reuse.representatives.size():impl_->shared_case_count;
    for(auto& meta:packed.metas) {
      meta.shape_result_cache=false;meta.shape_case_count=0;
      std::uint64_t inputs=0;
      if(!gpu_detail::shape_result_program_supported(packed,meta,&inputs))continue;
      if(!impl_->project_shape_inputs)inputs=~std::uint64_t{0};
      const auto* partition=impl_->shape_partitions.find_or_add(impl_->host_case_local_vals,impl_->host_case_local_set,inputs);
      if(!partition || static_cast<std::size_t>(partition->count)*4>static_cast<std::size_t>(base_cases))continue;
      meta.shape_result_cache=true;meta.shape_case_count=partition->count;
      meta.shape_case_group_offset=partition->group_offset;
      meta.shape_representative_offset=partition->representative_offset;
    }
    for(int index:generic_indices){packed.metas[index].shape_result_cache=false;packed.metas[index].shape_case_count=0;}
    for(const auto& meta:packed.metas)if(meta.shape_result_cache) {
      ++shape_result_programs;shape_result_invocations+=meta.shape_case_count;
      max_shape_groups=std::max(max_shape_groups,static_cast<std::size_t>(meta.shape_case_count));
    }
    const auto offset=(shared_bytes+alignof(gpu_detail::DResult)-1)&~(alignof(gpu_detail::DResult)-1);
    const auto bytes=offset+sizeof(gpu_detail::DResult)*max_shape_groups;
    if(shape_result_programs && bytes<=static_cast<std::size_t>(impl_->props.sharedMemPerBlock)) {
      region_workspace.shape_result_count=max_shape_groups;
      region_workspace.shape_result_offset=offset;shared_bytes=bytes;
    } else {
      shape_result_programs=shape_result_invocations=max_shape_groups=0;
      for(auto& meta:packed.metas){meta.shape_result_cache=false;meta.shape_case_count=0;}
    }
  }
  // A tile retains one logical warp; an untiled block retains all logical
  // fitness lanes, even though arena execution uses 32 physical threads.
  const auto arena_shared_bytes=[&](const gpu_detail::DRegionWorkspace& workspace) {
    const int slots=workspace.replay_warp_scores?32:workspace.fitness_lanes;
    return kernel_shared_bytes(packed.max_code_len,slots)+
        (workspace.measure_phase_cache?measure_bytes:0);
  };
  region_workspace.window_leaf_direct = std::getenv("GAGP_GPU_WINDOW_LEAF_DIRECT") &&
      std::string(std::getenv("GAGP_GPU_WINDOW_LEAF_DIRECT")) != "0";
  region_workspace.unboxed_window_frames = !private_intlists && region_workspace.window_executor && std::getenv("GAGP_UNBOXED_WINDOW_FRAMES");
  region_workspace.window_soa = region_workspace.unboxed_window_frames && std::getenv("GAGP_WINDOW_SOA");
  std::size_t frame_size = region_workspace.coordinate_executor ? sizeof(gpu_detail::DUnboxedCoordinateFrame) : region_workspace.unboxed_window_frames ?
      (region_workspace.two_state_frames ? sizeof(gpu_detail::DUnboxedWindowFrame<2>) : sizeof(gpu_detail::DUnboxedWindowFrame<1>)) : !region_workspace.compact_frames ? sizeof(gpu_detail::DRegionFrame) :
      (region_workspace.two_state_frames ? sizeof(gpu_detail::DCompactPairRegionFrame) :
       sizeof(gpu_detail::DCompactRegionFrame));
  const auto compact_frame_size=frame_size;
  if(capability_buckets) {
    const auto generic_size=!region_workspace.compact_frames?sizeof(gpu_detail::DRegionFrame):
        region_workspace.two_state_frames?sizeof(gpu_detail::DCompactPairRegionFrame):sizeof(gpu_detail::DCompactRegionFrame);
    frame_size=std::max(frame_size,generic_size);
  }
  for (const auto& segment : packed.region_segments) {
    region_workspace.frame_capacity =
        std::max(region_workspace.frame_capacity, segment.limits.frames);
    if (segment.memoized) {
      region_workspace.memo_capacity =
          std::max(region_workspace.memo_capacity, segment.limits.cells);
    }
  }
  // Bound transient scratch independently of population size. This limits only
  // concurrent program blocks, never a program's declared frame or memo limits.
  constexpr std::size_t kRegionWorkspaceBudget = 512u * 1024u * 1024u;
  const std::size_t workspace_bytes_per_thread =
      frame_size * region_workspace.frame_capacity +
      (sizeof(std::int64_t) * gpu_detail::DMAX_REGION_STATES + sizeof(Value)) *
          region_workspace.memo_capacity;
  const std::size_t workspace_bytes_per_block =
      workspace_bytes_per_thread * static_cast<std::size_t>(execution_threads);
  std::size_t region_blocks = workspace_bytes_per_block == 0 ? programs.size() :
      std::min(programs.size(), kRegionWorkspaceBudget / workspace_bytes_per_block);
  if (has_regions && !global_payload && region_blocks == 0) {
    return fitness_eval_single_error(ErrCode::Value, "GPU region workspace budget exceeded");
  }
  std::size_t generic_blocks=capability_buckets?std::min(region_blocks,generic_indices.size()):0;
  std::size_t workspace_threads=region_blocks*execution_threads;
  std::size_t workspace_frame_bytes=workspace_threads*region_workspace.frame_capacity*frame_size;
  const bool bucket_workspace=capability_buckets && std::getenv("GAGP_BUCKET_WORKSPACE");
  if(bucket_workspace) {
    const auto threads=static_cast<std::size_t>(execution_threads);
    const auto compact_frames=threads*region_workspace.frame_capacity*compact_frame_size;
    const auto generic_frames=threads*region_workspace.frame_capacity*frame_size;
    const auto memo=threads*region_workspace.memo_capacity*
        (sizeof(std::int64_t)*gpu_detail::DMAX_REGION_STATES+sizeof(Value));
    const auto required=[&](std::size_t c,std::size_t g) {
      return std::max(c*compact_frames,g*generic_frames)+std::max(c,g)*memo;
    };
    auto c=std::min(compact_indices.size(),kRegionWorkspaceBudget/std::max<std::size_t>(1,compact_frames+memo));
    // Sequential kernels reuse the same allocations, but their frame stride and
    // grid size can differ. Reserve room for at least one generic block.
    if(required(c,1)>kRegionWorkspaceBudget && memo)
      c=std::min(c,(kRegionWorkspaceBudget-generic_frames)/memo);
    if(!c || required(c,1)>kRegionWorkspaceBudget)
      return fitness_eval_single_error(ErrCode::Value,"GPU bucket workspace budget exceeded");
    std::size_t low=1,high=generic_indices.size();
    while(low<high){const auto mid=low+(high-low+1)/2;
      if(required(c,mid)<=kRegionWorkspaceBudget)low=mid;else high=mid-1;}
    region_blocks=c;generic_blocks=low;workspace_threads=std::max(c,low)*threads;
    workspace_frame_bytes=std::max(c*compact_frames,low*generic_frames);
  }
  const auto launch_prep_t1 = std::chrono::steady_clock::now();

  const bool overflow_diagnostics = std::getenv("GAGP_GPU_OVERFLOW_DIAGNOSTICS") &&
      std::string(std::getenv("GAGP_GPU_OVERFLOW_DIAGNOSTICS")) != "0";
  const bool adaptive_replay=std::getenv("GAGP_GPU_ADAPTIVE_REPLAY") &&
      std::string(std::getenv("GAGP_GPU_ADAPTIVE_REPLAY"))!="0";
  const std::size_t overflow_reason_bytes=adaptive_replay?programs.size()*sizeof(unsigned int):0;
  const int overflow_mask_words = (impl_->blocksize + 31) / 32;
  const std::size_t overflow_mask_bytes = overflow_diagnostics ?
      programs.size() * overflow_mask_words * sizeof(unsigned int) : 0;
  // One warp of input elements is a scheduling heuristic, not a semantic limit.
  const char* initial_tiling=std::getenv("GAGP_GPU_TILED_INITIAL_PAYLOAD");
  const bool tiled_initial=global_payload && initial_tiling &&
      (std::string(initial_tiling)=="1" ||
       (std::string(initial_tiling)=="auto" && impl_->shared_max_intlist_input_length>=32));
  const int initial_warps=(impl_->blocksize+31)/32;
  const auto initial_tiles_per_program=static_cast<std::size_t>(tiled_initial?initial_warps:1);
  if(programs.size()>static_cast<std::size_t>(std::numeric_limits<int>::max())/initial_tiles_per_program)
    return fitness_eval_single_error(ErrCode::Value,"GPU initial payload task count exceeds addressing capacity");
  const std::size_t initial_tasks=programs.size()*initial_tiles_per_program;
  const std::size_t initial_partial_bytes=tiled_initial?sizeof(double)*initial_tasks:0;
  const std::size_t concat_stats_bytes=concat_diagnostics?3*sizeof(unsigned long long):0;
  const std::size_t payload_metadata_bytes=overflow_mask_bytes+overflow_reason_bytes+concat_stats_bytes+
      sizeof(unsigned int)*programs.size()+(global_payload?sizeof(int):0);
  if(payload_metadata_bytes>=payload_workspace_budget)
    return fitness_eval_single_error(ErrCode::Value,"GPU payload workspace metadata exceeds configured scratch cap");
  const char* reuse_setting=std::getenv("GAGP_GPU_REUSE_PRIVATE_WORKSPACE");
  const bool request_reuse=reuse_setting && std::string(reuse_setting)=="1";
  const bool reuse_private=request_reuse && global_payload && private_intlists && region_workspace.compact_intlist_cells &&
      !region_workspace.concat_cache_enabled;
  auto& pool=impl_->private_workspace;
  if(!reuse_private) {
    if(!pool.clear())return fitness_eval_single_error(ErrCode::Value,"cuda private workspace release failure");
  } else {
    if(initial_partial_bytes>=payload_workspace_budget-payload_metadata_bytes ||
        !pool.trim(payload_workspace_budget-payload_metadata_bytes-initial_partial_bytes))
      return fitness_eval_single_error(ErrCode::Value,"GPU reusable private workspace headroom failure");
    pool.allocations=pool.peak_bytes=0;pool.busy=true;pool_lease.pool=&pool;
  }
  std::vector<int> reused(programs.size(),-1);
  std::size_t reused_count=0;
  if(owned && expected_revision==impl_->evaluation_revision && reused_parents && completed && completed->ok &&
      reused_parents->size()==programs.size() && completed->fitness.size()==programs.size() &&
      completed->case_counts.size()==programs.size() &&
      (impl_->policy.metric==FitnessMetric::Legacy || completed->wrong.size()==programs.size())) {
    for(std::size_t index=0;index<programs.size();++index) {
      const auto parent=(*reused_parents)[index];
      if(parent<0 || static_cast<std::size_t>(parent)>=programs.size() ||
          !std::isfinite(completed->fitness[parent]) || completed->case_counts[parent][4] ||
          completed->case_counts[parent][0]!=static_cast<unsigned>(impl_->shared_case_count))continue;
      reused[index]=parent;++reused_count;
    }
  }
  for(std::size_t index=0;index<packed.metas.size();++index)
    packed.metas[index].reuse_completed_fitness=reused[index]>=0;
  FitnessEvalResult out;
  out.fitness_reused_programs=reused_count;
  out.fitness_reused_program_cases=reused_count*impl_->shared_case_count;
  out.physical_case_count=reuse_integer_cases?impl_->integer_case_reuse.representatives.size():impl_->shared_case_count;
  out.physical_blocksize=global_payload?PayloadWorkspaceStorage::threads:execution_threads;
  out.shape_result_programs=shape_result_programs;
  out.shape_result_groups=region_workspace.shape_result_count;
  for(const auto& meta:packed.metas)if(!meta.reuse_completed_fitness)
    out.physical_program_cases+=meta.shape_result_cache?meta.shape_case_count:out.physical_case_count;
  if(region_workspace.simple_phases)out.simple_phase_count=std::count_if(
      packed.region_phases.begin(),packed.region_phases.end(),[](const auto& phase){return phase.program.simple_operation_ip>=0;});
  out.payload_workspace_budget_bytes=payload_workspace_budget;
  std::chrono::steady_clock::time_point teardown_t0;
  {
    gpu_detail::DeviceArena dev;
    PayloadWorkspaceStorage initial_payload(payload_workspace_budget,reuse_private?&pool:nullptr);
    struct IndexBuffers {
      unsigned long long* concat_stats=nullptr;
      int *compact=nullptr,*generic=nullptr,*compact_queue=nullptr,*generic_queue=nullptr;
      unsigned int* overflows=nullptr;
      unsigned int* overflow_reasons=nullptr;
      unsigned int* overflow_lane_masks=nullptr;
      ~IndexBuffers(){if(concat_stats)gpu_detail::cuda_pool_free(concat_stats);if(overflow_reasons)gpu_detail::cuda_pool_free(overflow_reasons);if(overflow_lane_masks)gpu_detail::cuda_pool_free(overflow_lane_masks);if(overflows)gpu_detail::cuda_pool_free(overflows);if(compact)gpu_detail::cuda_pool_free(compact);if(generic)gpu_detail::cuda_pool_free(generic);if(compact_queue)gpu_detail::cuda_pool_free(compact_queue);if(generic_queue)gpu_detail::cuda_pool_free(generic_queue);}
    } indices;
    const auto upload_t0 = std::chrono::steady_clock::now();
    if(shape_result_programs) {
      if(impl_->shape_partitions.dirty) {
        if(impl_->d_shape_representatives){gpu_detail::cuda_pool_free(impl_->d_shape_representatives);impl_->d_shape_representatives=nullptr;}
        if(impl_->d_shape_groups){gpu_detail::cuda_pool_free(impl_->d_shape_groups);impl_->d_shape_groups=nullptr;}
        if(!gpu_detail::cuda_alloc_and_copy_in(impl_->shape_partitions.representatives,&impl_->d_shape_representatives) ||
            !gpu_detail::cuda_alloc_and_copy_in(impl_->shape_partitions.groups,&impl_->d_shape_groups))
          return fitness_eval_single_error(ErrCode::Value,"cuda projected shape partition upload failure");
        impl_->shape_partitions.dirty=false;
      }
      region_workspace.shape_case_groups=impl_->d_shape_groups;
      region_workspace.shape_case_representatives=impl_->d_shape_representatives;
    }
    if (gpu_detail::cuda_pool_malloc(reinterpret_cast<void**>(&indices.overflows), sizeof(unsigned int)*programs.size()) != cudaSuccess ||
        cudaMemset(indices.overflows,0,sizeof(unsigned int)*programs.size()) != cudaSuccess)
      return fitness_eval_single_error(ErrCode::Value,"cuda payload overflow diagnostics allocation failure");
    region_workspace.payload_overflows=indices.overflows;
    if(concat_diagnostics) {
      if(gpu_detail::cuda_pool_malloc(reinterpret_cast<void**>(&indices.concat_stats),concat_stats_bytes)!=cudaSuccess ||
          cudaMemset(indices.concat_stats,0,concat_stats_bytes)!=cudaSuccess)
        return fitness_eval_single_error(ErrCode::Value,"cuda concat cache diagnostics allocation failure");
      region_workspace.concat_cache_stats=indices.concat_stats;
    }
    if(adaptive_replay) {
      if(gpu_detail::cuda_pool_malloc(reinterpret_cast<void**>(&indices.overflow_reasons),overflow_reason_bytes)!=cudaSuccess ||
          cudaMemset(indices.overflow_reasons,0,overflow_reason_bytes)!=cudaSuccess)
        return fitness_eval_single_error(ErrCode::Value,"cuda payload overflow reason allocation failure");
      region_workspace.payload_overflow_reasons=indices.overflow_reasons;
    }
    if (overflow_diagnostics) {
      if (gpu_detail::cuda_pool_malloc(reinterpret_cast<void**>(&indices.overflow_lane_masks),overflow_mask_bytes)!=cudaSuccess ||
          cudaMemset(indices.overflow_lane_masks,0,overflow_mask_bytes)!=cudaSuccess)
        return fitness_eval_single_error(ErrCode::Value,"cuda initial overflow lane diagnostics allocation failure");
      region_workspace.payload_overflow_lane_masks=indices.overflow_lane_masks;
      region_workspace.payload_overflow_mask_words=overflow_mask_words;
    }
    if (has_regions && !global_payload && std::getenv("GAGP_DYNAMIC_PROGRAM_QUEUE")) {
      if (!gpu_detail::cuda_alloc_and_copy_in(std::vector<int>{static_cast<int>(region_blocks)}, &indices.compact_queue) ||
          (capability_buckets && !gpu_detail::cuda_alloc_and_copy_in(std::vector<int>{static_cast<int>(generic_blocks)}, &indices.generic_queue)))
        return fitness_eval_single_error(ErrCode::Value, "cuda program queue allocation failure");
    }
    if (!prepared_case_views.empty() &&
        !gpu_detail::cuda_alloc_and_copy_in(prepared_case_views, &dev.d_shared_case_local_vals))
      return fitness_eval_single_error(ErrCode::Value, "cuda prepared case view allocation failure");
    const auto* view_case_values = prepared_case_views.empty()
        ? impl_->d_shared_case_local_vals : dev.d_shared_case_local_vals;
    if(capability_buckets && (!gpu_detail::cuda_alloc_and_copy_in(compact_indices,&indices.compact) ||
        !gpu_detail::cuda_alloc_and_copy_in(generic_indices,&indices.generic)))
      return fitness_eval_single_error(ErrCode::Value,"cuda capability index allocation failure");
    if (!gpu_detail::cuda_alloc_and_copy_in(packed.all_consts, &dev.d_consts) ||
        !gpu_detail::cuda_alloc_and_copy_in(packed.all_code, &dev.d_code) ||
        !gpu_detail::cuda_alloc_and_copy_in(packed.all_phase_consts, &dev.d_phase_consts) ||
        !gpu_detail::cuda_alloc_and_copy_in(packed.all_phase_code, &dev.d_phase_code) ||
        !gpu_detail::cuda_alloc_and_copy_in(packed.region_segments, &dev.d_region_segments) ||
        !gpu_detail::cuda_alloc_and_copy_in(packed.region_phases, &dev.d_region_phases) ||
        !gpu_detail::cuda_alloc_and_copy_in(packed.region_bindings, &dev.d_region_bindings) ||
        !gpu_detail::cuda_alloc_and_copy_in(packed.metas, &dev.d_metas) ||
        !gpu_detail::cuda_alloc_and_copy_in(payload_pack.string_entries, &dev.d_string_payload_entries) ||
        !gpu_detail::cuda_alloc_and_copy_in(payload_pack.string_bytes, &dev.d_string_payload_bytes) ||
        !gpu_detail::cuda_alloc_and_copy_in(payload_pack.list_entries, &dev.d_list_payload_entries) ||
        !gpu_detail::cuda_alloc_and_copy_in(payload_pack.list_values, &dev.d_list_payload_values)) {
      return fitness_eval_single_error(ErrCode::Value, "cuda allocation failure");
    }
    if (gpu_detail::cuda_pool_malloc(reinterpret_cast<void**>(&dev.d_fitness), sizeof(double) * programs.size()) != cudaSuccess) {
      return fitness_eval_single_error(ErrCode::Value, "cuda allocation failure");
    }
    if (capture_case_counts || impl_->policy.metric!=FitnessMetric::Legacy) {
      const auto bytes = sizeof(unsigned int) * 6 * programs.size();
      if (gpu_detail::cuda_pool_malloc(reinterpret_cast<void**>(&dev.d_case_counts), bytes) != cudaSuccess ||
          cudaMemset(dev.d_case_counts, 0, bytes) != cudaSuccess)
        return fitness_eval_single_error(ErrCode::Value, "cuda diagnostics allocation failure");
    }
    if (cudaMemset(dev.d_fitness, 0, sizeof(double) * programs.size()) != cudaSuccess) {
      return fitness_eval_single_error(ErrCode::Value, "cuda memset failure");
    }
    if (has_regions && !global_payload) {
      const std::size_t frame_bytes = workspace_frame_bytes;
      const std::size_t key_bytes = workspace_threads * region_workspace.memo_capacity *
                                    gpu_detail::DMAX_REGION_STATES * sizeof(std::int64_t);
      const std::size_t value_bytes = workspace_threads * region_workspace.memo_capacity * sizeof(Value);
      if (!impl_->reserve_region_workspace(frame_bytes, key_bytes, value_bytes)) {
        return fitness_eval_single_error(ErrCode::Value, "cuda region workspace allocation failure");
      }
      region_workspace.frames = impl_->region_frames;
      region_workspace.memo_keys = impl_->region_memo_keys;
      region_workspace.memo_values = impl_->region_memo_values;
    }
    if(global_payload) {
      impl_->release_region_workspace();
      if(!initial_payload.prepare(region_workspace,initial_tasks,impl_->blocksize,1024,8192,8192,true,payload_metadata_bytes+initial_partial_bytes))
        return fitness_eval_single_error(ErrCode::Value,"cuda initial global payload workspace capacity/allocation failure");
      if(tiled_initial && !initial_payload.allocate(
          reinterpret_cast<void**>(&initial_payload.workspace.replay_warp_scores),initial_partial_bytes))
        return fitness_eval_single_error(ErrCode::Value,"cuda initial payload partial-score allocation failure");
      out.initial_payload_tiled=tiled_initial;
      out.initial_payload_entry_capacity=1024;
      out.initial_payload_value_capacity=8192;
      out.initial_payload_physical_blocks=initial_payload.blocks;
      if(!gpu_detail::cuda_alloc_and_copy_in(std::vector<int>{static_cast<int>(initial_payload.blocks)},&indices.compact_queue))
        return fitness_eval_single_error(ErrCode::Value,"cuda initial global payload queue allocation failure");
    }
    const auto upload_t1 = std::chrono::steady_clock::now();

    struct KernelEvents {
      cudaEvent_t start = nullptr, end = nullptr;
      ~KernelEvents() {
        if (start) cudaEventDestroy(start);
        if (end) cudaEventDestroy(end);
      }
    } events;
    if (while_gpu_runs) {
      if (cudaEventCreate(&events.start) != cudaSuccess ||
          cudaEventCreate(&events.end) != cudaSuccess ||
          cudaEventRecord(events.start) != cudaSuccess)
        return fitness_eval_single_error(ErrCode::Value, "cuda overlap timing failure");
    }
    const auto kernel_t0 = std::chrono::steady_clock::now();
    if(global_payload) {
      const auto launch_private=[&](auto bound) {
      gpu_detail::evaluate_fitness_programs_impl<gpu_detail::DPayloadFlavor::IndexedArena,true,false,decltype(bound)::value>
          <<<static_cast<unsigned>(initial_payload.blocks),PayloadWorkspaceStorage::threads,arena_shared_bytes(initial_payload.workspace)>>>(
              static_cast<int>(initial_tasks),dev.d_consts,dev.d_code,dev.d_metas,
              view_case_values,impl_->d_shared_case_local_set,impl_->d_expected,
              dev.d_string_payload_entries,static_cast<int>(payload_pack.string_entries.size()),dev.d_string_payload_bytes,
              dev.d_list_payload_entries,static_cast<int>(payload_pack.list_entries.size()),dev.d_list_payload_values,
              dev.d_phase_code,dev.d_phase_consts,impl_->fuel,impl_->penalty,impl_->policy,dev.d_fitness,
              dev.d_region_segments,static_cast<int>(packed.region_segments.size()),
              dev.d_region_phases,static_cast<int>(packed.region_phases.size()),
              dev.d_region_bindings,static_cast<int>(packed.region_bindings.size()),initial_payload.workspace,
              dev.d_case_counts,impl_->d_arena_case_order,nullptr,indices.compact_queue);
      };
      if(private_launch_bound==512)launch_private(std::integral_constant<int,512>{});
      else if(private_launch_bound==1024)launch_private(std::integral_constant<int,1024>{});
      else launch_private(std::integral_constant<int,32>{});
      if(tiled_initial) {
        const auto initial_launch_error=cudaGetLastError();
        if(initial_launch_error!=cudaSuccess) {
          cudaDeviceSynchronize();
          return fitness_eval_single_error(ErrCode::Value,"cuda tiled initial payload launch failure");
        }
        // Default-stream ordering makes reduction part of the initial event
        // interval and enqueues it before the host overlap callback begins.
        gpu_detail::reduce_payload_replay<<<(programs.size()+127)/128,128>>>(
            programs.size(),initial_warps,nullptr,initial_payload.workspace.replay_warp_scores,dev.d_fitness);
      }
    } else if (direct_root) {
      if(relaxed_view_registers) {
      gpu_detail::evaluate_fitness_programs_impl<gpu_detail::DPayloadFlavor::BoundIntListViews, true, true,512>
          <<<static_cast<unsigned int>(region_blocks), execution_threads, shared_bytes>>>(
              static_cast<int>(capability_buckets?compact_indices.size():programs.size()), dev.d_consts, dev.d_code, dev.d_metas,
              view_case_values, impl_->d_shared_case_local_set, impl_->d_expected,
              dev.d_string_payload_entries, static_cast<int>(payload_pack.string_entries.size()), dev.d_string_payload_bytes,
              dev.d_list_payload_entries, static_cast<int>(payload_pack.list_entries.size()), dev.d_list_payload_values,
              dev.d_phase_code, dev.d_phase_consts,
              impl_->fuel, impl_->penalty, impl_->policy, dev.d_fitness,
              dev.d_region_segments, static_cast<int>(packed.region_segments.size()),
              dev.d_region_phases, static_cast<int>(packed.region_phases.size()),
              dev.d_region_bindings, static_cast<int>(packed.region_bindings.size()), region_workspace, dev.d_case_counts, execution_case_order, indices.compact, indices.compact_queue);
      } else {
      gpu_detail::evaluate_fitness_programs_impl<gpu_detail::DPayloadFlavor::BoundIntListViews, true, true>
          <<<static_cast<unsigned int>(region_blocks), execution_threads, shared_bytes>>>(
              static_cast<int>(capability_buckets?compact_indices.size():programs.size()), dev.d_consts, dev.d_code, dev.d_metas,
              view_case_values, impl_->d_shared_case_local_set, impl_->d_expected,
              dev.d_string_payload_entries, static_cast<int>(payload_pack.string_entries.size()), dev.d_string_payload_bytes,
              dev.d_list_payload_entries, static_cast<int>(payload_pack.list_entries.size()), dev.d_list_payload_values,
              dev.d_phase_code, dev.d_phase_consts,
              impl_->fuel, impl_->penalty, impl_->policy, dev.d_fitness,
              dev.d_region_segments, static_cast<int>(packed.region_segments.size()),
              dev.d_region_phases, static_cast<int>(packed.region_phases.size()),
              dev.d_region_bindings, static_cast<int>(packed.region_bindings.size()), region_workspace, dev.d_case_counts, execution_case_order, indices.compact, indices.compact_queue);
      }
    } else if (direct_phases) {
      gpu_detail::evaluate_fitness_programs_impl<gpu_detail::DPayloadFlavor::BoundIntListViews, true>
          <<<static_cast<unsigned int>(region_blocks), execution_threads, shared_bytes>>>(
              static_cast<int>(capability_buckets?compact_indices.size():programs.size()), dev.d_consts, dev.d_code, dev.d_metas,
              view_case_values, impl_->d_shared_case_local_set, impl_->d_expected,
              dev.d_string_payload_entries, static_cast<int>(payload_pack.string_entries.size()), dev.d_string_payload_bytes,
              dev.d_list_payload_entries, static_cast<int>(payload_pack.list_entries.size()), dev.d_list_payload_values,
              dev.d_phase_code, dev.d_phase_consts,
              impl_->fuel, impl_->penalty, impl_->policy, dev.d_fitness,
              dev.d_region_segments, static_cast<int>(packed.region_segments.size()),
              dev.d_region_phases, static_cast<int>(packed.region_phases.size()),
              dev.d_region_bindings, static_cast<int>(packed.region_bindings.size()), region_workspace, dev.d_case_counts, execution_case_order, indices.compact, indices.compact_queue);
    } else if (has_regions && use_views) {
      gpu_detail::evaluate_fitness_programs_impl<gpu_detail::DPayloadFlavor::IntListViews, true>
          <<<static_cast<unsigned int>(region_blocks), execution_threads, shared_bytes>>>(
              static_cast<int>(programs.size()), dev.d_consts, dev.d_code, dev.d_metas,
              view_case_values, impl_->d_shared_case_local_set, impl_->d_expected,
              dev.d_string_payload_entries, static_cast<int>(payload_pack.string_entries.size()), dev.d_string_payload_bytes,
              dev.d_list_payload_entries, static_cast<int>(payload_pack.list_entries.size()), dev.d_list_payload_values,
              dev.d_phase_code, dev.d_phase_consts,
              impl_->fuel, impl_->penalty, impl_->policy, dev.d_fitness,
              dev.d_region_segments, static_cast<int>(packed.region_segments.size()),
              dev.d_region_phases, static_cast<int>(packed.region_phases.size()),
              dev.d_region_bindings, static_cast<int>(packed.region_bindings.size()), region_workspace, dev.d_case_counts, execution_case_order, nullptr, indices.compact_queue);
    } else if (has_regions && extended_payload) {
      gpu_detail::evaluate_fitness_programs_impl<gpu_detail::DPayloadFlavor::ExtendedMixed, true>
          <<<static_cast<unsigned int>(region_blocks), impl_->blocksize, shared_bytes>>>(
              static_cast<int>(programs.size()), dev.d_consts, dev.d_code, dev.d_metas,
              impl_->d_shared_case_local_vals, impl_->d_shared_case_local_set, impl_->d_expected,
              dev.d_string_payload_entries, static_cast<int>(payload_pack.string_entries.size()), dev.d_string_payload_bytes,
              dev.d_list_payload_entries, static_cast<int>(payload_pack.list_entries.size()), dev.d_list_payload_values,
              dev.d_phase_code, dev.d_phase_consts,
              impl_->fuel, impl_->penalty, impl_->policy, dev.d_fitness,
              dev.d_region_segments, static_cast<int>(packed.region_segments.size()),
              dev.d_region_phases, static_cast<int>(packed.region_phases.size()),
              dev.d_region_bindings, static_cast<int>(packed.region_bindings.size()), region_workspace, dev.d_case_counts, nullptr, nullptr, indices.compact_queue);
    } else if (has_regions) {
      gpu_detail::evaluate_fitness_programs_impl<gpu_detail::DPayloadFlavor::Mixed, true>
          <<<static_cast<unsigned int>(region_blocks), impl_->blocksize, shared_bytes>>>(
              static_cast<int>(programs.size()), dev.d_consts, dev.d_code, dev.d_metas,
              impl_->d_shared_case_local_vals, impl_->d_shared_case_local_set, impl_->d_expected,
              dev.d_string_payload_entries, static_cast<int>(payload_pack.string_entries.size()), dev.d_string_payload_bytes,
              dev.d_list_payload_entries, static_cast<int>(payload_pack.list_entries.size()), dev.d_list_payload_values,
              dev.d_phase_code, dev.d_phase_consts,
              impl_->fuel, impl_->penalty, impl_->policy, dev.d_fitness,
              dev.d_region_segments, static_cast<int>(packed.region_segments.size()),
              dev.d_region_phases, static_cast<int>(packed.region_phases.size()),
              dev.d_region_bindings, static_cast<int>(packed.region_bindings.size()), region_workspace, dev.d_case_counts, nullptr, nullptr, indices.compact_queue);
    } else if (extended_payload) {
      gpu_detail::evaluate_fitness_programs_impl<gpu_detail::DPayloadFlavor::ExtendedMixed>
          <<<static_cast<unsigned int>(programs.size()), impl_->blocksize, shared_bytes>>>(
              static_cast<int>(programs.size()), dev.d_consts, dev.d_code, dev.d_metas,
              impl_->d_shared_case_local_vals, impl_->d_shared_case_local_set, impl_->d_expected,
              dev.d_string_payload_entries, static_cast<int>(payload_pack.string_entries.size()), dev.d_string_payload_bytes,
              dev.d_list_payload_entries, static_cast<int>(payload_pack.list_entries.size()), dev.d_list_payload_values,
              dev.d_phase_code, dev.d_phase_consts,
              impl_->fuel, impl_->penalty, impl_->policy, dev.d_fitness, nullptr, 0, nullptr, 0, nullptr, 0, region_workspace, dev.d_case_counts);
    } else {
      gpu_detail::evaluate_fitness_programs_impl<gpu_detail::DPayloadFlavor::Mixed>
          <<<static_cast<unsigned int>(programs.size()), impl_->blocksize, shared_bytes>>>(
              static_cast<int>(programs.size()), dev.d_consts, dev.d_code, dev.d_metas,
              impl_->d_shared_case_local_vals, impl_->d_shared_case_local_set, impl_->d_expected,
              dev.d_string_payload_entries, static_cast<int>(payload_pack.string_entries.size()), dev.d_string_payload_bytes,
              dev.d_list_payload_entries, static_cast<int>(payload_pack.list_entries.size()), dev.d_list_payload_values,
              dev.d_phase_code, dev.d_phase_consts,
              impl_->fuel, impl_->penalty, impl_->policy, dev.d_fitness, nullptr, 0, nullptr, 0, nullptr, 0, region_workspace, dev.d_case_counts);
    }
    if(capability_buckets) {
      const auto first_error=cudaGetLastError();
      if(first_error!=cudaSuccess){cudaDeviceSynchronize();return fitness_eval_single_error(ErrCode::Value,"compact bucket launch failure");}
      auto generic_workspace=region_workspace;
      generic_workspace.window_executor=false;generic_workspace.coordinate_executor=false;generic_workspace.unboxed_window_frames=false;generic_workspace.window_soa=false;
      gpu_detail::evaluate_fitness_programs_impl<gpu_detail::DPayloadFlavor::IntListViews,true>
          <<<static_cast<unsigned int>(generic_blocks),execution_threads,shared_bytes>>>(
              static_cast<int>(generic_indices.size()),dev.d_consts,dev.d_code,dev.d_metas,
              view_case_values,impl_->d_shared_case_local_set,impl_->d_expected,
              dev.d_string_payload_entries,static_cast<int>(payload_pack.string_entries.size()),dev.d_string_payload_bytes,
              dev.d_list_payload_entries,static_cast<int>(payload_pack.list_entries.size()),dev.d_list_payload_values,
              dev.d_phase_code,dev.d_phase_consts,impl_->fuel,impl_->penalty,impl_->policy,dev.d_fitness,
              dev.d_region_segments,static_cast<int>(packed.region_segments.size()),
              dev.d_region_phases,static_cast<int>(packed.region_phases.size()),
              dev.d_region_bindings,static_cast<int>(packed.region_bindings.size()),generic_workspace,
              dev.d_case_counts,execution_case_order,indices.generic,indices.generic_queue);
    }
    const cudaError_t launch_err = cudaGetLastError();
    if (while_gpu_runs && launch_err == cudaSuccess) {
      if (cudaEventRecord(events.end) != cudaSuccess) {
        cudaDeviceSynchronize();
        return fitness_eval_single_error(ErrCode::Value, "cuda overlap timing failure");
      }
      try { while_gpu_runs(); }
      catch (...) { if(reuse_private)cudaSetDevice(impl_->device_id);cudaDeviceSynchronize(); throw; }
      if(reuse_private && cudaSetDevice(impl_->device_id)!=cudaSuccess) {
        pool.clear();return fitness_eval_single_error(ErrCode::Value,"cuda private workspace device restore failure");
      }
    }
    const cudaError_t sync_err = cudaDeviceSynchronize();
    if (launch_err != cudaSuccess || sync_err != cudaSuccess) {
      std::string msg = "cuda kernel execution failure";
      if (launch_err != cudaSuccess) {
        msg += " launch=";
        msg += cudaGetErrorString(launch_err);
      }
      if (sync_err != cudaSuccess) {
        msg += " sync=";
        msg += cudaGetErrorString(sync_err);
      }
      return fitness_eval_single_error(ErrCode::Value, msg);
    }
    // Initial and replay global arenas never coexist, preserving the scratch
    // bound. Every initial lane has finished, including any overlap callback.
    const auto initial_kernel_t1 = std::chrono::steady_clock::now();
    initial_payload.clear();
    // Physical payload exhaustion is not a program error. Replay only affected
    // programs on GPU, replacing their entire original logical-lane reduction.
    // Global scratch bounds concurrency; no large per-thread local arrays and
    // no CPU evaluation fallback are introduced.
    double replay_kernel_ms=0;
    std::vector<unsigned int> overflow_counts(programs.size());
    if (cudaMemcpy(overflow_counts.data(),indices.overflows,sizeof(unsigned int)*programs.size(),cudaMemcpyDeviceToHost)!=cudaSuccess)
      return fitness_eval_single_error(ErrCode::Value,"cuda payload overflow copy-back failure");
    out.initial_payload_overflow_cases=overflow_counts;
    if (overflow_diagnostics) {
      out.initial_payload_overflow_logical_lanes=impl_->blocksize;
      out.initial_payload_overflow_lane_masks.resize(programs.size()*overflow_mask_words);
      if (cudaMemcpy(out.initial_payload_overflow_lane_masks.data(),indices.overflow_lane_masks,
          overflow_mask_bytes,cudaMemcpyDeviceToHost)!=cudaSuccess)
        return fitness_eval_single_error(ErrCode::Value,"cuda initial overflow lane diagnostics copy failure");
    }
    // Later retries must not change the initial distribution being measured.
    region_workspace.payload_overflow_lane_masks=nullptr;
    region_workspace.payload_overflow_mask_words=0;
    std::vector<int> replay_programs;
    for(std::size_t p=0;p<programs.size();++p)if(overflow_counts[p])replay_programs.push_back(static_cast<int>(p));
    out.payload_replay_programs=replay_programs.size();
    if(impl_->policy.list_allocation_limit>static_cast<unsigned>(std::numeric_limits<int>::max()))
      return fitness_eval_single_error(ErrCode::Value,"GPU logical list limit exceeds replay addressing capacity");
    const bool tiled_replay=std::getenv("GAGP_GPU_TILED_REPLAY") &&
        std::string(std::getenv("GAGP_GPU_TILED_REPLAY"))!="0";
    const int replay_warps=(impl_->blocksize+31)/32;
    std::vector<unsigned int> overflow_reasons(adaptive_replay?programs.size():0);
    int replay_entries=(tiled_replay || adaptive_replay)?4096:65536;
    int replay_values=static_cast<int>(std::max<unsigned>(adaptive_replay?16384:65536,impl_->policy.list_allocation_limit));
    // A clamped external 65536-cell constant plus a one-cell constant charges
    // 65536 logical cells but copies 65537. Keep the valid boundary in one replay.
    if(private_intlists && impl_->policy.list_allocation_limit==65536)
      replay_values=std::max(replay_values,65537);
    int replay_bytes=8192;
    while(!replay_programs.empty()) {
      PayloadWorkspaceStorage replay_storage(payload_workspace_budget,reuse_private?&pool:nullptr);
      const std::size_t task_count=replay_programs.size()*(tiled_replay?replay_warps:1);
      if(task_count>static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return fitness_eval_single_error(ErrCode::Value,"GPU replay task count exceeds addressing capacity");
      const std::size_t extra_bytes=payload_metadata_bytes+sizeof(int)*replay_programs.size()+
          (tiled_replay?sizeof(double)*task_count+sizeof(int):0);
      if(!replay_storage.prepare(region_workspace,task_count,impl_->blocksize,
          replay_entries,replay_values,replay_bytes,payload_index,extra_bytes))
        return fitness_eval_single_error(ErrCode::Value,"GPU payload replay workspace capacity/allocation failure; physical overflow remains unscored");
      auto replay_workspace=replay_storage.workspace;
      const auto replay_blocks=replay_storage.blocks;
      constexpr int replay_threads=PayloadWorkspaceStorage::threads;
      int* replay_indices=nullptr;
      if(!replay_storage.allocate(reinterpret_cast<void**>(&replay_indices),sizeof(int)*replay_programs.size()) ||
          cudaMemcpy(replay_indices,replay_programs.data(),sizeof(int)*replay_programs.size(),cudaMemcpyHostToDevice)!=cudaSuccess)
        return fitness_eval_single_error(ErrCode::Value,"cuda payload replay index allocation failure");
      int* replay_queue=nullptr;
      if(tiled_replay) {
        const int start=static_cast<int>(replay_blocks);
        if(!replay_storage.allocate(reinterpret_cast<void**>(&replay_workspace.replay_warp_scores),sizeof(double)*task_count) ||
            !replay_storage.allocate(reinterpret_cast<void**>(&replay_queue),sizeof(int)) ||
            cudaMemcpy(replay_queue,&start,sizeof(int),cudaMemcpyHostToDevice)!=cudaSuccess)
          return fitness_eval_single_error(ErrCode::Value,"cuda replay tile allocation failure");
      }
      KernelEvents replay_events;
      if(cudaEventCreate(&replay_events.start)!=cudaSuccess || cudaEventCreate(&replay_events.end)!=cudaSuccess ||
          cudaEventRecord(replay_events.start)!=cudaSuccess)
        return fitness_eval_single_error(ErrCode::Value,"cuda payload replay timing failure");
      if(tiled_replay) {
        gpu_detail::reset_payload_replay<<<(replay_programs.size()+127)/128,128>>>(
            replay_programs.size(),replay_indices,indices.overflows,dev.d_case_counts,indices.overflow_reasons);
        if(cudaGetLastError()!=cudaSuccess)
          return fitness_eval_single_error(ErrCode::Value,"cuda payload replay reset failure");
      }
      if(payload_index) {
      const auto launch_private=[&](auto bound) {
      gpu_detail::evaluate_fitness_programs_impl<gpu_detail::DPayloadFlavor::IndexedArena,true,false,decltype(bound)::value>
          <<<static_cast<unsigned>(replay_blocks),replay_threads,arena_shared_bytes(replay_workspace)>>>(
              static_cast<int>(task_count),dev.d_consts,dev.d_code,dev.d_metas,
              view_case_values,impl_->d_shared_case_local_set,impl_->d_expected,
              dev.d_string_payload_entries,static_cast<int>(payload_pack.string_entries.size()),dev.d_string_payload_bytes,
              dev.d_list_payload_entries,static_cast<int>(payload_pack.list_entries.size()),dev.d_list_payload_values,
              dev.d_phase_code,dev.d_phase_consts,impl_->fuel,impl_->penalty,impl_->policy,dev.d_fitness,
              dev.d_region_segments,static_cast<int>(packed.region_segments.size()),
              dev.d_region_phases,static_cast<int>(packed.region_phases.size()),
              dev.d_region_bindings,static_cast<int>(packed.region_bindings.size()),replay_workspace,
              dev.d_case_counts,impl_->d_arena_case_order,replay_indices,replay_queue);
      };
      if(private_launch_bound==512)launch_private(std::integral_constant<int,512>{});
      else if(private_launch_bound==1024)launch_private(std::integral_constant<int,1024>{});
      else launch_private(std::integral_constant<int,32>{});
      } else {
      gpu_detail::evaluate_fitness_programs_impl<gpu_detail::DPayloadFlavor::ArenaMixed,true>
          <<<static_cast<unsigned>(replay_blocks),replay_threads,arena_shared_bytes(replay_workspace)>>>(
              static_cast<int>(task_count),dev.d_consts,dev.d_code,dev.d_metas,
              view_case_values,impl_->d_shared_case_local_set,impl_->d_expected,
              dev.d_string_payload_entries,static_cast<int>(payload_pack.string_entries.size()),dev.d_string_payload_bytes,
              dev.d_list_payload_entries,static_cast<int>(payload_pack.list_entries.size()),dev.d_list_payload_values,
              dev.d_phase_code,dev.d_phase_consts,impl_->fuel,impl_->penalty,impl_->policy,dev.d_fitness,
              dev.d_region_segments,static_cast<int>(packed.region_segments.size()),
              dev.d_region_phases,static_cast<int>(packed.region_phases.size()),
              dev.d_region_bindings,static_cast<int>(packed.region_bindings.size()),replay_workspace,
              dev.d_case_counts,impl_->d_arena_case_order,replay_indices,replay_queue);
      }
      const auto replay_launch_error=cudaGetLastError();
      if(tiled_replay)
        gpu_detail::reduce_payload_replay<<<(replay_programs.size()+127)/128,128>>>(
            replay_programs.size(),replay_warps,replay_indices,replay_workspace.replay_warp_scores,dev.d_fitness);
      const auto reduce_launch_error=cudaGetLastError();
      const auto replay_event_error=cudaEventRecord(replay_events.end);
      const auto replay_sync_error=cudaDeviceSynchronize();
      if(replay_launch_error!=cudaSuccess || reduce_launch_error!=cudaSuccess || replay_event_error!=cudaSuccess || replay_sync_error!=cudaSuccess)
        return fitness_eval_single_error(ErrCode::Value,"cuda payload replay execution failure");
      float replay_elapsed=0;
      if(cudaEventElapsedTime(&replay_elapsed,replay_events.start,replay_events.end)!=cudaSuccess)
        return fitness_eval_single_error(ErrCode::Value,"cuda payload replay timing read failure");
      replay_kernel_ms+=replay_elapsed;
      ++out.payload_replay_launches;
      out.payload_replay_entry_capacity=replay_entries;
      out.payload_replay_value_capacity=replay_values;
      out.payload_replay_byte_capacity=replay_bytes;
      for(auto p:replay_programs)out.payload_replay_cases+=packed.metas[p].case_count;
      if(cudaMemcpy(overflow_counts.data(),indices.overflows,sizeof(unsigned int)*programs.size(),cudaMemcpyDeviceToHost)!=cudaSuccess)
        return fitness_eval_single_error(ErrCode::Value,"cuda payload replay diagnostics failure");
      replay_programs.erase(std::remove_if(replay_programs.begin(),replay_programs.end(),
          [&](int p){return overflow_counts[p]==0;}),replay_programs.end());
      // Capacity growth affects only physical storage, never the policy budget.
      // The fixed global budget makes an unresolved overflow explicit.
      if(!replay_programs.empty()) {
        if(adaptive_replay) {
          if(cudaMemcpy(overflow_reasons.data(),indices.overflow_reasons,overflow_reason_bytes,cudaMemcpyDeviceToHost)!=cudaSuccess)
            return fitness_eval_single_error(ErrCode::Value,"cuda replay capacity reason copy failure");
          unsigned reasons=0;
          for(int program:replay_programs) {
            const unsigned reason=overflow_reasons[program];
            // Unknown physical failures retain the conservative growth route.
            reasons|=(!reason || (reason&~7u))?7u:reason;
          }
          const auto grow=[](int& capacity) {
            if(capacity>std::numeric_limits<int>::max()/2)return false;
            capacity*=2;return true;
          };
          if(((reasons&gpu_detail::DPAYLOAD_OVERFLOW_ENTRIES) && !grow(replay_entries)) ||
             ((reasons&gpu_detail::DPAYLOAD_OVERFLOW_VALUES) && !grow(replay_values)) ||
             ((reasons&gpu_detail::DPAYLOAD_OVERFLOW_BYTES) && !grow(replay_bytes)))
            return fitness_eval_single_error(ErrCode::Value,"GPU adaptive replay capacity exceeds addressable storage");
        } else {
          if(replay_entries>std::numeric_limits<int>::max()/2 || replay_values>std::numeric_limits<int>::max()/2 || replay_bytes>std::numeric_limits<int>::max()/2)
            return fitness_eval_single_error(ErrCode::Value,"GPU payload replay capacity exceeds addressable storage");
          if(tiled_replay && replay_entries<65536) replay_entries*=2;
          else {replay_entries*=2;replay_values*=2;replay_bytes*=2;}
        }
      }
    }
    const auto kernel_t1 = std::chrono::steady_clock::now();

    const auto copy_t0 = std::chrono::steady_clock::now();
    std::vector<double> host_fitness(programs.size(), 0.0);
    if (cudaMemcpy(host_fitness.data(), dev.d_fitness, sizeof(double) * programs.size(), cudaMemcpyDeviceToHost) !=
        cudaSuccess) {
      return fitness_eval_single_error(ErrCode::Value, "cuda copy-back failure");
    }
    if (capture_case_counts || impl_->policy.metric!=FitnessMetric::Legacy) {
      std::vector<std::array<unsigned,6>> counts(programs.size());
      if (cudaMemcpy(counts.data(), dev.d_case_counts,
                     sizeof(unsigned int) * 6 * programs.size(), cudaMemcpyDeviceToHost) != cudaSuccess)
        return fitness_eval_single_error(ErrCode::Value, "cuda diagnostics copy-back failure");
      if(capture_case_counts) {
        out.case_counts.resize(programs.size());
        for(std::size_t i=0;i<counts.size();++i)std::copy_n(counts[i].begin(),5,out.case_counts[i].begin());
      }
      if(impl_->policy.metric!=FitnessMetric::Legacy)
        for(const auto& c:counts)out.wrong.push_back(c[5]);
    }
    if(concat_diagnostics && cudaMemcpy(out.concat_cache_stats.data(),indices.concat_stats,
        concat_stats_bytes,cudaMemcpyDeviceToHost)!=cudaSuccess)
      return fitness_eval_single_error(ErrCode::Value,"cuda concat cache diagnostics copy failure");
    for(std::size_t index=0;index<reused.size();++index)if(reused[index]>=0) {
      const auto parent=reused[index];host_fitness[index]=completed->fitness[parent];
      if(capture_case_counts)out.case_counts[index]=completed->case_counts[parent];
      if(!out.wrong.empty())out.wrong[index]=completed->wrong[parent];
    }
    const auto copy_t1 = std::chrono::steady_clock::now();

    out.ok = true;
    if(reuse_private) {
      out.payload_workspace_retained_bytes=pool.capacity;
      out.payload_workspace_pool_allocations=pool.allocations;
      out.payload_workspace_pool_peak_bytes=pool.peak_bytes;
    }
    out.execution_profile = use_views ? (std::getenv("GAGP_TYPED_VIEW_PHASE") ? "int-list-views-typed" : "int-list-views") : (extended_payload ? "extended-payload" : "mixed");
    if(requested_private_launch)out.execution_profile+=private_intlists && (global_payload || out.payload_replay_programs)?
        "-private-launch"+std::to_string(private_launch_bound):"-private-launch-declined";
    if(reused_count)out.execution_profile+="-completed-fitness-reuse";
    if(impl_->case_reuse_requested)out.execution_profile+=reuse_integer_cases?"-integer-case-reuse":"-integer-case-reuse-declined";
    if(region_workspace.coalesce_case_counts)out.execution_profile+="-coalesced-counts";
    if(requested_physical_block)out.execution_profile+=virtual_view_lanes?
        "-physical-view"+std::to_string(execution_threads):"-physical-view-declined";
    if(request_register_stack)out.execution_profile+=region_workspace.register_phase_stack?"-register-phase-stack":"-register-phase-stack-declined";
    if(request_relaxed)out.execution_profile+=relaxed_view_registers?"-view-reg512":"-view-reg512-declined";
    if(impl_->project_shape_inputs)out.execution_profile+=shape_result_programs?"-project-shape-inputs":"-project-shape-inputs-declined";
    if(impl_->shape_cache_requested)out.execution_profile+=shape_result_programs?"-shape-result-cache":"-shape-result-cache-declined";
    if(impl_->short_edit_enabled)out.execution_profile+=private_intlists?"-short-edit-exact":"-short-edit-declined";
    if(request_simple)out.execution_profile+=region_workspace.simple_phases?"-simple-phases":"-simple-phases-declined";
    if(request_reuse)out.execution_profile+=reuse_private?
        "-reuse-private-workspace":"-reuse-private-workspace-declined";
    if(request_private)out.execution_profile+=private_intlists?
        "-private-intlist-offset-v1":"-private-intlist-offset-v1-declined";
    if(request_private_globals)out.execution_profile+=private_global_offsets?
        "-private-global-offsets":"-private-global-offsets-declined";
    if(request_private_tuple)out.execution_profile+=private_intlists && private_tuple_policy?
        "-private-tuple-score":"-private-tuple-score-declined";
    if(request_warp_copy)out.execution_profile+=region_workspace.private_warp_copy?
        "-private-warp-copy":"-private-warp-copy-declined";
    if(request_private_typed)out.execution_profile+=region_workspace.private_typed_phases?
        "-private-typed-phases":"-private-typed-phases-declined";
    if(request_private_bound)out.execution_profile+=region_workspace.private_bound_phases?
        "-private-bound-phases":"-private-bound-phases-declined";
    if(request_trivial)out.execution_profile+=region_workspace.trivial_phases?
        "-trivial-phases":"-trivial-phases-declined";
    if(request_adjacent)out.execution_profile+=region_workspace.private_adjacent_concat?
        "-private-adjacent-concat":"-private-adjacent-concat-declined";
    if(request_cell_soa)out.execution_profile+=region_workspace.private_cell_soa?
        "-private-cell-soa":"-private-cell-soa-declined";
    if(request_compact)out.execution_profile+=region_workspace.compact_intlist_cells?
        "-compact-intlist-cells":"-compact-intlist-cells-declined";
    if(request_dense_memo)out.execution_profile+=region_workspace.dense_coordinate_memo?
        "-dense-coordinate-memo":"-dense-coordinate-memo-declined";
    if(global_payload) {
      out.execution_profile+="-global-arena";
      if(tiled_initial)out.execution_profile+="-initial-tiled";
    }
    if(region_workspace.concat_cache_enabled && (global_payload || out.payload_replay_programs))out.execution_profile+="-concat-cache";
    if(global_window_aliases)out.execution_profile+="-window-aliases";
    if(!private_intlists && region_workspace.fused_list_concat && extended_payload)out.execution_profile+="-fused-concat";
    if(!private_intlists && region_workspace.small_int_hash && extended_payload)out.execution_profile+="-small-int-hash";
    if(!private_intlists && payload_index && (global_payload || out.payload_replay_programs)) {
      out.execution_profile+="-list-index";
      if(region_workspace.payload_epoch_index)out.execution_profile+="-epoch";
    }
    if(out.payload_replay_programs)out.execution_profile+="-gpu-arena-replay";
    if(tiled_replay && out.payload_replay_programs)out.execution_profile+="-tiled";
    if(adaptive_replay && out.payload_replay_programs)out.execution_profile+="-adaptive";
    out.arena_cases_sorted=impl_->d_arena_case_order && (global_payload || out.payload_replay_programs);
    out.arena_case_order_bytes=impl_->d_arena_case_order?sizeof(int)*static_cast<std::size_t>(impl_->shared_case_count):0;
    if(out.arena_cases_sorted)out.execution_profile+="-arena-sorted";
    if (use_views && impl_->d_case_order) out.execution_profile += "-sorted";
    if (direct_phases) out.execution_profile += "-direct";
    if (direct_root) out.execution_profile += "-root";
    if (direct_phases && std::getenv("GAGP_HYBRID_PHASE_EXECUTOR")) out.execution_profile += "-hybrid";
    if (region_workspace.measure_phase_cache) out.execution_profile += "-measure-cache";
    if(request_private_measure)out.execution_profile+=private_intlists && region_workspace.measure_phase_cache?
        "-private-measure-cache":"-private-measure-cache-declined";
    if (region_workspace.coordinate_executor) out.execution_profile += "-coordinate";
    if (region_workspace.window_executor) out.execution_profile += "-window";
    if(request_private_window)out.execution_profile+=private_intlists && region_workspace.window_executor?
        "-private-window":"-private-window-declined";
    if (region_workspace.unboxed_window_frames) out.execution_profile += "-unboxed";
    if(direct_phases && std::any_of(packed.region_phases.begin(),packed.region_phases.end(),
        [](const auto& p){return p.program.add_left_binding>=0;}))out.execution_profile+="-bound-add";
    if(capability_buckets)out.execution_profile+="-cap-buckets";
    if(bucket_workspace)out.execution_profile+="-bucket-workspace";
    if(region_workspace.coordinate_ring_memo)out.execution_profile+="-ring4";
    out.fitness = std::move(host_fitness);
    out.timing.pack_ms = ms_between(pack_t0, pack_t1);
    out.timing.launch_prep_ms = ms_between(launch_prep_t0, launch_prep_t1);
    out.timing.upload_ms = ms_between(upload_t0, upload_t1);
    out.timing.kernel_ms = ms_between(kernel_t0, kernel_t1);
    out.timing.initial_kernel_wall_ms = ms_between(kernel_t0, initial_kernel_t1);
    out.timing.replay_kernel_event_ms = replay_kernel_ms;
    if (while_gpu_runs) {
      float elapsed = 0;
      if (cudaEventElapsedTime(&elapsed, events.start, events.end) != cudaSuccess)
        return fitness_eval_single_error(ErrCode::Value, "cuda overlap timing failure");
      out.timing.initial_kernel_event_ms = elapsed;
      out.timing.kernel_ms = elapsed + replay_kernel_ms;
    }
    out.timing.copyback_ms = ms_between(copy_t0, copy_t1);
    out.err = Err{ErrCode::Value, ""};
    teardown_t0 = std::chrono::steady_clock::now();
  }
  const auto teardown_t1 = std::chrono::steady_clock::now();
  out.timing.teardown_ms = ms_between(teardown_t0, teardown_t1);
  out.timing.total_ms = ms_between(all_t0, teardown_t1);
  return out;
}

}  // namespace gagp
