// SPDX-License-Identifier: GPL-3.0-or-later
//
// libFuzzer target: read_save must return a value or an error for any input, never crash, read out of
// bounds or trip a sanitizer. Run with `./nob fuzz save_file`.
#include <cstddef>
#include <cstdint>
#include <span>

#include "save/save_file.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const auto bytes = std::as_bytes(std::span(data, size));
  const auto file = rct3::save::read_save(bytes, {.verify_checksum = false});
  if (!file) return 0;

  // Sections tile the input exactly, in order.
  std::size_t at = 0;
  for (const auto& s : file->sections) {
    if (s.offset != at) __builtin_trap();
    at += s.size;
  }
  if (at != size) __builtin_trap();

  // Every object lies inside the input, starts with a valid class and is at least the fixed prefix.
  const auto* begin = reinterpret_cast<const std::byte*>(bytes.data());
  for (const auto& o : file->objects) {
    if (o.class_index >= file->classes.size()) __builtin_trap();
    if (o.bytes.size() < 12 || o.offset > size || o.bytes.size() > size - o.offset) __builtin_trap();
    if (o.bytes.data() != begin + o.offset) __builtin_trap();
  }
  // Names are views into the input.
  for (const auto& c : file->classes) {
    if (c.name.empty()) continue;
    const auto* p = reinterpret_cast<const std::byte*>(c.name.data());
    if (p < begin || p + c.name.size() > begin + size) __builtin_trap();
  }
  return 0;
}
