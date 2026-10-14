// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/queue_fences.hpp"

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

    std::vector<save::PlacedObject> queue_fences(std::span<const save::PathTile> paths, std::uint64_t last_id)
    {
        std::vector<save::PlacedObject> out;
        for (const save::PathTile& p : paths)
        {
            if (p.kind != save::PathKind::Queue || !p.queue || !p.elevation || p.queue->fence_symbol.empty())
                continue;
            const bool on_ground = is_ground_queue(p);
            if (!on_ground && p.elevation->slope != 0)
                continue;
            const auto in = side_of(p.queue->start_direction);
            const auto out_side = side_of(p.queue->end_direction);
            for (const TileSide side : { TileSide::Back, TileSide::Right, TileSide::Front, TileSide::Left })
            {
                if (side == in || side == out_side)
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
                if (!on_ground)
                {
                    o.base_height = p.elevation->quantised_height;
                    o.height_adjust = 0.0f;
                    o.force_absolute_height = true;
                }
                out.push_back(std::move(o));
            }
        }
        return out;
    }

} // namespace rct3::park
