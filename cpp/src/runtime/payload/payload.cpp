#include "gagp/runtime/payload/payload.hpp"
#include "staging.hpp"
#include <optional>
#include <cstring>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>
#include <utility>

namespace gagp::payload {

namespace {

struct PayloadKey {
  ValueTag tag = ValueTag::Invalid;
  std::int64_t packed = 0;

  bool operator==(const PayloadKey& other) const {
    return tag == other.tag && packed == other.packed;
  }
};

struct PayloadKeyHash {
  std::size_t operator()(const PayloadKey& k) const {
    const std::uint64_t a = static_cast<std::uint64_t>(k.packed);
    return static_cast<std::size_t>((a * 11400714819323198485ULL) ^
                                    static_cast<std::uint64_t>(k.tag));
  }
};

std::mutex g_mu;
std::unordered_map<PayloadKey, std::string, PayloadKeyHash> g_strings;
std::unordered_map<PayloadKey, std::vector<Value>, PayloadKeyHash> g_lists;

PayloadKey key_of(const Value& v) {
  return PayloadKey{v.tag, v.i};
}

std::uint64_t hash_bytes(const unsigned char* p, std::size_t n) {
  std::uint64_t h = Value::fnv1a_init();
  for (std::size_t i = 0; i < n; ++i) {
    h = Value::fnv1a_mix_u8(h, p[i]);
  }
  return h;
}

std::uint64_t hash_value_shallow(const Value& v) {
  std::uint64_t h = Value::fnv1a_init();
  h = Value::fnv1a_mix_u8(h, static_cast<std::uint8_t>(v.tag));
  if (v.tag == ValueTag::Invalid) return h;
  if (v.tag == ValueTag::Bool) return Value::fnv1a_mix_u8(h, v.b ? 1U : 0U);
  if (v.tag == ValueTag::Int || v.tag == ValueTag::Char || v.tag == ValueTag::String ||
      v.tag == ValueTag::IntList || v.tag == ValueTag::FloatList ||
      v.tag == ValueTag::StringList || v.tag == ValueTag::FallbackToken) {
    return Value::fnv1a_mix_u64(h, static_cast<std::uint64_t>(v.i));
  }
  union {
    double d;
    std::uint64_t u;
  } bits{};
  bits.d = v.f;
  return Value::fnv1a_mix_u64(h, bits.u);
}

std::uint64_t hash_list_payload(const std::vector<Value>& elems) {
  std::uint64_t h = Value::fnv1a_init();
  h = Value::fnv1a_mix_u8(h, 0xA1U);  // list domain-separator
  h = Value::fnv1a_mix_u64(h, static_cast<std::uint64_t>(elems.size()));
  for (const Value& e : elems) {
    h = Value::fnv1a_mix_u64(h, hash_value_shallow(e));
  }
  return h;
}

bool validate_int_list_elems(const std::vector<Value>& elems) {
  for (const Value& elem : elems) {
    if (elem.tag != ValueTag::Int) {
      return false;
    }
  }
  return true;
}

bool validate_float_list_elems(const std::vector<Value>& elems) {
  for (const Value& elem : elems) {
    if (elem.tag != ValueTag::Float) {
      return false;
    }
  }
  return true;
}

bool validate_string_list_elems(const std::vector<Value>& elems) {
  for (const Value& elem : elems) {
    if (elem.tag != ValueTag::String) {
      return false;
    }
  }
  return true;
}

void mark_live_payload_locked(const Value& value,
                              std::unordered_set<PayloadKey, PayloadKeyHash>* live_keys) {
  if (live_keys == nullptr) {
    return;
  }
  if (value.tag == ValueTag::String) {
    live_keys->insert(key_of(value));
    return;
  }
  if (value.tag != ValueTag::IntList && value.tag != ValueTag::FloatList &&
      value.tag != ValueTag::StringList) {
    return;
  }

  const PayloadKey key = key_of(value);
  auto [it, inserted] = live_keys->insert(key);
  (void)it;
  if (!inserted) {
    return;
  }

  auto list_it = g_lists.find(key);
  if (list_it == g_lists.end()) {
    return;
  }
  for (const Value& elem : list_it->second) {
    mark_live_payload_locked(elem, live_keys);
  }
}

}  // namespace

struct StagedPayloads::State {
  std::unordered_map<PayloadKey, std::string, PayloadKeyHash> strings;
  std::unordered_map<PayloadKey, std::vector<Value>, PayloadKeyHash> lists;
  std::unordered_map<PayloadKey, std::optional<std::string>, PayloadKeyHash> string_reads;
  std::unordered_map<PayloadKey, std::optional<std::vector<Value>>, PayloadKeyHash> list_reads;
  bool active = false, sealed = false, conflict = false, committed = false;
};

namespace {
thread_local StagedPayloads::State* staged = nullptr;

bool identical(const std::string& a, const std::string& b) { return a == b; }
bool identical(const std::vector<Value>& a, const std::vector<Value>& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i].tag != b[i].tag) return false;
    if (a[i].tag == ValueTag::Float) {
      if (std::memcmp(&a[i].f, &b[i].f, sizeof(double))) return false;
    } else if (a[i].tag == ValueTag::Bool) {
      if (a[i].b != b[i].b) return false;
    } else if (a[i].tag != ValueTag::Invalid && a[i].i != b[i].i) return false;
  }
  return true;
}

