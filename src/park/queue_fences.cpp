// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/queue_fences.hpp"

#include "park/ground_paths.hpp"
#include "park/object_placement.hpp"
#include "park/path_decks.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::park
{

    std::int32_t edge_turns(TileSide side) noexcept
    {
        const auto [dc, dr] = side_step(side);
        for (std::int32_t turns = 0; turns < 4; ++turns)
        {
            const glm::vec2 x = turned_x(turns);
            if (std::lround(x.x) == dc && std::lround(x.y) == dr)
                return turns;
        }
        return 0;
    }

    bool slope_falls(std::uint8_t slope) noexcept
    {
        return slope == 2 || slope == 5;
    }

    glm::vec2 edge_lean(const save::TerrainGrid& grid, save::Tile tile, TileSide side)
    {
        const auto [col, row] = tile;
        if (!grid.contains(col, row))
            return { };
        const auto& h = grid.at(col, row).corners; // (x, z) corners: 0 at both low ends, 3 at both high
        switch (side)
        {
            case TileSide::Back:
                return { (h[1] - h[0]) / grid.tile_size_x, 0 };
            case TileSide::Front:
                return { (h[3] - h[2]) / grid.tile_size_x, 0 };
            case TileSide::Left:
                return { 0, (h[2] - h[0]) / grid.tile_size_z };
            case TileSide::Right:
                return { 0, (h[3] - h[1]) / grid.tile_size_z };
        }
        return { };
    }

    QueueFences queue_fences(std::span<const save::PathTile> paths, const save::TerrainGrid& grid, std::uint64_t last_id)
    {
        QueueFences out;
        for (const save::PathTile& p : paths)
        {
            if (p.kind != save::PathKind::Queue || !p.queue || !p.elevation || p.queue->fence_symbol.empty())
                continue;
            const bool on_ground = is_ground_queue(p);
            const float rise = on_ground ? 0.0f : path_rise(p.elevation->slope);
            const auto in = side_of(p.queue->start_direction);
            const auto out_side = side_of(p.queue->end_direction);
            // A slope is fenced along the way its deck faces, leaning as the deck rises, whichever way the
            // line runs over it.
            const std::uint8_t heading = stored_direction(p.direction);
            const auto facing = side_of(p.direction);
            const auto ahead = edge_height(p, grid, heading);
            const auto behind = edge_height(p, grid, static_cast<std::uint8_t>((heading + 2) % 4));
            if (rise > 0 && (!facing || !ahead || !behind))
                continue;

            glm::vec2 slope_lean { };
            float low = static_cast<float>(p.elevation->quantised_height);
            if (rise > 0)
            {
                const auto [dc, dr] = side_step(*facing);
                const float up = *ahead - *behind;
                slope_lean = { up * static_cast<float>(dc) / grid.tile_size_x, up * static_cast<float>(dr) / grid.tile_size_z };
                low = std::min(*ahead, *behind);
            }

            for (const TileSide side : { TileSide::Back, TileSide::Right, TileSide::Front, TileSide::Left })
            {
                if (rise > 0 ? (static_cast<int>(side) - static_cast<int>(*facing)) % 2 == 0 : side == in || side == out_side)
                    continue;
                save::PlacedObject o;
                o.id = ++last_id;
                o.owner_ref = o.id;
                o.fixture = true;
                o.symbol = p.queue->fence_symbol;
                o.overlay = p.queue->fence_overlay;
                o.col = p.col;
                o.row = p.row;
                o.rotation = edge_turns(side);
                o.colours = p.queue->fence_colours;
                glm::vec2 lean = slope_lean;
                if (on_ground)
                {
                    lean = edge_lean(grid, { p.col, p.row }, side);
                }
                else
                {
                    o.base_height = static_cast<std::int32_t>(low);
                    o.height_adjust = rise / 2;
                    o.force_absolute_height = true;
                }
                if (lean != glm::vec2(0.0f))
                    out.leans.emplace(o.id, lean);
                out.pieces.push_back(std::move(o));
            }
        }
        return out;
    }

} // namespace rct3::park
