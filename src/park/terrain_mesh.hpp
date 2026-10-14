// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's ground as triangles: two per tile over its four corner heights, walls where a tile's edge
// stands higher or lower than its neighbour's (cliffs), and the water bodies' surfaces. The ground is
// grouped into batches by what it is painted with, a land surface or a cliff face, so each batch draws with
// one texture. Every triangle is flat-shaded by a fixed sun.
//
// Land surfaces fade into each other across a tile: at each corner every surface of the tiles touching it
// shows in proportion to how many of them have it. A tile is drawn once per surface touching its corners,
// and the vertex alpha says how much of what is below each layer covers. The batches come lowest surface
// first and must be drawn in order, with alpha blending and depth tests that pass on equal depth.
//
// World units throughout: X along the columns, Z along the rows, Y up.
#pragma once

#include "math/glm.hpp"
#include "save/model/world.hpp"

#include <cstdint>
#include <vector>

namespace rct3::park
{

    /// Position, shade and texture coordinate: what a textured draw takes.
    struct GroundVertex
    {
        glm::vec3 position { 0.0f };
        glm::vec4 colour { 1.0f };
        glm::vec2 uv { 0.0f };
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
    inline constexpr glm::vec4 kWaterColour { 0.20f, 0.42f, 0.55f, 0.65f };

    [[nodiscard]] TerrainMesh terrain_mesh(const save::TerrainGrid& grid, const save::WaterLayer& water);

} // namespace rct3::park
