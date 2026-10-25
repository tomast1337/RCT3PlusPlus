// SPDX-License-Identifier: GPL-3.0-or-later
//
// Picking scenery out of the park as a structure (save/model/structure.hpp), and building one again.
//
// Picking: the player marks tiles; the structure's box is the smallest one round the marked tiles, and it
// takes every scenery item free-standing on a marked tile. Paths and what stands on them, rides, stalls, track,
// supports and what lies about stay behind.
//
// Building: the box is centred on the tile pointed at and turned by quarter turns as a whole, so each item
// keeps its place in it: its tiles, its quarter of a tile and its facing turn with the box. Each item stands at
// its height in the structure above the ground at the pointer. It fits when every tile of the structure is on
// the park's own land and no item takes room a solid item or a path fills, as one item placed alone.
#pragma once

#include "park/object_mesh.hpp"
#include "park/path_clearance.hpp"
#include "save/model/structure.hpp"
#include "save/model/world.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace rct3::park
{

    /// Whether an item of `category` goes into a structure picked from the park.
    [[nodiscard]] bool goes_in_structure(std::uint32_t category) noexcept;

    /// The structure of the scenery in `objects` on the `marked` tiles, its heights above its lowest item. Empty
    /// (no tiles) when nothing is marked.
    [[nodiscard]] save::Structure pick_structure(std::span<const save::Tile> marked, std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid);

    /// Where `structure`'s tile (col, row) lands when it is centred on `at` and turned `turns` quarters.
    [[nodiscard]] save::Tile structure_tile(const save::Structure& structure, save::Tile at, std::int32_t turns, std::int32_t col, std::int32_t row);

    /// `structure`'s items as objects to add, centred on `at`, turned `turns` quarters, its lowest item at
    /// `height`. They have no ids yet.
    [[nodiscard]] std::vector<save::PlacedObject> structure_objects(const save::Structure& structure, save::Tile at, std::int32_t turns, std::int32_t height, const ObjectModels& models);

    enum class StructureProblem : std::uint8_t
    {
        None,
        NotOwned, ///< A tile of it is off the map or not the park's
        Blocked,  ///< An item takes room a solid item fills
        Path,     ///< An item takes room a path fills
    };

    /// Whether `placed` (from structure_objects) may be built.
    [[nodiscard]] StructureProblem structure_problem(const save::Structure& structure, save::Tile at, std::int32_t turns, std::span<const save::PlacedObject> placed, const ObjectModels& models, const SolidTiles& solids, std::span<const save::PathTile> paths, const save::TerrainGrid& grid);

} // namespace rct3::park
