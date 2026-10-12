// SPDX-License-Identifier: GPL-3.0-or-later
//
// Internal helpers for item readers that decode a fixed structure word by word.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

#include "ovl/overlay.hpp"
#include "ovl/pointer_fields.hpp"

namespace rct3::ovl::fields {

[[nodiscard]] inline std::uint32_t u32(std::span<const std::byte> bytes, std::size_t at) noexcept {
  std::uint32_t v = 0;
  std::memcpy(&v, bytes.data() + at, sizeof v);
  return v;
}

[[nodiscard]] inline std::int32_t i32(std::span<const std::byte> bytes, std::size_t at) noexcept {
  return static_cast<std::int32_t>(u32(bytes, at));
}

[[nodiscard]] inline float f32(std::span<const std::byte> bytes, std::size_t at) noexcept {
  float v = 0;
  std::memcpy(&v, bytes.data() + at, sizeof v);
  return v;
}

/// The string a pointer field at `location` (holding `value`) points to. Empty for a null pointer, which
/// is a zero that was not relocated, and for a pointer that leaves its block.
[[nodiscard]] inline std::string_view string_field(const AddressSpace& space, const PointerFields& pointers,
                                                   std::uint32_t location, std::uint32_t value) noexcept {
  if (value == 0 && !pointers.contains(location)) return {};
  const auto text = space.cstring_at(value);
  return text ? *text : std::string_view{};
}

}  // namespace rct3::ovl::fields
