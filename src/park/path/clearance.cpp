// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/path/clearance.hpp"

#include "park/item_category.hpp"
#include "park/object_placement.hpp"
#include "park/path/decks.hpp"
#include "park/path/furniture.hpp"
#include "park/support/columns.hpp"

#include <algorithm>
#include <utility>

namespace rct3::park::path
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
        return category != std::to_underlying(ItemCategory::Fence) && !in(ItemCategory::EnclosureFence, ItemCategory::EnclosureView) && !in(ItemCategory::SupportColumn, ItemCategory::SupportCap) && !in(ItemCategory::PathPiece, ItemCategory::GuestSpawn) && !in(ItemCategory::LitterBin, ItemCategory::PathFeature);
    }

    void add_path_decks(SolidTiles& solids, std::span<const save::PathTile> paths, const save::TerrainGrid& grid)
    {
        for (const save::PathTile& path : paths)
        {
            std::optional<float> deck;
            for (const Side side : kSides)
                if (const auto at = edge_height(path, grid, side); at && (!deck || *at < *deck))
                    deck = at;
            if (deck)
                solids.tiles[static_cast<std::uint32_t>(path.row) << 16 | path.col].push_back({ .low = *deck, .high = *deck });
        }
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
            support::SupportFootprint shape;
            shape.x_squares = m.squares[0];
            shape.z_squares = m.squares[1];
            const auto under = support::footprint_tiles(o, shape);
            std::uint8_t room = kWholeTile;
            switch (static_cast<Positioning>(m.positioning))
            {
                case Positioning::Quarter:
                    room = quarter_room(o.quadrant);
                    break;
                case Positioning::Half:
                    room = half_room(ModelTurn::from(o.rotation));
                    break;
                case Positioning::Edge:
                    room = edge_room(ModelTurn::from(o.rotation));
                    break;
                default:
                    break;
            }
            const bool track = m.category == std::to_underlying(ItemCategory::TrackPiece);
            const bool hanging = m.supports && std::ranges::any_of(m.supports->points, &support::SupportPoint::hanging);
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
                        const glm::vec2 at = support::spot_in_tiles(o, *m.supports, { { lx, lz }, { c % 3, c / 3 } }, true);
                        const int x = std::clamp(static_cast<int>((at.x - std::floor(at.x)) * 3), 0, 2);
                        const int z = std::clamp(static_cast<int>((at.y - std::floor(at.y)) * 3), 0, 2);
                        thirds |= static_cast<std::uint16_t>(1U << (3 * z + x));
                    }
                }
                SolidSpan span { base + static_cast<float>((*levels)[0]), base + static_cast<float>((*levels)[1]), room, thirds };
                if (track)
                    (hanging ? span.below : span.above) = kRiderRoom;
                out.tiles[key_of(under[i].first, under[i].second)].push_back(span);
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
        return std::ranges::any_of(found->second, [&](const SolidSpan& s) { return (s.room & kWholeTile) != 0 && s.low - s.below < high + kPathHeadroom && low < s.high + s.above; });
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
                // Track meets track at its levels alone; the riders' room keeps out only what is not track.
                if (there != solids.tiles.end() && std::ranges::any_of(there->second, [&](const SolidSpan& t) {
                        const float room = s.below + s.above == 0 || t.below + t.above == 0 ? 1.0f : 0.0f;
                        return (t.room & s.room) != 0 && (t.thirds & s.thirds) != 0 && t.low - room * t.below < s.high + room * s.above - kTopSlack && s.low - room * s.below < t.high + room * t.above - kTopSlack;
                    }))
                    return Clash::Solid;
            }
            for (const save::PathTile& path : paths)
            {
                // shortcut: a path's height is taken at one edge, so a slope's lower end may pass under an item;
                // take its lowest edge if that shows.
                const auto deck = edge_height(path, grid, Side::kNextRow);
                if (path.col == tile.col && path.row == tile.row && deck && blocked(mine, tile, *deck, *deck))
                    return Clash::Path;
            }
        }
        return Clash::None;
    }

    std::optional<Refused> first_refused(std::span<const save::PlacedObject> items, const ObjectModels& models, const SolidTiles& solids, std::span<const save::PathTile> paths, const save::TerrainGrid& grid)
    {
        constexpr float kGroundSlack = 0.05f; // Standing on the ground is not above it
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            const SolidTiles mine = solid_tiles(items.subspan(i, 1), models, grid);
            const auto owned = [&](std::int32_t col, std::int32_t row, std::span<const SolidSpan> spans) {
                if (!grid.contains(col, row))
                    return false;
                const save::TerrainCell& cell = grid.at(col, row);
                const bool above = !spans.empty() && std::ranges::all_of(spans, [&](const SolidSpan& s) { return s.low > cell.max_height() + kGroundSlack; });
                return cell.ownership() == save::Ownership::Owned || (cell.ownership() == save::Ownership::OwnedRights && above);
            };
            // An item without solid squares, by the tile it stands on.
            bool unowned = mine.tiles.empty() && !owned(items[i].col, items[i].row, { });
            for (const auto& [key, spans] : mine.tiles)
                unowned = unowned || !owned(static_cast<std::int32_t>(key & 0xFFFF), static_cast<std::int32_t>(key >> 16), spans);
            if (unowned || clash(mine, solids, paths, grid) != Clash::None)
                return Refused { .item = i, .unowned = unowned };
        }
        return std::nullopt;
    }

} // namespace rct3::park::path
