// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/queue_fences.hpp"

#include "park/ground_paths.hpp"
#include "park/object_placement.hpp"

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

    glm::vec2 edge_lean(const save::TerrainGrid& grid, int col, int row, TileSide side)
    {
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
            if (rise > 0 && (!in || !out_side))
                continue;

            // A slope's lean: up or down along the way from the start side to the end side.
            glm::vec2 slope_lean { };
            float low = static_cast<float>(p.elevation->quantised_height);
            if (rise > 0)
            {
                const auto [dc, dr] = side_step(*out_side);
                const float sign = slope_falls(p.elevation->slope) ? -1.0f : 1.0f;
                slope_lean = { sign * rise * static_cast<float>(dc) / grid.tile_size_x, sign * rise * static_cast<float>(dr) / grid.tile_size_z };
                if (slope_falls(p.elevation->slope))
                    low -= rise;
            }

            for (const TileSide side : { TileSide::Back, TileSide::Right, TileSide::Front, TileSide::Left })
            {
                if (side == in || side == out_side)
                    continue;
                // On a slope, the start and end sides are opposite; the other two run along it.
                if (rise > 0 && (static_cast<int>(side) - static_cast<int>(*in)) % 2 == 0)
                    continue;
                save::PlacedObject o;
                o.id = ++last_id;
                o.owner_ref = o.id;
                o.symbol = p.queue->fence_symbol;
                o.overlay = p.queue->fence_overlay;
                o.col = p.col;
                o.row = p.row;
                o.rotation = edge_turns(side);
                o.colours = p.queue->fence_colours;
                glm::vec2 lean = slope_lean;
                if (on_ground)
                {
                    lean = edge_lean(grid, p.col, p.row, side);
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
