#include "list_views.hpp"
#include "gagp/runtime/cpu/builtins_cpu.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#include "gagp/core/value_semantics.hpp"
#include "gagp/runtime/payload/payload.hpp"
#include "../payload/staging.hpp"

namespace gagp {
namespace detail { thread_local const CpuListViews* active_cpu_list_views = nullptr; }

namespace {

bool to_numeric_pair(const Value& a, const Value& b, double& a_out, double& b_out,
                     bool& any_float) {
  if (!is_numeric(a) || !is_numeric(b)) {
    return false;
  }
  if (a.tag != b.tag) {
    return false;
  }
  any_float = (a.tag == ValueTag::Float) || (b.tag == ValueTag::Float);
  a_out = (a.tag == ValueTag::Float) ? a.f : static_cast<double>(a.i);
  b_out = (b.tag == ValueTag::Float) ? b.f : static_cast<double>(b.i);
  return true;
}

BuiltinResult fail(ErrCode code, const std::string& message) {
  BuiltinResult out;
  out.is_error = true;
  out.err = Err{code, message};
  return out;
}

long long normalize_slice_idx(long long idx, long long n) {
  long long out = idx;
  if (out < 0) out += n;
  if (out < 0) out = 0;
  if (out > n) out = n;
  return out;
}

bool normalize_index_idx(long long idx, long long n, long long& out) {
  long long j = idx;
  if (j < 0) j += n;
  if (j < 0 || j >= n) {
    return false;
  }
  out = j;
  return true;
}

bool is_list_tag(ValueTag tag) {
  return tag == ValueTag::IntList || tag == ValueTag::FloatList || tag == ValueTag::StringList;
}

std::string format_float_current(double value) {
  if (std::isnan(value)) return "nan";
  if (std::isinf(value)) return value < 0.0 ? "-inf" : "inf";
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(6) << value;
  std::string out = oss.str();
  while (!out.empty() && out.back() == '0') {
    out.pop_back();
  }
  if (!out.empty() && out.back() == '.') {
    out.pop_back();
  }
  if (out == "-0") return "0";
  return out;
}

std::uint8_t list_type_code(const Value& v) {
  if (v.tag == ValueTag::IntList) return 2U;
  if (v.tag == ValueTag::FloatList) return 3U;
  return 4U;
}

Value make_list_hash_len(const Value& src, std::uint64_t h, std::uint32_t len) {
  if (src.tag == ValueTag::IntList) return Value::from_int_list_hash_len(h, len);
  if (src.tag == ValueTag::FloatList) return Value::from_float_list_hash_len(h, len);
  return Value::from_string_list_hash_len(h, len);
}

std::uint64_t prepend_list_hash48(std::uint8_t type_code, const Value& elem, const Value& src) {
  std::uint64_t h = Value::fnv1a_init();
  h = Value::fnv1a_mix_u8(h, type_code);
  h = Value::fnv1a_mix_u8(h, 0x70U);
  h = Value::fnv1a_mix_u64(h, Value::shallow_hash64(elem));
  h = Value::fnv1a_mix_u64(h, Value::container_hash48(src));
  h = Value::fnv1a_mix_u64(h, static_cast<std::uint64_t>(Value::container_len(src)));
  return (h & Value::k_container_hash_mask);
}

BuiltinResult make_exact_list(ValueTag tag, std::vector<Value>&& elems) {
  return BuiltinResult{false, payload::make_owned_list_value(tag, std::move(elems)), Err{ErrCode::Value, ""}};
}

bool is_ascii_letter(long long c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool is_ascii_digit(long long c) {
  return c >= '0' && c <= '9';
}

bool is_ascii_space(long long c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

bool is_ascii_vowel(long long c) {
  if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
  return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
}

long long ascii_to_lower(long long c) {
  return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

long long ascii_to_upper(long long c) {
  return (c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c;
}

bool list_accepts_element(ValueTag list, const Value& element) {
  if (list == ValueTag::IntList) return element.tag == ValueTag::Int;
  if (list == ValueTag::FloatList) return element.tag == ValueTag::Float;
  return list == ValueTag::StringList && element.tag == ValueTag::String;
}

// Same equality as the VM's `eq` on two values of one tag: integers by value,
// floats by IEEE comparison, strings by token.
bool list_elements_equal(const Value& a, const Value& b) {
  return a.tag == ValueTag::Float ? a.f == b.f : a.i == b.i;
}

// Python slicing-free insert position: negative indices count from the end, then
// the position clamps to [0, n].
long long insert_position(long long index, long long n) {
  long long j = index;
  if (j < 0) j += n;
  if (j < 0) j = 0;
  if (j > n) j = n;
  return j;
}

std::uint8_t derived_op(BuiltinId id) { return static_cast<std::uint8_t>(id); }

bool is_strip_family(BuiltinId id) {
  return id == BuiltinId::Strip || id == BuiltinId::LStrip || id == BuiltinId::RStrip ||
         id == BuiltinId::StripChars || id == BuiltinId::LStripChars || id == BuiltinId::RStripChars;
}

BuiltinResult exact_string(const std::string& text) {
  return BuiltinResult{false, payload::make_string_value(text), Err{ErrCode::Value, ""}};
}

BuiltinResult derived_string_token(BuiltinId id, const Value& source, std::int64_t operand) {
  const std::uint64_t h = Value::derived_container_hash48(1U, derived_op(id), source, operand, 0U);
  BuiltinResult out;
  out.value = Value::from_fallback_token(Value::pack_container_payload(h, Value::container_len(source)));
  return out;
}

}  // namespace

BuiltinResult builtin_call(BuiltinId id, const Value* args, std::size_t argc) {
  if (id == BuiltinId::Abs) {
    if (argc != 1) {
      return fail(ErrCode::Type, "abs expects 1 argument");
    }
    const Value& x = args[0];
    if (!is_numeric(x)) {
      return fail(ErrCode::Type, "abs expects a numeric argument");
    }
    BuiltinResult out;
    out.value = (x.tag == ValueTag::Float)
                    ? Value::from_float(vm_semantics::canonicalize_vm_float(x.f < 0 ? -x.f : x.f))
                                            : Value::from_int(x.i < 0 ? vm_semantics::wrap_int_neg(x.i) : x.i);
    return out;
  }

  if (id == BuiltinId::Min || id == BuiltinId::Max) {
    if (argc != 2) {
      return fail(ErrCode::Type, std::string(builtin_name(id)) + " expects 2 arguments");
    }
    double a = 0.0;
    double b = 0.0;
    bool any_float = false;
    if (!to_numeric_pair(args[0], args[1], a, b, any_float)) {
      return fail(ErrCode::Type, std::string(builtin_name(id)) + " expects numeric arguments");
    }
    const double pick = (id == BuiltinId::Min) ? (a <= b ? a : b) : (a >= b ? a : b);
    BuiltinResult out;
    out.value = any_float ? Value::from_float(vm_semantics::canonicalize_vm_float(pick))
                          : Value::from_int(id == BuiltinId::Min
                              ? std::min(args[0].i, args[1].i)
                              : std::max(args[0].i, args[1].i));
    return out;
  }

  if (id == BuiltinId::Clip) {
    if (argc != 3) {
      return fail(ErrCode::Type, "clip expects 3 arguments: clip(x, lo, hi)");
    }
    const Value& x = args[0];
    const Value& lo = args[1];
    const Value& hi = args[2];
    if (!is_numeric(x) || !is_numeric(lo) || !is_numeric(hi)) {
      return fail(ErrCode::Type, "clip expects numeric arguments");
    }
    if (x.tag != lo.tag || x.tag != hi.tag) {
      return fail(ErrCode::Type, "clip expects matching numeric argument types");
    }
    const bool any_float =
        (x.tag == ValueTag::Float) || (lo.tag == ValueTag::Float) || (hi.tag == ValueTag::Float);
    if (any_float) {
      const double x2 = (x.tag == ValueTag::Float) ? x.f : static_cast<double>(x.i);
      const double lo2 = (lo.tag == ValueTag::Float) ? lo.f : static_cast<double>(lo.i);
      const double hi2 = (hi.tag == ValueTag::Float) ? hi.f : static_cast<double>(hi.i);
      BuiltinResult out;
      const double picked = std::min(std::max(x2, lo2), hi2);
      out.value = Value::from_float(vm_semantics::canonicalize_vm_float(picked));
      return out;
    }

    const long long x2 = x.i;
    const long long lo2 = lo.i;
    const long long hi2 = hi.i;
    BuiltinResult out;
    out.value = Value::from_int(std::min(std::max(x2, lo2), hi2));
    return out;
  }

  if (id == BuiltinId::TruncDiv0 || id == BuiltinId::TruncMod0 || id == BuiltinId::IDiv0 || id == BuiltinId::IMod0) {
    if (argc != 2) {
      return fail(ErrCode::Type, std::string(builtin_name(id)) + " expects 2 arguments");
    }
    const Value& a = args[0];
    const Value& b = args[1];
    if (a.tag != ValueTag::Int || b.tag != ValueTag::Int) {
      return fail(ErrCode::Type, std::string(builtin_name(id)) + " expects integer arguments");
    }
    BuiltinResult out;
    if (b.i == 0) {
      out.value = Value::from_int(0);
    } else if (id == BuiltinId::TruncDiv0 || id == BuiltinId::TruncMod0) {
      out.value=Value::from_int(id==BuiltinId::TruncDiv0 ? vm_semantics::trunc_int_div0(a.i,b.i) : vm_semantics::trunc_int_mod0(a.i,b.i));
    } else if (id == BuiltinId::IDiv0) {
      out.value = Value::from_int(vm_semantics::wrap_int_div(a.i, b.i));
    } else {
      out.value = Value::from_int(vm_semantics::py_int_mod(a.i, b.i));
    }
    return out;
  }

  if (id == BuiltinId::Len) {
    if (argc != 1) {
      return fail(ErrCode::Type, "len expects 1 argument");
    }
    const Value& x = args[0];
    if (x.tag == ValueTag::IntList && x.b && detail::active_cpu_list_views)
      return detail::active_cpu_list_views->call(id, args, argc);
    if (!is_container(x)) {
      return fail(ErrCode::Type, "len expects string/typed-list argument");
    }
    BuiltinResult out;
    out.value = Value::from_int(static_cast<long long>(Value::container_len(x)));
    return out;
  }

  if (id == BuiltinId::Concat) {
    if (argc != 2) {
      return fail(ErrCode::Type, "concat expects 2 arguments");
    }
    const Value& a = args[0];
    const Value& b = args[1];
    if (a.tag == ValueTag::String && b.tag == ValueTag::String) {
      std::string sa;
      std::string sb;
      if (payload::lookup_string(a, &sa) && payload::lookup_string(b, &sb)) {
        if (payload::list_allocation_budget_active()) {
          if (sa.empty()) return BuiltinResult{false, b, Err{ErrCode::Value, ""}};
          if (sb.empty()) return BuiltinResult{false, a, Err{ErrCode::Value, ""}};
          payload::charge_list_allocation(sa.size() + sb.size());
        }
        payload::check_allocation_budget(sa.size() + sb.size());
        return BuiltinResult{false, payload::make_string_value(sa + sb), Err{ErrCode::Value, ""}};
      }
      const std::uint32_t len = Value::saturating_len_add(Value::container_len(a), Value::container_len(b));
      const std::uint64_t h = Value::combine_container_hash48(1U, a, b);
      BuiltinResult out;
      out.value = Value::from_fallback_token(Value::pack_container_payload(h, len));
      return out;
    }
    if (is_list_tag(a.tag) && a.tag == b.tag) {
      std::vector<Value> left_scratch, right_scratch;
      const auto* left = payload::read_list(a, left_scratch);
      const auto* right = left ? payload::read_list(b, right_scratch) : nullptr;
      if (left && right) {
        const auto& la = *left;
        const auto& lb = *right;
        if (payload::list_allocation_budget_active()) {
          if (la.empty()) return BuiltinResult{false, b, Err{ErrCode::Value, ""}};
          if (lb.empty()) return BuiltinResult{false, a, Err{ErrCode::Value, ""}};
          payload::charge_list_allocation(la.size() + lb.size());
        }
        std::vector<Value> out_elems;
        payload::check_allocation_budget((la.size() + lb.size()) * sizeof(Value));
        out_elems.reserve(la.size() + lb.size());
        out_elems.insert(out_elems.end(), la.begin(), la.end());
        out_elems.insert(out_elems.end(), lb.begin(), lb.end());
        return make_exact_list(a.tag, std::move(out_elems));
      }
      const std::uint32_t len = Value::saturating_len_add(Value::container_len(a), Value::container_len(b));
      const std::uint64_t h = Value::combine_container_hash48(list_type_code(a), a, b);
      BuiltinResult out;
      out.value = make_list_hash_len(a, h, len);
      return out;
    }
    return fail(ErrCode::Type, "concat expects matching string/typed-list arguments");
  }

  if (id == BuiltinId::Slice) {
    if (argc != 3) {
      return fail(ErrCode::Type, "slice expects 3 arguments: slice(x, lo, hi)");
    }
    const Value& x = args[0];
    if (x.tag == ValueTag::IntList && x.b && detail::active_cpu_list_views)
      return detail::active_cpu_list_views->call(id, args, argc);
    const Value& lo = args[1];
    const Value& hi = args[2];
    if (!is_container(x)) {
      return fail(ErrCode::Type, "slice expects string/typed-list as first argument");
    }
    if (lo.tag != ValueTag::Int || hi.tag != ValueTag::Int) {
      return fail(ErrCode::Type, "slice expects integer lo/hi");
    }
    const long long n = static_cast<long long>(Value::container_len(x));
    const long long l = normalize_slice_idx(lo.i, n);
    const long long h = normalize_slice_idx(hi.i, n);
    const long long out_len_ll = (h > l) ? (h - l) : 0LL;
    const std::uint32_t out_len =
        static_cast<std::uint32_t>(out_len_ll > static_cast<long long>(Value::k_container_len_max)
                                       ? Value::k_container_len_max
                                       : out_len_ll);
    if (x.tag == ValueTag::String) {
      std::string sx;
      if (payload::lookup_string(x, &sx)) {
        const std::size_t ls = static_cast<std::size_t>(l);
        const std::size_t hs = static_cast<std::size_t>(h);
        const std::size_t count = (hs > ls) ? (hs - ls) : 0U;
        return BuiltinResult{false, payload::make_string_value(sx.substr(ls, count)), Err{ErrCode::Value, ""}};
      }
      const std::uint64_t out_h = Value::slice_container_hash48(1U, x, lo.i, hi.i);
      BuiltinResult out;
      out.value = Value::from_fallback_token(Value::pack_container_payload(out_h, out_len));
      return out;
    }
    if (is_list_tag(x.tag)) {
      std::vector<Value> scratch;
      if (const auto* source = payload::read_list(x, scratch)) {
        const auto& lx = *source;
        const std::size_t ls = static_cast<std::size_t>(l);
        const std::size_t hs = static_cast<std::size_t>(h);
        if (hs > ls && hs > lx.size()) return fail(ErrCode::Value, "slice exceeds registered list payload");
        std::vector<Value> out_elems;
        out_elems.reserve(hs > ls ? (hs - ls) : 0U);
        for (std::size_t k = ls; k < hs; ++k) {
          out_elems.push_back(lx[k]);
        }
        return make_exact_list(x.tag, std::move(out_elems));
      }
      const std::uint64_t out_h = Value::slice_container_hash48(list_type_code(x), x, lo.i, hi.i);
      BuiltinResult out;
      out.value = make_list_hash_len(x, out_h, out_len);
      return out;
    }
    return fail(ErrCode::Type, "slice expects string/typed-list as first argument");
  }

  if (id == BuiltinId::Index || id == BuiltinId::IndexNonnegative || id == BuiltinId::IndexClampedOrZero) {
    if (argc != 2) {
      return fail(ErrCode::Type, "index expects 2 arguments: index(x, i)");
    }
    const Value& x = args[0];
    if (x.tag == ValueTag::IntList && x.b && detail::active_cpu_list_views)
      return detail::active_cpu_list_views->call(id, args, argc);
    const Value& i = args[1];
    if (!is_container(x) || (id == BuiltinId::IndexClampedOrZero && x.tag != ValueTag::IntList)) {
      return fail(ErrCode::Type, "index expects string/typed-list as first argument");
    }
    if (i.tag != ValueTag::Int) {
      return fail(ErrCode::Type, "index expects integer index");
    }
    const long long n = static_cast<long long>(Value::container_len(x));
    long long j = 0;
    if (id == BuiltinId::IndexClampedOrZero) {
      if (n == 0) return BuiltinResult{false, Value::from_int(0), Err{ErrCode::Value, ""}};
      j = i.i < 0 ? 0 : (i.i >= n ? n - 1 : i.i);
    } else if ((id == BuiltinId::IndexNonnegative && i.i < 0) || !normalize_index_idx(i.i, n, j)) {
      return fail(ErrCode::Value, "index out of range");
    }
    BuiltinResult out;
    if (payload::lookup_index(x, static_cast<std::size_t>(j), &out.value)) {
      if (id == BuiltinId::IndexClampedOrZero && out.value.tag != ValueTag::Int)
        return fail(ErrCode::Type, "index_clamped_or_zero requires integer elements");
      return out;
    }
    if (x.tag == ValueTag::String) {
      out.value = Value::from_fallback_token(Value::index_container_token64(5U, x, j));
      return out;
    }
    if (is_list_tag(x.tag)) {
      out.value = Value::from_fallback_token(Value::index_container_token64(list_type_code(x), x, j));
      return out;
    }
    return fail(ErrCode::Type, "index expects string/typed-list as first argument");
  }

  if (id == BuiltinId::Append) {
    if (argc != 2) {
      return fail(ErrCode::Type, "append expects 2 arguments");
    }
    const Value& xs = args[0];
    const Value& elem = args[1];
    if (!is_list_tag(xs.tag)) {
      return fail(ErrCode::Type, "append expects typed-list as first argument");
    }
    if (xs.tag == ValueTag::IntList && elem.tag != ValueTag::Int) {
      return fail(ErrCode::Type, "append expects int element for IntList");
    }
    if (xs.tag == ValueTag::FloatList && elem.tag != ValueTag::Float) {
      return fail(ErrCode::Type, "append expects float element for FloatList");
    }
    if (xs.tag == ValueTag::StringList && elem.tag != ValueTag::String) {
      return fail(ErrCode::Type, "append expects string element for StringList");
    }
    std::vector<Value> lx;
    if (payload::lookup_list(xs, &lx)) {
      payload::charge_list_allocation(lx.size() + 1);
      lx.push_back(elem);
      return make_exact_list(xs.tag, std::move(lx));
    }
    const std::uint32_t len = Value::saturating_len_add(Value::container_len(xs), 1U);
    const std::uint64_t h = Value::append_list_hash48(list_type_code(xs), xs, elem);
    BuiltinResult out;
    out.value = make_list_hash_len(xs, h, len);
    return out;
  }

  if (id == BuiltinId::Prepend) {
    if (argc != 2) {
      return fail(ErrCode::Type, "prepend expects 2 arguments");
    }
    const Value& xs = args[0];
    const Value& elem = args[1];
    if (!is_list_tag(xs.tag)) {
      return fail(ErrCode::Type, "prepend expects typed-list as first argument");
    }
    if (xs.tag == ValueTag::IntList && elem.tag != ValueTag::Int) {
      return fail(ErrCode::Type, "prepend expects int element for IntList");
    }
    if (xs.tag == ValueTag::FloatList && elem.tag != ValueTag::Float) {
      return fail(ErrCode::Type, "prepend expects float element for FloatList");
    }
    if (xs.tag == ValueTag::StringList && elem.tag != ValueTag::String) {
      return fail(ErrCode::Type, "prepend expects string element for StringList");
    }
    std::vector<Value> lx;
    if (payload::lookup_list(xs, &lx)) {
      payload::charge_list_allocation(lx.size() + 1);
      lx.insert(lx.begin(), elem);
      return make_exact_list(xs.tag, std::move(lx));
    }
    const std::uint32_t len = Value::saturating_len_add(Value::container_len(xs), 1U);
    const std::uint64_t h = prepend_list_hash48(list_type_code(xs), elem, xs);
    BuiltinResult out;
    out.value = make_list_hash_len(xs, h, len);
    return out;
  }

  if (id == BuiltinId::Reverse) {
    if (argc != 1) {
      return fail(ErrCode::Type, "reverse expects 1 argument");
    }
    const Value& x = args[0];
    if (x.tag == ValueTag::String) {
      std::string sx;
      if (payload::lookup_string(x, &sx)) {
        payload::charge_list_allocation(sx.size());
        std::reverse(sx.begin(), sx.end());
        return BuiltinResult{false, payload::make_string_value(sx), Err{ErrCode::Value, ""}};
      }
      const std::uint64_t h = Value::reverse_container_hash48(7U, x);
      BuiltinResult out;
      out.value = Value::from_fallback_token(Value::pack_container_payload(h, Value::container_len(x)));
      return out;
    }
    if (!is_list_tag(x.tag)) {
      return fail(ErrCode::Type, "reverse expects string/typed-list argument");
    }
    std::vector<Value> lx;
    if (payload::lookup_list(x, &lx)) {
      payload::charge_list_allocation(lx.size());
      std::reverse(lx.begin(), lx.end());
      return make_exact_list(x.tag, std::move(lx));
    }
    const std::uint64_t h = Value::reverse_container_hash48(list_type_code(x), x);
    BuiltinResult out;
    out.value = make_list_hash_len(x, h, Value::container_len(x));
    return out;
  }

  if (id == BuiltinId::Find || id == BuiltinId::Contains) {
    if (argc != 2) {
      return fail(ErrCode::Type, std::string(builtin_name(id)) + " expects 2 arguments");
    }
    const Value& haystack = args[0];
    const Value& needle = args[1];
    if (id == BuiltinId::Contains && is_list_tag(haystack.tag)) {
      if (!list_accepts_element(haystack.tag, needle)) {
        return fail(ErrCode::Type, "contains expects an element of the list's type");
      }
      std::vector<Value> scratch;
      const auto* elements = payload::read_list(haystack, scratch);
      if (!elements) return fail(ErrCode::Value, "contains requires exact list payload");
      bool found = false;
      for (const Value& element : *elements) {
        if (list_elements_equal(element, needle)) { found = true; break; }
      }
      BuiltinResult out;
      out.value = Value::from_bool(found);
      return out;
    }
    if (haystack.tag != ValueTag::String || needle.tag != ValueTag::String) {
      return fail(ErrCode::Type, std::string(builtin_name(id)) + " expects (string,string)");
    }
    std::string hs;
    std::string nd;
    if (!payload::lookup_string(haystack, &hs) || !payload::lookup_string(needle, &nd)) {
      return fail(ErrCode::Value, std::string(builtin_name(id)) + " requires exact string payload");
    }
    const std::size_t pos = hs.find(nd);
    BuiltinResult out;
    if (id == BuiltinId::Find) {
      out.value = Value::from_int(pos == std::string::npos ? -1LL : static_cast<long long>(pos));
    } else {
      out.value = Value::from_bool(pos != std::string::npos);
    }
    return out;
  }

  if (id == BuiltinId::IsInt) {
    if (argc != 1) {
      return fail(ErrCode::Type, "is_int expects 1 argument");
    }
    BuiltinResult out;
    out.value = Value::from_bool(args[0].tag == ValueTag::Int);
    return out;
  }

  if (id == BuiltinId::CharToString) {
    if (argc != 1 || args[0].tag != ValueTag::Char) {
      return fail(ErrCode::Type, "char_to_string expects char");
    }
    const long long c = args[0].i;
    if (c < 0 || c > 255) {
      return fail(ErrCode::Value, "char_to_string supports byte-sized char in CPU slice");
    }
    payload::charge_list_allocation(1);
    return BuiltinResult{false, payload::make_string_value(std::string(1, static_cast<char>(c))), Err{ErrCode::Value, ""}};
  }

  if (id == BuiltinId::StringToChar) {
    if (argc != 1 || args[0].tag != ValueTag::String) {
      return fail(ErrCode::Type, "string_to_char expects string");
    }
    std::string s;
    if (!payload::lookup_string(args[0], &s)) {
      return fail(ErrCode::Value, "string_to_char requires exact string payload");
    }
    if (s.size() != 1U) {
      return fail(ErrCode::Value, "string_to_char expects length-1 string");
    }
    return BuiltinResult{false, Value::from_char(static_cast<unsigned char>(s[0])), Err{ErrCode::Value, ""}};
  }

  if (id == BuiltinId::Ord) {
    if (argc != 1 || args[0].tag != ValueTag::Char) {
      return fail(ErrCode::Type, "ord expects char");
    }
    return BuiltinResult{false, Value::from_int(args[0].i), Err{ErrCode::Value, ""}};
  }

  if (id == BuiltinId::Chr) {
    if (argc != 1 || args[0].tag != ValueTag::Int) {
      return fail(ErrCode::Type, "chr expects int");
    }
    if (args[0].i < 0 || args[0].i > 255) {
      return fail(ErrCode::Value, "invalid character code point");
    }
    return BuiltinResult{false, Value::from_char(args[0].i), Err{ErrCode::Value, ""}};
  }

  if (id == BuiltinId::IsLetter || id == BuiltinId::IsDigit || id == BuiltinId::IsSpace ||
      id == BuiltinId::IsVowel || id == BuiltinId::ToLower || id == BuiltinId::ToUpper) {
    if (argc != 1 || (args[0].tag != ValueTag::Char &&
        !(id == BuiltinId::IsLetter && args[0].tag == ValueTag::Int))) {
      return fail(ErrCode::Type, std::string(builtin_name(id)) + (id == BuiltinId::IsLetter ? " expects char or int" : " expects char"));
    }
    const long long c = args[0].i;
    BuiltinResult out;
    if (id == BuiltinId::IsLetter) out.value = Value::from_bool(is_ascii_letter(c));
    else if (id == BuiltinId::IsDigit) out.value = Value::from_bool(is_ascii_digit(c));
    else if (id == BuiltinId::IsSpace) out.value = Value::from_bool(is_ascii_space(c));
    else if (id == BuiltinId::IsVowel) out.value = Value::from_bool(is_ascii_vowel(c));
    else if (id == BuiltinId::ToLower) out.value = Value::from_char(ascii_to_lower(c));
    else out.value = Value::from_char(ascii_to_upper(c));
    return out;
  }

  if (id == BuiltinId::ToString) {
    if (argc != 1 || !is_numeric(args[0])) {
      return fail(ErrCode::Type, "to_string expects int or float");
    }
    const std::string text = args[0].tag == ValueTag::Int ? std::to_string(args[0].i)
                                                          : format_float_current(args[0].f);
    payload::charge_list_allocation(text.size());
    return BuiltinResult{false, payload::make_string_value(text), Err{ErrCode::Value, ""}};
  }

  if (id == BuiltinId::Singleton) {
    if (argc != 1) {
      return fail(ErrCode::Type, "singleton expects 1 argument");
    }
    const Value& x = args[0];
    if (x.tag == ValueTag::Char) {
      if (x.i < 0 || x.i > 255) {
        return fail(ErrCode::Value, "singleton supports byte-sized char in CPU slice");
      }
      payload::charge_list_allocation(1);
      return BuiltinResult{false, payload::make_string_value(std::string(1, static_cast<char>(x.i))), Err{ErrCode::Value, ""}};
    }
    if (x.tag == ValueTag::Int || x.tag == ValueTag::Float || x.tag == ValueTag::String)
      payload::charge_list_allocation(1);
    if (x.tag == ValueTag::Int) {
      return BuiltinResult{false, payload::make_int_list_value({x}), Err{ErrCode::Value, ""}};
    }
    if (x.tag == ValueTag::Float) {
      return BuiltinResult{false, payload::make_float_list_value({x}), Err{ErrCode::Value, ""}};
    }
    if (x.tag == ValueTag::String) {
      return BuiltinResult{false, payload::make_string_list_value({x}), Err{ErrCode::Value, ""}};
    }
    return fail(ErrCode::Type, "singleton expects int, float, string, or char");
  }

  if (id == BuiltinId::Insert || id == BuiltinId::SetAt) {
    const std::string name = builtin_name(id);
    if (argc != 3) {
      return fail(ErrCode::Type, name + " expects 3 arguments");
    }
    const Value& xs = args[0];
    const Value& index = args[1];
    const Value& elem = args[2];
    if (!is_list_tag(xs.tag)) {
      return fail(ErrCode::Type, name + " expects typed-list as first argument");
    }
    if (index.tag != ValueTag::Int) {
      return fail(ErrCode::Type, name + " expects integer index");
    }
    if (!list_accepts_element(xs.tag, elem)) {
      return fail(ErrCode::Type, name + " expects an element of the list's type");
    }
    std::vector<Value> lx;
    if (payload::lookup_list(xs, &lx)) {
      const long long n = static_cast<long long>(lx.size());
      if (id == BuiltinId::Insert) {
        payload::charge_list_allocation(lx.size() + 1);
        lx.insert(lx.begin() + insert_position(index.i, n), elem);
      } else {
        if (n == 0) return BuiltinResult{false, xs, Err{ErrCode::Value, ""}};
        payload::charge_list_allocation(lx.size());
        lx[static_cast<std::size_t>(vm_semantics::py_int_mod(index.i, n))] = elem;
      }
      return make_exact_list(xs.tag, std::move(lx));
    }
    const std::uint32_t length = Value::container_len(xs);
    if (id == BuiltinId::SetAt && length == 0U) return BuiltinResult{false, xs, Err{ErrCode::Value, ""}};
    const std::uint64_t h = Value::derived_container_hash48(list_type_code(xs), derived_op(id), xs, index.i,
                                                            Value::shallow_hash64(elem));
    BuiltinResult out;
    out.value = make_list_hash_len(xs, h, id == BuiltinId::Insert ? Value::saturating_len_add(length, 1U) : length);
    return out;
  }

  if (id == BuiltinId::RemoveAt) {
    if (argc != 2) {
      return fail(ErrCode::Type, "remove_at expects 2 arguments");
    }
    const Value& xs = args[0];
    const Value& index = args[1];
    if (!is_list_tag(xs.tag)) {
      return fail(ErrCode::Type, "remove_at expects typed-list as first argument");
    }
    if (index.tag != ValueTag::Int) {
      return fail(ErrCode::Type, "remove_at expects integer index");
    }
    std::vector<Value> lx;
    if (payload::lookup_list(xs, &lx)) {
      if (lx.empty()) return BuiltinResult{false, xs, Err{ErrCode::Value, ""}};
      payload::charge_list_allocation(lx.size() - 1);
      const long long n = static_cast<long long>(lx.size());
      lx.erase(lx.begin() + vm_semantics::py_int_mod(index.i, n));
      return make_exact_list(xs.tag, std::move(lx));
    }
    const std::uint32_t length = Value::container_len(xs);
    if (length == 0U) return BuiltinResult{false, xs, Err{ErrCode::Value, ""}};
    const std::uint64_t h = Value::derived_container_hash48(list_type_code(xs), derived_op(id), xs, index.i, 0U);
    BuiltinResult out;
    out.value = make_list_hash_len(xs, h, length - 1U);
    return out;
  }

  if (id == BuiltinId::IndexMod) {
    if (argc != 2) {
      return fail(ErrCode::Type, "index_mod expects 2 arguments: index_mod(x, i)");
    }
    const Value& x = args[0];
    const Value& index = args[1];
    if (!is_container(x)) {
      return fail(ErrCode::Type, "index_mod expects string/typed-list as first argument");
    }
    if (index.tag != ValueTag::Int) {
      return fail(ErrCode::Type, "index_mod expects integer index");
    }
    const auto zero_value = [&]() {
      if (x.tag == ValueTag::IntList) return BuiltinResult{false, Value::from_int(0), Err{ErrCode::Value, ""}};
      if (x.tag == ValueTag::FloatList) return BuiltinResult{false, Value::from_float(0.0), Err{ErrCode::Value, ""}};
      return exact_string("");
    };
    if (x.tag == ValueTag::String) {
      std::string text;
      if (payload::lookup_string(x, &text)) {
        if (text.empty()) return exact_string("");
        const long long j = vm_semantics::py_int_mod(index.i, static_cast<long long>(text.size()));
        payload::charge_list_allocation(1);
        return exact_string(std::string(1, text[static_cast<std::size_t>(j)]));
      }
      const long long n = static_cast<long long>(Value::container_len(x));
      if (n == 0) return exact_string("");
      const long long j = vm_semantics::py_int_mod(index.i, n);
      BuiltinResult out;
      out.value = Value::from_fallback_token(Value::index_container_token64(5U, x, j));
      return out;
    }
    std::vector<Value> scratch;
    if (const auto* elements = payload::read_list(x, scratch)) {
      if (elements->empty()) return zero_value();
      const long long j = vm_semantics::py_int_mod(index.i, static_cast<long long>(elements->size()));
      return BuiltinResult{false, (*elements)[static_cast<std::size_t>(j)], Err{ErrCode::Value, ""}};
    }
    const long long n = static_cast<long long>(Value::container_len(x));
    if (n == 0) return zero_value();
    const long long j = vm_semantics::py_int_mod(index.i, n);
    BuiltinResult out;
    out.value = Value::from_fallback_token(Value::index_container_token64(list_type_code(x), x, j));
    return out;
  }

  if (id == BuiltinId::Range) {
    if (argc != 2) {
      return fail(ErrCode::Type, "range expects 2 arguments: range(a, b)");
    }
    if (args[0].tag != ValueTag::Int || args[1].tag != ValueTag::Int) {
      return fail(ErrCode::Type, "range expects integer bounds");
    }
    const long long a = args[0].i;
    const long long b = args[1].i;
    if (b <= a) return make_exact_list(ValueTag::IntList, {});
    // b > a, so the unsigned difference is exact; nothing is allocated before
    // the length cap and the logical allocation budget have been checked.
    const std::uint64_t count = static_cast<std::uint64_t>(b) - static_cast<std::uint64_t>(a);
    if (count > Value::k_container_len_max) {
      return fail(ErrCode::Value, "range exceeds the maximum list length");
    }
    payload::charge_list_allocation(static_cast<std::size_t>(count));
    payload::check_allocation_budget(static_cast<std::size_t>(count) * sizeof(Value));
    std::vector<Value> elems;
    elems.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t k = 0; k < count; ++k) {
      elems.push_back(Value::from_int(vm_semantics::wrap_int_add(a, static_cast<long long>(k))));
    }
    return make_exact_list(ValueTag::IntList, std::move(elems));
  }

  if (is_strip_family(id)) {
    const bool with_chars = id == BuiltinId::StripChars || id == BuiltinId::LStripChars || id == BuiltinId::RStripChars;
    const bool left = id != BuiltinId::RStrip && id != BuiltinId::RStripChars;
    const bool right = id != BuiltinId::LStrip && id != BuiltinId::LStripChars;
    if (argc != (with_chars ? 2U : 1U)) {
      return fail(ErrCode::Type, std::string(builtin_name(id)) + (with_chars ? " expects 2 arguments" : " expects 1 argument"));
    }
    if (args[0].tag != ValueTag::String || (with_chars && args[1].tag != ValueTag::String)) {
      return fail(ErrCode::Type, std::string(builtin_name(id)) + " expects string arguments");
    }
    std::string text;
    std::string chars;
    if (!payload::lookup_string(args[0], &text) || (with_chars && !payload::lookup_string(args[1], &chars))) {
      return derived_string_token(id, args[0], with_chars ? args[1].i : 0);
    }
    const auto strippable = [&](char c) {
      return with_chars ? chars.find(c) != std::string::npos
                        : is_ascii_space(static_cast<unsigned char>(c));
    };
    std::size_t begin = 0;
    std::size_t end = text.size();
    if (left) while (begin < end && strippable(text[begin])) ++begin;
    if (right) while (end > begin && strippable(text[end - 1])) --end;
    payload::charge_list_allocation(end - begin);
    return exact_string(text.substr(begin, end - begin));
  }

  if (id == BuiltinId::StartsWith || id == BuiltinId::EndsWith) {
    if (argc != 2) {
      return fail(ErrCode::Type, std::string(builtin_name(id)) + " expects 2 arguments");
    }
    if (args[0].tag != ValueTag::String || args[1].tag != ValueTag::String) {
      return fail(ErrCode::Type, std::string(builtin_name(id)) + " expects (string,string)");
    }
    std::string text;
    std::string affix;
    if (!payload::lookup_string(args[0], &text) || !payload::lookup_string(args[1], &affix)) {
      return fail(ErrCode::Value, std::string(builtin_name(id)) + " requires exact string payload");
    }
    const bool fits = affix.size() <= text.size();
    const bool matches = fits && (id == BuiltinId::StartsWith
        ? text.compare(0, affix.size(), affix) == 0
        : text.compare(text.size() - affix.size(), affix.size(), affix) == 0);
    BuiltinResult out;
    out.value = Value::from_bool(matches);
    return out;
  }

  if (id == BuiltinId::Capitalize) {
    if (argc != 1 || args[0].tag != ValueTag::String) {
      return fail(ErrCode::Type, "capitalize expects string");
    }
    std::string text;
    if (!payload::lookup_string(args[0], &text)) return derived_string_token(id, args[0], 0);
    payload::charge_list_allocation(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
      const long long c = static_cast<unsigned char>(text[i]);
      text[i] = static_cast<char>(i == 0 ? ascii_to_upper(c) : ascii_to_lower(c));
    }
    return exact_string(text);
  }

  return fail(ErrCode::Name, "unknown builtin");
}

BuiltinResult builtin_call(BuiltinId id, const std::vector<Value>& args) {
  return builtin_call(id, args.data(), args.size());
}

}  // namespace gagp
