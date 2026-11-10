// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/led_signs.hpp"

#include "park/footpath_builder.hpp"
#include "park/item_category.hpp"
#include "park/path/decks.hpp"
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
                    return from != tile && path::stored_heading_or_bare(p.queue->end_direction).beyond({ p.col, p.row }) == save::Tile { paths[tile].col, paths[tile].row };
                });
            };
            const auto head = std::ranges::find_if(tiles, [&](std::size_t tile) { return !led_into(tile); });
            if (head == tiles.end())
                continue;
            const save::PathTile& p = paths[*head];
            const auto side = path::stored_heading(p.queue->start_direction);
            if (!side)
                continue;
            spots.push_back({ line, p.col, p.row, path::edge_height(p, grid, *side).value_or(0), edge_turns(*side) });
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

    ovl::texture::RgbaImage sign_window(const ovl::texture::RgbaImage& loop, std::uint32_t shift, std::uint32_t width, LampColour colour)
    {
        ovl::texture::RgbaImage out;
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
