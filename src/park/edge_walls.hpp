// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fences and walls standing along a tile's edge, which paths do not join across. Two paths side by side
// join where their shared edge meets at one height; a fence or wall along that edge, standing at that height,
// keeps them apart, so they draw with a closed side there and guests do not walk through it.
//
// This is our own design: the game refuses fences and walls on path tiles. Here they go on them, and a player
// fences off a path to split it in two.
//
// Queues keep their own sides: their railings are fences on the edges a line of guests does not cross, and
// whether a queue joins a path is up to the queue alone. Railings of rides, and items set on a path's side
// (lamps, benches), are not fences and never cut.
#pragma once

#include "park/object_mesh.hpp"
#include "save/model/world.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>

namespace rct3::park
{

    /// A fence or wall along side `side` of `tile`, sides as path_decks numbers them, from height `low` to
    /// `high` in world units.
    struct EdgeWall
    {
        save::Tile tile;
        std::uint8_t side = 0;
        float low = 0;
        float high = 0;
    };

    /// The walls by tile and side, each listed on both tiles it stands between: key (row << 16 | col) << 2 | side.
    using EdgeWalls = std::unordered_multimap<std::uint64_t, EdgeWall>;

    /// The wall `object` is, if it is a fence or wall that stands on an edge and is shown.
    [[nodiscard]] std::optional<EdgeWall> edge_wall(const save::PlacedObject& object, const ObjectModels& models, const save::TerrainGrid& grid);

    /// The walls among `objects`.
    [[nodiscard]] EdgeWalls edge_walls(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid);

    /// Whether a wall stands along `side` of `tile` where a path's edge meets it at `height`.
    [[nodiscard]] bool walled(const EdgeWalls& walls, save::Tile tile, std::uint8_t side, float height);

} // namespace rct3::park
