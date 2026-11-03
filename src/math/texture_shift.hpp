// SPDX-License-Identifier: GPL-3.0-or-later
//
// How far a texture is slid along its v, as moving water and chains slide theirs. The texture repeats, so a
// slide of a whole number of lengths looks the same as none: only the fraction is kept.
#pragma once

#include <cmath>

namespace rct3
{

    /// A slide along a texture's v, in texture lengths, always from 0 up to (not including) 1.
    struct TextureShift
    {
        float lengths = 0;

        /// The slide of `lengths` texture lengths, either way, kept as its fraction.
        [[nodiscard]] static TextureShift by(float lengths)
        {
            return { lengths - std::floor(lengths) };
        }

        friend constexpr bool operator==(const TextureShift&, const TextureShift&) = default;
    };

} // namespace rct3
