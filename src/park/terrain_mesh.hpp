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
// A wall can have openings: a band of height on a tile where the ground is hollowed out, as round a tunnel,
// leaves that band out of every wall along the tile's edges.
//
// World units throughout: X along the columns, Z along the rows, Y up.
#pragma once

#include "math/glm.hpp"
#include "math/hex_colour.hpp"
#include "save/model/world.hpp"

#include <cstdint>
#include <span>
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

    /// Position and colour, for an untextured draw, and how deep the water stands over the ground there.
    struct WaterVertex
    {
        glm::vec3 position { 0.0f };
        glm::vec4 colour { 1.0f };
        float depth = 0; ///< World units, 0 where the ground reaches the surface or rises above it
    };

    enum class GroundPaint : std::uint8_t
    {
        Surface,       ///< A land surface, by its index in the terrain types
        Cliff,         ///< A cliff face, likewise
        PathA,         ///< A ground path's first picture, by its style's index in the park
        PathB,         ///< Its second picture
        Queue,         ///< A ground queue's picture, by its painting's index in the park
        TunnelWall,    ///< A tunnel's side wall, by the cliff face of the ground it goes through
        TunnelCeiling, ///< Its ceiling, likewise
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
    inline constexpr glm::vec4 kWaterColour = unit(hex("#2c64a0a6"));

    /// A band of height on a tile that the walls along its edges leave open.
    struct GroundOpening
    {
        int col = 0;
        int row = 0;
        float low = 0;
        float high = 0;
    };

    [[nodiscard]] TerrainMesh terrain_mesh(const save::TerrainGrid& grid, const save::WaterLayer& water, std::span<const GroundOpening> openings = { });

    /// How far above the pool's surface height its water stands: just under the top of the rim piece there,
    /// which rises a fifth of a unit.
    inline constexpr float kPoolBrim = 0.1f;
    /// Pool water is clearer and lighter than the lakes'.
    inline constexpr glm::vec4 kPoolColour = unit(hex("#3c9ccc90"));

    /// The water of pools: a level square on each pool square that holds water, as two triangles like the
    /// lakes', deep down to the pool's floor. Squares off the grid are left out.
    /// shortcut: every square is whole, so water shows past the rim of rounded corner pieces; upgrade by
    /// cutting it to the piece's shape when that shows.
    [[nodiscard]] std::vector<WaterVertex> pool_water(const save::TerrainGrid& grid, std::span<const save::PoolWater> pools);

    /// How much of the ground round grid point (x, z) has `surface`: the share of the tiles touching it,
    /// which is how much of that surface shows there.
    [[nodiscard]] float surface_share(const save::TerrainGrid& grid, int x, int z, std::uint8_t surface);

    /// The normal (any length, pointing up) a tile's ground is lit by at its corner `corner`: the area-weighted sum of the
    /// faces of every tile meeting at that grid point at the same height, so the ground shades smoothly
    /// across tiles and stays sharp where a cliff splits it.
    [[nodiscard]] glm::vec3 ground_normal(const save::TerrainGrid& grid, int col, int row, int corner);

    /// Adds `more`'s ground (not its water) after `mesh`'s, its batches after `mesh`'s, and grows the box.
    void add_ground(TerrainMesh& mesh, const TerrainMesh& more);

    /// The triangles of `mesh` whose middle lies over a tile of `grid` set in `keep` (row by row), in the same
    /// batches.
    [[nodiscard]] TerrainMesh only_tiles(const TerrainMesh& mesh, const save::TerrainGrid& grid, const std::vector<bool>& keep);

} // namespace rct3::park
