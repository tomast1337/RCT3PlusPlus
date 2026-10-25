// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/path_clearance.hpp"

#include "park/item_category.hpp"
#include "park/object_placement.hpp"
#include "park/path_decks.hpp"
#include "park/path_furniture.hpp"
#include "park/support_columns.hpp"

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
                const auto& levels = m.solid[i];
                if (!levels)
                    continue;
                // The thirds the square fills, turned with the item onto the park's.
                std::uint16_t thirds = kAllThirds;
                if (m.supports && i < m.supports->squares.size() && m.supports->squares[i].cells != 0)
                {
                    thirds = 0;
                    const std::int32_t lx = static_cast<std::int32_t>(i) % m.squares[0];
                    const std::int32_t lz = static_cast<std::int32_t>(i) / m.squares[0];
                    for (std::int32_t c = 0; c < 9; ++c)
                    {
                        if ((m.supports->squares[i].cells & (1U << c)) == 0)
                            continue;
                        const glm::vec2 at = spot_in_tiles(o, *m.supports, { { lx, lz }, { c % 3, c / 3 } }, true);
                        const int x = std::clamp(static_cast<int>((at.x - std::floor(at.x)) * 3), 0, 2);
                        const int z = std::clamp(static_cast<int>((at.y - std::floor(at.y)) * 3), 0, 2);
                        thirds |= static_cast<std::uint16_t>(1U << (3 * z + x));
                    }
                }
                out.tiles[key_of(under[i].first, under[i].second)].push_back({ base + static_cast<float>((*levels)[0]), base + static_cast<float>((*levels)[1]), room, thirds });
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

    Clash clash(const SolidTiles& mine, const SolidTiles& solids, std::span<const save::PathTile> paths, const save::TerrainGrid& grid)
    {
        constexpr float kTopSlack = 0.05f; // An item may rest on another's top
        for (const auto& [key, spans] : mine.tiles)
        {
            const save::Tile tile { static_cast<std::int32_t>(key & 0xFFFF), static_cast<std::int32_t>(key >> 16) };
            const auto there = solids.tiles.find(key);
            for (const SolidSpan& s : spans)
            {
                if (there != solids.tiles.end() && std::ranges::any_of(there->second, [&](const SolidSpan& t) { return (t.room & s.room) != 0 && (t.thirds & s.thirds) != 0 && t.low < s.high - kTopSlack && s.low < t.high - kTopSlack; }))
                    return Clash::Solid;
            }
            for (const save::PathTile& path : paths)
            {
                // shortcut: a path's height is taken at one edge, so a slope's lower end may pass under an item;
                // take its lowest edge if that shows.
                const auto deck = edge_height(path, grid, 0);
                if (path.col == tile.col && path.row == tile.row && deck && blocked(mine, tile, *deck, *deck))
                    return Clash::Path;
            }
        }
        return Clash::None;
    }

} // namespace rct3::park
