// SPDX-License-Identifier: GPL-3.0-or-later
//
// Whether scenery may stand where the player puts it. In a park it goes only on land the park owns, out of the
// room solid items take (other scenery, rides, track) and off paths. A sandbox loosens both: scenery stands on
// any tile of the map and may run through rides, track and paths, though still not through other scenery.
#pragma once

#include "core/types.hpp"
#include "park/object/mesh.hpp"
#include "park/path/clearance.hpp"
#include "park/ride/placement.hpp"
#include "save/model/world.hpp"

#include <span>

namespace rct3::park
{

    enum class FitRules : u8
    {
        Park,
        Sandbox,
    };

    /// The room of `objects` that new scenery may not share under `rules`: every solid item's in a park, only
    /// other scenery's in a sandbox.
    [[nodiscard]] path::SolidTiles scenery_room(std::span<const save::model::PlacedObject> objects, const object::ObjectModels& models, const save::model::TerrainGrid& grid, FitRules rules);

    /// What keeps the scenery items `placed` (their models loaded) from standing together, if anything: a tile
    /// off the map or, in a park, one the park does not own; `room` (from scenery_room) taken already; or, in a
    /// park, a path of `paths` in the way.
    [[nodiscard]] ride::SiteProblem scenery_problem(std::span<const save::model::PlacedObject> placed, const object::ObjectModels& models, const path::SolidTiles& room, std::span<const save::model::PathTile> paths, const save::model::TerrainGrid& grid, FitRules rules);

} // namespace rct3::park
