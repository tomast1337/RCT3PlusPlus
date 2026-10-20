// SPDX-License-Identifier: GPL-3.0-or-later
//
// The fonts the interface draws with, by the role a widget asks for rather than by face or size, so a
// language or resolution can swap the whole set.
//
// Windows and their controls keep one size of text on every screen, as the game's do; the scaled roles
// grow with the screen, for the full-screen menus.
#pragma once

#include "core/maybe.hpp"
#include "ui/font.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace rct3::ui
{

    enum class FontRole : std::uint8_t
    {
        Tiny,
        Small,
        Medium,
        Large,
        AllScripts, ///< Has the glyphs of every language's own name, for a list of languages
        ScaledSmall,
        ScaledLarge,
        ScaledMedium,
    };

    inline constexpr std::size_t kFontRoleCount = 8;

    class FontSet
    {
    public:
        void set(FontRole role, Font font)
        {
            fonts_[static_cast<std::size_t>(role)] = std::move(font);
        }
        /// Nothing when the role has no font; text in it is then not drawn.
        [[nodiscard]] std::optional<const Font&> get(FontRole role) const
        {
            const auto& f = fonts_[static_cast<std::size_t>(role)];
            return f ? std::optional<const Font&>(*f) : std::nullopt;
        }

    private:
        std::array<std::optional<Font>, kFontRoleCount> fonts_;
    };

} // namespace rct3::ui
