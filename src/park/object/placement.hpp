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
#include "park/turns.hpp"
#include "save/model/world.hpp"

#include <array>
#include <concepts>
#include <cstdint>
#include <optional>

namespace rct3::park::object
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
        Corner = 7, ///< Round one corner of the tile: along the edge its +X faces and the next one round
        PathSideJoin = 8,
    };

    /// How far from the tile's centre an edge or corner item's model origin goes, along its own +X.
    inline constexpr float kEdgeOffset = 2.0f;
    /// Likewise for items against a path's side, and centres of quarter and half tiles from the tile centre.
    inline constexpr float kPathSideOffset = 1.6f;
    inline constexpr float kQuarterOffset = 1.0f;

    /// The direction a model's +X points after `turn`, in the park's X and Z.
    [[nodiscard]] glm::vec2 turned_x(ModelTurn turn);

    // A placed model does not count its turns as a path deck piece does: unturned, its +X faces the column
    // before, and each quarter turn moves it one side on clockwise. These two convert between the two counts.

    /// The side of a tile that a model's +X faces after `turn`.
    [[nodiscard]] constexpr Side faced_side(ModelTurn turn) noexcept
    {
        return Side::kColBefore.turned(turn.value);
    }

    /// The turn that faces a model's +X at `side`: faced_side turned back.
    [[nodiscard]] constexpr ModelTurn turn_facing(Side side) noexcept
    {
        return ModelTurn::from(side.value - Side::kColBefore.value);
    }

    /// The turn an item that snaps to the pointer takes, from where the pointer is inside its tile (`at`, 0 to 1
    /// along X and Z): an edge or half item faces the tile's edge nearest the pointer, its quarter of the tile
    /// cut by the diagonals; a corner item turns to the corner of the quarter the pointer is in. None for items
    /// the player turns.
    [[nodiscard]] std::optional<ModelTurn> snapped_turn(Positioning positioning, glm::vec2 at);

    /// Of the sides of a tile for which `suits` holds, the one nearest the point `at` inside it (0 to 1 along X
    /// and Z); the lowest of equals. None when no side suits.
    template<std::predicate<Side> Suits> [[nodiscard]] std::optional<Side> nearest_side(glm::vec2 at, Suits&& suits)
    {
        const std::array<float, 4> away { 1 - at.y, 1 - at.x, at.y, at.x }; // By side
        std::optional<Side> best;
        for (const Side side : kSides)
            if (suits(side) && (!best || away[static_cast<std::size_t>(side.value)] < away[static_cast<std::size_t>(best->value)]))
                best = side;
        return best;
    }

    /// Where the middle of `object`'s footprint is, in tiles from the grid's corner, for an item covering
    /// `squares` tiles along its own X and Z. The object names the footprint's tile of lowest column and row
    /// whichever way it turns, and its model is built round the footprint's middle: the middle of a tile
    /// when both sides are odd, a tile's corner when both are even.
    [[nodiscard]] glm::vec2 footprint_middle(const save::model::PlacedObject& object, std::array<std::int32_t, 2> squares);

    /// `object`, covering `squares`, moved and turned with a layout it was saved in, so that the layout's object
    /// `from`, covering `from_squares`, stands as `to`: round the middle of `from`'s footprint, its height kept
    /// as far above `from`'s.
    [[nodiscard]] save::model::PlacedObject moved_with(save::model::PlacedObject object, std::array<std::int32_t, 2> squares, const save::model::PlacedObject& from, std::array<std::int32_t, 2> from_squares, const save::model::PlacedObject& to);

    /// The tile under square (x, z) of a footprint of `squares`, counted along the object's own X and Z.
    [[nodiscard]] save::model::Tile square_tile(const save::model::PlacedObject& object, std::array<std::int32_t, 2> squares, int x, int z);

    /// Whether `object` stands on the ground rather than at a height of its own.
    [[nodiscard]] bool on_ground(const save::model::PlacedObject& object);

    /// The model-to-park transform of `object`, whose item positions itself as `positioning` and covers
    /// `squares` tiles along its own X and Z. On the ground it stands at the ground's height under its
    /// anchor when `ground_height`, otherwise at its stored height.
    [[nodiscard]] glm::mat4 object_transform(const save::model::PlacedObject& object, Positioning positioning, const save::model::TerrainGrid& grid, std::array<std::int32_t, 2> squares = { 1, 1 }, bool ground_height = true);

    /// Whether the park owns every tile `object` covers, for an item covering `squares` tiles along its own X
    /// and Z. Land bought only to build over counts for things raised off the ground, not for those on it.
    [[nodiscard]] bool on_owned_land(const save::model::PlacedObject& object, std::array<std::int32_t, 2> squares, const save::model::TerrainGrid& grid);

} // namespace rct3::park::object