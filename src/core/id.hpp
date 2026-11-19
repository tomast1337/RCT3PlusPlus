// SPDX-License-Identifier: GPL-3.0-or-later
//
// A place in one list, as its own type: each list's places are a different type (by their tag), so a ride's
// place cannot be passed where a stall's is meant, and no plain number turns into one by accident. Made from a
// number only by naming the type (`RideIndex { k }`); `.value` is the plain number, to index the list with.
#pragma once

#include "core/types.hpp"

#include <compare>
#include <cstddef>
#include <functional>

namespace rct3::core
{

    template<typename Tag, typename T = usize> struct Id
    {
        T value { };

        constexpr Id() noexcept = default;
        constexpr explicit Id(T v) noexcept
            : value(v)
        {
        }

        friend constexpr auto operator<=>(Id, Id) noexcept = default;
    };

} // namespace rct3::core

template<typename Tag, typename T> struct std::hash<rct3::core::Id<Tag, T>>
{
    usize operator()(rct3::core::Id<Tag, T> id) const noexcept
    {
        return std::hash<T> { }(id.value);
    }
};
