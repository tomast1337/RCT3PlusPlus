// SPDX-License-Identifier: GPL-3.0-or-later
//
// The words of an LED sign (see park/led_signs.hpp) drawn from a game font into the text loop its board scrolls.
#pragma once

#include "ovl/texture_image.hpp"
#include "ui/font.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace rct3::ui
{

    /// The text loop for `text`, `height` pixels tall, in `font` at its own size: how much the letters cover
    /// each pixel, 0 to 255 in every channel, as wide as a whole number of lamps. The glyphs' pages are
    /// `pages`, by the index the glyphs give, with coverage in alpha.
    [[nodiscard]] ovl::RgbaImage sign_text_loop(const Font& font, std::span<const ovl::RgbaImage> pages, std::u16string_view text, std::uint32_t height);

} // namespace rct3::ui
