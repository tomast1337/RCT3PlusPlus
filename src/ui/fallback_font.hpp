// SPDX-License-Identifier: GPL-3.0-or-later
//
// The fallback for characters the game fonts lack: which codes to render and at what size. The fallback is
// rendered once per game font, at the size where its capital letters are as tall as that font's. Both hang
// off the same baseline, so a borrowed letter lines up with its neighbours; the line height stays the game
// font's. Capitals rather than ascents: fonts leave different room above their capitals for accents, so equal
// ascents can still put letters of very different sizes side by side.
#pragma once

#include "ui/truetype_font.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace rct3::ui
{

    /// Rendered ahead of time. Basic Latin is included for fonts that miss a few of its signs.
    inline constexpr std::array kFallbackRanges = {
        CodeRange { 0x0020, 0x007E }, // Basic Latin
        CodeRange { 0x00A0, 0x00FF }, // Latin-1 Supplement
        CodeRange { 0x0100, 0x017F }, // Latin Extended-A
        CodeRange { 0x0180, 0x024F }, // Latin Extended-B
        CodeRange { 0x0370, 0x03FF }, // Greek and Coptic
        CodeRange { 0x0400, 0x04FF }, // Cyrillic
        CodeRange { 0x2000, 0x206F }, // General Punctuation
        CodeRange { 0x20A0, 0x20CF }, // Currency Symbols
        CodeRange { 0x2100, 0x214F }, // Letterlike Symbols
        CodeRange { 0x2190, 0x21FF }, // Arrows
        CodeRange { 0x2500, 0x257F }, // Box Drawing
        CodeRange { 0x2580, 0x259F }, // Block Elements
        CodeRange { 0x25A0, 0x25FF }, // Geometric Shapes
    };

    /// How far a capital H reaches above the baseline in `font`; its ascent when it has no H.
    [[nodiscard]] inline float cap_height(const Font& font)
    {
        auto h = font.find(u'H');
        return h && h->box.y0 < 0 ? -h->box.y0 : font.ascent();
    }

    /// How a pixel font is sized. Its glyphs sit on a grid of `grid` pixels per em, so they are drawn exactly
    /// only at whole multiples of it; a size within `tolerance` (a fraction of the size) of a multiple takes
    /// the multiple. A grid of 0 means an outline font: any whole pixel size.
    struct PixelGrid
    {
        float grid = 0;
        float tolerance = 0;
    };

    /// GNU Unifont, the fallback the viewer uses, draws every glyph on a 16-pixel em.
    inline constexpr PixelGrid kUnifontGrid { 16, 0.15f };

    /// The em size, in whole pixels, at which a fallback with capitals `cap_per_em` of its em tall has
    /// capitals `primary_cap` tall, moved onto the pixel grid when close to it. At least one pixel.
    [[nodiscard]] inline float fallback_em_pixels(float primary_cap, float cap_per_em, PixelGrid pixels = { })
    {
        if (!(cap_per_em > 0) || !(primary_cap > 0))
            return 1;
        const float em = primary_cap / cap_per_em;
        if (pixels.grid > 0)
        {
            const float snapped = std::max(1.0f, std::round(em / pixels.grid)) * pixels.grid;
            if (std::abs(snapped - em) <= pixels.tolerance * em)
                return snapped;
        }
        return std::max(1.0f, std::round(em));
    }

} // namespace rct3::ui
