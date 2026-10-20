// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/queue_signs.hpp"

#include "park/footpath_builder.hpp"
#include "park/item_category.hpp"
#include "park/path_decks.hpp"
#include "park/queue_fences.hpp"
#include "strings/ascii.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <ranges>
#include <utility>

namespace rct3::park
{

    namespace
    {
        constexpr std::string_view kQueueSignSetting = "QueueSign";
        constexpr std::size_t kChannels = 4;
    } // namespace

    std::vector<QueueSignSpot> queue_sign_spots(std::span<const save::PathTile> paths, const save::TerrainGrid& grid)
    {
        std::map<std::uint64_t, std::vector<std::size_t>> lines;
        for (std::size_t i = 0; i < paths.size(); ++i)
        {
            if (paths[i].queue && paths[i].queue->queue_line_ref != 0)
                lines[paths[i].queue->queue_line_ref].push_back(i);
        }
        std::vector<QueueSignSpot> spots;
        for (const auto& [line, tiles] : lines)
        {
            // shortcut: heights are not compared and a branching line takes its first head; upgrade if signs
            // show on the wrong deck of stacked queues.
            const auto led_into = [&](std::size_t tile) {
                return std::ranges::any_of(tiles, [&](std::size_t from) {
                    const save::PathTile& p = paths[from];
                    return from != tile && step({ p.col, p.row }, stored_direction(p.queue->end_direction)) == PathSpot { paths[tile].col, paths[tile].row };
                });
            };
            const auto head = std::ranges::find_if(tiles, [&](std::size_t tile) { return !led_into(tile); });
            if (head == tiles.end())
                continue;
            const save::PathTile& p = paths[*head];
            const auto side = side_of(p.queue->start_direction);
            if (!side)
                continue;
            spots.push_back({ line, p.col, p.row, edge_height(p, grid, stored_direction(p.queue->start_direction)).value_or(0), edge_turns(*side) });
        }
        return spots;
    }

    std::string queue_sign_item(std::span<const ovl::SceneryItemEntry> items)
    {
        for (const ovl::SceneryItemEntry& e : items)
        {
            if (!e.item || e.item->type != std::to_underlying(ItemCategory::Sign))
                continue;
            for (const ovl::SceneryItemParameter& p : e.item->parameters)
            {
                // The setting's words, such as " EnableNoEntry QueueSign".
                for (const auto word : std::views::split(p.value, ' '))
                {
                    if (strings::equals_ignoring_case(std::string_view(word), kQueueSignSetting))
                        return std::string(e.name);
                }
            }
        }
        return { };
    }

    ovl::RgbaImage sign_text_loop(const ui::Font& font, std::span<const ovl::RgbaImage> pages, std::u16string_view text, std::uint32_t height)
    {
        float width = 0;
        for (const char16_t c : text)
        {
            if (const auto g = font.find(c))
                width += g->advance;
        }
        ovl::RgbaImage loop;
        loop.width = static_cast<std::uint32_t>(std::ceil(width)) + kSignGap * height;
        loop.width += (kSignLamp - loop.width % kSignLamp) % kSignLamp;
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
                const ovl::RgbaImage& page = pages[g->page];
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

    ovl::RgbaImage sign_window(const ovl::RgbaImage& loop, std::uint32_t shift, std::uint32_t width, LampColour colour)
    {
        ovl::RgbaImage out;
        out.width = width;
        out.height = loop.height;
        out.pixels.resize(static_cast<std::size_t>(width) * loop.height * kChannels);
        if (loop.width == 0)
            return out;
        const float middle = static_cast<float>(kSignLamp) / 2;
        const float radius = static_cast<float>(kSignLamp) * 0.42f;
        for (std::uint32_t y = 0; y < loop.height; ++y)
        {
            const std::uint32_t lamp_y = std::min(y / kSignLamp * kSignLamp + kSignLamp / 2, loop.height - 1);
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const std::uint32_t lamp_x = (x / kSignLamp * kSignLamp + kSignLamp / 2 + shift) % loop.width;
                const bool lit = loop.pixels[(static_cast<std::size_t>(lamp_y) * loop.width + lamp_x) * kChannels] >= kSignLit;
                const float dx = static_cast<float>(x % kSignLamp) + 0.5f - middle;
                const float dy = static_cast<float>(y % kSignLamp) + 0.5f - middle;
                const bool dot = dx * dx + dy * dy <= radius * radius;
                const float share = dot ? (lit ? 1.0f : kSignLampOff) : (lit ? kSignGlow : 0.0f);
                std::uint8_t* pixel = &out.pixels[(static_cast<std::size_t>(y) * width + x) * kChannels];
                for (std::size_t k = 0; k < 3; ++k)
                    pixel[k] = static_cast<std::uint8_t>(std::max(static_cast<float>(kSignDark), static_cast<float>(colour[k]) * share));
                pixel[3] = 255;
            }
        }
        return out;
    }

    std::uint32_t sign_shift(float seconds, std::uint32_t length)
    {
        if (length == 0)
            return 0;
        const auto moved = static_cast<std::uint32_t>(std::fmod(std::max(seconds, 0.0f) * kSignScroll, static_cast<float>(length)));
        return moved / kSignLamp * kSignLamp;
    }

} // namespace rct3::park
