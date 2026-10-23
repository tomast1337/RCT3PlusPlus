// SPDX-License-Identifier: GPL-3.0-or-later
//
// What stands in a path's way besides other paths. A scenery item says, per square of its footprint, whether
// the square is solid and which levels over the item's base it fills; level n fills the world unit from n to
// n + 1 over the base. A path piece may not pass through a solid square's levels, and keeps kPathHeadroom clear
// over its top for guests to walk.
//
// Some items never block: fences, which stand along a tile's edges; path pieces, which paths check among themselves; the support columns, which are
// built again round the paths; the park entrance, which paths run through; and what belongs on or along a path
// (benches, bins, signs, lamps and the like, litter, where guests walk in), by their category or by being
// positioned on a path.
#pragma once

#include "park/footpath_builder.hpp"
#include "park/object_mesh.hpp"
#include "save/model/world.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace rct3::park
{

    /// The room of a tile an item takes, as bits: one per quarter, as save::PlacedObject::quadrant counts them,
    /// then one per edge, as the item's quarter turns count them. Items whose rooms share no bit stand side by
    /// side on one tile, as small props in different quarters do, or a wall along an edge over a floor.
    inline constexpr std::uint8_t kWholeTile = 0x0F;
    [[nodiscard]] constexpr std::uint8_t quarter_room(std::int32_t quadrant) noexcept
    {
        return static_cast<std::uint8_t>(1U << (quadrant & 3));
    }
    /// The two quarters on the side an item's +X faces after `turns` quarter turns (see turned_x).
    [[nodiscard]] constexpr std::uint8_t half_room(std::int32_t turns) noexcept
    {
        constexpr std::array<std::uint8_t, 4> kHalves { 0b0101, 0b1100, 0b1010, 0b0011 }; // -X, +Z, +X, -Z
        return kHalves[static_cast<std::size_t>(turns & 3)];
    }
    [[nodiscard]] constexpr std::uint8_t edge_room(std::int32_t turns) noexcept
    {
        return static_cast<std::uint8_t>(0x10U << (turns & 3));
    }

    /// Levels filled on one tile, in world units, and the room of the tile they fill.
    struct SolidSpan
    {
        float low = 0;
        float high = 0;
        std::uint8_t room = kWholeTile;
    };

    /// What fills each tile, by tile (row << 16 | column).
    struct SolidTiles
    {
        std::unordered_map<std::uint32_t, std::vector<SolidSpan>> tiles;
    };

    /// Whether an item of `category` may stand in a path's way.
    [[nodiscard]] bool blocks_paths(std::uint32_t category) noexcept;

    /// The solid squares of `objects` that are shown and block paths, by the tile under each.
    [[nodiscard]] SolidTiles solid_tiles(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid);

    /// Whether something in `solids` fills `spot` between `low` and `high` and the headroom over it.
    [[nodiscard]] bool blocked(const SolidTiles& solids, save::Tile spot, float low, float high);

} // namespace rct3::park
