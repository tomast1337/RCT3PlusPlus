// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/path/clearance.hpp"

#include "core/types.hpp"
#include "park/item_category.hpp"
#include "park/object/placement.hpp"
#include "park/path/decks.hpp"
#include "park/path/furniture.hpp"
#include "park/support/columns.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace rct3::park::path
{

    namespace
    {
        u32 key_of(i32 col, i32 row)
        {
            return static_cast<u32>(row) << 16 | static_cast<u16>(col);
        }
    } // namespace

    bool blocks_paths(u32 category) noexcept
    {
        const auto in = [&](ItemCategory low, ItemCategory high) { return category >= std::to_underlying(low) && category <= std::to_underlying(high); };
        return category != std::to_underlying(ItemCategory::Fence) && !in(ItemCategory::EnclosureFence, ItemCategory::EnclosureView) && !in(ItemCategory::SupportColumn, ItemCategory::SupportCap) && !in(ItemCategory::PathPiece, ItemCategory::GuestSpawn) && !in(ItemCategory::LitterBin, ItemCategory::PathFeature);
    }

    void add_path_decks(SolidTiles& solids, std::span<const save::model::PathTile> paths, const save::model::TerrainGrid& grid)
    {
        for (const save::model::PathTile& path : paths)
        {
            std::optional<f32> deck;
            for (const Side side : kSides)
                if (const auto at = edge_height(path, grid, side); at && (!deck || *at < *deck))
                    deck = at;
            if (deck)
                solids.tiles[static_cast<u32>(path.row) << 16 | path.col].push_back({ .low = *deck, .high = *deck });
        }
    }

    SolidTiles solid_tiles(std::span<const save::model::PlacedObject> objects, const object::ObjectModels& models, const save::model::TerrainGrid& grid)
    {
        SolidTiles out;
        for (const save::model::PlacedObject& o : objects)
        {
            if (o.hidden)
                continue;
            const auto model = models.find(object::model_ref(o));
            if (model == models.end() || model->second.solid.empty() || !blocks_paths(model->second.category) || on_paths(static_cast<object::Positioning>(model->second.positioning)))
                continue;
            const object::ObjectModel& m = model->second;
            const f32 base = object::object_transform(o, static_cast<object::Positioning>(m.positioning), grid, m.squares, m.ground_height)[3].y;
            support::SupportFootprint shape;
            shape.x_squares = m.squares[0];
            shape.z_squares = m.squares[1];
            const auto under = support::footprint_tiles(o, shape);
            u8 room = kWholeTile;
            switch (static_cast<object::Positioning>(m.positioning))
            {
                case object::Positioning::Quarter:
                    room = quarter_room(o.quadrant);
                    break;
                case object::Positioning::Half:
                    room = half_room(ModelTurn::from(o.rotation));
                    break;
                case object::Positioning::Edge:
                    room = edge_room(ModelTurn::from(o.rotation));
                    break;
                default:
                    break;
            }
            const bool track = m.category == std::to_underlying(ItemCategory::TrackPiece);
            const bool hanging = m.supports && std::ranges::any_of(m.supports->points, &support::SupportPoint::hanging);
            for (usize i = 0; i < under.size() && i < m.solid.size(); ++i)
            {
                const auto& levels = m.solid[i];
                if (!levels)
                    continue;
                // The thirds the square fills, turned with the item onto the park's.
                u16 thirds = kAllThirds;
                if (m.supports && i < m.supports->squares.size() && m.supports->squares[i].cells != 0)
                {
                    thirds = 0;
                    const i32 lx = static_cast<i32>(i) % m.squares[0];
                    const i32 lz = static_cast<i32>(i) / m.squares[0];
                    for (i32 c = 0; c < 9; ++c)
                    {
                        if ((m.supports->squares[i].cells & (1U << c)) == 0)
                            continue;
                        const glm::vec2 at = support::spot_in_tiles(o, *m.supports, { { lx, lz }, { c % 3, c / 3 } }, support::SpotFit::MiddleOfThird);
                        const i32 x = std::clamp(static_cast<i32>((at.x - std::floor(at.x)) * 3), 0, 2);
                        const i32 z = std::clamp(static_cast<i32>((at.y - std::floor(at.y)) * 3), 0, 2);
                        thirds |= static_cast<u16>(1U << (3 * z + x));
                    }
                }
                SolidSpan span { base + static_cast<f32>((*levels)[0]), base + static_cast<f32>((*levels)[1]), room, thirds };
                if (track)
                    (hanging ? span.below : span.above) = kRiderRoom;
                out.tiles[key_of(under[i].col, under[i].row)].push_back(span);
            }
        }
        return out;
    }

    bool blocked(const SolidTiles& solids, save::model::Tile spot, f32 low, f32 high)
    {
        const auto found = solids.tiles.find(key_of(spot.col, spot.row));
        if (found == solids.tiles.end())
            return false;
        // A wall along an edge parts paths there instead (edge_walls.hpp).
        return std::ranges::any_of(found->second, [&](const SolidSpan& s) { return (s.room & kWholeTile) != 0 && s.low - s.below < high + kPathHeadroom && low < s.high + s.above; });
    }

    Clash clash(const SolidTiles& mine, const SolidTiles& solids, std::span<const save::model::PathTile> paths, const save::model::TerrainGrid& grid)
    {
        constexpr f32 kTopSlack = 0.05f; // An item may rest on another's top
        for (const auto& [key, spans] : mine.tiles)
        {
            const save::model::Tile tile { static_cast<i32>(key & 0xFFFF), static_cast<i32>(key >> 16) };
            const auto there = solids.tiles.find(key);
            for (const SolidSpan& s : spans)
            {
                // Track meets track at its levels alone; the riders' room keeps out only what is not track.
                if (there != solids.tiles.end() && std::ranges::any_of(there->second, [&](const SolidSpan& t) {
                        const f32 room = s.below + s.above == 0 || t.below + t.above == 0 ? 1.0f : 0.0f;
                        return (t.room & s.room) != 0 && (t.thirds & s.thirds) != 0 && t.low - room * t.below < s.high + room * s.above - kTopSlack && s.low - room * s.below < t.high + room * t.above - kTopSlack;
                    }))
                    return Clash::Solid;
            }
            for (const save::model::PathTile& path : paths)
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

    std::optional<Refused> first_refused(std::span<const save::model::PlacedObject> items, const object::ObjectModels& models, const SolidTiles& solids, std::span<const save::model::PathTile> paths, const save::model::TerrainGrid& grid)
    {
        constexpr f32 kGroundSlack = 0.05f; // Standing on the ground is not above it
        for (usize i = 0; i < items.size(); ++i)
        {
            const SolidTiles mine = solid_tiles(items.subspan(i, 1), models, grid);
            const auto owned = [&](i32 col, i32 row, std::span<const SolidSpan> spans) {
                if (!grid.contains(col, row))
                    return false;
                const save::model::TerrainCell& cell = grid.at(col, row);
                const bool above = !spans.empty() && std::ranges::all_of(spans, [&](const SolidSpan& s) { return s.low > cell.max_height() + kGroundSlack; });
                return cell.ownership() == save::model::Ownership::Owned || (cell.ownership() == save::model::Ownership::OwnedRights && above);
            };
            // An item without solid squares, by the tile it stands on.
            bool unowned = mine.tiles.empty() && !owned(items[i].col, items[i].row, { });
            for (const auto& [key, spans] : mine.tiles)
                unowned = unowned || !owned(static_cast<i32>(key & 0xFFFF), static_cast<i32>(key >> 16), spans);
            if (unowned || clash(mine, solids, paths, grid) != Clash::None)
                return Refused { .item = i, .unowned = unowned };
        }
        return std::nullopt;
    }

} // namespace rct3::park::path
