// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/sign_text.hpp"

#include "park/led_signs.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::ui
{

    namespace
    {
        constexpr std::size_t kChannels = 4;
    } // namespace

    ovl::texture::RgbaImage sign_text_loop(const font::Font& font, std::span<const ovl::texture::RgbaImage> pages, std::u16string_view text, std::uint32_t height)
    {
        float width = 0;
        for (const char16_t c : text)
        {
            if (const auto g = font.find(c))
                width += g->advance;
        }
        ovl::texture::RgbaImage loop;
        loop.width = static_cast<std::uint32_t>(std::ceil(width)) + park::kSignGap * height;
        loop.width += (park::kSignLamp - loop.width % park::kSignLamp) % park::kSignLamp;
        loop.height = height;
        loop.pixels.assign(static_cast<std::size_t>(loop.width) * height * kChannels, 0);
        for (std::size_t i = kChannels - 1; i < loop.pixels.size(); i += kChannels)
            loop.pixels[i] = 255;
        // The line's middle on the board's middle.
        const float baseline = (static_cast<float>(height) - font.line_height()) / 2 + font.ascent();
        float pen = 0;
        for (const char16_t c : text)
        {
            const auto g = font.find(c);
            if (!g)
                continue;
            if (g->page < pages.size() && g->box.width() > 0 && g->box.height() > 0)
            {
                const ovl::texture::RgbaImage& page = pages[g->page];
                const auto left = static_cast<int>(std::lround(pen + g->box.x0));
                const auto top = static_cast<int>(std::lround(baseline + g->box.y0));
                const auto wide = static_cast<int>(std::lround(g->box.width()));
                const auto tall = static_cast<int>(std::lround(g->box.height()));
                for (int y = 0; y < tall; ++y)
                {
                    // Glyph texture coordinates may run either way down the page.
                    const float v = g->uv.y0 + (g->uv.y1 - g->uv.y0) * (static_cast<float>(y) + 0.5f) / static_cast<float>(tall);
                    const auto from_y = std::clamp(static_cast<std::int64_t>(v * static_cast<float>(page.height)), std::int64_t { 0 }, static_cast<std::int64_t>(page.height) - 1);
                    for (int x = 0; x < wide; ++x)
                    {
                        const int tx = left + x;
                        const int ty = top + y;
                        if (tx < 0 || ty < 0 || tx >= static_cast<int>(loop.width) || ty >= static_cast<int>(height))
                            continue;
                        const float u = g->uv.x0 + (g->uv.x1 - g->uv.x0) * (static_cast<float>(x) + 0.5f) / static_cast<float>(wide);
                        const auto from_x = std::clamp(static_cast<std::int64_t>(u * static_cast<float>(page.width)), std::int64_t { 0 }, static_cast<std::int64_t>(page.width) - 1);
                        const std::uint8_t cover = page.pixels[(static_cast<std::size_t>(from_y) * page.width + static_cast<std::size_t>(from_x)) * kChannels + 3];
                        const std::size_t at = (static_cast<std::size_t>(ty) * loop.width + static_cast<std::size_t>(tx)) * kChannels;
                        for (std::size_t k = 0; k < 3; ++k)
                            loop.pixels[at + k] = std::max(loop.pixels[at + k], cover);
                    }
                }
            }
            pen += g->advance;
        }
        return loop;
    }

} // namespace rct3::ui