template<class Map, class Reads, class Data>
bool staged_lookup(const PayloadKey& key, Data* out, Map& writes, Reads& reads, const Map& global) {
  const auto own = writes.find(key);
  if (own != writes.end()) { *out = own->second; return true; }
  const auto captured = reads.find(key);
  if (captured != reads.end()) {
    if (!captured->second) return false;
    *out = *captured->second;
    return true;
  }
  // Capture each committed dependency once; commit validates that it stayed live
  // and unchanged. Repeated worker reads then need no global registry lock.
  std::lock_guard<std::mutex> lock(g_mu);
  const auto found = global.find(key);
  std::optional<Data> observed;
  if (found != global.end()) observed = found->second;
  reads.emplace(key, observed);
  if (!observed) return false;
  *out = std::move(*observed);
  return true;
}

template<class Map, class Reads>
bool merge_staged(const Map& writes, const Reads& reads, const Map& global, Map& merged) {
  for (const auto& item : reads) {
    const auto current = global.find(item.first);
    if (!item.second) {
      if (current != global.end() || merged.count(item.first)) return false;
    } else if (current == global.end() || !identical(*item.second, current->second)) return false;
  }
  for (const auto& item : writes) {
    const auto current = global.find(item.first);
    if (current != global.end() && !identical(item.second, current->second)) return false;
    const auto combined = merged.emplace(item.first, item.second);
    if (!combined.second && !identical(item.second, combined.first->second)) return false;
  }
  return true;
}
}  // namespace

StagedPayloads::StagedPayloads() : state_(std::make_unique<State>()) {}
StagedPayloads::~StagedPayloads() = default;
bool StagedPayloads::has_active_scope() { return staged != nullptr; }
StagedPayloads::Scope::Scope(StagedPayloads& transaction) : state_(transaction.state_.get()) {
  if (staged || state_->active || state_->sealed)
    throw std::logic_error("payload generation transaction cannot be nested or reused");
  state_->active = true;
  staged = state_;
}
StagedPayloads::Scope::~Scope() {
  staged = nullptr;
  state_->active = false;
  state_->sealed = true;
}
bool StagedPayloads::read_snapshot_unchanged() const {
  const auto& state = *state_;
  if (staged || state.active || !state.sealed || state.conflict ||
      !state.strings.empty() || !state.lists.empty()) return false;
  std::lock_guard<std::mutex> lock(g_mu);
  decltype(g_strings) no_strings;
  decltype(g_lists) no_lists;
  return merge_staged(state.strings, state.string_reads, g_strings, no_strings) &&
      merge_staged(state.lists, state.list_reads, g_lists, no_lists);
}

bool StagedPayloads::commit_all(const std::vector<StagedPayloads*>& transactions) {
  if (staged) throw std::logic_error("payload transactions commit only outside worker scopes");
  std::lock_guard<std::mutex> lock(g_mu);
  decltype(g_strings) strings;
  decltype(g_lists) lists;
  for (const auto* transaction : transactions) {
    if (!transaction || transaction->state_->active || !transaction->state_->sealed ||
        transaction->state_->committed) throw std::logic_error("invalid payload transaction commit");
    const auto& state = *transaction->state_;
    if (state.conflict || !merge_staged(state.strings, state.string_reads, g_strings, strings) ||
        !merge_staged(state.lists, state.list_reads, g_lists, lists)) return false;
  }
  // Also reject a read that missed a value another (later) worker staged. This
  // conservative rule keeps speculative failures from depending on job order.
  for (const auto* transaction : transactions) {
    for (const auto& item : transaction->state_->string_reads)
      if (!item.second && strings.count(item.first)) return false;
    for (const auto& item : transaction->state_->list_reads)
      if (!item.second && lists.count(item.first)) return false;
  }
  // Allocate before changing contents; node transfer then needs no value copies.
  if (!strings.empty()) g_strings.reserve(g_strings.size() + strings.size());
  if (!lists.empty()) g_lists.reserve(g_lists.size() + lists.size());
  g_strings.merge(strings);
  g_lists.merge(lists);
  for (auto* transaction : transactions) transaction->state_->committed = true;
  return true;
}

