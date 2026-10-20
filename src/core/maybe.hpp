// SPDX-License-Identifier: GPL-3.0-or-later
//
// Nullable references. Our APIs hand out `std::optional<T&>` where something may be missing; stored links
// (a widget's parent, the open menu) and third-party calls (EnTT's try_get) stay plain pointers, and
// `maybe` turns one into the other at the edge. `std::optional<T&>` is the size of a pointer.
#pragma once

#include <optional>

namespace rct3
{

    /// What `p` points to, or nothing when it is null.
    template<typename T> [[nodiscard]] constexpr std::optional<T&> maybe(T* p) noexcept
    {
        return p != nullptr ? std::optional<T&>(*p) : std::nullopt;
    }

    /// Back to a pointer, for storing a link: null when `r` holds nothing.
    template<typename T> [[nodiscard]] constexpr T* as_pointer(std::optional<T&> r) noexcept
    {
        return r ? &*r : nullptr;
    }

} // namespace rct3
