// SPDX-License-Identifier: GPL-3.0-or-later
//
// Trees on the land beyond the map's edge, so the woods do not stop where the park does. They are grown
// round the park's own trees and plants: each one near the edge seeds a few of its kind on the land beyond
// it, nearer ones commoner, so the woods by the edge carry on past it and open land stays open. Only fairly
// flat land above the water takes a tree. They stand unturned.
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

    /// How far from its park tree a tree beyond the edge may stand, as a share of the map's width plus its
    /// height. Fitted to the shipped parks' lists: none stands farther than a quarter.
    inline constexpr float kOffMapReachShare = 0.25f;
    /// How many places each park tree tries beyond the edge. Chosen by eye: as many trees as the
    /// shipped parks list, within a tenth or so.
    inline constexpr int kOffMapTries = 8;
    /// The least upward share of the land's slope a tree stands on. Chosen by eye: hillsides yes, cliffs no.
    inline constexpr float kOffMapFlatness = 0.9f;
    /// How far each tree's foot sits below the land, so it never floats where the land curves.
    inline constexpr float kTreeSink = 0.3f;

    /// True for a placed item drawn as a tree or plant: one that sways in the wind.
    [[nodiscard]] bool is_plant(const save::PlacedObject& object, const ObjectModels& models);

    /// Trees for the land round `grid`, grown round the plants on it among `objects` from index `from` on,
    /// numbered after the highest id among all `objects`. None when those include no plants.
    [[nodiscard]] std::vector<save::PlacedObject> off_map_trees(std::span<const save::PlacedObject> objects, std::size_t from, const ObjectModels& models, const save::TerrainGrid& grid, const LandRings& land, random::RandomState& scenery);

} // namespace rct3::park
