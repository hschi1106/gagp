#include "gagp/evolution/grammar/identity.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#ifdef GAGP_HAS_OPENSSL_SHA256
#include <openssl/sha.h>
#endif

namespace gagp::evo::grammar {
namespace {
constexpr std::array<std::uint32_t, 64> constants{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
std::uint32_t rotate(std::uint32_t word, unsigned count) { return (word >> count) | (word << (32 - count)); }

void compress(std::array<std::uint32_t, 8>& state, const unsigned char* block) {
  std::array<std::uint32_t, 64> words{};
  for (unsigned i = 0; i < 16; ++i)
    for (unsigned j = 0; j < 4; ++j) words[i] = (words[i] << 8) | block[4 * i + j];
  for (unsigned i = 16; i < 64; ++i) {
    const auto x = words[i - 15], y = words[i - 2];
    words[i] = words[i - 16] + (rotate(x, 7) ^ rotate(x, 18) ^ (x >> 3)) + words[i - 7] +
        (rotate(y, 17) ^ rotate(y, 19) ^ (y >> 10));
  }
  auto a = state[0], b = state[1], c = state[2], d = state[3];
  auto e = state[4], f = state[5], g = state[6], h = state[7];
  for (unsigned i = 0; i < 64; ++i) {
    const auto first = h + (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25)) +
        ((e & f) ^ (~e & g)) + constants[i] + words[i];
    const auto second = (rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
    h = g; g = f; f = e; e = d + first; d = c; c = b; b = a; a = first + second;
  }
  state[0] += a; state[1] += b; state[2] += c; state[3] += d;
  state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}
}  // namespace

std::string content_sha256(std::string_view bytes) {
  if (bytes.size() > std::numeric_limits<std::uint64_t>::max() / 8)
    throw std::length_error("SHA-256 input length overflow");
#ifdef GAGP_HAS_OPENSSL_SHA256
  // A caller-owned digest keeps concurrent identity construction independent.
  // Retain the portable implementation if the provider cannot supply SHA-256.
  std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
  if (SHA256(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), digest.data())) {
    std::string result;
    result.reserve(64);
    for (auto byte : digest) {
      result += "0123456789abcdef"[byte >> 4];
      result += "0123456789abcdef"[byte & 15];
    }
    return result;
  }
#endif
  std::array<std::uint32_t, 8> state{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::size_t offset = 0;
  while (bytes.size() - offset >= 64) {
    compress(state, reinterpret_cast<const unsigned char*>(bytes.data() + offset));
    offset += 64;
  }
  std::array<unsigned char, 128> tail{};
  const auto remaining = bytes.size() - offset;
  for (std::size_t i = 0; i < remaining; ++i) tail[i] = static_cast<unsigned char>(bytes[offset + i]);
  tail[remaining] = 0x80;
  const unsigned length = remaining < 56 ? 64 : 128;
  const auto bits = static_cast<std::uint64_t>(bytes.size()) * 8;
  for (unsigned i = 0; i < 8; ++i) tail[length - 1 - i] = static_cast<unsigned char>(bits >> (i * 8));
  compress(state, tail.data());
  if (length == 128) compress(state, tail.data() + 64);
  std::string result;
  result.reserve(64);
  for (auto word : state)
    for (int shift = 28; shift >= 0; shift -= 4) result += "0123456789abcdef"[(word >> shift) & 15];
  return result;
}

}  // namespace gagp::evo::grammar