void clear() {
  if (staged) throw std::logic_error("cannot clear registry during staged generation");
  std::lock_guard<std::mutex> lock(g_mu);
  g_strings.clear();
  g_lists.clear();
}

void retain_only(const std::vector<Value>& roots) {
  if (staged) throw std::logic_error("cannot prune registry during staged generation");
  std::lock_guard<std::mutex> lock(g_mu);
  std::unordered_set<PayloadKey, PayloadKeyHash> live_keys;
  live_keys.reserve(roots.size() * 2U + 8U);
  for (const Value& root : roots) {
    mark_live_payload_locked(root, &live_keys);
  }

  for (auto it = g_strings.begin(); it != g_strings.end();) {
    if (live_keys.find(it->first) == live_keys.end()) {
      it = g_strings.erase(it);
    } else {
      ++it;
    }
  }
  for (auto it = g_lists.begin(); it != g_lists.end();) {
    if (live_keys.find(it->first) == live_keys.end()) {
      it = g_lists.erase(it);
    } else {
      ++it;
    }
  }
}

PayloadStats stats() {
  std::lock_guard<std::mutex> lock(g_mu);
  PayloadStats out;
  out.string_entries = g_strings.size();
  out.list_entries = g_lists.size();
  for (const auto& kv : g_strings) {
    out.string_bytes += kv.second.size();
  }
  for (const auto& kv : g_lists) {
    out.list_value_count += kv.second.size();
  }
  return out;
}

void register_string(const Value& key, const std::string& s) {
  if (key.tag != ValueTag::String) return;
  if (staged) {
    const auto previous = staged->strings.find(key_of(key));
    if (previous != staged->strings.end() && !identical(previous->second, s)) staged->conflict = true;
    staged->strings[key_of(key)] = s;
    return;
  }
  std::lock_guard<std::mutex> lock(g_mu);
  g_strings[key_of(key)] = s;
}

void register_list(const Value& key, const std::vector<Value>& elems) {
  if (key.tag != ValueTag::IntList && key.tag != ValueTag::FloatList &&
      key.tag != ValueTag::StringList) return;
  if (staged) {
    const auto previous = staged->lists.find(key_of(key));
    if (previous != staged->lists.end() && !identical(previous->second, elems)) staged->conflict = true;
    staged->lists[key_of(key)] = elems;
    return;
  }
  std::lock_guard<std::mutex> lock(g_mu);
  g_lists[key_of(key)] = elems;
}

bool lookup_string(const Value& key, std::string* out) {
  if (key.tag != ValueTag::String || out == nullptr) return false;
  if (staged) return staged_lookup(key_of(key), out, staged->strings, staged->string_reads, g_strings);
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_strings.find(key_of(key));
  if (it == g_strings.end()) return false;
  *out = it->second;
  return true;
}

bool lookup_list(const Value& key, std::vector<Value>* out) {
  if ((key.tag != ValueTag::IntList && key.tag != ValueTag::FloatList &&
       key.tag != ValueTag::StringList) ||
      out == nullptr) return false;
  if (staged) return staged_lookup(key_of(key), out, staged->lists, staged->list_reads, g_lists);
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_lists.find(key_of(key));
  if (it == g_lists.end()) return false;
  *out = it->second;
  return true;
}

