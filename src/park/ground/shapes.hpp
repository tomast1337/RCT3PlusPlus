// SPDX-License-Identifier: GPL-3.0-or-later
//
// Raising and lowering the ground in shapes: a hill, a mountain, a mesa or a ridge up, a canyon, a trough or a
// crater down. Our own shapes.
//
// A brush has a middle anywhere on the ground, in tile units (column, row, fractions allowed), and a reach.
// Every tile keeps its own four corners, and each corner within the reach moves by the shape's share at its
// distance times the amount. Tiles that agree at a corner point still agree afterwards, so smooth ground stays
// in one piece, and tiles that do not keep their difference: a cliff under the brush rides up or down with the
// ground. The heights are those the stroke found when it began, so moving the pointer back undoes what it did.
// A ridge runs along `along` and is narrow across it.
//
// A corner point is held where the owner says so (land the park does not own, paths, things standing on it).
#pragma once

#include "save/model/world.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <glm/vec2.hpp>
#include <vector>

namespace rct3::park::ground
{

    enum class GroundShape : std::uint8_t
    {
        Ridge,
        Mountain,
        Mesa,
        Hill,
        Canyon,
        Trough,
        Crater,
    };

    inline constexpr float kDragDeadZone = 25; ///< Pixels the pointer moves from where it pressed before the ground starts to move
    inline constexpr float kDragRate = 0.25f;  ///< Height units the middle of a brush moves for each pixel past that

    /// How far the middle of a dragged brush has gone with the pointer `pixels` up from where it pressed (down
    /// when negative): nothing inside the dead zone, then evenly, with no steps.
    [[nodiscard]] float drag_amount(float pixels);

    /// The share of the amount the shape takes at `across`, the distance from the middle (or for a ridge from
    /// its line) over the reach, and `along`, the distance along a ridge's line over the reach. Raising shapes
    /// give 0 to 1, lowering ones -1 to 0 (a crater's rim rises a little); 0 from the reach out.
    [[nodiscard]] float shape_share(GroundShape shape, float across, float along = 0);

    /// The ground as a stroke found it.
    struct GroundBefore
    {
        int width = 0;                             ///< Of the grid, in tiles
        std::vector<std::array<float, 4>> corners; ///< Every tile's own, row by row
        std::vector<bool> held;                    ///< By corner point, (width + 1) by (height + 1), row by row
    };

    /// The grid's ground as it is, its corner points held where `free` refuses a tile touching them.
    [[nodiscard]] GroundBefore ground_before(const save::model::TerrainGrid& grid, const std::function<bool(save::model::Tile)>& free);

    /// A stroke `amount` units high (down when negative) of `shape`, centred on `middle` with `reach` tiles,
    /// over the heights `before`. Returns the sum of how far every corner of every tile moved from where it stood.
    float shape_ground(save::model::TerrainGrid& grid, const GroundBefore& before, GroundShape shape, glm::vec2 middle, float reach, glm::vec2 along, float amount);

} // namespace rct3::park::ground
