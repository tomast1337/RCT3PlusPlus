// SPDX-License-Identifier: GPL-3.0-or-later
//
// Internal helpers for item readers that decode a fixed structure word by word.
#pragma once

#include "core/types.hpp"
#include "ovl/overlay.hpp"
#include "ovl/pointer_fields.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace rct3::ovl::fields
{

    [[nodiscard]] inline u32 read_u32(std::span<const std::byte> bytes, usize at) noexcept
    {
        u32 v = 0;
        std::memcpy(&v, bytes.data() + at, sizeof v);
        return v;
    }

    [[nodiscard]] inline i32 read_i32(std::span<const std::byte> bytes, usize at) noexcept
    {
        return static_cast<i32>(read_u32(bytes, at));
    }

    [[nodiscard]] inline f32 read_f32(std::span<const std::byte> bytes, usize at) noexcept
    {
        f32 v = 0;
        std::memcpy(&v, bytes.data() + at, sizeof v);
        return v;
    }

    /// The string a pointer field at `location` (holding `value`) points to. Empty for a null pointer, which
    /// is a zero that was not relocated, and for a pointer that leaves its block.
    [[nodiscard]] inline std::string_view string_field(const AddressSpace& space, const PointerFields& pointers, u32 location, u32 value) noexcept
    {
        if (value == 0 && !pointers.contains(location))
            return { };
        const auto text = space.cstring_at(value);
        return text ? *text : std::string_view { };
    }

} // namespace rct3::ovl::fields
