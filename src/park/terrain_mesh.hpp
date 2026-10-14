// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's ground as triangles: two per tile over its four corner heights, walls where a tile's edge
// stands higher or lower than its neighbour's (cliffs), and the water bodies' surfaces. The ground is
// grouped into batches by what it is painted with, a land surface or a cliff face, so each batch draws with
// one texture. Each vertex carries the normal the ground is lit by, smooth across tiles that meet at the same
// height; cliffs stay sharp.
//
// Land surfaces fade into each other across a tile: at each corner every surface of the tiles touching it
// shows in proportion to how many of them have it. A tile is drawn once per surface touching its corners,
// and the vertex alpha says how much of what is below each layer covers. The batches come lowest surface
// first and must be drawn in order, with alpha blending and depth tests that pass on equal depth.
//
// World units throughout: X along the columns, Z along the rows, Y up.
#pragma once

#include "math/glm.hpp"
#include "math/hex_colour.hpp"
#include "save/model/world.hpp"

#include <cstdint>
#include <vector>

namespace rct3::park
{

    /// Position, colour, texture coordinate and the normal the light falls on: what a textured draw takes.
    /// The normal need not be of unit length. Zero, for things with no one facing (pictures standing in for
    /// trees, the far mountains, water), is lit as if it faced up from wherever it is seen.
    struct GroundVertex
    {
        glm::vec3 position { 0.0f };
        glm::vec4 colour { 1.0f };
        glm::vec2 uv { 0.0f };
        glm::vec3 normal { 0.0f };
    };

    /// Position and colour, for an untextured draw.
    struct WaterVertex
    {
        glm::vec3 position { 0.0f };
        glm::vec4 colour { 1.0f };
    };

    enum class GroundPaint : std::uint8_t
    {
        Surface, ///< A land surface, by its index in the terrain types
        Cliff,   ///< A cliff face, likewise
        PathA,   ///< A ground path's first picture, by its style's index in the park
        PathB,   ///< Its second picture
        Queue,   ///< A ground queue's picture, by its painting's index in the park
    };

    /// A run of `count` indices from `first` painted with one surface or cliff face.
    struct GroundBatch
    {
        GroundPaint paint = GroundPaint::Surface;
        std::uint8_t index = 0;
        std::uint32_t first = 0;
        std::uint32_t count = 0;
    };

    struct TerrainMesh
    {
        std::vector<GroundVertex> vertices;
        std::vector<std::uint32_t> indices;
        std::vector<GroundBatch> batches;
        std::vector<WaterVertex> water; ///< Triangles, three vertices each
        glm::vec3 low { 0.0f };         ///< Corners of the box round the ground
        glm::vec3 high { 0.0f };
    };

    /// World units one repeat of a ground texture covers.
    inline constexpr float kTextureSpan = 16;
    /// Height differences smaller than this make no wall.
    inline constexpr float kFlat = 0.001f;
    inline constexpr glm::vec4 kWaterColour = unit(hex("#336b8ca6"));

    [[nodiscard]] TerrainMesh terrain_mesh(const save::TerrainGrid& grid, const save::WaterLayer& water);

    /// How much of the ground round grid point (x, z) has `surface`: the share of the tiles touching it,
    /// which is how much of that surface shows there.
    [[nodiscard]] float surface_share(const save::TerrainGrid& grid, int x, int z, std::uint8_t surface);

    /// The normal (any length, pointing up) a tile's ground is lit by at its corner `corner`: the area-weighted sum of the
    /// faces of every tile meeting at that grid point at the same height, so the ground shades smoothly
    /// across tiles and stays sharp where a cliff splits it.
    [[nodiscard]] glm::vec3 ground_normal(const save::TerrainGrid& grid, int col, int row, int corner);

    /// Adds `more`'s ground (not its water) after `mesh`'s, its batches after `mesh`'s, and grows the box.
    void add_ground(TerrainMesh& mesh, const TerrainMesh& more);

} // namespace rct3::park
