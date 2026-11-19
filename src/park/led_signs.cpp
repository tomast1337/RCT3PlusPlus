// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/led_signs.hpp"

#include "core/types.hpp"
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
        constexpr usize kChannels = 4;
    } // namespace

    std::vector<QueueSignSpot> queue_sign_spots(std::span<const save::model::PathTile> paths, const save::model::TerrainGrid& grid)
    {
        std::map<u64, std::vector<usize>> lines;
        for (usize i = 0; i < paths.size(); ++i)
        {
            if (paths[i].queue && paths[i].queue->queue_line_ref != 0)
                lines[paths[i].queue->queue_line_ref].push_back(i);
        }
        std::vector<QueueSignSpot> spots;
        for (const auto& [line, tiles] : lines)
        {
            // shortcut: heights are not compared and a branching line takes its first head; upgrade if signs
            // show on the wrong deck of stacked queues.
            const auto led_into = [&](usize tile) {
                return std::ranges::any_of(tiles, [&](usize from) {
                    const save::model::PathTile& p = paths[from];
                    return from != tile && path::stored_heading_or_bare(p.queue->end_direction).beyond({ p.col, p.row }) == save::model::Tile { paths[tile].col, paths[tile].row };
                });
            };
            const auto head = std::ranges::find_if(tiles, [&](usize tile) { return !led_into(tile); });
            if (head == tiles.end())
                continue;
            const save::model::PathTile& p = paths[*head];
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

    ovl::texture::RgbaImage sign_window(const ovl::texture::RgbaImage& loop, u32 shift, u32 width, LampColour colour)
    {
        ovl::texture::RgbaImage out;
        out.width = width;
        out.height = loop.height;
        out.pixels.resize(static_cast<usize>(width) * loop.height * kChannels);
        if (loop.width == 0)
            return out;
        const f32 middle = static_cast<f32>(kSignLamp) / 2;
        const f32 radius = static_cast<f32>(kSignLamp) * 0.42f;
        for (u32 y = 0; y < loop.height; ++y)
        {
            const u32 lamp_y = std::min(y / kSignLamp * kSignLamp + kSignLamp / 2, loop.height - 1);
            for (u32 x = 0; x < width; ++x)
            {
                const u32 lamp_x = (x / kSignLamp * kSignLamp + kSignLamp / 2 + shift) % loop.width;
                const bool lit = loop.pixels[(static_cast<usize>(lamp_y) * loop.width + lamp_x) * kChannels] >= kSignLit;
                const f32 dx = static_cast<f32>(x % kSignLamp) + 0.5f - middle;
                const f32 dy = static_cast<f32>(y % kSignLamp) + 0.5f - middle;
                const bool dot = dx * dx + dy * dy <= radius * radius;
                const f32 share = dot ? (lit ? 1.0f : kSignLampOff) : (lit ? kSignGlow : 0.0f);
                u8* pixel = &out.pixels[(static_cast<usize>(y) * width + x) * kChannels];
                for (usize k = 0; k < 3; ++k)
                    pixel[k] = static_cast<u8>(std::max(static_cast<f32>(kSignDark), static_cast<f32>(colour[k]) * share));
                pixel[3] = 255;
            }
        }
        return out;
    }

    u32 sign_shift(f32 seconds, u32 length)
    {
        if (length == 0)
            return 0;
        const auto moved = static_cast<u32>(std::fmod(std::max(seconds, 0.0f) * kSignScroll.value, static_cast<f32>(length)));
        return moved / kSignLamp * kSignLamp;
    }

} // namespace rct3::park
