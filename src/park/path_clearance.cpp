// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/path_clearance.hpp"

#include "park/item_category.hpp"
#include "park/object_placement.hpp"
#include "park/path_furniture.hpp"
#include "park/track_supports.hpp"

#include <algorithm>
#include <utility>

namespace rct3::park
{

    namespace
    {
        std::uint32_t key_of(int col, int row)
        {
            return static_cast<std::uint32_t>(row) << 16 | static_cast<std::uint16_t>(col);
        }
    } // namespace

    bool blocks_paths(std::uint32_t category) noexcept
    {
        const auto in = [&](ItemCategory low, ItemCategory high) { return category >= std::to_underlying(low) && category <= std::to_underlying(high); };
        return category != std::to_underlying(ItemCategory::Fence) && !in(ItemCategory::SupportColumn, ItemCategory::SupportCap) && !in(ItemCategory::PathPiece, ItemCategory::GuestSpawn) && !in(ItemCategory::LitterBin, ItemCategory::PathFeature);
    }

    SolidTiles solid_tiles(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid)
    {
        SolidTiles out;
        for (const save::PlacedObject& o : objects)
        {
            if (o.hidden)
                continue;
            const auto model = models.find(model_ref(o));
            if (model == models.end() || model->second.solid.empty() || !blocks_paths(model->second.category) || on_paths(static_cast<Positioning>(model->second.positioning)))
                continue;
            const ObjectModel& m = model->second;
            const float base = object_transform(o, static_cast<Positioning>(m.positioning), grid, m.squares, m.ground_height)[3].y;
            SupportFootprint shape;
            shape.x_squares = m.squares[0];
            shape.z_squares = m.squares[1];
            const auto under = footprint_tiles(o, shape);
            std::uint8_t room = kWholeTile;
            switch (static_cast<Positioning>(m.positioning))
            {
                case Positioning::Quarter:
                    room = quarter_room(o.quadrant);
                    break;
                case Positioning::Half:
                    room = half_room(o.rotation);
                    break;
                case Positioning::Edge:
                    room = edge_room(o.rotation);
                    break;
                default:
                    break;
            }
            for (std::size_t i = 0; i < under.size() && i < m.solid.size(); ++i)
            {
                if (const auto& levels = m.solid[i])
                    out.tiles[key_of(under[i].first, under[i].second)].push_back({ base + static_cast<float>((*levels)[0]), base + static_cast<float>((*levels)[1] + 1), room });
            }
        }
        return out;
    }

    bool blocked(const SolidTiles& solids, save::Tile spot, float low, float high)
    {
        const auto found = solids.tiles.find(key_of(spot.col, spot.row));
        if (found == solids.tiles.end())
            return false;
        // A wall along an edge parts paths there instead (edge_walls.hpp).
        return std::ranges::any_of(found->second, [&](const SolidSpan& s) { return (s.room & kWholeTile) != 0 && s.low < high + kPathHeadroom && low < s.high; });
    }

} // namespace rct3::park
