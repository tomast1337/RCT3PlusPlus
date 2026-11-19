// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/sign_text.hpp"

#include "core/types.hpp"
#include "park/led_signs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rct3::ui
{

    namespace
    {
        constexpr usize kChannels = 4;
    } // namespace

    ovl::texture::RgbaImage sign_text_loop(const font::Font& font, std::span<const ovl::texture::RgbaImage> pages, std::u16string_view text, u32 height)
    {
        f32 width = 0;
        for (const char16_t c : text)
        {
            if (const auto g = font.find(c))
                width += g->advance;
        }
        ovl::texture::RgbaImage loop;
        loop.width = static_cast<u32>(std::ceil(width)) + park::kSignGap * height;
        loop.width += (park::kSignLamp - loop.width % park::kSignLamp) % park::kSignLamp;
        loop.height = height;
        loop.pixels.assign(static_cast<usize>(loop.width) * height * kChannels, 0);
        for (usize i = kChannels - 1; i < loop.pixels.size(); i += kChannels)
            loop.pixels[i] = 255;
        // The line's middle on the board's middle.
        const f32 baseline = (static_cast<f32>(height) - font.line_height()) / 2 + font.ascent();
        f32 pen = 0;
        for (const char16_t c : text)
        {
            const auto g = font.find(c);
            if (!g)
                continue;
            if (g->page < pages.size() && g->box.width() > 0 && g->box.height() > 0)
            {
                const ovl::texture::RgbaImage& page = pages[g->page];
                const auto left = static_cast<i32>(std::lround(pen + g->box.x0));
                const auto top = static_cast<i32>(std::lround(baseline + g->box.y0));
                const auto wide = static_cast<i32>(std::lround(g->box.width()));
                const auto tall = static_cast<i32>(std::lround(g->box.height()));
                for (i32 y = 0; y < tall; ++y)
                {
                    // Glyph texture coordinates may run either way down the page.
                    const f32 v = g->uv.y0 + (g->uv.y1 - g->uv.y0) * (static_cast<f32>(y) + 0.5f) / static_cast<f32>(tall);
                    const auto from_y = std::clamp(static_cast<i64>(v * static_cast<f32>(page.height)), i64 { 0 }, static_cast<i64>(page.height) - 1);
                    for (i32 x = 0; x < wide; ++x)
                    {
                        const i32 tx = left + x;
                        const i32 ty = top + y;
                        if (tx < 0 || ty < 0 || tx >= static_cast<i32>(loop.width) || ty >= static_cast<i32>(height))
                            continue;
                        const f32 u = g->uv.x0 + (g->uv.x1 - g->uv.x0) * (static_cast<f32>(x) + 0.5f) / static_cast<f32>(wide);
                        const auto from_x = std::clamp(static_cast<i64>(u * static_cast<f32>(page.width)), i64 { 0 }, static_cast<i64>(page.width) - 1);
                        const u8 cover = page.pixels[(static_cast<usize>(from_y) * page.width + static_cast<usize>(from_x)) * kChannels + 3];
                        const usize at = (static_cast<usize>(ty) * loop.width + static_cast<usize>(tx)) * kChannels;
                        for (usize k = 0; k < 3; ++k)
                            loop.pixels[at + k] = std::max(loop.pixels[at + k], cover);
                    }
                }
            }
            pen += g->advance;
        }
        return loop;
    }

} // namespace rct3::ui
