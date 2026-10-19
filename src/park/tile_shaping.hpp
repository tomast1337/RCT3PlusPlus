// SPDX-License-Identifier: GPL-3.0-or-later
//
// Moving the ground tile by tile, as the land panel's tile tools do. Each tile has its own four corners, so a
// tile moved alone leaves its neighbours as they are and a cliff opens between them. A tool works on one tile,
// where the pointer's place inside it picks the whole tile, one of its edges or one of its corners, or on a
// square of tiles, whole.
//
// A stroke keeps the corners as it found them. Dragging up by an amount takes the picked corners up to the
// lowest of them plus that amount, and leaves those already higher: a sloped tile is lifted flat from below.
// Dragging down mirrors it from the highest. How a corner lands then depends on the tool:
//
//   Free   where the drag takes it
//   Snap   on the nearest of the heights it started at and those the tiles round the same point have, when
//          one is within kSnapReach, so a tile catches on its neighbours' edges and closes the cliff; else
//          where the drag takes it
//   Whole  on the nearest whole unit
//
// Tiles the owner refuses keep their corners.
#pragma once

#include "park/terrain_mesh.hpp"
#include "save/model/world.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <vector>

namespace rct3::park
{

    /// What a one-tile tool moves. Left and right are the tile's low and high x edges, near and far its low and
    /// high z edges.
    enum class TilePart : std::uint8_t
    {
        Whole,
        Left,
        Right,
        Near,
        Far,
        NearLeft,
        NearRight,
        FarLeft,
        FarRight,
    };

    /// Which of a tile's corners (in the grid's corner order) `part` moves.
    [[nodiscard]] std::array<bool, 4> part_corners(TilePart part);

    /// From the pointer's place inside a tile, `within` 0 to 1 along x and z: near an edge picks it, near two
    /// a corner, else the whole tile.
    [[nodiscard]] TilePart part_at(glm::vec2 within);

    /// How far into a tile, as a share of it, the pointer picks an edge.
    inline constexpr float kPartEdge = 0.25f;

    /// How near a height a snapping corner must come to catch on it, in height units.
    inline constexpr float kSnapReach = 0.4f;

    enum class TileShaping : std::uint8_t
    {
        Free,
        Snap,
        Whole,
    };

    /// The tiles from (`col0`, `row0`) to (`col1`, `row1`), both in, and their corners as a stroke found them.
    struct TileStroke
    {
        int col0 = 0;
        int row0 = 0;
        int col1 = -1;
        int row1 = -1;
        TilePart part = TilePart::Whole;
        std::vector<std::array<float, 4>> before; ///< Row by row
        float low = 0;                            ///< Of the picked corners
        float high = 0;
    };

    /// Starts a stroke over the tiles from (`col0`, `row0`) to (`col1`, `row1`), clipped to the grid.
    [[nodiscard]] TileStroke begin_tile_stroke(const save::TerrainGrid& grid, int col0, int row0, int col1, int row1, TilePart part);

    /// Sets the stroke's tiles for a drag of `amount` height units (up positive) from where it began, on the
    /// tiles `free` allows. Returns the sum of how far every corner moved from where the stroke found it.
    float shape_tiles(save::TerrainGrid& grid, const TileStroke& stroke, TileShaping shaping, float amount, const std::function<bool(int col, int row)>& free);

    /// Adds strips on the ground that show what a stroke would move: the tiles' edges, the one edge, or a cross
    /// on the one corner.
    void tile_marks(TerrainMesh& mesh, const save::TerrainGrid& grid, int col0, int row0, int col1, int row1, TilePart part, glm::vec4 colour);

} // namespace rct3::park
