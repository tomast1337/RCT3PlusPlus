// SPDX-License-Identifier: GPL-3.0-or-later
//
// Trees on the land beyond the map's edge, so the woods do not stop where the park does. They are scattered
// in groves of one kind, picked among the trees and plants the park itself has, so the land round a desert
// park grows cacti and the land round a forest grows pines. Groves are thickest by the edge and thin out
// with distance. Each tree stands on the surrounding land at a random quarter turn.
//
// The scatter draws from the scenery stream: the same seed and park give the same trees.
#pragma once

#include "park/object_mesh.hpp"
#include "park/surrounding_land.hpp"
#include "random/generator.hpp"
#include "save/model/world.hpp"

#include <span>
#include <vector>

namespace rct3::park
{

    /// How far beyond the edge trees grow, in world units.
    inline constexpr float kOffMapTreeReach = 160;
    /// Groves per tree in the park, and the most there may be.
    inline constexpr float kGrovesPerParkTree = 0.15f;
    inline constexpr std::size_t kMaxGroves = 300;
    /// Trees in a grove, and how far they spread round its middle.
    inline constexpr std::int64_t kMinGrove = 1;
    inline constexpr std::int64_t kMaxGrove = 7;
    inline constexpr float kGroveSpread = 10;
    /// How far each tree's foot sits below the land, so it never floats where the land curves.
    inline constexpr float kTreeSink = 0.3f;

    /// True for a placed item drawn as a tree or plant: one that sways in the wind.
    [[nodiscard]] bool is_plant(const save::PlacedObject& object, const ObjectModels& models);

    /// How many groves the trees among `objects` standing on `grid` call for beyond its edge.
    [[nodiscard]] std::size_t off_map_groves(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid);

    /// Trees for the land round `grid`, of the kinds among `objects` on it, numbered after the highest id there: those
    /// of the groves past the first `made` that the park calls for. None when the park has no trees. Planting
    /// more in the park adds groves to those made, the stream going on where it stopped.
    [[nodiscard]] std::vector<save::PlacedObject> off_map_trees(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid, const LandRings& land, random::RandomState& scenery, std::size_t made = 0);

} // namespace rct3::park
