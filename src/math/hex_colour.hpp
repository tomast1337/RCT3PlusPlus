// SPDX-License-Identifier: GPL-3.0-or-later
//
// Colours written in the code as web-style hex text, "#rrggbb" or "#rrggbbaa", so editors that show
// colour swatches show them. The text is read while compiling: anything else (a missing '#', a wrong
// length, a letter past 'f') stops the build. Each kind of colour has a thin wrapper next to its type
// (ui::hex, render::hex) built on this one.
#pragma once

#include "math/glm.hpp"

#include <cstdint>
#include <string_view>

namespace rct3
{

    /// Eight bits per channel, alpha 255 when the text gives none.
    struct HexColour
    {
        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t b = 0;
        std::uint8_t a = 255;

        friend constexpr bool operator==(const HexColour&, const HexColour&) = default;
    };

    namespace detail
    {
        /// Not constexpr on purpose: reaching it while compiling is the error a bad colour text reports.
        inline void malformed_hex_colour()
        {
        }

        consteval std::uint8_t hex_digit(char c)
        {
            if (c >= '0' && c <= '9')
                return static_cast<std::uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f')
                return static_cast<std::uint8_t>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F')
                return static_cast<std::uint8_t>(c - 'A' + 10);
            malformed_hex_colour();
            return 0;
        }

        consteval std::uint8_t hex_byte(std::string_view text, std::size_t at)
        {
            return static_cast<std::uint8_t>(hex_digit(text[at]) * 16 + hex_digit(text[at + 1]));
        }
    } // namespace detail

    /// The colour `text` ("#rrggbb" or "#rrggbbaa", either case) names.
    consteval HexColour hex(std::string_view text)
    {
        if ((text.size() != 7 && text.size() != 9) || text[0] != '#')
            detail::malformed_hex_colour();
        return { detail::hex_byte(text, 1), detail::hex_byte(text, 3), detail::hex_byte(text, 5), text.size() == 9 ? detail::hex_byte(text, 7) : std::uint8_t { 255 } };
    }

    /// The colour as floats 0..1 in red, green, blue, alpha order.
    constexpr glm::vec4 unit(HexColour c)
    {
        return { c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f };
    }

} // namespace rct3