bool lookup_index(const Value& key, std::size_t index, Value* out) {
  if (out == nullptr) return false;
  const bool is_list = key.tag == ValueTag::IntList || key.tag == ValueTag::FloatList ||
                       key.tag == ValueTag::StringList;
  if (key.tag != ValueTag::String && !is_list) return false;
  if (staged) {
    if (key.tag == ValueTag::String) {
      std::string value;
      if (!lookup_string(key, &value) || index >= value.size()) return false;
      *out = Value::from_char(static_cast<unsigned char>(value[index]));
    } else {
      std::vector<Value> values;
      if (!lookup_list(key, &values) || index >= values.size()) return false;
      *out = values[index];
    }
    return true;
  }


  std::lock_guard<std::mutex> lock(g_mu);
  if (key.tag == ValueTag::String) {
    const auto it = g_strings.find(key_of(key));
    if (it == g_strings.end() || index >= it->second.size()) return false;
    *out = Value::from_char(static_cast<unsigned char>(it->second[index]));
    return true;
  }

  const auto it = g_lists.find(key_of(key));
  if (it == g_lists.end() || index >= it->second.size()) return false;
  *out = it->second[index];
  return true;
}

bool lookup_string_packed(std::int64_t packed, std::string* out) {
  if (out == nullptr) return false;
  if (staged) return staged_lookup(PayloadKey{ValueTag::String, packed}, out, staged->strings, staged->string_reads, g_strings);
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_strings.find(PayloadKey{ValueTag::String, packed});
  if (it == g_strings.end()) return false;
  *out = it->second;
  return true;
}

bool lookup_list_packed(ValueTag tag, std::int64_t packed, std::vector<Value>* out) {
  if (out == nullptr) return false;
  if (staged) return staged_lookup(PayloadKey{tag, packed}, out, staged->lists, staged->list_reads, g_lists);
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_lists.find(PayloadKey{tag, packed});
  if (it == g_lists.end()) return false;
  *out = it->second;
  return true;
}

Value make_string_value(const std::string& s) {
  const std::uint64_t h = hash_bytes(reinterpret_cast<const unsigned char*>(s.data()), s.size());
  const std::uint32_t len =
      static_cast<std::uint32_t>(s.size() > static_cast<std::size_t>(Value::k_container_len_max)
                                     ? Value::k_container_len_max
                                     : s.size());
  const Value out = Value::from_string_hash_len(h, len);
  register_string(out, s);
  return out;
}

Value make_int_list_value(const std::vector<Value>& elems) {
  if (!validate_int_list_elems(elems)) {
    throw std::runtime_error("IntList payload requires int elements");
  }
  const std::uint64_t h = hash_list_payload(elems);
  const std::uint32_t len =
      static_cast<std::uint32_t>(elems.size() > static_cast<std::size_t>(Value::k_container_len_max)
                                     ? Value::k_container_len_max
                                     : elems.size());
  const Value out = Value::from_int_list_hash_len(h, len);
  register_list(out, elems);
  return out;
}

Value make_float_list_value(const std::vector<Value>& elems) {
  if (!validate_float_list_elems(elems)) {
    throw std::runtime_error("FloatList payload requires float elements");
  }
  const std::uint64_t h = hash_list_payload(elems);
  const std::uint32_t len =
      static_cast<std::uint32_t>(elems.size() > static_cast<std::size_t>(Value::k_container_len_max)
                                     ? Value::k_container_len_max
                                     : elems.size());
  const Value out = Value::from_float_list_hash_len(h, len);
  register_list(out, elems);
  return out;
}

Value make_string_list_value(const std::vector<Value>& elems) {
  if (!validate_string_list_elems(elems)) {
    throw std::runtime_error("StringList payload requires string elements");
  }
  const std::uint64_t h = hash_list_payload(elems);
  const std::uint32_t len =
      static_cast<std::uint32_t>(elems.size() > static_cast<std::size_t>(Value::k_container_len_max)
                                     ? Value::k_container_len_max
                                     : elems.size());
  const Value out = Value::from_string_list_hash_len(h, len);
  register_list(out, elems);
  return out;
}

std::vector<StringSnapshot> snapshot_strings() {
  std::lock_guard<std::mutex> lock(g_mu);
  std::vector<StringSnapshot> out;
  out.reserve(g_strings.size());
  for (const auto& kv : g_strings) {
    StringSnapshot s;
    s.key.tag = kv.first.tag;
    s.key.i = kv.first.packed;
    s.data = kv.second;
    out.push_back(std::move(s));
  }
  return out;
}

std::vector<ListSnapshot> snapshot_lists() {
  std::lock_guard<std::mutex> lock(g_mu);
  std::vector<ListSnapshot> out;
  out.reserve(g_lists.size());
  for (const auto& kv : g_lists) {
    ListSnapshot s;
    s.key.tag = kv.first.tag;
    s.key.i = kv.first.packed;
    s.elems = kv.second;
    out.push_back(std::move(s));
  }
  return out;
}

}  // namespace gagp::payload
