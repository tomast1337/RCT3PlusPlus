// SPDX-License-Identifier: GPL-3.0-or-later
//
// Colours written in the code as web-style hex text, "#rrggbb" or "#rrggbbaa", so editors that show
// colour swatches show them. The text is read while compiling: anything else (a missing '#', a wrong
// length, a letter past 'f') stops the build. Each kind of colour has a thin wrapper next to its type
// (ui::hex, render::hex) built on this one.
#pragma once

#include "core/types.hpp"
#include "math/glm.hpp"

#include <cstdint>
#include <string_view>

namespace rct3::math
{

    /// Eight bits per channel, alpha 255 when the text gives none.
    struct HexColour
    {
        u8 r = 0;
        u8 g = 0;
        u8 b = 0;
        u8 a = 255;

        friend constexpr bool operator==(const HexColour&, const HexColour&) = default;
    };

    namespace detail
    {
        /// Not constexpr on purpose: reaching it while compiling is the error a bad colour text reports.
        inline void malformed_hex_colour()
        {
        }

        consteval u8 hex_digit(char c)
        {
            if (c >= '0' && c <= '9')
                return static_cast<u8>(c - '0');
            if (c >= 'a' && c <= 'f')
                return static_cast<u8>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F')
                return static_cast<u8>(c - 'A' + 10);
            malformed_hex_colour();
            return 0;
        }

        consteval u8 hex_byte(std::string_view text, usize at)
        {
            return static_cast<u8>(hex_digit(text[at]) * 16 + hex_digit(text[at + 1]));
        }
    } // namespace detail

    /// The colour `text` ("#rrggbb" or "#rrggbbaa", either case) names.
    consteval HexColour hex(std::string_view text)
    {
        if ((text.size() != 7 && text.size() != 9) || text[0] != '#')
            detail::malformed_hex_colour();
        return { detail::hex_byte(text, 1), detail::hex_byte(text, 3), detail::hex_byte(text, 5), text.size() == 9 ? detail::hex_byte(text, 7) : u8 { 255 } };
    }

    /// The colour as floats 0..1 in red, green, blue, alpha order.
    constexpr glm::vec4 unit(HexColour c)
    {
        return { c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f };
    }

} // namespace rct3::math
