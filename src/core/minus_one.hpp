// SPDX-License-Identifier: GPL-3.0-or-later
//
// Files that keep a value that may be missing as a plain number, -1 for none. The code that reads such a file
// turns the number into a std::optional here, so nothing past it sees the -1.
#pragma once

#include <optional>

namespace rct3::core
{

    /// The value a file keeps as `stored`, -1 for none; any number below zero, which none of these values can
    /// be, reads as none too.
    template<typename T> [[nodiscard]] constexpr std::optional<T> none_if_negative(T stored) noexcept
    {
        return stored < T { } ? std::nullopt : std::optional<T> { stored };
    }

} // namespace rct3::core
