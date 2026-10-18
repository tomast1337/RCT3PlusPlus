// SPDX-License-Identifier: GPL-3.0-or-later
//
// What stands in a path's way besides other paths. A scenery item says, per square of its footprint, whether
// the square is solid and which levels over the item's base it fills; level n fills the world unit from n to
// n + 1 over the base. A path piece may not pass through a solid square's levels, and keeps kPathHeadroom clear
// over its top for guests to walk.
//
// Some items never block: fences, which stand along a tile's edges; path pieces, which paths check among themselves; the support columns, which are
// built again round the paths; the park entrance, which paths run through; and what belongs on or along a path
// (benches, bins, signs, lamps and the like, litter, where guests walk in).
#pragma once

#include "park/footpath_builder.hpp"
#include "park/object_mesh.hpp"
#include "save/model/world.hpp"

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace rct3::park
{

    /// Levels filled on one tile, in world units.
    struct SolidSpan
    {
        float low = 0;
        float high = 0;
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
    [[nodiscard]] bool blocked(const SolidTiles& solids, PathSpot spot, float low, float high);

} // namespace rct3::park
