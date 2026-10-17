// SPDX-License-Identifier: GPL-3.0-or-later
//
// Land beyond the edges of the map, so the park does not stand on an island in the sky. Nothing in the
// park file describes it: it is made from the map's edge. Rings go round the map, each further out than the
// last by a step that grows, and each holds as many points as the edge, spread along its sides the same
// way. The first ring is the edge itself, with the park ground's own heights, light and mix of surfaces
// there, so the two join without a seam. Going out, the heights are smoothed along each ring and settle
// towards the edge's average height, so hills at the edge roll on and flatten; the surfaces blur along the
// ring the same way and give way to the edge's commonest one, so a path or a field at the edge does not
// stretch away to the horizon. The land is lit smoothly and fades out with distance into whatever is drawn
// behind it.
//
// As on the park's ground, each surface is a layer, lowest index first, and each layer's vertex alpha is
// what it covers of the layers below and of what is behind, so every point shows its surfaces in
// proportion, faded by its distance.
//
// Where the park's water reaches the edge and stands over the ground there, it goes on out as a sea: the
// land under it is kept kSeaDepth below its surface, and the water carries on to kSeaReach, past where the
// eye can see, the water's shader fading it into the horizon near the far plane.
#pragma once

#include "park/terrain_mesh.hpp"
#include "save/model/world.hpp"

#include <optional>
#include <vector>

namespace rct3::park
{

    /// How far the first ring is from the edge, in world units; each next step is kSurroundGrowth times it.
    inline constexpr float kSurroundFirstStep = 6;
    inline constexpr float kSurroundGrowth = 1.4f;
    /// How far from the edge the land goes. It is fully see-through there.
    inline constexpr float kSurroundReach = 1800;
    /// From the edge, where the land starts fading.
    inline constexpr float kSurroundFadeStart = 500;
    /// How much of its height above the average a ring keeps from the one inside it.
    inline constexpr float kSurroundKeep = 0.75f;
    /// How many times each ring's heights and surfaces are smoothed along it.
    inline constexpr int kSurroundSmoothing = 3;
    /// How much of each next ring is the edge's commonest surface rather than the ring inside's mix.
    inline constexpr float kSurroundSettle = 0.35f;
    /// How far under the sea the land beyond the edge stays.
    inline constexpr float kSeaDepth = 3;
    /// How far from the edge the sea goes: past the far plane wherever the eye is over the map.
    inline constexpr float kSeaReach = 6000;

    /// The land's shape without its paint: how far out each ring is, and the height of each of its points.
    /// Every ring has `points` points, walking round the map as its edge does.
    struct LandRings
    {
        std::size_t points = 0;
        std::vector<float> distances; ///< Ring by ring, from 0 at the edge out to kSurroundReach
        std::vector<float> heights;   ///< Ring after ring, `points` each
        /// Round the edge, the level of the sea going out from each point; NaN where there is none.
        std::vector<float> sea;
    };

    [[nodiscard]] LandRings land_rings(const save::TerrainGrid& grid, const save::WaterLayer& water = { });

    /// The land's height at world (x, z) beyond the map's edge, between its points. Nothing on the map, past
    /// the land's reach, or under the sea.
    [[nodiscard]] std::optional<float> land_height(const LandRings& rings, const save::TerrainGrid& grid, float x, float z);

    /// The height the land settles at far out: the mean height round the map's edge.
    [[nodiscard]] float edge_level(const save::TerrainGrid& grid);

    /// The land round `grid`, batched by surface as terrain_mesh's ground is, and the sea where `water` goes
    /// on past the edge. Empty for an empty grid.
    [[nodiscard]] TerrainMesh surrounding_land(const save::TerrainGrid& grid, const save::WaterLayer& water = { });

} // namespace rct3::park
