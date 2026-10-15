#pragma once

#include "gagp/core/builtin.hpp"
#include "arith_device.cuh"

namespace gagp::gpu_detail {

struct DPayloadTables {
  const DStringPayloadEntry* string_entries = nullptr;
  int string_entry_count = 0;
  const char* string_bytes = nullptr;
  const DListPayloadEntry* list_entries = nullptr;
  int list_entry_count = 0;
  const Value* list_values = nullptr;
  int list_value_count = 0;
};

template <int MaxStringEntries, int MaxListEntries, int MaxStringBytes, int MaxListValues>
struct DThreadPayloadStateT {
  static constexpr bool kEpochLists = false;
  static constexpr bool kIndexedLists = false;
  static constexpr bool kConcatCache = false;
  static constexpr int kMaxStringEntries = MaxStringEntries;
  static constexpr int kMaxListEntries = MaxListEntries;
  static constexpr int kMaxStringBytes = MaxStringBytes;
  static constexpr int kMaxListValues = MaxListValues;

  DStringPayloadEntry string_entries[(MaxStringEntries > 0) ? MaxStringEntries : 1];
  DListPayloadEntry list_entries[(MaxListEntries > 0) ? MaxListEntries : 1];
  int string_entry_capacity = MaxStringEntries;
  int list_entry_capacity = MaxListEntries;
  int string_entry_count = 0;
  int list_entry_count = 0;
  int string_bytes_used = 0;
  int list_values_used = 0;
  unsigned logical_list_limit = 0;
  unsigned long long logical_list_used = 0;
  bool physical_capacity_exceeded = false;
  unsigned physical_capacity_reason = 0;
  bool fused_list_concat = false;
  bool small_int_hash = false;
  static constexpr bool private_intlists = false;
  static constexpr bool compact_intlist_cells = false;
  static constexpr int intlist_cell_stride = 1;
  static constexpr const std::int64_t* intlist_cells = nullptr;
  bool alias_windows = false;
  char string_bytes[(MaxStringBytes > 0) ? MaxStringBytes : 1];
  Value list_values[(MaxListValues > 0) ? MaxListValues : 1];
};

using DNoPayloadState = DThreadPayloadStateT<0, 0, 0, 0>;
using DStringPayloadState = DThreadPayloadStateT<DMAX_THREAD_PAYLOAD_ENTRIES, 0, DMAX_THREAD_STRING_BYTES, 0>;
using DListPayloadState = DThreadPayloadStateT<0, DMAX_THREAD_PAYLOAD_ENTRIES, 0, DMAX_THREAD_LIST_VALUES>;
using DMixedPayloadState =
    DThreadPayloadStateT<DMAX_THREAD_PAYLOAD_ENTRIES,
                         DMAX_THREAD_PAYLOAD_ENTRIES,
                         DMAX_THREAD_STRING_BYTES,
                         DMAX_THREAD_LIST_VALUES>;

using DExtendedPayloadState = DThreadPayloadStateT<1024, 1024, 8192, 8192>;

// Runtime-sized replay storage. No large per-thread local arrays.
struct DArenaPayloadState {
  static constexpr bool kConcatCache = true;
  DConcatCacheEntry* concat_cache = nullptr;
  bool concat_cache_diagnostics = false;
  unsigned long long concat_hits = 0, concat_misses = 0, concat_cells = 0;
  static constexpr bool kEpochLists = false;
  static constexpr bool kIndexedLists = false;
  bool private_intlists = false;
  bool trivial_phases = false;
  bool compact_intlist_cells = false;
  int intlist_cell_stride = 1;
  std::int64_t* intlist_cells = nullptr;
  bool alias_windows = false;
  static constexpr int kMaxStringEntries = 1;
  static constexpr int kMaxListEntries = 1;
  DStringPayloadEntry* string_entries = nullptr;
  DListPayloadEntry* list_entries = nullptr;
  char* string_bytes = nullptr;
  Value* list_values = nullptr;
  int kMaxStringBytes = 0, kMaxListValues = 0;
  int string_entry_capacity = 0, list_entry_capacity = 0;
  int string_entry_count = 0, list_entry_count = 0;
  int string_bytes_used = 0, list_values_used = 0;
  unsigned logical_list_limit = 0;
  unsigned long long logical_list_used = 0;
  bool physical_capacity_exceeded = false;
  unsigned physical_capacity_reason = 0;
  bool fused_list_concat = false;
  bool small_int_hash = false;
};

// Opt-in variants keep the default evaluator's storage layout unchanged.
struct DIndexedExtendedPayloadState : DExtendedPayloadState {
  static constexpr bool kIndexedLists = true;
  static constexpr int list_index_capacity = 2048;
  int list_index[list_index_capacity];
};
struct DIndexedArenaPayloadState : DArenaPayloadState {
  static constexpr bool kEpochLists = true;
  std::uint64_t* epoch_index = nullptr;
  unsigned* epoch_counter = nullptr;
  unsigned epoch = 0;
  static constexpr bool kIndexedLists = true;
  int list_index_capacity = 0;
  int* list_index = nullptr;
};

template <DPayloadFlavor Flavor>
inline constexpr bool d_exact_payload_flavor = Flavor == DPayloadFlavor::ExtendedMixed ||
    Flavor == DPayloadFlavor::ArenaMixed || Flavor == DPayloadFlavor::IndexedExtended ||
    Flavor == DPayloadFlavor::IndexedArena;

template <DPayloadFlavor Flavor>
struct DPayloadFlavorTraits;

template <>
struct DPayloadFlavorTraits<DPayloadFlavor::None> {
  using State = DNoPayloadState;
  static constexpr bool kHasString = false;
  static constexpr bool kHasList = false;
};

template <>
struct DPayloadFlavorTraits<DPayloadFlavor::StringOnly> {
  using State = DStringPayloadState;
  static constexpr bool kHasString = true;
  static constexpr bool kHasList = false;
};

template <>
struct DPayloadFlavorTraits<DPayloadFlavor::ListOnly> {
  using State = DListPayloadState;
  static constexpr bool kHasString = false;
  static constexpr bool kHasList = true;
};

template <>
struct DPayloadFlavorTraits<DPayloadFlavor::IntListViews> {
  using State = DNoPayloadState;
  static constexpr bool kHasString = false;
  static constexpr bool kHasList = false;
};

template <>
struct DPayloadFlavorTraits<DPayloadFlavor::BoundIntListViews>
    : DPayloadFlavorTraits<DPayloadFlavor::IntListViews> {};

template <>
struct DPayloadFlavorTraits<DPayloadFlavor::Mixed> {
  using State = DMixedPayloadState;
  static constexpr bool kHasString = true;
  static constexpr bool kHasList = true;
};

template <>
struct DPayloadFlavorTraits<DPayloadFlavor::ExtendedMixed> {
  using State = DExtendedPayloadState;
  static constexpr bool kHasString = true;
  static constexpr bool kHasList = true;
};

template <>
struct DPayloadFlavorTraits<DPayloadFlavor::ArenaMixed> {
  using State = DArenaPayloadState;
  static constexpr bool kHasString = true;
  static constexpr bool kHasList = true;
};

template <>
struct DPayloadFlavorTraits<DPayloadFlavor::IndexedExtended> : DPayloadFlavorTraits<DPayloadFlavor::ExtendedMixed> {
  using State = DIndexedExtendedPayloadState;
};
template <>
struct DPayloadFlavorTraits<DPayloadFlavor::IndexedArena> : DPayloadFlavorTraits<DPayloadFlavor::ArenaMixed> {
  using State = DIndexedArenaPayloadState;
};

__device__ inline long long d_norm_slice_idx(long long idx, long long n) {
  long long out = idx;
  if (out < 0) out += n;
  if (out < 0) out = 0;
  if (out > n) out = n;
  return out;
}

__device__ inline bool d_norm_index_idx(long long idx, long long n, long long& out) {
  long long j = idx;
  if (j < 0) j += n;
  if (j < 0 || j >= n) {
    return false;
  }
  out = j;
  return true;
}

__device__ inline std::uint64_t d_hash_bytes(const char* p, int n) {
  std::uint64_t h = Value::fnv1a_init();
  for (int i = 0; i < n; ++i) {
    h = Value::fnv1a_mix_u8(h, static_cast<std::uint8_t>(p[i]));
  }
  return h;
}

__device__ inline std::uint64_t d_hash_value_shallow(const Value& v) {
  std::uint64_t h = Value::fnv1a_init();
  h = Value::fnv1a_mix_u8(h, static_cast<std::uint8_t>(v.tag));
  if (v.tag == ValueTag::Invalid) return h;
  if (v.tag == ValueTag::Bool) return Value::fnv1a_mix_u8(h, v.b ? 1U : 0U);
  if (v.tag == ValueTag::Float) {
    union {
      double d;
      std::uint64_t u;
    } bits{};
    bits.d = v.f;
    return Value::fnv1a_mix_u64(h, bits.u);
  }
  return Value::fnv1a_mix_u64(h, static_cast<std::uint64_t>(v.i));
}

// Compile-time initialized global memory, deliberately not CUDA constant
// memory: lanes may request unrelated integer values without serialization.
struct DSmallIntHashTable { unsigned long long values[512]{}; };
constexpr DSmallIntHashTable d_make_small_int_hash_table() {
  DSmallIntHashTable table{};
  for (int i = 0; i < 512; ++i) {
    auto h = (1469598103934665603ULL ^ static_cast<unsigned char>(ValueTag::Int)) * 1099511628211ULL;
    const auto bits = static_cast<unsigned long long>(static_cast<long long>(i - 256));
    for (int byte = 0; byte < 8; ++byte)
      h = (h ^ ((bits >> (byte * 8)) & 255ULL)) * 1099511628211ULL;
    table.values[i] = h;
  }
  return table;
}
static __device__ const DSmallIntHashTable d_small_int_hash_table = d_make_small_int_hash_table();

__device__ inline std::uint64_t d_hash_concat_element(const Value& value, bool small_int_hash) {
  if (small_int_hash && value.tag == ValueTag::Int && value.i >= -256 && value.i <= 255)
    return __ldg(&d_small_int_hash_table.values[static_cast<int>(value.i) + 256]);
  return d_hash_value_shallow(value);
}

__device__ inline std::uint64_t d_hash_list_payload(const Value* elems, int n) {
  std::uint64_t h = Value::fnv1a_init();
  h = Value::fnv1a_mix_u8(h, 0xA1U);
  h = Value::fnv1a_mix_u64(h, static_cast<std::uint64_t>(n));
  for (int i = 0; i < n; ++i) {
    h = Value::fnv1a_mix_u64(h, d_hash_value_shallow(elems[i]));
  }
  return h;
}

__device__ inline int d_find_string_payload_entry(const DPayloadTables& tables, std::int64_t packed) {
  int lo = 0;
  int hi = tables.string_entry_count;
  while (lo < hi) {
    const int mid = lo + ((hi - lo) >> 1);
    const std::int64_t mid_packed = tables.string_entries[mid].packed;
    if (mid_packed < packed) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  if (lo >= tables.string_entry_count || tables.string_entries[lo].packed != packed) {
    return -1;
  }
  return lo;
}

__device__ inline bool d_is_typed_list_tag(ValueTag tag) {
  return tag == ValueTag::IntList || tag == ValueTag::FloatList || tag == ValueTag::StringList;
}

__device__ inline std::uint8_t d_list_type_code(const Value& v) {
  if (v.tag == ValueTag::IntList) return 2U;
  if (v.tag == ValueTag::FloatList) return 3U;
  return 4U;
}

__device__ inline Value d_make_list_hash_len(ValueTag tag, std::uint64_t h, std::uint32_t len) {
  if (tag == ValueTag::IntList) return Value::from_int_list_hash_len(h, len);
  if (tag == ValueTag::FloatList) return Value::from_float_list_hash_len(h, len);
  return Value::from_string_list_hash_len(h, len);
}

__device__ inline std::uint64_t d_prepend_list_hash48(std::uint8_t type_code,
                                                      const Value& elem,
                                                      const Value& src) {
  std::uint64_t h = Value::fnv1a_init();
  h = Value::fnv1a_mix_u8(h, type_code);
  h = Value::fnv1a_mix_u8(h, 0x70U);
  h = Value::fnv1a_mix_u64(h, Value::shallow_hash64(elem));
  h = Value::fnv1a_mix_u64(h, Value::container_hash48(src));
  h = Value::fnv1a_mix_u64(h, static_cast<std::uint64_t>(Value::container_len(src)));
  return (h & Value::k_container_hash_mask);
}

__device__ inline bool d_list_accepts_elem(ValueTag list_tag, const Value& elem) {
  if (list_tag == ValueTag::IntList) return elem.tag == ValueTag::Int;
  if (list_tag == ValueTag::FloatList) return elem.tag == ValueTag::Float;
  if (list_tag == ValueTag::StringList) return elem.tag == ValueTag::String;
  return false;
}

__device__ inline bool d_is_ascii_letter(long long c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

__device__ inline bool d_is_ascii_digit(long long c) {
  return c >= '0' && c <= '9';
}

__device__ inline bool d_is_ascii_space(long long c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

__device__ inline bool d_is_ascii_vowel(long long c) {
  if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
  return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
}

__device__ inline long long d_ascii_to_lower(long long c) {
  return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

__device__ inline long long d_ascii_to_upper(long long c) {
  return (c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c;
}

__device__ inline int d_append_int_decimal(char* out, long long value) {
  int n = 0;
  unsigned long long mag = 0;
  if (value < 0) {
    out[n++] = '-';
    mag = static_cast<unsigned long long>(-(value + 1)) + 1ULL;
  } else {
    mag = static_cast<unsigned long long>(value);
  }
  char digits[32];
  int digit_count = 0;
  do {
    digits[digit_count++] = static_cast<char>('0' + (mag % 10ULL));
    mag /= 10ULL;
  } while (mag != 0ULL);
  for (int i = digit_count - 1; i >= 0; --i) {
    out[n++] = digits[i];
  }
  return n;
}

__device__ inline int d_append_literal(char* out, const char* value) {
  int n = 0;
  while (value[n] != '\0') {
    out[n] = value[n];
    ++n;
  }
  return n;
}

__device__ inline int d_append_float_decimal(char* out, double value) {
  if (value != value) {
    return d_append_literal(out, "nan");
  }
  if (value > 1.7976931348623157e308) {
    return d_append_literal(out, "inf");
  }
  if (value < -1.7976931348623157e308) {
    return d_append_literal(out, "-inf");
  }
  int n = 0;
  if (value < 0.0) {
    out[n++] = '-';
    value = -value;
  }
  long long whole = static_cast<long long>(value);
  double frac = value - static_cast<double>(whole);
  long long frac_scaled = static_cast<long long>(frac * 1000000.0 + 0.5);
  if (frac_scaled >= 1000000LL) {
    ++whole;
    frac_scaled -= 1000000LL;
  }
  n += d_append_int_decimal(out + n, whole);
  if (frac_scaled == 0LL) {
    if (n == 2 && out[0] == '-' && out[1] == '0') {
      out[0] = '0';
      return 1;
    }
    return n;
  }
  out[n++] = '.';
  int frac_start = n;
  char digits[6];
  for (int i = 5; i >= 0; --i) {
    digits[i] = static_cast<char>('0' + (frac_scaled % 10LL));
    frac_scaled /= 10LL;
  }
  for (int i = 0; i < 6; ++i) {
    out[n++] = digits[i];
  }
  while (n > frac_start && out[n - 1] == '0') {
    --n;
  }
  if (n == frac_start) {
    --n;
  }
  return n;
}

__device__ inline int d_find_list_payload_entry(const DPayloadTables& tables, ValueTag tag, std::int64_t packed) {
  int lo = 0;
  int hi = tables.list_entry_count;
  while (lo < hi) {
    const int mid = lo + ((hi - lo) >> 1);
    const DListPayloadEntry mid_entry = tables.list_entries[mid];
    const bool go_right =
        (static_cast<int>(mid_entry.tag) < static_cast<int>(tag)) ||
        (mid_entry.tag == tag && mid_entry.packed < packed);
    if (go_right) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  if (lo >= tables.list_entry_count || tables.list_entries[lo].tag != tag || tables.list_entries[lo].packed != packed) {
    return -1;
  }
  return lo;
}

// The escape bit means actual length 65536+low16. A clamped external length
// 65535 may hide one extra element, so a full-budget concat can copy 65537.
// That consumes all logical cells; rewinds never refund them. Other generated
// lists fit <=65536. Numeric observations retain the low-16-bit encoding.
constexpr std::uint64_t DPRIVATE_LIST_GLOBAL = 1ULL << 47;
constexpr std::uint64_t DPRIVATE_LIST_LENGTH_HIGH = 1ULL << 46;
constexpr std::uint64_t DPRIVATE_LIST_OFFSET_MASK = (1ULL << 31) - 1;
__device__ inline Value d_private_intlist(int offset, int length, bool global = false) {
  auto bits=static_cast<std::uint64_t>(offset) | (global?DPRIVATE_LIST_GLOBAL:0) |
      (length>=65536?DPRIVATE_LIST_LENGTH_HIGH:0);
  Value value=Value::from_int_list_hash_len(bits,static_cast<unsigned>(length));
  value.b=true;return value;
}

// Shared descriptor decoding keeps boxed and compact storage bounds identical.
__device__ inline bool d_private_intlist_bounds(const Value& value, int local_capacity,
    int global_capacity, bool& global, int& offset, int& length) {
  if(value.tag!=ValueTag::IntList || !value.b)return false;
  const auto bits=Value::container_hash48(value);
  if(bits & ~(DPRIVATE_LIST_GLOBAL|DPRIVATE_LIST_LENGTH_HIGH|DPRIVATE_LIST_OFFSET_MASK))return false;
  global=(bits&DPRIVATE_LIST_GLOBAL)!=0;
  offset=static_cast<int>(bits&DPRIVATE_LIST_OFFSET_MASK);
  length=static_cast<int>(Value::container_len(value));
  if(bits&DPRIVATE_LIST_LENGTH_HIGH) {
    length+=65536;if(length>(global?65536:65537))return false;
  }
  const int capacity=global?global_capacity:local_capacity;
  return capacity>=0 && offset<=capacity && length<=capacity-offset;
}

// Private read-only view: b marks an offset into this launch's list table.
// Static capability analysis excludes container equality and container outputs.
__device__ inline Value d_list_view(std::uint64_t offset, std::uint32_t length) {
  Value value = Value::from_int_list_hash_len(offset, length);
  value.b = true;
  return value;
}
__device__ inline bool d_convert_list_view(Value& value, const DPayloadTables& tables) {
  if (value.tag != ValueTag::IntList || value.b) return true;
  const int entry = d_find_list_payload_entry(tables, value.tag, value.i);
  if (entry < 0) return false;
  value = d_list_view(tables.list_entries[entry].offset, Value::container_len(value));
  return true;
}

template <typename State>
__device__ inline bool d_lookup_string_payload(const DPayloadTables& tables,
                                               const State& st,
                                               const Value& v,
                                               const char*& ptr,
                                               int& len) {
  if (v.tag != ValueTag::String) return false;
  if constexpr (State::kMaxStringEntries > 0) {
    for (int i = 0; i < st.string_entry_count; ++i) {
      if (st.string_entries[i].packed == v.i) {
        ptr = st.string_bytes + st.string_entries[i].offset;
        len = st.string_entries[i].len;
        return true;
      }
    }
  }
  const int idx = d_find_string_payload_entry(tables, v.i);
  if (idx >= 0) {
    ptr = tables.string_bytes + tables.string_entries[idx].offset;
    len = tables.string_entries[idx].len;
    return true;
  }
  return false;
}

__device__ inline unsigned d_private_list_bucket(const Value& value, unsigned capacity) {
  auto bits=static_cast<std::uint64_t>(value.i) ^ (static_cast<std::uint64_t>(value.tag)*0x9e3779b97f4a7c15ULL);
  bits ^= bits >> 33; bits *= 0xff51afd7ed558ccdULL; bits ^= bits >> 33;
  return static_cast<unsigned>(bits) & (capacity-1);
}

// One physical lane owns its table across sequential cases and queue tasks.
// Epoch zero denotes never initialized; wrap clears before reusing epoch one.
template <typename State>
__device__ inline void d_reset_private_list_index(State& st) {
  if constexpr (State::kEpochLists) {
    if (st.epoch_index) {
      unsigned next = *st.epoch_counter + 1u;
      if (!next) {
        for (int i=0;i<st.list_index_capacity;++i) st.epoch_index[i]=0;
        next=1;
      }
      st.epoch=next;*st.epoch_counter=next;
      return;
    }
  }
  for (int i=0;i<st.list_index_capacity;++i) st.list_index[i]=0;
}
template <typename State>
__device__ inline int d_private_list_index_read(const State& st, unsigned slot) {
  if constexpr (State::kEpochLists) {
    if (st.epoch_index) {
      const auto word=st.epoch_index[slot];
      return unsigned(word>>32)==st.epoch ? int(unsigned(word)) : 0;
    }
  }
  return st.list_index[slot];
}
template <typename State>
__device__ inline void d_private_list_index_write(State& st, unsigned slot, int encoded) {
  if constexpr (State::kEpochLists) {
    if (st.epoch_index) {
      st.epoch_index[slot]=(std::uint64_t(st.epoch)<<32)|unsigned(encoded);
      return;
    }
  }
  st.list_index[slot]=encoded;
}

template <typename State>
__device__ inline int d_find_local_list_entry(const State& st, const Value& value) {
  if constexpr (State::kIndexedLists) {
    unsigned slot=d_private_list_bucket(value,st.list_index_capacity);
    for(int probe=0;probe<st.list_index_capacity;++probe) {
      const int encoded=d_private_list_index_read(st,slot);
      if(!encoded)return -1;
      const int entry=encoded-1;
      if(st.list_entries[entry].tag==value.tag && st.list_entries[entry].packed==value.i)return entry;
      slot=(slot+1)&(st.list_index_capacity-1);
    }
    return -1;
  } else {
    for(int i=0;i<st.list_entry_count;++i)
      if(st.list_entries[i].tag==value.tag && st.list_entries[i].packed==value.i)return i;
    return -1;
  }
}

template <typename State>
__device__ inline bool d_index_local_list_entry(State& st, const Value& value, int entry) {
  if constexpr (State::kIndexedLists) {
    unsigned slot=d_private_list_bucket(value,st.list_index_capacity);
    for(int probe=0;probe<st.list_index_capacity;++probe) {
      const int encoded=d_private_list_index_read(st,slot);
      if(!encoded){d_private_list_index_write(st,slot,entry+1);return true;}
      const auto& first=st.list_entries[encoded-1];
      // Original linear lookup returns the first registration of an identity,
      // including a repeated canonical hash or an immutable input-window alias.
      if(first.tag==value.tag && first.packed==value.i)return true;
      slot=(slot+1)&(st.list_index_capacity-1);
    }
    st.physical_capacity_exceeded=true;st.physical_capacity_reason|=DPAYLOAD_OVERFLOW_ENTRIES;return false;
  }
  return true;
}

// Scalar regions discard their temporary registry suffix. Rebuild the private
// index only when entries were discarded: clearing individual slots would break
// open-address probe chains, and stale indices can alias later reused entries.
template <typename State>
__device__ inline void d_restore_local_list_entries(State& st, int count) {
  if (st.list_entry_count == count) return;
  st.list_entry_count = count;
  if constexpr (State::kIndexedLists) {
    d_reset_private_list_index(st);
    for (int i = 0; i < count; ++i) {
      const auto& entry = st.list_entries[i];
      Value key;
      key.tag = entry.tag;
      key.i = entry.packed;
      // A surviving subset fits the same table; insertion retains first-match
      // semantics for duplicate identities under the nonlogical policy.
      d_index_local_list_entry(st, key, i);
    }
  }
}

template <typename State>
__device__ inline void d_clear_concat_cache(State& st) {
  if constexpr (State::kConcatCache)
    if (st.concat_cache) for (int i=0;i<DCONCAT_CACHE_SLOTS;++i)
      st.concat_cache[i].tag=ValueTag::Invalid;
}

template <typename State>
__device__ inline DConcatCacheEntry* d_concat_cache_slot(State& st,
    const Value* left, int left_len, const Value* right, int right_len, ValueTag tag) {
  if constexpr (State::kConcatCache) {
    if(st.concat_cache) {
      auto key=(reinterpret_cast<std::uintptr_t>(left)>>4) ^
          ((reinterpret_cast<std::uintptr_t>(right)>>4)*0x9e3779b97f4a7c15ULL) ^
          (static_cast<unsigned long long>(left_len)<<32) ^ right_len ^ static_cast<unsigned>(tag);
      key^=key>>33;key*=0xff51afd7ed558ccdULL;key^=key>>33;
      return st.concat_cache+(key&(DCONCAT_CACHE_SLOTS-1));
    }
  }
  return nullptr;
}

// Entries and cells can rewind independently under logical deduplication.
// Invalidate before either rewind so reused addresses never match stale keys.
template <typename State>
__device__ inline void d_restore_local_list_storage(State& st,int entries,int values) {
  if(st.list_entry_count!=entries || st.list_values_used!=values)d_clear_concat_cache(st);
  d_restore_local_list_entries(st,entries);
  st.list_values_used=values;
}

template <typename State>
__device__ inline bool d_lookup_list_payload(const DPayloadTables& tables,
                                             const State& st,
                                             const Value& v,
                                             const Value*& ptr,
                                             int& len) {
  if (!d_is_typed_list_tag(v.tag)) return false;
  if (st.private_intlists && v.b) {
    bool global=false;int offset=0;
    if(!d_private_intlist_bounds(v,st.list_values_used,tables.list_value_count,global,offset,len))return false;
    // Compact local storage must never be exposed through a boxed Value pointer.
    if(!global && st.compact_intlist_cells)return false;
    const auto* backing=global?tables.list_values:st.list_values;
    if(len && !backing)return false;
    ptr=backing?backing+offset:nullptr;
    return true;
  }
  if constexpr (State::kMaxListEntries > 0) {
    // Private lists never enter the registry; its index is deliberately not
    // initialized for this invocation. Canonical external lists go straight to
    // immutable storage and cannot resolve through stale scratch entries.
    if(!st.private_intlists) {
      const int entry=d_find_local_list_entry(st,v);
      if(entry>=0) {
        const int offset=st.list_entries[entry].offset;
        ptr=offset>=0?st.list_values+offset:tables.list_values+(-offset-1);
        len=st.list_entries[entry].len;
        return true;
      }
    }
  }
  const int idx = d_find_list_payload_entry(tables, v.tag, v.i);
  if (idx >= 0) {
    ptr = tables.list_values + tables.list_entries[idx].offset;
    len = tables.list_entries[idx].len;
    return true;
  }
  return false;
}

// Local compact integers and immutable boxed tables have separate typed
// pointers. Reader dispatch can be hoisted outside copying and distance loops.
struct DPrivateIntListSpan {
  const Value* boxed=nullptr;
  const std::int64_t* integers=nullptr;
  int size=0;
  int integer_stride=1;
  template<bool Compact> __device__ std::int64_t read(int index) const {
    if constexpr (Compact)return integers[static_cast<std::size_t>(index)*integer_stride];
    else return boxed[index].i;
  }
  __device__ std::int64_t at(int index) const {
    return integers?read<true>(index):read<false>(index);
  }
};

template<class State>
__device__ inline bool d_private_intlist_span(const DPayloadTables& tables,
    const State& state,const Value& value,DPrivateIntListSpan& span) {
  if(!state.private_intlists || value.tag!=ValueTag::IntList)return false;
  bool global=true;int offset=0,length=0;
  if(value.b) {
    if(!d_private_intlist_bounds(value,state.list_values_used,tables.list_value_count,global,offset,length))return false;
  } else {
    const int entry=d_find_list_payload_entry(tables,ValueTag::IntList,value.i);
    if(entry<0)return false;
    offset=tables.list_entries[entry].offset;length=tables.list_entries[entry].len;
    if(offset<0 || length<0 || length>65536 || offset>tables.list_value_count ||
        length>tables.list_value_count-offset)return false;
  }
  span={};span.size=length;
  if(!global && state.compact_intlist_cells) {
    if((state.intlist_cell_stride!=1 && state.intlist_cell_stride!=32) || (length && !state.intlist_cells))return false;
    span.integer_stride=state.intlist_cell_stride;
    // Empty windows at capacity have no cell; do not form a pointer beyond
    // the block allocation for nonzero lanes of an interleaved buffer.
    span.integers=length?state.intlist_cells+static_cast<std::size_t>(offset)*span.integer_stride:nullptr;
  } else {
    const auto* backing=global?tables.list_values:state.list_values;
    if(length && !backing)return false;
    span.boxed=backing?backing+offset:nullptr;
  }
  return true;
}

__device__ inline void d_copy_private_intlist(std::int64_t* target,const DPrivateIntListSpan& source,int target_stride) {
  if(source.integers)for(int i=0;i<source.size;++i)target[static_cast<std::size_t>(i)*target_stride]=source.read<true>(i);
  else for(int i=0;i<source.size;++i)target[static_cast<std::size_t>(i)*target_stride]=source.read<false>(i);
}

template <typename State>
__device__ inline bool d_has_exact_payload(const DPayloadTables& tables, const State& state,
                                         const Value& value) {
  int length = 0;
  if (value.tag == ValueTag::String) {
    const char* bytes = nullptr;
    return d_lookup_string_payload(tables, state, value, bytes, length);
  }
  if (d_is_typed_list_tag(value.tag)) {
    const Value* elements = nullptr;
    return d_lookup_list_payload(tables, state, value, elements, length);
  }
  return true;
}

template <typename State>
__device__ inline bool d_register_local_string(State& st, const Value& v, int offset, int len) {
  if constexpr (State::kMaxStringEntries <= 0) {
    (void)st;
    (void)v;
    (void)offset;
    (void)len;
    return false;
  }
  if (st.string_entry_count >= st.string_entry_capacity) { st.physical_capacity_exceeded = true; st.physical_capacity_reason |= DPAYLOAD_OVERFLOW_ENTRIES; return false; }
  st.string_entries[st.string_entry_count++] = DStringPayloadEntry{v.i, offset, len};
  return true;
}

template <typename State>
__device__ inline bool d_register_local_list(State& st, const Value& v, int offset, int len) {
  if constexpr (State::kMaxListEntries <= 0) {
    (void)st;
    (void)v;
    (void)offset;
    (void)len;
    return false;
  }
  // Under the logical arena policy, physical registry entries are identities,
  // not allocation events. Keep the first resident identical-key payload.
  if (st.logical_list_limit && d_find_local_list_entry(st,v)>=0) return true;
  if (st.list_entry_count >= st.list_entry_capacity) {
    st.physical_capacity_exceeded = true; st.physical_capacity_reason |= DPAYLOAD_OVERFLOW_ENTRIES; return false;
  }
  const int entry=st.list_entry_count++;
  st.list_entries[entry] = DListPayloadEntry{v.tag, v.i, offset, len};
  return d_index_local_list_entry(st,v,entry);
}

template <typename State>
__device__ inline bool d_charge_list_allocation(State& state, unsigned cells) {
  if (!state.logical_list_limit || !cells) return true;
  if (state.logical_list_used > state.logical_list_limit ||
      cells > state.logical_list_limit - state.logical_list_used) return false;
  state.logical_list_used += cells;
  return true;
}

template <DPayloadFlavor Flavor>
__device__ inline bool d_builtin_call(BuiltinId bid,
                                      const Value* args,
                                      int argc,
                                      const DPayloadTables& tables,
                                      typename DPayloadFlavorTraits<Flavor>::State& payload_state,
                                      Value& out,
                                      ErrCode& err) {
  using PayloadTraits = DPayloadFlavorTraits<Flavor>;

  // This branch is reachable only after whole-program capability admission.
  // It shares the arena, fuel driver, charging and physical-overflow replay.
  if constexpr (Flavor == DPayloadFlavor::ArenaMixed || Flavor == DPayloadFlavor::IndexedArena) {
    auto& st=payload_state;
    if(st.private_intlists && (bid==BuiltinId::Singleton || bid==BuiltinId::Concat || bid==BuiltinId::Slice ||
        bid==BuiltinId::Index || bid==BuiltinId::IndexNonnegative || bid==BuiltinId::IndexClampedOrZero)) {
      if(st.compact_intlist_cells && (st.intlist_cell_stride!=1 && st.intlist_cell_stride!=32)) {
        err=ErrCode::Value;return false;
      }
      DPrivateIntListSpan a,b;
      if(bid==BuiltinId::Singleton) {
        if(argc!=1 || args[0].tag!=ValueTag::Int){err=ErrCode::Type;return false;}
        if(!d_charge_list_allocation(st,1)){err=ErrCode::Value;return false;}
        if(st.list_values_used>=st.kMaxListValues) {
          st.physical_capacity_exceeded=true;st.physical_capacity_reason|=DPAYLOAD_OVERFLOW_VALUES;
          err=ErrCode::Value;return false;
        }
        const int offset=st.list_values_used++;
        if(st.compact_intlist_cells)st.intlist_cells[static_cast<std::size_t>(offset)*st.intlist_cell_stride]=args[0].i;
        else st.list_values[offset]=args[0];
        out=d_private_intlist(offset,1);return true;
      }
      if(bid==BuiltinId::Index || bid==BuiltinId::IndexNonnegative || bid==BuiltinId::IndexClampedOrZero) {
        if(argc!=2 || args[0].tag!=ValueTag::IntList || args[1].tag!=ValueTag::Int){err=ErrCode::Type;return false;}
        const auto length=static_cast<long long>(Value::container_len(args[0]));
        long long index=0;
        if(bid==BuiltinId::IndexClampedOrZero) {
          if(!length){out=Value::from_int(0);return true;}
          index=args[1].i<0?0:args[1].i>=length?length-1:args[1].i;
        } else if((bid==BuiltinId::IndexNonnegative && args[1].i<0) ||
            !d_norm_index_idx(args[1].i,length,index)){err=ErrCode::Value;return false;}
        if(!d_private_intlist_span(tables,st,args[0],a) || index>=a.size){err=ErrCode::Value;return false;}
        out=Value::from_int(a.at(static_cast<int>(index)));return true;
      }
      if(argc!=(bid==BuiltinId::Concat?2:3) || args[0].tag!=ValueTag::IntList ||
          (bid==BuiltinId::Concat?args[1].tag!=ValueTag::IntList:
           (args[1].tag!=ValueTag::Int || args[2].tag!=ValueTag::Int))) {
        err=ErrCode::Type;return false;
      }
      if(!d_private_intlist_span(tables,st,args[0],a)) {err=ErrCode::Value;return false;}
      if(bid==BuiltinId::Slice) {
        const auto length=static_cast<long long>(Value::container_len(args[0]));
        const int lo=static_cast<int>(d_norm_slice_idx(args[1].i,length));
        const int hi=static_cast<int>(d_norm_slice_idx(args[2].i,length));
        const int count=hi>lo?hi-lo:0;
        // Positive logical allocation admission already selects immutable window
        // aliasing in the ordinary arena; no copy policy is removed here.
        if(args[0].b) {
          const auto bits=Value::container_hash48(args[0]);
          out=d_private_intlist(static_cast<int>(bits&DPRIVATE_LIST_OFFSET_MASK)+lo,count,
              (bits&DPRIVATE_LIST_GLOBAL)!=0);
        } else {
          const int entry=d_find_list_payload_entry(tables,ValueTag::IntList,args[0].i);
          if(entry<0){err=ErrCode::Value;return false;}
          out=d_private_intlist(tables.list_entries[entry].offset+lo,count,true);
        }
        return true;
      }
      if(!d_private_intlist_span(tables,st,args[1],b)){err=ErrCode::Value;return false;}
      const auto left=Value::container_len(args[0]),right=Value::container_len(args[1]);
      if(!left){out=args[1];return true;}if(!right){out=args[0];return true;}
      if(!d_charge_list_allocation(st,left+right)){err=ErrCode::Value;return false;}
      const int n=a.size,m=b.size;
      if(n<0 || m<0 || n>65537-m || n+m>st.kMaxListValues-st.list_values_used) {
        st.physical_capacity_exceeded=true;st.physical_capacity_reason|=DPAYLOAD_OVERFLOW_VALUES;
        err=ErrCode::Value;return false;
      }
      const int offset=st.list_values_used;
      if(st.compact_intlist_cells) {
        if(n)d_copy_private_intlist(st.intlist_cells+static_cast<std::size_t>(offset)*st.intlist_cell_stride,a,st.intlist_cell_stride);
        if(m)d_copy_private_intlist(st.intlist_cells+static_cast<std::size_t>(offset+n)*st.intlist_cell_stride,b,st.intlist_cell_stride);
      } else {
        for(int i=0;i<n;++i)st.list_values[offset+i]=a.boxed[i];
        for(int i=0;i<m;++i)st.list_values[offset+n+i]=b.boxed[i];
      }
      st.list_values_used+=n+m;out=d_private_intlist(offset,n+m);return true;
    }
  }

  if constexpr ((Flavor == DPayloadFlavor::IntListViews || Flavor == DPayloadFlavor::BoundIntListViews)) {
    if ((bid == BuiltinId::Index || bid == BuiltinId::IndexNonnegative || bid == BuiltinId::IndexClampedOrZero) || bid == BuiltinId::Slice) {
      if (argc != ((bid == BuiltinId::Index || bid == BuiltinId::IndexNonnegative || bid == BuiltinId::IndexClampedOrZero) ? 2 : 3) || args[0].tag != ValueTag::IntList ||
          args[1].tag != ValueTag::Int || (argc == 3 && args[2].tag != ValueTag::Int)) {
        err = ErrCode::Type; return false;
      }
      Value source = args[0];
      if (!d_convert_list_view(source, tables)) { err = ErrCode::Value; return false; }
      const auto offset = Value::container_hash48(source);
      const auto length = static_cast<long long>(Value::container_len(source));
      if (bid == BuiltinId::Index || bid == BuiltinId::IndexNonnegative || bid == BuiltinId::IndexClampedOrZero) {
        long long index;
        if (bid == BuiltinId::IndexClampedOrZero) {
          if (!length) { out = Value::from_int(0); return true; }
          index = args[1].i < 0 ? 0 : (args[1].i >= length ? length-1 : args[1].i);
        } else if ((bid == BuiltinId::IndexNonnegative && args[1].i < 0) || !d_norm_index_idx(args[1].i, length, index)) { err = ErrCode::Value; return false; }
        out = tables.list_values[offset + index];
        if (bid == BuiltinId::IndexClampedOrZero && out.tag != ValueTag::Int) { err = ErrCode::Type; return false; }
      } else {
        const auto lo = d_norm_slice_idx(args[1].i, length);
        const auto hi = d_norm_slice_idx(args[2].i, length);
        out = d_list_view(offset + lo, static_cast<std::uint32_t>(hi > lo ? hi - lo : 0));
      }
      return true;
    }
  }

  if (bid == BuiltinId::Abs) {
    if (argc != 1) {
      err = ErrCode::Type;
      return false;
    }
    const Value& x = args[0];
    if (!d_is_num(x)) {
      err = ErrCode::Type;
      return false;
    }
    out = (x.tag == ValueTag::Float)
              ? Value::from_float(vm_semantics::canonicalize_vm_float(x.f < 0 ? -x.f : x.f))
                                      : Value::from_int(x.i < 0 ? -x.i : x.i);
    return true;
  }

  if (bid == BuiltinId::Min || bid == BuiltinId::Max) {
    if (argc != 2) {
      err = ErrCode::Type;
      return false;
    }
    double a = 0.0;
    double b = 0.0;
    bool any_float = false;
    if (!d_to_numeric_pair(args[0], args[1], a, b, any_float)) {
      err = ErrCode::Type;
      return false;
    }
    const double pick =
        (bid == BuiltinId::Min) ? ((a <= b) ? a : b) : ((a >= b) ? a : b);
    out = any_float ? Value::from_float(vm_semantics::canonicalize_vm_float(pick))
                    : Value::from_int(bid == BuiltinId::Min
                        ? (args[0].i <= args[1].i ? args[0].i : args[1].i)
                        : (args[0].i >= args[1].i ? args[0].i : args[1].i));
    return true;
  }

  if (bid == BuiltinId::Clip) {
    if (argc != 3) {
      err = ErrCode::Type;
      return false;
    }
    const Value& x = args[0];
    const Value& lo = args[1];
    const Value& hi = args[2];
    if (!d_is_num(x) || !d_is_num(lo) || !d_is_num(hi)) {
      err = ErrCode::Type;
      return false;
    }
    if (x.tag != lo.tag || x.tag != hi.tag) {
      err = ErrCode::Type;
      return false;
    }
    const bool any_float =
        (x.tag == ValueTag::Float) || (lo.tag == ValueTag::Float) || (hi.tag == ValueTag::Float);
    if (any_float) {
      const double x2 = (x.tag == ValueTag::Float) ? x.f : static_cast<double>(x.i);
      const double lo2 = (lo.tag == ValueTag::Float) ? lo.f : static_cast<double>(lo.i);
      const double hi2 = (hi.tag == ValueTag::Float) ? hi.f : static_cast<double>(hi.i);
      const double lower = (x2 > lo2) ? x2 : lo2;
      const double picked = (lower < hi2) ? lower : hi2;
      out = Value::from_float(vm_semantics::canonicalize_vm_float(picked));
      return true;
    }
    const long long x2 = x.i;
    const long long lo2 = lo.i;
    const long long hi2 = hi.i;
    const long long lower = (x2 > lo2) ? x2 : lo2;
    out = Value::from_int((lower < hi2) ? lower : hi2);
    return true;
  }

  if (bid == BuiltinId::TruncDiv0 || bid == BuiltinId::TruncMod0 || bid == BuiltinId::IDiv0 || bid == BuiltinId::IMod0) {
    if (argc != 2) {
      err = ErrCode::Type;
      return false;
    }
    const Value& a = args[0];
    const Value& b = args[1];
    if (a.tag != ValueTag::Int || b.tag != ValueTag::Int) {
      err = ErrCode::Type;
      return false;
    }
    if (b.i == 0) {
      out = Value::from_int(0);
    } else if (bid == BuiltinId::TruncDiv0 || bid == BuiltinId::TruncMod0) {
      out=Value::from_int(bid==BuiltinId::TruncDiv0 ? vm_semantics::trunc_int_div0(a.i,b.i) : vm_semantics::trunc_int_mod0(a.i,b.i));
    } else if (bid == BuiltinId::IDiv0) {
      out = Value::from_int(vm_semantics::wrap_int_div(a.i, b.i));
    } else {
      out = Value::from_int(d_int_mod(a.i, b.i));
    }
    return true;
  }

  if (bid == BuiltinId::Len) {
    if (argc != 1) {
      err = ErrCode::Type;
      return false;
    }
    const Value& x = args[0];
    if (!(x.tag == ValueTag::String || d_is_typed_list_tag(x.tag))) {
      err = ErrCode::Type;
      return false;
    }
    out = Value::from_int(static_cast<long long>(Value::container_len(x)));
    return true;
  }

  if (bid == BuiltinId::Concat) {
    if (argc != 2) {
      err = ErrCode::Type;
      return false;
    }
    const Value& a = args[0];
    const Value& b = args[1];
    if (a.tag == ValueTag::String && b.tag == ValueTag::String) {
      if constexpr (PayloadTraits::kHasString) {
        auto& st = payload_state;
        const char* ap = nullptr;
        const char* bp = nullptr;
        int al = 0;
        int bl = 0;
        if (d_lookup_string_payload(tables, st, a, ap, al) &&
            d_lookup_string_payload(tables, st, b, bp, bl) &&
            al >= 0 && bl >= 0 &&
            st.string_bytes_used + al + bl <= st.kMaxStringBytes) {
          const int off = st.string_bytes_used;
          for (int i = 0; i < al; ++i) st.string_bytes[off + i] = ap[i];
          for (int i = 0; i < bl; ++i) st.string_bytes[off + al + i] = bp[i];
          st.string_bytes_used += al + bl;
          const std::uint64_t h = d_hash_bytes(st.string_bytes + off, al + bl);
          out = Value::from_string_hash_len(h, static_cast<std::uint32_t>(al + bl));
          if (!d_register_local_string(st, out, off, al + bl)) { err = ErrCode::Value; return false; }
          return true;
        }
      }
      if constexpr (d_exact_payload_flavor<Flavor>) {
        if (d_has_exact_payload(tables, payload_state, a) && d_has_exact_payload(tables, payload_state, b)) { payload_state.physical_capacity_exceeded = true; payload_state.physical_capacity_reason |= DPAYLOAD_OVERFLOW_BYTES; err = ErrCode::Value; return false; }
      }
    const std::uint32_t len = Value::saturating_len_add(Value::container_len(a), Value::container_len(b));
      const std::uint64_t h = Value::combine_container_hash48(1U, a, b);
      out = Value::from_fallback_token(Value::pack_container_payload(h, len));
      return true;
    }
    if (d_is_typed_list_tag(a.tag) && a.tag == b.tag) {
      const bool fused=payload_state.fused_list_concat;
      const Value* ap = nullptr;
      const Value* bp = nullptr;
      int al = 0, bl = 0;
      // Resolution does not mutate the registry. Reuse precisely the same
      // first local/global matches through charging, copying and failure paths.
      const bool resolved=fused && d_lookup_list_payload(tables,payload_state,a,ap,al) &&
          d_lookup_list_payload(tables,payload_state,b,bp,bl);
      if (payload_state.logical_list_limit && (fused ? resolved :
          (d_has_exact_payload(tables, payload_state, a) && d_has_exact_payload(tables, payload_state, b)))) {
        const unsigned al = Value::container_len(a), bl = Value::container_len(b);
        if (!al) { out = b; return true; }
        if (!bl) { out = a; return true; }
        if (!d_charge_list_allocation(payload_state, al + bl)) { err = ErrCode::Value; return false; }
      }
      if constexpr (PayloadTraits::kHasList) {
        auto& st = payload_state;
        if ((fused ? resolved : (d_lookup_list_payload(tables, st, a, ap, al) &&
            d_lookup_list_payload(tables, st, b, bp, bl))) &&
            al >= 0 && bl >= 0 &&
            st.list_values_used + al + bl <= st.kMaxListValues) {
          const int off = st.list_values_used;
          auto* cached=fused?d_concat_cache_slot(st,ap,al,bp,bl,a.tag):nullptr;
          const bool hit=cached && cached->tag==a.tag && cached->left==ap && cached->right==bp &&
              cached->left_len==al && cached->right_len==bl;
          int result_offset=off;
          std::uint64_t h;
          // Capacity/charge checks above still apply to hits. Reserve the same
          // cells below, but register the earlier immutable backing so no caller
          // can observe the unwritten reservation. Canonical lookup is unchanged.
          if(hit) {
            h=cached->hash;result_offset=cached->output_offset;
          } else if(fused) {
            h=Value::fnv1a_mix_u8(Value::fnv1a_init(),0xA1U);
            h=Value::fnv1a_mix_u64(h,static_cast<std::uint64_t>(al+bl));
            for(int i=0;i<al;++i) {
              const Value value=ap[i];st.list_values[off+i]=value;
              h=Value::fnv1a_mix_u64(h,d_hash_concat_element(value,st.small_int_hash));
            }
            for(int i=0;i<bl;++i) {
              const Value value=bp[i];st.list_values[off+al+i]=value;
              h=Value::fnv1a_mix_u64(h,d_hash_concat_element(value,st.small_int_hash));
            }
          } else {
            for (int i = 0; i < al; ++i) st.list_values[off + i] = ap[i];
            for (int i = 0; i < bl; ++i) st.list_values[off + al + i] = bp[i];
            h=d_hash_list_payload(st.list_values+off,al+bl);
          }
          st.list_values_used += al + bl;
          out = d_make_list_hash_len(a.tag, h, static_cast<std::uint32_t>(al + bl));
          if constexpr (Flavor == DPayloadFlavor::ArenaMixed || Flavor == DPayloadFlavor::IndexedArena) {
            if(cached && st.concat_cache_diagnostics) {
              if(hit){++st.concat_hits;st.concat_cells+=al+bl;}else ++st.concat_misses;
            }
          }
          if (!d_register_local_list(st, out, result_offset, al + bl)) { err = ErrCode::Value; return false; }
          if(cached && !hit) *cached=DConcatCacheEntry{ap,bp,h,al,bl,off,a.tag};
          return true;
        }
      }
      if constexpr (d_exact_payload_flavor<Flavor>) {
        if (fused ? resolved : (d_has_exact_payload(tables, payload_state, a) && d_has_exact_payload(tables, payload_state, b))) { payload_state.physical_capacity_exceeded = true; payload_state.physical_capacity_reason |= DPAYLOAD_OVERFLOW_VALUES; err = ErrCode::Value; return false; }
      }
    const std::uint32_t len = Value::saturating_len_add(Value::container_len(a), Value::container_len(b));
      const std::uint64_t h = Value::combine_container_hash48(d_list_type_code(a), a, b);
      out = d_make_list_hash_len(a.tag, h, len);
      return true;
    }
    err = ErrCode::Type;
    return false;
  }

  if (bid == BuiltinId::Slice) {
    if (argc != 3) {
      err = ErrCode::Type;
      return false;
    }
    const Value& x = args[0];
    const Value& lo = args[1];
    const Value& hi = args[2];
    if (!(x.tag == ValueTag::String || d_is_typed_list_tag(x.tag))) {
      err = ErrCode::Type;
      return false;
    }
    if (lo.tag != ValueTag::Int || hi.tag != ValueTag::Int) {
      err = ErrCode::Type;
      return false;
    }
    const long long n = static_cast<long long>(Value::container_len(x));
    const long long l = d_norm_slice_idx(lo.i, n);
    const long long h = d_norm_slice_idx(hi.i, n);
    const long long out_len_ll = (h > l) ? (h - l) : 0LL;
    const std::uint32_t out_len =
        static_cast<std::uint32_t>(out_len_ll > static_cast<long long>(Value::k_container_len_max)
                                       ? Value::k_container_len_max
                                       : out_len_ll);
    if (x.tag == ValueTag::String) {
      if constexpr (PayloadTraits::kHasString) {
        auto& st = payload_state;
        const char* xp = nullptr;
        int xl = 0;
        if (d_lookup_string_payload(tables, st, x, xp, xl) &&
            l >= 0 && l <= xl && h >= 0 && h <= xl &&
            st.string_bytes_used + static_cast<int>(out_len) <= st.kMaxStringBytes) {
          const int off = st.string_bytes_used;
          for (int i = 0; i < static_cast<int>(out_len); ++i) {
            st.string_bytes[off + i] = xp[static_cast<int>(l) + i];
          }
          st.string_bytes_used += static_cast<int>(out_len);
          const std::uint64_t out_h_exact = d_hash_bytes(st.string_bytes + off, static_cast<int>(out_len));
          out = Value::from_string_hash_len(out_h_exact, out_len);
          if (!d_register_local_string(st, out, off, static_cast<int>(out_len))) { err = ErrCode::Value; return false; }
          return true;
        }
      }
      if constexpr (d_exact_payload_flavor<Flavor>) {
        if (d_has_exact_payload(tables, payload_state, x)) { payload_state.physical_capacity_exceeded = true; payload_state.physical_capacity_reason |= DPAYLOAD_OVERFLOW_BYTES; err = ErrCode::Value; return false; }
      }
    const std::uint64_t out_h = Value::slice_container_hash48(1U, x, lo.i, hi.i);
      out = Value::from_fallback_token(Value::pack_container_payload(out_h, out_len));
      return true;
    }
    if constexpr (PayloadTraits::kHasList) {
      auto& st = payload_state;
      const Value* xp = nullptr;
      int xl = 0;
      bool alias_windows=st.logical_list_limit!=0;
      if constexpr (d_exact_payload_flavor<Flavor>)
        alias_windows|=st.alias_windows;
      if (alias_windows && d_lookup_list_payload(tables, st, x, xp, xl) &&
          l >= 0 && l <= xl && h >= 0 && h <= xl) {
        // A source window owns no cells. Alias the immutable case table or an
        // earlier append-only local allocation, preserving exact content hashes.
        int source_offset = 0;
        bool found = false;
        const int local_entry=d_find_local_list_entry(st,x);
        if(local_entry>=0){source_offset=st.list_entries[local_entry].offset;found=true;}
        if (!found) {
          const int entry = d_find_list_payload_entry(tables, x.tag, x.i);
          if (entry >= 0) { source_offset = -tables.list_entries[entry].offset - 1; found = true; }
        }
        if (found) {
          const int offset = source_offset >= 0 ? source_offset + static_cast<int>(l)
                                               : source_offset - static_cast<int>(l);
          const auto hash = d_hash_list_payload(out_len ? xp + l : nullptr, out_len);
          out = d_make_list_hash_len(x.tag, hash, out_len);
          if (!d_register_local_list(st, out, offset, out_len)) { err = ErrCode::Value; return false; }
          return true;
        }
      }
      if (d_lookup_list_payload(tables, st, x, xp, xl) &&
          l >= 0 && l <= xl && h >= 0 && h <= xl &&
          st.list_values_used + static_cast<int>(out_len) <= st.kMaxListValues) {
        const int off = st.list_values_used;
        for (int i = 0; i < static_cast<int>(out_len); ++i) {
          st.list_values[off + i] = xp[static_cast<int>(l) + i];
        }
        st.list_values_used += static_cast<int>(out_len);
        const std::uint64_t out_h_exact = d_hash_list_payload(st.list_values + off, static_cast<int>(out_len));
        out = d_make_list_hash_len(x.tag, out_h_exact, out_len);
        if (!d_register_local_list(st, out, off, static_cast<int>(out_len))) { err = ErrCode::Value; return false; }
        return true;
      }
    }
    if constexpr (d_exact_payload_flavor<Flavor>) {
        if (d_has_exact_payload(tables, payload_state, x)) { payload_state.physical_capacity_exceeded = true; payload_state.physical_capacity_reason |= DPAYLOAD_OVERFLOW_VALUES; err = ErrCode::Value; return false; }
      }
    const std::uint64_t out_h = Value::slice_container_hash48(d_list_type_code(x), x, lo.i, hi.i);
    out = d_make_list_hash_len(x.tag, out_h, out_len);
    return true;
  }

  if (bid == BuiltinId::Index || bid == BuiltinId::IndexNonnegative || bid == BuiltinId::IndexClampedOrZero) {
    if (argc != 2) {
      err = ErrCode::Type;
      return false;
    }
    const Value& x = args[0];
    const Value& i = args[1];
    if (!(x.tag == ValueTag::String || d_is_typed_list_tag(x.tag)) ||
        (bid == BuiltinId::IndexClampedOrZero && x.tag != ValueTag::IntList)) {
      err = ErrCode::Type;
      return false;
    }
    if (i.tag != ValueTag::Int) {
      err = ErrCode::Type;
      return false;
    }
    const long long n = static_cast<long long>(Value::container_len(x));
    long long j = 0;
    if (bid == BuiltinId::IndexClampedOrZero) {
      if (n == 0) { out = Value::from_int(0); return true; }
      j = i.i < 0 ? 0 : (i.i >= n ? n - 1 : i.i);
    } else if ((bid == BuiltinId::IndexNonnegative && i.i < 0) || !d_norm_index_idx(i.i, n, j)) {
      err = ErrCode::Value;
      return false;
    }
    if (x.tag == ValueTag::String) {
      if constexpr (PayloadTraits::kHasString) {
        auto& st = payload_state;
        const char* xp = nullptr;
        int xl = 0;
        if (d_lookup_string_payload(tables, st, x, xp, xl) && j < xl) {
          out = Value::from_char(static_cast<unsigned char>(xp[static_cast<int>(j)]));
          return true;
        }
      }
      if constexpr (d_exact_payload_flavor<Flavor>) {
        if (d_has_exact_payload(tables, payload_state, x)) { err = ErrCode::Value; return false; }
      }
      out = Value::from_fallback_token(Value::index_container_token64(5U, x, j));
      return true;
    }
    if constexpr (PayloadTraits::kHasList) {
      auto& st = payload_state;
      const Value* xp = nullptr;
      int xl = 0;
      if (d_lookup_list_payload(tables, st, x, xp, xl) && j < xl) {
        out = xp[static_cast<int>(j)];
        if (bid == BuiltinId::IndexClampedOrZero && out.tag != ValueTag::Int) { err = ErrCode::Type; return false; }
        return true;
      }
    }
    if constexpr (d_exact_payload_flavor<Flavor>) {
        if (d_has_exact_payload(tables, payload_state, x)) { err = ErrCode::Value; return false; }
      }
    out = Value::from_fallback_token(Value::index_container_token64(d_list_type_code(x), x, j));
    return true;
  }

  if (bid == BuiltinId::Append) {
    if (argc != 2) {
      err = ErrCode::Type;
      return false;
    }
    const Value& xs = args[0];
    const Value& elem = args[1];
    if (!d_is_typed_list_tag(xs.tag)) {
      err = ErrCode::Type;
      return false;
    }
    if (!d_list_accepts_elem(xs.tag, elem)) {
      err = ErrCode::Type;
      return false;
    }
    if constexpr (PayloadTraits::kHasList) {
      auto& st = payload_state;
      const Value* xp = nullptr;
      int xl = 0;
      if (d_lookup_list_payload(tables, st, xs, xp, xl) && st.list_values_used + xl + 1 <= st.kMaxListValues) {
        const int off = st.list_values_used;
        for (int i = 0; i < xl; ++i) st.list_values[off + i] = xp[i];
        st.list_values[off + xl] = elem;
        st.list_values_used += xl + 1;
        const std::uint64_t h = d_hash_list_payload(st.list_values + off, xl + 1);
        out = d_make_list_hash_len(xs.tag, h, static_cast<std::uint32_t>(xl + 1));
        if (!d_register_local_list(st, out, off, xl + 1)) { err = ErrCode::Value; return false; }
        return true;
      }
    }
    if constexpr (d_exact_payload_flavor<Flavor>) {
        if (d_has_exact_payload(tables, payload_state, xs)) { payload_state.physical_capacity_exceeded = true; payload_state.physical_capacity_reason |= DPAYLOAD_OVERFLOW_VALUES; err = ErrCode::Value; return false; }
      }
    const std::uint32_t len = Value::saturating_len_add(Value::container_len(xs), 1U);
    const std::uint64_t h = Value::append_list_hash48(d_list_type_code(xs), xs, elem);
    out = d_make_list_hash_len(xs.tag, h, len);
    return true;
  }

  if (bid == BuiltinId::Prepend) {
    if (argc != 2) {
      err = ErrCode::Type;
      return false;
    }
    const Value& xs = args[0];
    const Value& elem = args[1];
    if (!d_is_typed_list_tag(xs.tag) || !d_list_accepts_elem(xs.tag, elem)) {
      err = ErrCode::Type;
      return false;
    }
    if constexpr (PayloadTraits::kHasList) {
      auto& st = payload_state;
      const Value* xp = nullptr;
      int xl = 0;
      if (d_lookup_list_payload(tables, st, xs, xp, xl) && st.list_values_used + xl + 1 <= st.kMaxListValues) {
        const int off = st.list_values_used;
        st.list_values[off] = elem;
        for (int i = 0; i < xl; ++i) st.list_values[off + 1 + i] = xp[i];
        st.list_values_used += xl + 1;
        const std::uint64_t h = d_hash_list_payload(st.list_values + off, xl + 1);
        out = d_make_list_hash_len(xs.tag, h, static_cast<std::uint32_t>(xl + 1));
        if (!d_register_local_list(st, out, off, xl + 1)) { err = ErrCode::Value; return false; }
        return true;
      }
    }
    if constexpr (d_exact_payload_flavor<Flavor>) {
        if (d_has_exact_payload(tables, payload_state, xs)) { payload_state.physical_capacity_exceeded = true; payload_state.physical_capacity_reason |= DPAYLOAD_OVERFLOW_VALUES; err = ErrCode::Value; return false; }
      }
    const std::uint32_t len = Value::saturating_len_add(Value::container_len(xs), 1U);
    const std::uint64_t h = d_prepend_list_hash48(d_list_type_code(xs), elem, xs);
    out = d_make_list_hash_len(xs.tag, h, len);
    return true;
  }

  if (bid == BuiltinId::Reverse) {
    if (argc != 1) {
      err = ErrCode::Type;
      return false;
    }
    const Value& x = args[0];
    if (x.tag == ValueTag::String) {
      if constexpr (PayloadTraits::kHasString) {
        auto& st = payload_state;
        const char* xp = nullptr;
        int xl = 0;
        if (d_lookup_string_payload(tables, st, x, xp, xl) && st.string_bytes_used + xl <= st.kMaxStringBytes) {
          const int off = st.string_bytes_used;
          for (int i = 0; i < xl; ++i) {
            st.string_bytes[off + i] = xp[xl - 1 - i];
          }
          st.string_bytes_used += xl;
          const std::uint64_t h = d_hash_bytes(st.string_bytes + off, xl);
          out = Value::from_string_hash_len(h, static_cast<std::uint32_t>(xl));
          if (!d_register_local_string(st, out, off, xl)) { err = ErrCode::Value; return false; }
          return true;
        }
      }
      if constexpr (d_exact_payload_flavor<Flavor>) {
        if (d_has_exact_payload(tables, payload_state, x)) { payload_state.physical_capacity_exceeded = true; payload_state.physical_capacity_reason |= DPAYLOAD_OVERFLOW_BYTES; err = ErrCode::Value; return false; }
      }
      out = Value::from_fallback_token(Value::pack_container_payload(Value::reverse_container_hash48(7U, x),
                                                                     Value::container_len(x)));
      return true;
    }
    if (!d_is_typed_list_tag(x.tag)) {
      err = ErrCode::Type;
      return false;
    }
    if constexpr (PayloadTraits::kHasList) {
      auto& st = payload_state;
      const Value* xp = nullptr;
      int xl = 0;
      if (d_lookup_list_payload(tables, st, x, xp, xl) && st.list_values_used + xl <= st.kMaxListValues) {
        const int off = st.list_values_used;
        for (int i = 0; i < xl; ++i) {
          st.list_values[off + i] = xp[xl - 1 - i];
        }
        st.list_values_used += xl;
        const std::uint64_t h = d_hash_list_payload(st.list_values + off, xl);
        out = d_make_list_hash_len(x.tag, h, static_cast<std::uint32_t>(xl));
        if (!d_register_local_list(st, out, off, xl)) { err = ErrCode::Value; return false; }
        return true;
      }
    }
    if constexpr (d_exact_payload_flavor<Flavor>) {
        if (d_has_exact_payload(tables, payload_state, x)) { payload_state.physical_capacity_exceeded = true; payload_state.physical_capacity_reason |= DPAYLOAD_OVERFLOW_VALUES; err = ErrCode::Value; return false; }
      }
    out = d_make_list_hash_len(x.tag, Value::reverse_container_hash48(d_list_type_code(x), x), Value::container_len(x));
    return true;
  }

  if (bid == BuiltinId::Find || bid == BuiltinId::Contains) {
    if (argc != 2) {
      err = ErrCode::Type;
      return false;
    }
    const Value& haystack = args[0];
    const Value& needle = args[1];
    if (haystack.tag != ValueTag::String || needle.tag != ValueTag::String) {
      err = ErrCode::Type;
      return false;
    }
    if constexpr (PayloadTraits::kHasString) {
      auto& st = payload_state;
      const char* hp = nullptr;
      const char* np = nullptr;
      int hl = 0;
      int nl = 0;
      if (d_lookup_string_payload(tables, st, haystack, hp, hl) &&
          d_lookup_string_payload(tables, st, needle, np, nl)) {
        int found = -1;
        if (nl == 0) {
          found = 0;
        } else if (nl <= hl) {
          for (int i = 0; i <= hl - nl; ++i) {
            bool match = true;
            for (int j = 0; j < nl; ++j) {
              if (hp[i + j] != np[j]) {
                match = false;
                break;
              }
            }
            if (match) {
              found = i;
              break;
            }
          }
        }
        out = (bid == BuiltinId::Find) ? Value::from_int(found) : Value::from_bool(found >= 0);
        return true;
      }
    }
    err = ErrCode::Value;
    return false;
  }

  if (bid == BuiltinId::IsInt) {
    if (argc != 1) {
      err = ErrCode::Type;
      return false;
    }
    out = Value::from_bool(args[0].tag == ValueTag::Int);
    return true;
  }

  if (bid == BuiltinId::CharToString) {
    if (argc != 1 || args[0].tag != ValueTag::Char || args[0].i < 0 || args[0].i > 255) {
      err = (argc == 1 && args[0].tag == ValueTag::Char) ? ErrCode::Value : ErrCode::Type;
      return false;
    }
    if constexpr (PayloadTraits::kHasString) {
      auto& st = payload_state;
      if (st.string_bytes_used + 1 <= st.kMaxStringBytes) {
        const int off = st.string_bytes_used;
        st.string_bytes[off] = static_cast<char>(args[0].i);
        st.string_bytes_used += 1;
        out = Value::from_string_hash_len(d_hash_bytes(st.string_bytes + off, 1), 1U);
        if (!d_register_local_string(st, out, off, 1)) { err = ErrCode::Value; return false; }
        return true;
      }
    }
    if constexpr (d_exact_payload_flavor<Flavor>) { payload_state.physical_capacity_exceeded = true; payload_state.physical_capacity_reason |= DPAYLOAD_OVERFLOW_BYTES; err = ErrCode::Value; return false; }
    const char c = static_cast<char>(args[0].i);
    out = Value::from_string_hash_len(d_hash_bytes(&c, 1), 1U);
    return true;
  }

  if (bid == BuiltinId::StringToChar) {
    if (argc != 1 || args[0].tag != ValueTag::String) {
      err = ErrCode::Type;
      return false;
    }
    if (Value::container_len(args[0]) != 1U) {
      err = ErrCode::Value;
      return false;
    }
    if constexpr (PayloadTraits::kHasString) {
      auto& st = payload_state;
      const char* sp = nullptr;
      int sl = 0;
      if (d_lookup_string_payload(tables, st, args[0], sp, sl) && sl == 1) {
        out = Value::from_char(static_cast<unsigned char>(sp[0]));
        return true;
      }
    }
    err = ErrCode::Value;
    return false;
  }

  if (bid == BuiltinId::Ord) {
    if (argc != 1 || args[0].tag != ValueTag::Char) {
      err = ErrCode::Type;
      return false;
    }
    out = Value::from_int(args[0].i);
    return true;
  }

  if (bid == BuiltinId::Chr) {
    if (argc != 1 || args[0].tag != ValueTag::Int) {
      err = ErrCode::Type;
      return false;
    }
    if (args[0].i < 0 || args[0].i > 255) {
      err = ErrCode::Value;
      return false;
    }
    out = Value::from_char(args[0].i);
    return true;
  }

  if (bid == BuiltinId::IsLetter || bid == BuiltinId::IsDigit || bid == BuiltinId::IsSpace ||
      bid == BuiltinId::IsVowel || bid == BuiltinId::ToLower || bid == BuiltinId::ToUpper) {
    if (argc != 1 || (args[0].tag != ValueTag::Char &&
        !(bid == BuiltinId::IsLetter && args[0].tag == ValueTag::Int))) {
      err = ErrCode::Type;
      return false;
    }
    const long long c = args[0].i;
    if (bid == BuiltinId::IsLetter) out = Value::from_bool(d_is_ascii_letter(c));
    else if (bid == BuiltinId::IsDigit) out = Value::from_bool(d_is_ascii_digit(c));
    else if (bid == BuiltinId::IsSpace) out = Value::from_bool(d_is_ascii_space(c));
    else if (bid == BuiltinId::IsVowel) out = Value::from_bool(d_is_ascii_vowel(c));
    else if (bid == BuiltinId::ToLower) out = Value::from_char(d_ascii_to_lower(c));
    else out = Value::from_char(d_ascii_to_upper(c));
    return true;
  }

  if (bid == BuiltinId::ToString) {
    if (argc != 1 || (args[0].tag != ValueTag::Int && args[0].tag != ValueTag::Float)) {
      err = ErrCode::Type;
      return false;
    }
    char tmp[64];
    const int len = args[0].tag == ValueTag::Int
                        ? d_append_int_decimal(tmp, args[0].i)
                        : d_append_float_decimal(tmp, args[0].f);
    if constexpr (PayloadTraits::kHasString) {
      auto& st = payload_state;
      if (st.string_bytes_used + len <= st.kMaxStringBytes) {
        const int off = st.string_bytes_used;
        for (int i = 0; i < len; ++i) {
          st.string_bytes[off + i] = tmp[i];
        }
        st.string_bytes_used += len;
        out = Value::from_string_hash_len(d_hash_bytes(st.string_bytes + off, len),
                                          static_cast<std::uint32_t>(len));
        if (!d_register_local_string(st, out, off, len)) { err = ErrCode::Value; return false; }
        return true;
      }
    }
    if constexpr (d_exact_payload_flavor<Flavor>) { payload_state.physical_capacity_exceeded = true; payload_state.physical_capacity_reason |= DPAYLOAD_OVERFLOW_BYTES; err = ErrCode::Value; return false; }
    out = Value::from_string_hash_len(d_hash_bytes(tmp, len), static_cast<std::uint32_t>(len));
    return true;
  }

  if (bid == BuiltinId::Singleton) {
    if (argc != 1) {
      err = ErrCode::Type;
      return false;
    }
    const Value& x = args[0];
    if (x.tag == ValueTag::Char) {
      if (x.i < 0 || x.i > 255) {
        err = ErrCode::Value;
        return false;
      }
      if constexpr (PayloadTraits::kHasString) {
        auto& st = payload_state;
        if (st.string_bytes_used + 1 <= st.kMaxStringBytes) {
          const int off = st.string_bytes_used;
          st.string_bytes[off] = static_cast<char>(x.i);
          st.string_bytes_used += 1;
          out = Value::from_string_hash_len(d_hash_bytes(st.string_bytes + off, 1), 1U);
          if (!d_register_local_string(st, out, off, 1)) { err = ErrCode::Value; return false; }
          return true;
        }
      }
      if constexpr (d_exact_payload_flavor<Flavor>) { payload_state.physical_capacity_exceeded = true; payload_state.physical_capacity_reason |= DPAYLOAD_OVERFLOW_BYTES; err = ErrCode::Value; return false; }
      const char c = static_cast<char>(x.i);
      out = Value::from_string_hash_len(d_hash_bytes(&c, 1), 1U);
      return true;
    }
    if (x.tag == ValueTag::Int || x.tag == ValueTag::Float || x.tag == ValueTag::String) {
      if (!d_charge_list_allocation(payload_state, 1)) { err = ErrCode::Value; return false; }
      const ValueTag list_tag = (x.tag == ValueTag::Int)
                                    ? ValueTag::IntList
                                    : ((x.tag == ValueTag::Float) ? ValueTag::FloatList : ValueTag::StringList);
      if constexpr (PayloadTraits::kHasList) {
        auto& st = payload_state;
        if (st.list_values_used + 1 <= st.kMaxListValues) {
          const int off = st.list_values_used;
          st.list_values[off] = x;
          st.list_values_used += 1;
          out = d_make_list_hash_len(list_tag, d_hash_list_payload(st.list_values + off, 1), 1U);
          if (!d_register_local_list(st, out, off, 1)) { err = ErrCode::Value; return false; }
          return true;
        }
      }
      if constexpr (d_exact_payload_flavor<Flavor>) { payload_state.physical_capacity_exceeded = true; payload_state.physical_capacity_reason |= DPAYLOAD_OVERFLOW_VALUES; err = ErrCode::Value; return false; }
      std::uint64_t h = Value::fnv1a_init();
      h = Value::fnv1a_mix_u8(h, 0xA1U);
      h = Value::fnv1a_mix_u64(h, 1U);
      h = Value::fnv1a_mix_u64(h, d_hash_value_shallow(x));
      out = d_make_list_hash_len(list_tag, h, 1U);
      return true;
    }
    err = ErrCode::Type;
    return false;
  }

  err = ErrCode::Name;
  return false;
}

}  // namespace gagp::gpu_detail
