// SPDX-License-Identifier: GPL-3.0-or-later
//
// A value that may be missing, which saved files keep as a plain number with -1 for none (and any number below
// zero, which none of these values can be, read as none). In memory it is a std::optional, used as one; only the
// code that reads and writes a file turns it into that number and back, with `stored` and `from_stored`. The park
// file's serializer (parkfile/binary.hpp) does so for every member of this type, so its bytes are those of the
// plain number.
#pragma once

#include <concepts>
#include <optional>
#include <utility>

namespace rct3::core
{

    template<typename T> struct NoneAsMinusOne : std::optional<T>
    {
        using std::optional<T>::optional;
        constexpr NoneAsMinusOne(std::optional<T> value) noexcept
            : std::optional<T>(std::move(value))
        {
        }

        /// The number a file keeps for none.
        static constexpr T kNone { -1 };

        /// What a file keeps for this value.
        [[nodiscard]] constexpr T stored() const noexcept
        {
            return this->value_or(kNone);
        }

        // Two of them compare as their optionals do; without these, std::optional's own comparisons are ambiguous
        // for a type derived from it.
        friend constexpr bool operator==(const std::same_as<NoneAsMinusOne> auto& a, const std::same_as<NoneAsMinusOne> auto& b) noexcept
        {
            return static_cast<const std::optional<T>&>(a) == static_cast<const std::optional<T>&>(b);
        }
        friend constexpr auto operator<=>(const std::same_as<NoneAsMinusOne> auto& a, const std::same_as<NoneAsMinusOne> auto& b) noexcept
        {
            return static_cast<const std::optional<T>&>(a) <=> static_cast<const std::optional<T>&>(b);
        }
    };

    /// The value a file keeps as `stored`.
    template<typename T> [[nodiscard]] constexpr NoneAsMinusOne<T> from_stored(T stored) noexcept
    {
        return stored < T { } ? NoneAsMinusOne<T> { } : NoneAsMinusOne<T> { stored };
    }

} // namespace rct3::core
