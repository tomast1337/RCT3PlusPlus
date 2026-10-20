// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where a placed object stands: the transform from its model's coordinates to the park's. A model is
// built round the centre of a tile, and the object names its tile, its height, a quarter turn and, for
// small items, which quarter of the tile it takes.
//
// Worked out from the game's parks: heights are world units (stacked walls and path slopes step by their
// models' heights). Slope pieces rise along their model's -X, and in a park a run of them climbs along +X
// when unturned and along +Z with three quarter turns; so the turns go clockwise seen from above and the
// unturned model faces -X. Drawn this way, paths, coaster track and walls join up. Coaster curves of two
// by two and three by two tiles, run into from the pieces before them, show where a larger item's tile is. An object with no
// height above the ground, and not pinned to its stored height, stands on the ground under its anchor;
// its stored height is the next whole unit up. Edge items sit on the edge their +X faces. Where items on
// paths and in quarter tiles sit within the tile is a guess from the models' shapes.
#pragma once

#include "math/glm.hpp"
#include "save/model/world.hpp"

#include <array>
#include <cstdint>

namespace rct3::park
{

    /// How an item sits on its tile, as its scenery item stores it.
    enum class Positioning : std::uint16_t
    {
        Tile = 0,          ///< Centred on the tile
        PathSide = 1,      ///< On a path, against one side
        PathSideOuter = 2, ///< Just outside a path's side
        Edge = 3,          ///< On one edge of the tile (walls, fences)
        Quarter = 4,       ///< In one quarter of the tile
        Half = 5,          ///< In the half of the tile its +X faces
        PathCentre = 6,
        Corner = 7,
        PathSideJoin = 8,
    };

    /// How far from the tile's centre an edge item's model origin goes, along its own +X.
    inline constexpr float kEdgeOffset = 2.0f;
    /// Likewise for items against a path's side, and centres of quarter and half tiles from the tile centre.
    inline constexpr float kPathSideOffset = 1.6f;
    inline constexpr float kQuarterOffset = 1.0f;

    /// The direction a model's +X points after `turns` quarter turns, in the park's X and Z.
    [[nodiscard]] glm::vec2 turned_x(std::int32_t turns);

    /// Where the middle of `object`'s footprint is, in tiles from the grid's corner, for an item covering
    /// `squares` tiles along its own X and Z. The object names the footprint's tile of lowest column and row
    /// whichever way it turns, and its model is built round the footprint's middle: the middle of a tile
    /// when both sides are odd, a tile's corner when both are even.
    [[nodiscard]] glm::vec2 footprint_middle(const save::PlacedObject& object, std::array<std::int32_t, 2> squares);

    /// Whether `object` stands on the ground rather than at a height of its own.
    [[nodiscard]] bool on_ground(const save::PlacedObject& object);

    /// The model-to-park transform of `object`, whose item positions itself as `positioning` and covers
    /// `squares` tiles along its own X and Z. On the ground it stands at the ground's height under its
    /// anchor when `ground_height`, otherwise at its stored height.
    [[nodiscard]] glm::mat4 object_transform(const save::PlacedObject& object, Positioning positioning, const save::TerrainGrid& grid, std::array<std::int32_t, 2> squares = { 1, 1 }, bool ground_height = true);

    /// Whether the park owns every tile `object` covers, for an item covering `squares` tiles along its own X
    /// and Z. Land bought only to build over counts for things raised off the ground, not for those on it.
    [[nodiscard]] bool on_owned_land(const save::PlacedObject& object, std::array<std::int32_t, 2> squares, const save::TerrainGrid& grid);

} // namespace rct3::park
