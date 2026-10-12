// SPDX-License-Identifier: GPL-3.0-or-later
//
// Builds synthetic OVL bytes for tests. Values are invented and only shaped like the format.
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace rct3::test {

struct Builder {
  std::vector<std::byte> bytes;

  void raw(const void* p, std::size_t n) {
    const auto* b = static_cast<const std::byte*>(p);
    bytes.insert(bytes.end(), b, b + n);
  }
  void u8(std::uint8_t v) { raw(&v, 1); }
  void u16(std::uint16_t v) { raw(&v, 2); }
  void u32(std::uint32_t v) { raw(&v, 4); }
  void str(const std::string& s) {
    u16(static_cast<std::uint16_t>(s.size()));
    raw(s.data(), s.size());
  }
};

/// Bytes of every block, per block type (index 0..8). Missing types are empty.
using BlockBytes = std::vector<std::vector<std::vector<std::byte>>>;

inline std::vector<std::byte> bytes_of(std::initializer_list<std::uint8_t> values) {
  std::vector<std::byte> out;
  for (auto v : values) out.push_back(static_cast<std::byte>(v));
  return out;
}

inline std::vector<std::byte> bytes_of_string(const std::string& text) {
  std::vector<std::byte> out;
  for (char c : text) out.push_back(static_cast<std::byte>(c));
  return out;
}

inline void append_u32(std::vector<std::byte>& out, std::uint32_t v) {
  const auto* p = reinterpret_cast<const std::byte*>(&v);
  out.insert(out.end(), p, p + 4);
}

inline void append_u16(std::vector<std::byte>& out, std::uint16_t v) {
  const auto* p = reinterpret_cast<const std::byte*>(&v);
  out.insert(out.end(), p, p + 2);
}

/// A complete file of the given version with no external references and no loaders: definitions,
/// post-block metadata (v4: two zero words, v5: bytes {1,2,3} and words {7,8}), block data, relocations,
/// then `tail` verbatim.
inline std::vector<std::byte> build_file(std::uint32_t version, const BlockBytes& blocks,
                                         const std::vector<std::uint32_t>& relocations,
                                         const std::vector<std::byte>& tail = {}) {
  Builder b;
  b.raw("FGRK", 4);
  b.u8(0x02);
  b.u8(0), b.u8(0), b.u8(0);
  b.u32(version);
  if (version != 1) b.u32(0x10);
  if (version == 5) b.u16(0), b.u16(0);
  b.u32(0);  // external refs
  b.u32(0);  // secondary refs
  b.u32(0);  // loaders

  static const std::vector<std::vector<std::byte>> none;
  for (std::size_t t = 0; t < 9; ++t) {
    const auto& type = t < blocks.size() ? blocks[t] : none;
    b.u32(static_cast<std::uint32_t>(type.size()));
    if (version > 1) {
      b.u32(0);  // unknown
      for (const auto& block : type) b.u32(static_cast<std::uint32_t>(block.size()));
    }
  }

  if (version == 4) b.u32(0), b.u32(0);
  if (version == 5) {
    b.u32(3), b.u8(1), b.u8(2), b.u8(3);
    b.u32(2), b.u32(7), b.u32(8);
  }

  for (const auto& type : blocks) {
    for (const auto& block : type) {
      if (version == 1) b.u32(static_cast<std::uint32_t>(block.size()));
      b.bytes.insert(b.bytes.end(), block.begin(), block.end());
    }
  }

  b.u32(static_cast<std::uint32_t>(relocations.size()));
  for (auto r : relocations) b.u32(r);
  b.bytes.insert(b.bytes.end(), tail.begin(), tail.end());
  return b.bytes;
}

}  // namespace rct3::test
