// SPDX-License-Identifier: GPL-3.0-or-later
//
// Raising and lowering the ground in shapes: a hill, a mountain, a mesa or a ridge up, a canyon, a trough or a
// crater down. Our own shapes.
//
// They work on the corner points of the tiles, which neighbouring tiles share, so the ground stays in one
// piece. A brush has a middle anywhere on the ground, in tile units (column, row, fractions allowed), and a
// reach. Each corner point within the reach moves by the shape's share at its distance times the amount; the
// heights are those the stroke found when it began, so moving the pointer back undoes what it did. A ridge
// runs along `along` and is narrow across it.
//
// A corner point is held where the owner says so (land the park does not own, paths, things standing on it),
// and where the tiles round it do not agree on its height: the top or bottom of a cliff stays as it is.
#pragma once

#include "save/model/world.hpp"

#include <cstdint>
#include <functional>
#include <glm/vec2.hpp>
#include <vector>

namespace rct3::park
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

    /// The share of the amount the shape takes at `across`, the distance from the middle (or for a ridge from
    /// its line) over the reach, and `along`, the distance along a ridge's line over the reach. Raising shapes
    /// give 0 to 1, lowering ones -1 to 0 (a crater's rim rises a little); 0 from the reach out.
    [[nodiscard]] float shape_share(GroundShape shape, float across, float along = 0);

    /// The corner points' heights, (width + 1) by (height + 1), row by row.
    struct CornerHeights
    {
        int columns = 0; ///< The grid's width + 1
        std::vector<float> heights;
        std::vector<bool> held;
    };

    /// The grid's corner points as they are, held where `free` refuses a tile touching them or at a cliff.
    [[nodiscard]] CornerHeights corner_heights(const save::TerrainGrid& grid, const std::function<bool(int col, int row)>& free);

    /// Sets corner point (`col`, `row`) to `height` in each tile that has it.
    void set_corner_point(save::TerrainGrid& grid, int col, int row, float height);

    /// A stroke `amount` units high (down when negative) of `shape`, centred on `middle` with `reach` tiles,
    /// over the heights `before`. Returns the sum of how far every corner point moved from where it stood.
    float shape_ground(save::TerrainGrid& grid, const CornerHeights& before, GroundShape shape, glm::vec2 middle, float reach, glm::vec2 along, float amount);

} // namespace rct3::park
